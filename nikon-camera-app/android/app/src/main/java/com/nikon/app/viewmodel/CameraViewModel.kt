package com.nikon.app.viewmodel

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.nikon.app.ble.BleManager
import com.nikon.app.jni.CameraApi
import com.nikon.app.jni.CameraBridge
import com.nikon.app.jni.StatusChangeCallback
import com.nikon.app.jni.TransferProgressCallback
import com.nikon.app.transfer.TransferManager
import com.nikon.data.settings.SettingsRepository
import com.nikon.data.storage.StorageManager
import com.nikon.model.AppSettings
import com.nikon.model.CameraFile
import com.nikon.model.CameraInfo
import com.nikon.model.CameraProperties
import com.nikon.model.PictureControl
import com.nikon.model.TransferJob
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.receiveAsFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/**
 * CameraViewModel — 主 ViewModel (门面)
 *
 * v4(阶段4): 从 796 行巨型类拆分为多个领域 Manager, 本类退化为门面:
 *  - [ConnectionManager]: 扫描/连接/断开/BLE/状态事件
 *  - [CameraMediaManager]: 文件列举/缩略图/删除/传输路径
 *  - [LiveViewManager]: 实时取景
 *  - [CameraSettingsManager]: 参数轮询/格式化/设置/FTP/拍摄
 *  - [TransferManager]: 传输任务状态机(v3 已拆出)
 *
 * 公有 API 与 StateFlow 保持不变(UI 与测试零改动), 仅内部实现被委托。
 * 单元测试通过反射访问的成员(transferProgressCallback / buildDestPath /
 * formatXxx)仍保留在本类, 保持兼容。
 */
