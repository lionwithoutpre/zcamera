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
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.receiveAsFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
/**
 * CameraViewModel — 主 ViewModel
 *
 * 持有 CameraAPI native handle 的生命周期。
 * 所有 native 调用均在 IO 调度器执行, UI 状态更新在主线程。
 *
 * v2:补全传输任务管理 / 设置状态 / 相机参数轮询。
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
     * 直接用 AndroidViewModel 的 application,不走 companion object instance(利于依赖注入和测试)。
     */
    private val app: com.nikon.app.NikonApplication get() = getApplication()
    private val _handle: Long get() = app.cameraHandle

    /** 存储管理器: 传输完成后自动保存到系统相册 */
    private val storageManager = StorageManager(application)

    /** BLE 管理器: 相机发现 + 唤醒 (SnapBridge)。在 onCleared 时 close。 */
    private val bleManager = BleManager(application)

    /** 设置仓库: load/save/键名唯一入口 (v3 从 ViewModel 拆出) */
    private val settingsRepo = SettingsRepository(application)

    /**
     * 独立清理协程作用域。
     * onCleared 后 viewModelScope 随即被取消,在那里 launch 的收尾命令
     * (如 StopLiveView)大概率执行不完;改用不受其生命周期影响的独立 scope。
     */
    private val shutdownScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    /**
     * 缩略图内存缓存 (objectHandle → JPEG 字节)。
     * 相册网格会为同一文件反复请求缩略图, 缓存避免重复走 PTP GetThumb。
     * 访问顺序 LRU, 容量上限 128 项, 超过时移除最久未用。
     */
    private val thumbnailCache = object : LinkedHashMap<Long, ByteArray>(16, 0.75f, true) {
        override fun removeEldestEntry(
            eldest: MutableMap.MutableEntry<Long, ByteArray>?,
        ): Boolean = size > 128
    }

    /** Service 是否就绪(handle != 0) — UI 层据此守卫按钮 */
    private val _serviceReady = MutableStateFlow(false)
    val serviceReady: StateFlow<Boolean> = _serviceReady.asStateFlow()

    /** 刷新 serviceReady 状态(Service 启动/销毁时调) */
    fun refreshServiceReady() {
        _serviceReady.value = _handle != 0L
    }

    /** 统一的 handle 守卫:未就绪时写 error 并返回 false */
    private fun ensureHandle(): Boolean {
        if (_handle == 0L) {
            reportError("相机服务未就绪,请稍候")
            return false
        }
        return true
    }

    // ─── UI 状态 ─────────────────────────────────────────────

    private val _status = MutableStateFlow(CameraBridge.STATUS_DISCONNECTED)
    val status: StateFlow<Int> = _status.asStateFlow()

    private val _cameras = MutableStateFlow<List<CameraInfo>>(emptyList())
    val cameras: StateFlow<List<CameraInfo>> = _cameras.asStateFlow()

    private val _error = MutableStateFlow<String?>(null)
    val error: StateFlow<String?> = _error.asStateFlow()

    /** 错误事件 Channel — 排队发送,不会因连续错误丢失 */
    private val _errorEvents = kotlinx.coroutines.channels.Channel<String>(capacity = kotlinx.coroutines.channels.Channel.BUFFERED)
    val errorEvents = _errorEvents.receiveAsFlow()

    // ─── 传输任务(v3:状态机整体委托 TransferManager)────────
    // 任务 id 稳定不替换、进度回调按 nativeJobId 匹配、所有变更原子化,
    // 详见 TransferManager 类注释。依赖 _settings, 故用 by lazy 延迟创建。
    private val transferManager: TransferManager by lazy {
        TransferManager(
            bridge = bridge,
            scope = viewModelScope,
            ioDispatcher = ioDispatcher,
            handleProvider = { _handle },
            settingsProvider = { _settings.value },
            onError = ::reportError,
            onActiveChanged = { active ->
                _status.value = if (active) CameraBridge.STATUS_TRANSFERRING
                                else CameraBridge.STATUS_CONNECTED
            },
            onJobDone = { job ->
                // 完成时自动保存到系统相册(目录取 settings.storageTarget, 支持用户自定义);
                // 若开启 FTP 自动上传, 再导出到 FTP
                viewModelScope.launch(ioDispatcher) {
                    val albumOverride = settingsAlbumDir()
                    storageManager.saveToGallery(job.destPath, job.filename, albumOverride)
                    if (_settings.value.ftpAutoUpload) {
                        exportToFtp(job.destPath)
                    }
                }
            },
        )
    }
    val transferJobs: StateFlow<List<TransferJob>> get() = transferManager.jobs

    // ─── 相机参数(v2 新增,轮询读取)──────────────────────────
    private val _cameraProperties = MutableStateFlow(CameraProperties())
    val cameraProperties: StateFlow<CameraProperties> = _cameraProperties.asStateFlow()

    private var propPollJob: Job? = null

    // ─── App 设置(跨页面共享 + SharedPreferences 持久化)──
    private val _settings = MutableStateFlow(settingsRepo.load())
    val settings: StateFlow<AppSettings> = _settings.asStateFlow()

    // ─── 文件列表(v2 新增)────────────────────────────────────
    private val _fileList = MutableStateFlow<List<CameraFile>>(emptyList())
    val fileList: StateFlow<List<CameraFile>> = _fileList.asStateFlow()

    private val _fileListLoading = MutableStateFlow(false)
    val fileListLoading: StateFlow<Boolean> = _fileListLoading.asStateFlow()

    // ─── LiveView 帧(v2 新增)─────────────────────────────────
    private val _liveViewFrame = MutableStateFlow<ByteArray?>(null)
    val liveViewFrame: StateFlow<ByteArray?> = _liveViewFrame.asStateFlow()

    private val _liveViewActive = MutableStateFlow(false)
    val liveViewActive: StateFlow<Boolean> = _liveViewActive.asStateFlow()

    private var lvPollJob: Job? = null
    private var statusPollJob: Job? = null

    // ─── BLE 状态 (SnapBridge 发现 / 唤醒) ────────────────────
    private val _bleDevices = MutableStateFlow<List<BleManager.Device>>(emptyList())
    val bleDevices: StateFlow<List<BleManager.Device>> = _bleDevices.asStateFlow()

    private val _bleScanning = MutableStateFlow(false)
    val bleScanning: StateFlow<Boolean> = _bleScanning.asStateFlow()

    val bleSupported: Boolean get() = bleManager.isBleSupported
    val bleEnabled: Boolean get() = bleManager.isBluetoothEnabled

    // ─── 初始化 ───────────────────────────────────────────────
    // 不再自己 nativeCreate,handle 由 CameraService 持有。
    // Service 启动后 Application.cameraHandle 有值,所有方法读 _handle 即可。

    init {
        // 轻量轮询:仅检测 Service 何时就绪(handle 从 0 变非 0) / 销毁(非 0→0)。
        // 连接状态不再轮询 —— native 状态机变化通过 [statusCallback] 即时回调
        // (USB 拔出等 native 层主动断开也会回调 DISCONNECTED)。
        // 测试可关闭(enablePolling=false)以避免虚拟时钟下自旋。
        if (enablePolling) startServiceReadyPolling()
    }

    /** native 状态回调实现: 连接状态事件化推送(替代 1s 轮询 nativeGetStatus)。 */
    private val statusCallback = object : StatusChangeCallback {
        override fun onStatusChanged(status: Int) = onNativeStatusChanged(status)
    }

    /**
     * 每 1s 轻量轮询 Service 生命周期(handle 0↔非0), 并在就绪时注册状态回调。
     * 注意: 不做 nativeGetStatus 轮询 —— 状态变化由 native 回调驱动, 消除轮询延迟。
     */
    private fun startServiceReadyPolling() {
        statusPollJob?.cancel()
        statusPollJob = viewModelScope.launch {
            while (isActive) {
                val h = _handle
                if (h != 0L) {
                    if (!_serviceReady.value) {
                        _serviceReady.value = true
                        // Service 就绪时注册状态回调(覆盖自动连接成功场景)
                        registerStatusCallback(h)
                    }
                } else {
                    if (_serviceReady.value) {
                        _serviceReady.value = false
                        _status.value = CameraBridge.STATUS_DISCONNECTED
                    }
                }
                delay(1000)
            }
        }
    }

    /** 注册连接状态回调(幂等: 重复注册会替换旧回调)。 */
    private fun registerStatusCallback(h: Long) {
        try {
            bridge.nativeSetStatusCallback(h, statusCallback)
        } catch (_: Exception) { }
    }

    /**
     * native 状态机回调入口(可能来自 USB 拔出/自动连接成功等 native 侧状态变化)。
     * 逻辑与旧轮询内的状态同步一致, 只是触发方式从"轮询"变为"事件"。
     */
    private fun onNativeStatusChanged(nativeStatus: Int) {
        when (nativeStatus) {
            CameraBridge.STATUS_CONNECTED -> {
                val local = _status.value
                if (local != CameraBridge.STATUS_CONNECTED &&
                    local != CameraBridge.STATUS_TRANSFERRING &&
                    local != CameraBridge.STATUS_CONNECTING &&
                    local != CameraBridge.STATUS_SCANNING) {
                    _status.value = CameraBridge.STATUS_CONNECTED
                    onNativeConnected(_handle)
                }
            }
            CameraBridge.STATUS_DISCONNECTED,
            CameraBridge.STATUS_ERROR -> {
                if (_status.value == CameraBridge.STATUS_CONNECTED ||
                    _status.value == CameraBridge.STATUS_TRANSFERRING) {
                    _status.value = nativeStatus
                    propPollJob?.cancel()
                }
            }
            else -> Unit
        }
    }

    /**
     * native 层进入 CONNECTED 时补齐连接后的初始化 (注册传输进度回调 + 启动参数轮询)。
     * 手动 connect()/connectWifi() 已各自处理; 这里覆盖 CameraService 自动连接
     * (USB fd 注入) 路径 — 旧实现漏掉后, 自动连接成功也没有任何进度回调。
     */
    private fun onNativeConnected(h: Long) {
        bridge.nativeRegisterProgressCallback(h, transferProgressCallback)
        registerStatusCallback(h)
        startPropertyPolling()
    }

    override fun onCleared() {
        super.onCleared()
        // 停止所有轮询,但不 destroy handle — 那是 Service 的职责
        propPollJob?.cancel()
        lvPollJob?.cancel()
        statusPollJob?.cancel()
        bleManager.close()
        // LiveView 如果开着,通知 Service 层停止(通过 handle)。
        // 用 shutdownScope: viewModelScope 此刻即将被取消,无法保证命令发出。
        if (_liveViewActive.value && _handle != 0L) {
            val h = _handle
            shutdownScope.launch {
                bridge.nativeStopLiveView(h)
            }
        }
    }

    // ─── 连接管理 ─────────────────────────────────────────────

    fun scan() {
        if (!ensureHandle()) return
        viewModelScope.launch {
            _status.value = CameraBridge.STATUS_SCANNING
            _cameras.value = emptyList()
            // 保证扫描动画至少显示 3 秒,避免一闪而过
            val scanStart = System.currentTimeMillis()

            // Android 默认关闭 Wi-Fi 组播接收, mDNS 发现相机需要持 MulticastLock
            val wifiManager = getApplication<Application>()
                .getSystemService(android.content.Context.WIFI_SERVICE) as? android.net.wifi.WifiManager
            val multicastLock = wifiManager?.createMulticastLock("nikon-connect-scan")
            multicastLock?.setReferenceCounted(false)
            try { multicastLock?.acquire() } catch (_: Exception) {}

            val rawList = try {
                withContext(ioDispatcher) {
                    bridge.nativeScan(_handle) ?: emptyArray()
                }
            } finally {
                try { multicastLock?.release() } catch (_: Exception) {}
            }

            val elapsed = System.currentTimeMillis() - scanStart
            if (elapsed < 3000) {
                delay(3000 - elapsed)
            }
            val parsed = rawList.mapNotNull { CameraInfo.fromRaw(it) }
            _cameras.value = parsed
            _status.value = CameraBridge.STATUS_DISCONNECTED
        }
    }

    fun connect(cameraId: String) {
        if (!ensureHandle()) return
        viewModelScope.launch {
            _status.value = CameraBridge.STATUS_CONNECTING
            val rc = withContext(ioDispatcher) {
                bridge.nativeConnect(_handle, cameraId)
            }
            if (rc == CameraBridge.CAM_OK) {
                _status.value = CameraBridge.STATUS_CONNECTED
                // 注册传输进度回调 + 状态回调
                bridge.nativeRegisterProgressCallback(_handle, transferProgressCallback)
                registerStatusCallback(_handle)
                startPropertyPolling()
            } else {
                _status.value = CameraBridge.STATUS_ERROR
                reportError("连接失败 (错误码: $rc)")
            }
        }
    }

    /**
     * Wi-Fi 直连相机 PTP/IP 服务。
     * 手机需已连接到相机热点 (系统设置里连接), 相机 IP 通常为 192.168.1.1, 端口 15740。
     */
    fun connectWifi(ip: String, port: Int) {
        if (!ensureHandle()) return
        viewModelScope.launch {
            _status.value = CameraBridge.STATUS_CONNECTING
            val rc = withContext(ioDispatcher) {
                bridge.nativeConnectWifi(_handle, ip, port)
            }
            if (rc == CameraBridge.CAM_OK) {
                _status.value = CameraBridge.STATUS_CONNECTED
                bridge.nativeRegisterProgressCallback(_handle, transferProgressCallback)
                registerStatusCallback(_handle)
                startPropertyPolling()
                // 持久化本次连接端点: 供 CameraService WiFi 断连后真正重连使用
                settingsRepo.saveLastWifiEndpoint(ip, port)
                // 填充设备信息供仪表盘显示 (Android 上 USB 扫描恒为空)。
                // 电量/存储用 -1 表示"未知/读取中": Wi-Fi 连接不返回真实值,
                // 不能填硬编码假数据(如 100%/0GB)误导用户, UI 层对负值显示占位符。
                _cameras.value = listOf(
                    CameraInfo("$ip|wifi|$port", "NIKON (WiFi)", ip, 1, -1, -1.0, -1.0)
                )
            } else {
                _status.value = CameraBridge.STATUS_ERROR
                reportError("Wi-Fi 连接失败 (错误码: $rc)")
            }
        }
    }

    fun disconnect() {
        propPollJob?.cancel()
        viewModelScope.launch(ioDispatcher) {
            bridge.nativeDisconnect(_handle)
        }
        _status.value = CameraBridge.STATUS_DISCONNECTED
    }

    // ─── BLE 发现 / 唤醒 ───────────────────────────────────────

    /** 启动 BLE 扫描 (结果流入 bleDevices)。 */
    fun startBleScan() {
        _bleDevices.value = emptyList()
        _bleScanning.value = true
        val started = bleManager.startScan { device ->
            if (device.isNikon) {
                // 原子去重后追加 (扫描回调线程与主线程可能并发)
                _bleDevices.update { cur ->
                    if (cur.none { it.address == device.address }) cur + device else cur
                }
            }
        }
        if (!started) {
            _bleScanning.value = false
            reportError("BLE 扫描启动失败, 请检查蓝牙是否开启")
        }
    }

    /** 停止 BLE 扫描。 */
    fun stopBleScan() {
        bleManager.stopScan()
        _bleScanning.value = false
    }

    /** 连接 BLE 设备并尝试唤醒 (结果通过 error 通道反馈)。 */
    fun connectBleWake(address: String) {
        bleManager.connectAndWake(address) { ok, msg ->
            if (ok) {
                viewModelScope.launch { _errorEvents.send("BLE: $msg") }
            } else {
                reportError("BLE 唤醒失败: $msg")
            }
        }
    }

    // ─── 拍摄控制 ─────────────────────────────────────────────

    fun capture() {
        if (!ensureHandle()) return
        viewModelScope.launch {
            val rc = withContext(ioDispatcher) {
                bridge.nativeCapture(_handle)
            }
            if (rc != CameraBridge.CAM_OK) {
                reportError("拍摄失败 (错误码: $rc)")
            }
        }
    }

    fun captureBurst(count: Int, intervalMs: Int = 0) {
        if (!ensureHandle()) return
        viewModelScope.launch {
            withContext(ioDispatcher) {
                bridge.nativeCaptureBurst(_handle, count, intervalMs)
            }
        }
    }

    // ─── 相机参数 ─────────────────────────────────────────────

    fun setProperty(propId: Int, value: Long) {
        if (!ensureHandle()) return
        viewModelScope.launch(ioDispatcher) {
            bridge.nativeSetProperty(_handle, propId, value)
        }
    }

    suspend fun getProperty(propId: Int): Long {
        if (_handle == 0L) return 0L
        return withContext(ioDispatcher) {
            bridge.nativeGetProperty(_handle, propId)
        }
    }

    /**
     * 参数增减调节:先 getProperty 拿当前值,再加 delta 后 setProperty。
     * 用于 LiveView 参数浮层的 +1/-1 按钮。
     * 注意:PTP 属性值的编码因属性而异,这里只做简单线性加减。
     * 对于快门/光圈等档位型参数,更精确的做法是档位映射表。
     */
    fun adjustProperty(propId: Int, delta: Long) {
        if (!ensureHandle()) return
        viewModelScope.launch {
            val current = withContext(ioDispatcher) {
                bridge.nativeGetProperty(_handle, propId)
            }
            val newVal = current + delta
            withContext(ioDispatcher) {
                bridge.nativeSetProperty(_handle, propId, newVal)
            }
        }
    }

    /** 参数轮询一次批量读取的属性码(顺序与 vals 下标一一对应) */
    private val pollPropIds = intArrayOf(
        CameraBridge.PROP_SHUTTER_SPEED,
        CameraBridge.PROP_APERTURE,
        CameraBridge.PROP_ISO,
        CameraBridge.PROP_EXPOSURE_COMP,
        CameraBridge.PROP_FOCUS_MODE,
        CameraBridge.PROP_WHITE_BALANCE,
        CameraBridge.PROP_IMAGE_QUALITY,
    )

    /**
     * 连接成功后启动参数轮询,每 2s 刷新一次拍摄参数。
     * UI 通过 cameraProperties StateFlow 观察。
     * v3: 七个属性改为单次批量 JNI 调用 (旧版 7 次独立跨边界调用)。
     */
    private fun startPropertyPolling() {
        propPollJob?.cancel()
        propPollJob = viewModelScope.launch {
            while (isActive && _status.value == CameraBridge.STATUS_CONNECTED) {
                try {
                    if (_handle != 0L) {
                        val vals = withContext(ioDispatcher) {
                            bridge.nativeGetProperties(_handle, pollPropIds)
                        }
                        if (vals != null && vals.size == pollPropIds.size) {
                            _cameraProperties.value = _cameraProperties.value.copy(
                                shutterSpeed = formatShutter(vals[0]),
                                aperture = formatAperture(vals[1]),
                                iso = formatIso(vals[2]),
                                ev = formatEv(vals[3]),
                                focusMode = formatFocusMode(vals[4]),
                                whiteBalance = formatWb(vals[5]),
                                imageQuality = formatQuality(vals[6]),
                            )
                        }
                    }
                } catch (e: Exception) {
                    // native 调用失败,保持上次值
                }
                delay(2000)
            }
        }
    }

    // ─── 文件传输 ─────────────────────────────────────────────

    /** 传输进度回调对象(native 层通过 JNI 回调),转发给 TransferManager */
    private val transferProgressCallback = object : TransferProgressCallback {
        override fun onProgress(jobId: Int, speedMbps: Double, percent: Int, status: Int) {
            transferManager.onNativeProgress(jobId, speedMbps, percent, status)
        }
    }

    fun startTransfer(objectHandle: Long, destPath: String) {
        if (!ensureHandle()) return
        transferManager.startTransfer(objectHandle, destPath)
    }

    fun cancelTransfer(jobId: Int) {
        transferManager.cancelTransfer(jobId)
    }

    /** 全部暂停:native 层无 pause 语义,等同于取消所有活跃任务 */
    fun pauseAllTransfers() {
        transferManager.pauseAllTransfers()
    }

    /** 全部取消:取消所有非完成态任务 */
    fun cancelAllTransfers() {
        transferManager.cancelAllTransfers()
    }

    /** 断点续传:重新对同一 objectHandle 发起传输(native 层支持 offset 续传) */
    fun resumeTransfer(jobId: Int) {
        transferManager.resumeTransfer(jobId)
    }

    /** 重新传输:从头开始 */
    fun retryTransfer(jobId: Int) {
        transferManager.retryTransfer(jobId)
    }

    // ─── FTP 自动化 ────────────────────────────────────────────

    /**
     * 将本地文件异步上传到 FTP (先按当前设置下发 FtpConfig, 再导出)。
     * 用户名为空时 C 层回退为 anonymous 登录。
     */
    fun exportToFtp(localPath: String) {
        if (_handle == 0L) return
        viewModelScope.launch(ioDispatcher) {
            val s = _settings.value
            bridge.nativeSetFtpConfig(
                _handle, s.ftpHost, s.ftpPort,
                s.ftpUsername, s.ftpPassword, s.ftpRemotePath,
                s.ftpsEncryption, s.ftpAutoUpload,
            )
            val rc = bridge.nativeExportToFtp(_handle, localPath)
            if (rc != CameraBridge.CAM_OK) {
                reportError("FTP 上传失败 (错误码: $rc)")
            }
        }
    }

    // ─── App 设置(v2 新增)────────────────────────────────────

    fun updateSettings(transform: (AppSettings) -> AppSettings) {
        _settings.value = transform(_settings.value)
        settingsRepo.save(_settings.value)
    }

    /** 加密存储是否可用(FTP 密码安全落盘)。不可用 → UI 显示安全警告。 */
    val secretStorageAvailable: Boolean get() = settingsRepo.isSecretStorageAvailable()

    // ─── 文件列举 / 缩略图 / 删除 ─────────────────────────────

    /**
     * 拉取相机文件列表。
     * @param storageId 0=全部; 1=CF-A; 2=SD-B
     */
    fun listFiles(storageId: Int = CameraBridge.STORAGE_ALL) {
        if (!ensureHandle()) return
        viewModelScope.launch {
            _fileListLoading.value = true
            val raw = withContext(ioDispatcher) {
                bridge.nativeListFiles(_handle, storageId)
            }
            val parsed = raw?.mapNotNull { CameraFile.fromRaw(it) } ?: emptyList()
            _fileList.value = parsed
            _fileListLoading.value = false
        }
    }

    /** 生成传输目标路径(App 私有目录,绕过 Scoped Storage 限制)
     *  Android 10+ 不能写公共 /DCIM,改用 getExternalFilesDir(DCIM) */
    private fun buildDestPath(filename: String): String {
        val dir = getApplication<Application>().getExternalFilesDir(android.os.Environment.DIRECTORY_DCIM)
            ?: getApplication<Application>().filesDir
        return "${dir.absolutePath}/NikonConnect/$filename"
    }

    /**
     * 从 settings.storageTarget (如 "/DCIM/NikonConnect") 解析相册子目录名。
     * 用户可自定义保存目录; 无法解析时回退默认 "NikonConnect"。
     */
    private fun settingsAlbumDir(): String {
        val raw = _settings.value.storageTarget.trim()
        return raw.substringAfterLast('/').takeIf { it.isNotBlank() }
            ?: StorageManager.DEFAULT_ALBUM_DIR
    }

    /** 便捷方法:用 app 私有目录作为传输目标 */
    fun startTransferToApp(objectHandle: Long, filename: String) {
        startTransfer(objectHandle, buildDestPath(filename))
    }

    /** 获取缩略图 JPEG 字节(挂起,UI 侧用 rememberAsyncImage 或 BitmapFactory 解码)。
     *  命中缓存直接返回, 未命中则走 nativeGetThumbnail 并写缓存。 */
    suspend fun getThumbnail(objectHandle: Long): ByteArray? {
        thumbnailCache[objectHandle]?.let { return it }
        if (_handle == 0L) return null
        val bytes = withContext(ioDispatcher) {
            bridge.nativeGetThumbnail(_handle, objectHandle)
        } ?: return null
        thumbnailCache[objectHandle] = bytes
        return bytes
    }

    /**
     * 读取相机当前 Picture Control 参数 (PTP 0x90CC)。
     * @return 解码后的参数; 未连接/失败返回 null
     */
    suspend fun getPictureControl(): PictureControl? {
        if (_handle == 0L) return null
        val raw = withContext(ioDispatcher) { bridge.nativeGetPictCtrl(_handle) }
            ?: return null
        return PictureControl.fromBytes(raw)
    }

    /**
     * 写入 Picture Control 参数 (PTP 0x90CD)。
     * 与 setProperty 一致: 内部起协程, 失败写 error。
     */
    fun applyPictureControl(pc: PictureControl) {
        if (!ensureHandle()) return
        viewModelScope.launch(ioDispatcher) {
            val rc = bridge.nativeSetPictCtrl(_handle, pc.toBytes())
            if (rc != CameraBridge.CAM_OK) {
                reportError("应用预设失败 (错误码: $rc)")
            }
        }
    }

    fun deleteFile(objectHandle: Long) {
        if (!ensureHandle()) return
        viewModelScope.launch(ioDispatcher) {
            val rc = bridge.nativeDeleteFile(_handle, objectHandle)
            if (rc == CameraBridge.CAM_OK) {
                _fileList.update { it.filterNot { f -> f.objectHandle == objectHandle } }
            } else {
                reportError("删除失败 (错误码: $rc)")
            }
        }
    }

    // ─── 实时取景 ─────────────────────────────────────────────

    fun startLiveView() {
        if (!ensureHandle()) return
        if (_liveViewActive.value) return
        viewModelScope.launch {
            val rc = withContext(ioDispatcher) {
                bridge.nativeStartLiveView(_handle)
            }
            if (rc == CameraBridge.CAM_OK) {
                _liveViewActive.value = true
                startLiveViewPolling()
            } else {
                reportError("启动实时取景失败 (错误码: $rc)")
            }
        }
    }

    fun stopLiveView() {
        lvPollJob?.cancel()
        if (!_liveViewActive.value) return
        viewModelScope.launch(ioDispatcher) {
            bridge.nativeStopLiveView(_handle)
        }
        _liveViewActive.value = false
        _liveViewFrame.value = null
    }

    /**
     * 帧轮询:~30fps,delay(33ms)。nativeGetLiveViewFrame 同步阻塞取一帧 JPEG。
     * 每帧更新 liveViewFrame StateFlow,UI 侧 collect 后 BitmapFactory 解码渲染。
     *
     * 注:曾尝试做"UI 消费完上一帧才取下一帧"的背压节流,但因依赖 UI 侧回调,
     * 一旦 UI 因任何原因(帧未渲染/组件未挂载/Activity 退后台)未消费,取帧循环
     * 会永久停摆、LiveView 彻底卡死。故回退为固定 ~30fps 简单轮询,让 StateFlow
     * 自动合并中间帧,兼顾实时性与稳定性。
     */
    private fun startLiveViewPolling() {
        lvPollJob?.cancel()
        lvPollJob = viewModelScope.launch {
            while (isActive && _liveViewActive.value) {
                val frame = withContext(ioDispatcher) {
                    bridge.nativeGetLiveViewFrame(_handle)
                }
                if (frame != null && frame.isNotEmpty()) {
                    _liveViewFrame.value = frame
                }
                delay(33)  // 目标 ~30fps
            }
        }
    }

    // ─── 工具 ────────────────────────────────────────────────

    fun clearError() { _error.value = null }

    /** 统一的错误上报:写 StateFlow + 发 Channel 事件 */
    private fun reportError(msg: String) {
        _error.value = msg
        viewModelScope.launch { _errorEvents.send(msg) }
    }

    // ─── 参数格式化(native 返回 Long,转为 UI 显示字符串)──────

    private fun formatShutter(raw: Long): String {
        if (raw == 0L) return "Bulb"
        if (raw > 0) return "1/${raw}s"
        return "${-raw}s"  // 负数表示慢于 1 秒,如 -5 → "5s"
    }

    private fun formatAperture(raw: Long): String = "f/${raw / 10.0}"

    private fun formatIso(raw: Long): String = "ISO $raw"

    private fun formatEv(raw: Long): String {
        val v = raw / 10.0
        return "${if (v >= 0) "+" else ""}${"%.1f".format(v)}EV"
    }

    private fun formatFocusMode(raw: Long): String = when (raw.toInt()) {
        0 -> "MF"
        1 -> "AF-S"
        2 -> "AF-C"
        3 -> "AF-F"
        else -> "AF"
    }

    private fun formatWb(raw: Long): String = when (raw.toInt()) {
        0 -> "自动"
        1 -> "白炽灯"
        2 -> "荧光灯"
        3 -> "直射阳光"
        4 -> "闪光灯"
        5 -> "阴天"
        6 -> "阴影"
        else -> "自动"
    }

    private fun formatQuality(raw: Long): String = when (raw.toInt()) {
        0 -> "RAW"
        1 -> "JPEG"
        2 -> "RAW + JPEG"
        3 -> "TIFF"
        else -> "RAW"
    }
}
