package com.nikon.app.viewmodel

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.nikon.app.jni.CameraApi
import com.nikon.app.jni.CameraBridge
import com.nikon.app.jni.TransferProgressCallback
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.receiveAsFlow
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

    private val _transferProgress = MutableStateFlow<TransferState?>(null)
    val transferProgress: StateFlow<TransferState?> = _transferProgress.asStateFlow()

    // ─── 传输任务列表(v2 新增)─────────────────────────────────
    // 本地维护,native 层无列举接口;发起传输时 add,完成/失败时 update
    private val _transferJobs = MutableStateFlow<List<TransferJob>>(emptyList())
    val transferJobs: StateFlow<List<TransferJob>> = _transferJobs.asStateFlow()

    private var nextJobId = 1

    // ─── 相机参数(v2 新增,轮询读取)──────────────────────────
    private val _cameraProperties = MutableStateFlow(CameraProperties())
    val cameraProperties: StateFlow<CameraProperties> = _cameraProperties.asStateFlow()

    private var propPollJob: Job? = null

    // ─── App 设置(v2 新增,跨页面共享 + SharedPreferences 持久化)──
    private val _prefs = application.getSharedPreferences("nikon_settings", android.content.Context.MODE_PRIVATE)
    private val _settings = MutableStateFlow(loadSettingsFromPrefs())
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

    // ─── 初始化 ───────────────────────────────────────────────
    // 不再自己 nativeCreate,handle 由 CameraService 持有。
    // Service 启动后 Application.cameraHandle 有值,所有方法读 _handle 即可。

    init {
        // 轻量轮询:检测 Service 何时就绪(handle 从 0 变非 0)
        // 以及连接状态变化(USB 拔出等 native 层主动断开的场景)
        // 测试可关闭(enablePolling=false)以避免虚拟时钟下自旋。
        if (enablePolling) startStatusPolling()
    }

    /**
     * 每 1s 轮询:
     * 1) handle 从 0→非0:Service 就绪,更新 serviceReady
     * 2) handle 非0:调 nativeGetStatus 同步 _status(USB 拔出时 native 层会切 DISCONNECTED)
     * 3) handle 从 非0→0:Service 销毁,更新 serviceReady + 状态
     */
    private fun startStatusPolling() {
        statusPollJob?.cancel()
        statusPollJob = viewModelScope.launch {
            while (isActive) {
                val h = _handle
                if (h != 0L) {
                    if (!_serviceReady.value) _serviceReady.value = true
                    // 同步连接状态(native 层可能因 USB 拔出主动断开)
                    try {
                        val nativeStatus = withContext(ioDispatcher) {
                            bridge.nativeGetStatus(h)
                        }
                        // 只在状态确实变化时更新,避免覆盖 ViewModel 主动设的中间态
                        if (nativeStatus == CameraBridge.STATUS_DISCONNECTED ||
                            nativeStatus == CameraBridge.STATUS_ERROR) {
                            if (_status.value == CameraBridge.STATUS_CONNECTED ||
                                _status.value == CameraBridge.STATUS_TRANSFERRING) {
                                _status.value = nativeStatus
                                propPollJob?.cancel()
                            }
                        }
                    } catch (_: Exception) { }
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

    override fun onCleared() {
        super.onCleared()
        // 停止所有轮询,但不 destroy handle — 那是 Service 的职责
        propPollJob?.cancel()
        lvPollJob?.cancel()
        statusPollJob?.cancel()
        // LiveView 如果开着,通知 Service 层停止(通过 handle)
        if (_liveViewActive.value && _handle != 0L) {
            viewModelScope.launch(ioDispatcher) {
                bridge.nativeStopLiveView(_handle)
            }
        }
    }

    // ─── 连接管理 ─────────────────────────────────────────────

    fun scan() {
        if (!ensureHandle()) return
        viewModelScope.launch {
            _status.value = CameraBridge.STATUS_SCANNING
            val rawList = withContext(ioDispatcher) {
                bridge.nativeScan(_handle) ?: emptyArray()
            }
            val parsed = rawList.mapNotNull { CameraInfo.fromRaw(it) }
            _cameras.value = parsed
            _status.value = if (parsed.isEmpty())
                CameraBridge.STATUS_DISCONNECTED
            else
                CameraBridge.STATUS_DISCONNECTED  // 等用户选择后再连接
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
                // 注册传输进度回调
                bridge.nativeRegisterProgressCallback(_handle, transferProgressCallback)
                startPropertyPolling()
            } else {
                _status.value = CameraBridge.STATUS_ERROR
                reportError("连接失败 (错误码: $rc)")
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

    /**
     * 连接成功后启动参数轮询,每 2s 刷新一次拍摄参数。
     * UI 通过 cameraProperties StateFlow 观察。
     */
    private fun startPropertyPolling() {
        propPollJob?.cancel()
        propPollJob = viewModelScope.launch {
            while (isActive && _status.value == CameraBridge.STATUS_CONNECTED) {
                try {
                    val shutter = getProperty(CameraBridge.PROP_SHUTTER_SPEED)
                    val aperture = getProperty(CameraBridge.PROP_APERTURE)
                    val iso = getProperty(CameraBridge.PROP_ISO)
                    val ev = getProperty(CameraBridge.PROP_EXPOSURE_COMP)
                    val focus = getProperty(CameraBridge.PROP_FOCUS_MODE)
                    val wb = getProperty(CameraBridge.PROP_WHITE_BALANCE)
                    val quality = getProperty(CameraBridge.PROP_IMAGE_QUALITY)

                    _cameraProperties.value = _cameraProperties.value.copy(
                        shutterSpeed = formatShutter(shutter),
                        aperture = formatAperture(aperture),
                        iso = formatIso(iso),
                        ev = formatEv(ev),
                        focusMode = formatFocusMode(focus),
                        whiteBalance = formatWb(wb),
                        imageQuality = formatQuality(quality),
                    )
                } catch (e: Exception) {
                    // native 调用失败,保持上次值
                }
                delay(2000)
            }
        }
    }

    // ─── 文件传输 ─────────────────────────────────────────────

    /** 传输进度回调对象(native 层通过 JNI 回调) */
    private val transferProgressCallback = object : TransferProgressCallback {
        override fun onProgress(jobId: Int, speedMbps: Double, percent: Int, status: Int) {
            val mappedStatus = when (status) {
                2    -> TransferStatus.DONE
                -1   -> TransferStatus.FAILED
                else -> TransferStatus.ACTIVE
            }
            val note = when (mappedStatus) {
                TransferStatus.DONE   -> "完成"
                TransferStatus.FAILED -> "传输失败"
                else                  -> "传输中"
            }
            updateJob(jobId) { it.copy(percent = percent, speedMbps = speedMbps, status = mappedStatus, note = note) }

            // 完成或失败时 promote 下一个
            if (mappedStatus == TransferStatus.DONE || mappedStatus == TransferStatus.FAILED) {
                promoteNextWaiting()
                if (_transferJobs.value.none { it.status == TransferStatus.ACTIVE }) {
                    _status.value = CameraBridge.STATUS_CONNECTED
                }
            }
        }
    }

    fun startTransfer(objectHandle: Long, destPath: String) {
        if (!ensureHandle()) return
        // 并发控制:活跃任务数 >= concurrentJobs 时排队 WAITING
        val activeCount = _transferJobs.value.count { it.status == TransferStatus.ACTIVE }
        if (activeCount >= _settings.value.concurrentJobs) {
            // 排队:用负数 id 占位(native 还没启动)
            val tempId = -(nextJobId++)
            val job = TransferJob(
                id = tempId,
                objectHandle = objectHandle,
                destPath = destPath,
                filename = destPath.substringAfterLast('/'),
                status = TransferStatus.WAITING,
                percent = 0,
                speedMbps = 0.0,
                note = "等待中",
            )
            _transferJobs.value = _transferJobs.value + job
            return
        }

        _status.value = CameraBridge.STATUS_TRANSFERRING
        launchNativeTransfer(objectHandle, destPath)
    }

    /** 调 nativeStartTransfer,用返回的 nativeJobId 创建 ACTIVE job */
    private fun launchNativeTransfer(objectHandle: Long, destPath: String) {
        viewModelScope.launch {
            val nativeJobId = withContext(ioDispatcher) {
                bridge.nativeStartTransfer(_handle, objectHandle, destPath)
            }
            if (nativeJobId < 0) {
                reportError("传输失败 (错误码: $nativeJobId)")
                _transferJobs.value = _transferJobs.value + TransferJob(
                    id = nextJobId++,
                    objectHandle = objectHandle,
                    destPath = destPath,
                    filename = destPath.substringAfterLast('/'),
                    status = TransferStatus.FAILED,
                    percent = 0,
                    speedMbps = 0.0,
                    note = "传输失败 (错误码: $nativeJobId)",
                )
            } else {
                // 用 nativeJobId 创建 job,progress 回调会持续更新
                _transferJobs.value = _transferJobs.value + TransferJob(
                    id = nativeJobId,
                    objectHandle = objectHandle,
                    destPath = destPath,
                    filename = destPath.substringAfterLast('/'),
                    status = TransferStatus.ACTIVE,
                    percent = 0,
                    speedMbps = 0.0,
                    note = "传输中",
                )
            }
            promoteNextWaiting()
            if (_transferJobs.value.none { it.status == TransferStatus.ACTIVE }) {
                _status.value = CameraBridge.STATUS_CONNECTED
            }
        }
    }

    /** 把队列里第一个 WAITING 任务提升为 ACTIVE 并启动(带并发校验) */
    private fun promoteNextWaiting() {
        val activeCount = _transferJobs.value.count { it.status == TransferStatus.ACTIVE }
        if (activeCount >= _settings.value.concurrentJobs) return
        val next = _transferJobs.value.firstOrNull { it.status == TransferStatus.WAITING } ?: return
        // 标记为 ACTIVE(保留 tempId,启动成功后会用 nativeJobId 替换)
        updateJob(next.id) { it.copy(status = TransferStatus.ACTIVE, note = "传输中") }
        viewModelScope.launch {
            val nativeJobId = withContext(ioDispatcher) {
                bridge.nativeStartTransfer(_handle, next.objectHandle, next.destPath)
            }
            if (nativeJobId < 0) {
                updateJob(next.id) { it.copy(status = TransferStatus.FAILED, note = "传输失败 (错误码: $nativeJobId)") }
                reportError("传输失败 (错误码: $nativeJobId)")
            } else {
                // 用 nativeJobId 替换 tempId
                _transferJobs.value = _transferJobs.value.map {
                    if (it.id == next.id) it.copy(id = nativeJobId) else it
                }
            }
            promoteNextWaiting()
            if (_transferJobs.value.none { it.status == TransferStatus.ACTIVE }) {
                _status.value = CameraBridge.STATUS_CONNECTED
            }
        }
    }

    fun cancelTransfer(jobId: Int) {
        if (jobId > 0 && _handle != 0L) {
            viewModelScope.launch(ioDispatcher) {
                bridge.nativeCancelTransfer(_handle, jobId)
            }
        }
        updateJob(jobId) { it.copy(status = TransferStatus.CANCELLED, note = "已取消") }
        promoteNextWaiting()
    }

    /** 全部暂停:native 层无 pause 语义,等同于取消所有活跃任务 */
    fun pauseAllTransfers() {
        val active = _transferJobs.value.filter { it.status == TransferStatus.ACTIVE }
        active.forEach { job ->
            if (job.id > 0 && _handle != 0L) {
                viewModelScope.launch(ioDispatcher) {
                    bridge.nativeCancelTransfer(_handle, job.id)
                }
            }
            updateJob(job.id) { it.copy(status = TransferStatus.PAUSED, note = "已暂停") }
        }
    }

    /** 全部取消:取消所有非完成态任务 */
    fun cancelAllTransfers() {
        val cancellable = _transferJobs.value.filter {
            it.status == TransferStatus.ACTIVE || it.status == TransferStatus.PAUSED || it.status == TransferStatus.WAITING
        }
        cancellable.forEach { job ->
            if (job.id > 0 && _handle != 0L) {
                viewModelScope.launch(ioDispatcher) {
                    bridge.nativeCancelTransfer(_handle, job.id)
                }
            }
            updateJob(job.id) { it.copy(status = TransferStatus.CANCELLED, note = "已取消") }
        }
    }

    /** 断点续传:重新对同一 objectHandle 发起传输(native 层支持 offset 续传) */
    fun resumeTransfer(jobId: Int) {
        val job = _transferJobs.value.firstOrNull { it.id == jobId } ?: return
        // 用负数 tempId 占位,启动后替换为 nativeJobId
        val tempId = -(nextJobId++)
        _transferJobs.value = _transferJobs.value + job.copy(id = tempId, status = TransferStatus.ACTIVE, percent = job.percent, note = "断点续传中")
        // 移除旧 job
        _transferJobs.value = _transferJobs.value.filterNot { it.id == jobId }
        _status.value = CameraBridge.STATUS_TRANSFERRING
        viewModelScope.launch {
            val nativeJobId = withContext(ioDispatcher) {
                bridge.nativeStartTransfer(_handle, job.objectHandle, job.destPath)
            }
            if (nativeJobId < 0) {
                updateJob(tempId) { it.copy(status = TransferStatus.FAILED, note = "续传失败 (错误码: $nativeJobId)") }
                reportError("续传失败 (错误码: $nativeJobId)")
            } else {
                _transferJobs.value = _transferJobs.value.map {
                    if (it.id == tempId) it.copy(id = nativeJobId) else it
                }
            }
            if (_transferJobs.value.none { it.status == TransferStatus.ACTIVE }) {
                _status.value = CameraBridge.STATUS_CONNECTED
            }
        }
    }

    /** 重新传输:从头开始 */
    fun retryTransfer(jobId: Int) {
        val job = _transferJobs.value.firstOrNull { it.id == jobId } ?: return
        val tempId = -(nextJobId++)
        _transferJobs.value = _transferJobs.value + job.copy(id = tempId, status = TransferStatus.ACTIVE, percent = 0, note = "重新传输中")
        _transferJobs.value = _transferJobs.value.filterNot { it.id == jobId }
        _status.value = CameraBridge.STATUS_TRANSFERRING
        viewModelScope.launch {
            val nativeJobId = withContext(ioDispatcher) {
                bridge.nativeStartTransfer(_handle, job.objectHandle, job.destPath)
            }
            if (nativeJobId < 0) {
                updateJob(tempId) { it.copy(status = TransferStatus.FAILED, note = "重传失败 (错误码: $nativeJobId)") }
                reportError("重传失败 (错误码: $nativeJobId)")
            } else {
                _transferJobs.value = _transferJobs.value.map {
                    if (it.id == tempId) it.copy(id = nativeJobId) else it
                }
            }
            if (_transferJobs.value.none { it.status == TransferStatus.ACTIVE }) {
                _status.value = CameraBridge.STATUS_CONNECTED
            }
        }
    }

    private fun updateJob(jobId: Int, transform: (TransferJob) -> TransferJob) {
        _transferJobs.value = _transferJobs.value.map {
            if (it.id == jobId) transform(it) else it
        }
    }

    // ─── App 设置(v2 新增)────────────────────────────────────

    fun updateSettings(transform: (AppSettings) -> AppSettings) {
        _settings.value = transform(_settings.value)
        saveSettingsToPrefs(_settings.value)
    }

    private fun loadSettingsFromPrefs(): AppSettings {
        return AppSettings(
            autoTransfer    = _prefs.getBoolean("autoTransfer", true),
            concurrentJobs  = _prefs.getInt("concurrentJobs", 3),
            formatJpg       = _prefs.getBoolean("formatJpg", true),
            formatNef       = _prefs.getBoolean("formatNef", true),
            formatMov       = _prefs.getBoolean("formatMov", false),
            autoChunk       = _prefs.getBoolean("autoChunk", true),
            speedAdaptive   = _prefs.getBoolean("speedAdaptive", true),
            smallFileFirst  = _prefs.getBoolean("smallFileFirst", true),
            resumeTransfer  = _prefs.getBoolean("resumeTransfer", true),
            ftpsEncryption  = _prefs.getBoolean("ftpsEncryption", true),
            ftpAutoUpload   = _prefs.getBoolean("ftpAutoUpload", false),
            preferUsb       = _prefs.getBoolean("preferUsb", true),
            notifyComplete  = _prefs.getBoolean("notifyComplete", true),
            notifyFail      = _prefs.getBoolean("notifyFail", true),
            ftpHost         = _prefs.getString("ftpHost", "192.168.1.100") ?: "192.168.1.100",
            ftpPort         = _prefs.getInt("ftpPort", 21),
            storageTarget      = _prefs.getString("storageTarget", "/DCIM/NikonConnect") ?: "/DCIM/NikonConnect",
            transferBlockSize  = _prefs.getString("transferBlockSize", "自动") ?: "自动",
            wifiPollIntervalMs = _prefs.getInt("wifiPollIntervalMs", 1000),
        )
    }

    private fun saveSettingsToPrefs(s: AppSettings) {
        _prefs.edit().apply {
            putBoolean("autoTransfer", s.autoTransfer)
            putInt("concurrentJobs", s.concurrentJobs)
            putBoolean("formatJpg", s.formatJpg)
            putBoolean("formatNef", s.formatNef)
            putBoolean("formatMov", s.formatMov)
            putBoolean("autoChunk", s.autoChunk)
            putBoolean("speedAdaptive", s.speedAdaptive)
            putBoolean("smallFileFirst", s.smallFileFirst)
            putBoolean("resumeTransfer", s.resumeTransfer)
            putBoolean("ftpsEncryption", s.ftpsEncryption)
            putBoolean("ftpAutoUpload", s.ftpAutoUpload)
            putBoolean("preferUsb", s.preferUsb)
            putBoolean("notifyComplete", s.notifyComplete)
            putBoolean("notifyFail", s.notifyFail)
            putString("ftpHost", s.ftpHost)
            putInt("ftpPort", s.ftpPort)
            putString("storageTarget", s.storageTarget)
            putString("transferBlockSize", s.transferBlockSize)
            putInt("wifiPollIntervalMs", s.wifiPollIntervalMs)
        }.apply()
    }

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

    /** 便捷方法:用 app 私有目录作为传输目标 */
    fun startTransferToApp(objectHandle: Long, filename: String) {
        startTransfer(objectHandle, buildDestPath(filename))
    }

    /** 获取缩略图 JPEG 字节(挂起,UI 侧用 rememberAsyncImage 或 BitmapFactory 解码) */
    suspend fun getThumbnail(objectHandle: Long): ByteArray? {
        if (_handle == 0L) return null
        return withContext(ioDispatcher) {
            bridge.nativeGetThumbnail(_handle, objectHandle)
        }
    }

    fun deleteFile(objectHandle: Long) {
        if (!ensureHandle()) return
        viewModelScope.launch(ioDispatcher) {
            val rc = bridge.nativeDeleteFile(_handle, objectHandle)
            if (rc == CameraBridge.CAM_OK) {
                _fileList.value = _fileList.value.filterNot { it.objectHandle == objectHandle }
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
                delay(33)  // ~30fps
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

// ─── 数据类 ──────────────────────────────────────────────────

data class CameraInfo(
    val id: String,
    val model: String,
    val serial: String,
    val transport: Int,       // 0=USB 1=Wi-Fi
    val batteryLevel: Int,
    val storageFreeGb: Double,
    val storageTotalGb: Double,
) {
    val transportLabel: String get() = if (transport == 0) "USB" else "Wi-Fi"

    companion object {
        fun fromRaw(raw: String): CameraInfo? {
            val parts = raw.split("|")
            if (parts.size < 7) return null
            return runCatching {
                CameraInfo(
                    id             = parts[0],
                    model          = parts[1],
                    serial         = parts[2],
                    transport      = parts[3].toInt(),
                    batteryLevel   = parts[4].toInt(),
                    storageFreeGb  = parts[5].toDouble(),
                    storageTotalGb = parts[6].toDouble(),
                )
            }.getOrNull()
        }
    }
}

data class TransferState(
    val jobId: Int,
    val filename: String,
    val percent: Int,
    val speedMbps: Double,
    val status: Int,  // 0=等待 1=传输中 2=完成 -1=失败
)

/** 传输任务(v2) */
data class TransferJob(
    val id: Int,
    val objectHandle: Long,
    val destPath: String,
    val filename: String,
    val format: String = "NEF",    // UI 推断或从文件名解析
    val sizeMb: Double = 0.0,
    val status: TransferStatus,
    val percent: Int,
    val speedMbps: Double,
    val note: String,
)

enum class TransferStatus {
    WAITING, ACTIVE, PAUSED, DONE, FAILED, CANCELLED
}

/** 相机拍摄参数(v2) */
data class CameraProperties(
    val shutterSpeed: String = "--",
    val aperture: String = "--",
    val iso: String = "--",
    val ev: String = "--",
    val focusMode: String = "--",
    val whiteBalance: String = "--",
    val imageQuality: String = "--",
)

/** App 设置(v2,跨页面共享;持久化待下一阶段) */
data class AppSettings(
    val autoTransfer: Boolean = true,
    val concurrentJobs: Int = 3,
    val formatJpg: Boolean = true,
    val formatNef: Boolean = true,
    val formatMov: Boolean = false,
    val autoChunk: Boolean = true,
    val speedAdaptive: Boolean = true,
    val smallFileFirst: Boolean = true,
    val resumeTransfer: Boolean = true,
    val ftpsEncryption: Boolean = true,
    val ftpAutoUpload: Boolean = false,
    val preferUsb: Boolean = true,
    val notifyComplete: Boolean = true,
    val notifyFail: Boolean = true,
    val ftpHost: String = "192.168.1.100",
    val ftpPort: Int = 21,
    val storageTarget: String = "/DCIM/NikonConnect",
    val transferBlockSize: String = "自动",
    val wifiPollIntervalMs: Int = 1000,
)

/** 相机文件(v2,对应 C 层 FileInfo) */
data class CameraFile(
    val objectHandle: Long,
    val filename: String,
    val size: Long,
    val datetime: String,
    val isRaw: Boolean,
    val isJpeg: Boolean,
    val width: Int,
    val height: Int,
    val storageId: Int,
) {
    /** 文件格式标签:NEF / JPG / 其他 */
    val format: String get() = when {
        isRaw -> "NEF"
        isJpeg -> "JPG"
        else -> filename.substringAfterLast('.', "").uppercase()
    }

    /** 人类可读大小 */
    val sizeLabel: String get() = when {
        size >= 1024 * 1024 -> "%.1f MB".format(size / (1024.0 * 1024.0))
        size >= 1024 -> "%.1f KB".format(size / 1024.0)
        else -> "$size B"
    }

    /** 日期分组标签(取 datetime 前 10 位 "YYYY-MM-DD") */
    val dateGroup: String get() = datetime.take(10)

    companion object {
        /** 解析 JNI 返回的 "handle|name|size|datetime|is_raw|is_jpeg|w|h|storage" */
        fun fromRaw(raw: String): CameraFile? {
            val p = raw.split("|")
            if (p.size < 9) return null
            return runCatching {
                CameraFile(
                    objectHandle = p[0].toLong(),
                    filename     = p[1],
                    size         = p[2].toLong(),
                    datetime     = p[3],
                    isRaw        = p[4] == "1",
                    isJpeg       = p[5] == "1",
                    width        = p[6].toInt(),
                    height       = p[7].toInt(),
                    storageId    = p[8].toInt(),
                )
            }.getOrNull()
        }
    }
}