class CameraViewModel(
    application: Application,
    private val bridge: CameraApi = CameraBridge,
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
    /**
     * 是否启动后台状态轮询。默认开启(生产)。
     * 单元测试注入 false: 轮询是 `while(isActive) delay(1000)` 无限循环,
     * 在 coroutine-test 的虚拟时钟下会被 runCurrent/advanceUntilIdle 不断推进而自旋 OOM。
     * 测试改为直接调用 refreshServiceReady() 验证状态同步。
     */
    private val enablePolling: Boolean = true,
) : AndroidViewModel(application) {

    /**
     * native handle — 不再自己 create/destroy,从 Application 读取。
     * handle 由 CameraService 持有,Service 启动后写入 Application,销毁后置 0。
     */
    private val app: com.nikon.app.NikonApplication get() = getApplication()
    private val _handle: Long get() = app.cameraHandle

    /** 存储管理器: 传输完成后自动保存到系统相册 */
    private val storageManager = StorageManager(application)

    /** 设置仓库: load/save/键名唯一入口 (v3 从 ViewModel 拆出) */
    private val settingsRepo = SettingsRepository(application)

    /**
     * 独立清理协程作用域。
     * onCleared 后 viewModelScope 随即被取消,在那里 launch 的收尾命令
     * (如 StopLiveView)大概率执行不完;改用不受其生命周期影响的独立 scope。
     */
    private val shutdownScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    // ─── 领域 Manager(阶段4 拆分)────────────────────────────

    /** 连接管理: 扫描/连接/断开/BLE/状态事件 */
    private val connectionManager = ConnectionManager(
        application = application,
        bridge = bridge,
        handleProvider = { _handle },
        scope = viewModelScope,
        ioDispatcher = ioDispatcher,
        enablePolling = enablePolling,
        settingsRepo = settingsRepo,
        onConnected = ::onNativeConnected,
        onDisconnected = ::onDisconnected,
        onError = ::reportError,
        onBleWakeEvent = { msg -> viewModelScope.launch { _errorEvents.send(msg) } },
    )

    /** 媒体管理: 文件列表/缩略图/删除/传输路径 */
    private val mediaManager = CameraMediaManager(
        application = application,
        bridge = bridge,
        handleProvider = { _handle },
        scope = viewModelScope,
        ioDispatcher = ioDispatcher,
        onError = ::reportError,
        onStartTransfer = ::startTransfer,
    )

    /** 实时取景: 启停/帧轮询 */
    private val liveViewManager = LiveViewManager(
        bridge = bridge,
        handleProvider = { _handle },
        scope = viewModelScope,
        ioDispatcher = ioDispatcher,
        onError = ::reportError,
    )

    /** 相机参数 + App 设置 + FTP + 拍摄 */
    private val settingsManager = CameraSettingsManager(
        bridge = bridge,
        handleProvider = { _handle },
        scope = viewModelScope,
        ioDispatcher = ioDispatcher,
        settingsRepo = settingsRepo,
        onError = ::reportError,
        isConnected = { connectionManager.status.value == CameraBridge.STATUS_CONNECTED },
    )

    // ─── UI 状态(门面透出)────────────────────────────────

    val status: StateFlow<Int> get() = connectionManager.status
    val serviceReady: StateFlow<Boolean> get() = connectionManager.serviceReady
    val cameras: StateFlow<List<CameraInfo>> get() = connectionManager.cameras
    val bleDevices: StateFlow<List<BleManager.Device>> get() = connectionManager.bleDevices
    val bleScanning: StateFlow<Boolean> get() = connectionManager.bleScanning
    val bleSupported: Boolean get() = connectionManager.bleSupported
    val bleEnabled: Boolean get() = connectionManager.bleEnabled

    private val _error = MutableStateFlow<String?>(null)
    val error: StateFlow<String?> = _error.asStateFlow()

    /** 错误事件 Channel — 排队发送,不会因连续错误丢失 */
    private val _errorEvents = kotlinx.coroutines.channels.Channel<String>(capacity = kotlinx.coroutines.channels.Channel.BUFFERED)
    val errorEvents = _errorEvents.receiveAsFlow()

    val cameraProperties: StateFlow<CameraProperties> get() = settingsManager.cameraProperties
    val settings: StateFlow<AppSettings> get() = settingsManager.settings
    val secretStorageAvailable: Boolean get() = settingsManager.secretStorageAvailable

    val fileList: StateFlow<List<CameraFile>> get() = mediaManager.fileList
    val fileListLoading: StateFlow<Boolean> get() = mediaManager.fileListLoading

    val liveViewFrame: StateFlow<ByteArray?> get() = liveViewManager.liveViewFrame
    val liveViewActive: StateFlow<Boolean> get() = liveViewManager.liveViewActive

    // ─── 传输任务(v3:状态机整体委托 TransferManager)────────
    // 任务 id 稳定不替换、进度回调按 nativeJobId 匹配、所有变更原子化,
    // 详见 TransferManager 类注释。依赖 _settings, 故用 by lazy 延迟创建。
    private val transferManager: TransferManager by lazy {
        TransferManager(
            bridge = bridge,
            scope = viewModelScope,
            ioDispatcher = ioDispatcher,
            handleProvider = { _handle },
            settingsProvider = { _settingsProvider() },
            onError = ::reportError,
            onActiveChanged = { active ->
                connectionManager.onTransferActiveChanged(active)
            },
            onJobDone = { job ->
                // 完成时自动保存到系统相册(目录取 settings.storageTarget, 支持用户自定义);
                // 若开启 FTP 自动上传, 再导出到 FTP
                viewModelScope.launch(ioDispatcher) {
                    val albumOverride = settingsAlbumDir()
                    storageManager.saveToGallery(job.destPath, job.filename, albumOverride)
                    if (_settingsProvider().ftpAutoUpload) {
                        exportToFtp(job.destPath)
                    }
                }
            },
        )
    }
    val transferJobs: StateFlow<List<TransferJob>> get() = transferManager.jobs

    /** 传输任务所需的设置快照(委托 settingsManager) */
    private fun _settingsProvider(): AppSettings = settingsManager.settings.value

    /** 从 settings.storageTarget 解析相册子目录名(门面保留, 供 onJobDone 使用) */
    private fun settingsAlbumDir(): String {
        val raw = _settingsProvider().storageTarget.trim()
        return raw.substringAfterLast('/').takeIf { it.isNotBlank() }
            ?: StorageManager.DEFAULT_ALBUM_DIR
    }

    // ─── 连接门面(委托 ConnectionManager)────────────────────

    fun refreshServiceReady() = connectionManager.refreshServiceReady()

    fun scan() = connectionManager.scan()

    fun connect(cameraId: String) = connectionManager.connect(cameraId)

    fun connectWifi(ip: String, port: Int) = connectionManager.connectWifi(ip, port)

    fun disconnect() = connectionManager.disconnect()

    fun startBleScan() = connectionManager.startBleScan()

    fun stopBleScan() = connectionManager.stopBleScan()

    fun connectBleWake(address: String) = connectionManager.connectBleWake(address)

    // ─── 拍摄 / 参数 / 设置(委托给 settingsManager)────────────

    fun capture() = settingsManager.capture()

    fun captureBurst(count: Int, intervalMs: Int = 0) =
        settingsManager.captureBurst(count, intervalMs)

    fun setProperty(propId: Int, value: Long) = settingsManager.setProperty(propId, value)

    suspend fun getProperty(propId: Int): Long = settingsManager.getProperty(propId)

    fun adjustProperty(propId: Int, delta: Long) = settingsManager.adjustProperty(propId, delta)

    fun updateSettings(transform: (AppSettings) -> AppSettings) =
        settingsManager.updateSettings(transform)

    fun exportToFtp(localPath: String) = settingsManager.exportToFtp(localPath)

    // ─── 传输(委托给 TransferManager)────────────────────────

    fun startTransfer(objectHandle: Long, destPath: String) {
        if (_handle == 0L) {
            reportError("相机服务未就绪,请稍候")
            return
        }
        transferManager.startTransfer(objectHandle, destPath)
    }

    fun cancelTransfer(jobId: Int) = transferManager.cancelTransfer(jobId)

    /** 全部暂停:native 层无 pause 语义,等同于取消所有活跃任务 */
    fun pauseAllTransfers() = transferManager.pauseAllTransfers()

    /** 全部取消:取消所有非完成态任务 */
    fun cancelAllTransfers() = transferManager.cancelAllTransfers()

    /** 断点续传:重新对同一 objectHandle 发起传输(native 层支持 offset 续传) */
    fun resumeTransfer(jobId: Int) = transferManager.resumeTransfer(jobId)

    /** 重新传输:从头开始 */
    fun retryTransfer(jobId: Int) = transferManager.retryTransfer(jobId)

    // ─── 媒体(委托给 mediaManager)──────────────────────────

    /** 拉取相机文件列表。
     *  @param storageId 0=全部; 1=CF-A; 2=SD-B */
    fun listFiles(storageId: Int = CameraBridge.STORAGE_ALL) = mediaManager.listFiles(storageId)

    /** 便捷方法:用 app 私有目录作为传输目标 */
    fun startTransferToApp(objectHandle: Long, filename: String) =
        mediaManager.startTransferToApp(objectHandle, filename)

    /** 获取缩略图 JPEG 字节(挂起,UI 侧用 rememberAsyncImage 或 BitmapFactory 解码)。 */
    suspend fun getThumbnail(objectHandle: Long): ByteArray? =
        mediaManager.getThumbnail(objectHandle)

    fun deleteFile(objectHandle: Long) = mediaManager.deleteFile(objectHandle)

    // ─── 实时取景(委托给 liveViewManager)────────────────────

    fun startLiveView() = liveViewManager.startLiveView()

    fun stopLiveView() = liveViewManager.stopLiveView()

    // ─── Picture Control ──────────────────────────────────────

    /**
     * 读取相机当前 Picture Control 参数 (PTP 0x90CC)。
     * @return 解码后的参数; 未连接/失败返回 null
     */
    suspend fun getPictureControl(): PictureControl? {
        val h = _handle
        if (h == 0L) return null
        val raw = withContext(ioDispatcher) { bridge.nativeGetPictCtrl(h) }
            ?: return null
        return PictureControl.fromBytes(raw)
    }

    /**
     * 写入 Picture Control 参数 (PTP 0x90CD)。
     * 与 setProperty 一致: 内部起协程, 失败写 error。
     */
    fun applyPictureControl(pc: PictureControl) {
        val h = _handle
        if (h == 0L) {
            reportError("相机服务未就绪,请稍候")
            return
        }
        viewModelScope.launch(ioDispatcher) {
            val rc = bridge.nativeSetPictCtrl(h, pc.toBytes())
            if (rc != CameraBridge.CAM_OK) {
                reportError("应用预设失败 (错误码: $rc)")
            }
        }
    }

    // ─── 连接状态协调(由 ConnectionManager 回调)──────────────

    /**
     * native 层进入 CONNECTED 时补齐连接后的初始化 (注册传输进度回调 + 参数轮询)。
     * 手动 connect()/connectWifi() 已各自处理; 这里覆盖 CameraService 自动连接
     * (USB fd 注入) 路径 — 旧实现漏掉后, 自动连接成功也没有任何进度回调。
     */
    private fun onNativeConnected(h: Long) {
        bridge.nativeRegisterProgressCallback(h, transferProgressCallback)
        connectionManager.registerStatusCallback()
        settingsManager.startPropertyPolling()
    }

    /** 从 CONNECTED/TRANSFERRING 跌出时停止参数轮询。 */
    private fun onDisconnected() {
        settingsManager.stopPropertyPolling()
    }

    // ─── 传输进度回调(JNI 线程进入, 转发 TransferManager)────

    /** 传输进度回调对象(native 层通过 JNI 回调)。单测通过反射访问, 保留在此。 */
    private val transferProgressCallback = object : TransferProgressCallback {
        override fun onProgress(jobId: Int, speedMbps: Double, percent: Int, status: Int) {
            transferManager.onNativeProgress(jobId, speedMbps, percent, status)
        }
    }

    // ─── 工具 ────────────────────────────────────────────────

    fun clearError() { _error.value = null }

    /** 统一的错误上报:写 StateFlow + 发 Channel 事件 */
    private fun reportError(msg: String) {
        _error.value = msg
        viewModelScope.launch { _errorEvents.send(msg) }
    }

    // ─── 兼容保留: 单测通过反射调用 ──────────────────────────

    /** 反射兼容点: 单测经 buildDestPath 验证 Scoped Storage 路径生成 */
    private fun buildDestPath(filename: String): String = mediaManager.buildDestPath(filename)

    /** 反射兼容点: 单测经 formatXxx 验证参数格式化 */
    private fun formatShutter(raw: Long): String = settingsManager.formatShutter(raw)
    private fun formatAperture(raw: Long): String = settingsManager.formatAperture(raw)
    private fun formatIso(raw: Long): String = settingsManager.formatIso(raw)
    private fun formatEv(raw: Long): String = settingsManager.formatEv(raw)
    private fun formatFocusMode(raw: Long): String = settingsManager.formatFocusMode(raw)
    private fun formatWb(raw: Long): String = settingsManager.formatWb(raw)
    private fun formatQuality(raw: Long): String = settingsManager.formatQuality(raw)

    override fun onCleared() {
        super.onCleared()
        // 停止所有轮询,但不 destroy handle — 那是 Service 的职责
        settingsManager.stopPropertyPolling()
        liveViewManager.close()
        connectionManager.close()
        // LiveView 如果开着,通知 Service 层停止(通过 handle)。
        // 用 shutdownScope: viewModelScope 此刻即将被取消,无法保证命令发出。
        if (liveViewManager.liveViewActive.value && _handle != 0L) {
            val h = _handle
            shutdownScope.launch {
                bridge.nativeStopLiveView(h)
            }
        }
    }
}