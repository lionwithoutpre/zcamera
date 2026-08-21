package com.nikon.app.viewmodel

import com.nikon.app.jni.CameraApi
import com.nikon.app.jni.CameraBridge
import com.nikon.data.settings.SettingsRepository
import com.nikon.model.AppSettings
import com.nikon.model.CameraProperties
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/**
 * CameraSettingsManager — 相机参数与 App 设置域
 *
 * 从 CameraViewModel 拆分(阶段4): 负责
 *  - 相机参数轮询(连接后每 2s 批量读取, 格式化后发布 cameraProperties)
 *  - 参数读写 / 增减调节
 *  - App 设置读写 + SharedPreferences 持久化(经 SettingsRepository)
 *  - FTP 自动上传(按当前设置下发 FtpConfig 再导出)
 *  - 拍摄控制(capture / captureBurst)
 *
 * 持有 [CameraViewModel] 回调, 错误通过 onError 同步给门面。
 */
internal class CameraSettingsManager(
    private val bridge: CameraApi,
    private val handleProvider: () -> Long,
    private val scope: CoroutineScope,
    private val ioDispatcher: CoroutineDispatcher,
    private val settingsRepo: SettingsRepository,
    private val onError: (String) -> Unit,
    /** 参数轮询仅在已连接时运行(从外部读取状态, 避免跨域持有状态) */
    private val isConnected: () -> Boolean,
) {
    private val _cameraProperties = MutableStateFlow(CameraProperties())
    val cameraProperties: StateFlow<CameraProperties> = _cameraProperties.asStateFlow()

    private val _settings = MutableStateFlow(settingsRepo.load())
    val settings: StateFlow<AppSettings> = _settings.asStateFlow()

    private var propPollJob: Job? = null

    /** 加密存储是否可用(FTP 密码安全落盘)。不可用 → UI 显示安全警告。 */
    val secretStorageAvailable: Boolean get() = settingsRepo.isSecretStorageAvailable()

    // ─── App 设置 ──────────────────────────────────────────────

    fun updateSettings(transform: (AppSettings) -> AppSettings) {
        _settings.value = transform(_settings.value)
        settingsRepo.save(_settings.value)
    }

    // ─── 拍摄控制 ──────────────────────────────────────────────

    fun capture() {
        val h = handleProvider()
        if (h == 0L) {
            onError("相机服务未就绪,请稍候")
            return
        }
        scope.launch {
            val rc = withContext(ioDispatcher) {
                bridge.nativeCapture(h)
            }
            if (rc != CameraBridge.CAM_OK) {
                onError("拍摄失败 (错误码: $rc)")
            }
        }
    }

    fun captureBurst(count: Int, intervalMs: Int = 0) {
        val h = handleProvider()
        if (h == 0L) {
            onError("相机服务未就绪,请稍候")
            return
        }
        scope.launch {
            withContext(ioDispatcher) {
                bridge.nativeCaptureBurst(h, count, intervalMs)
            }
        }
    }

    // ─── 相机参数 ──────────────────────────────────────────────

    fun setProperty(propId: Int, value: Long) {
        val h = handleProvider()
        if (h == 0L) {
            onError("相机服务未就绪,请稍候")
            return
        }
        scope.launch(ioDispatcher) {
            bridge.nativeSetProperty(h, propId, value)
        }
    }

    suspend fun getProperty(propId: Int): Long {
        val h = handleProvider()
        if (h == 0L) return 0L
        return withContext(ioDispatcher) {
            bridge.nativeGetProperty(h, propId)
        }
    }

    /**
     * 参数增减调节:先 getProperty 拿当前值,再加 delta 后 setProperty。
     * 用于 LiveView 参数浮层的 +1/-1 按钮。
     * 注意:PTP 属性值的编码因属性而异,这里只做简单线性加减。
     * 对于快门/光圈等档位型参数,更精确的做法是档位映射表。
     */
    fun adjustProperty(propId: Int, delta: Long) {
        val h = handleProvider()
        if (h == 0L) {
            onError("相机服务未就绪,请稍候")
            return
        }
        scope.launch {
            val current = withContext(ioDispatcher) {
                bridge.nativeGetProperty(h, propId)
            }
            val newVal = current + delta
            withContext(ioDispatcher) {
                bridge.nativeSetProperty(h, propId, newVal)
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
    fun startPropertyPolling() {
        propPollJob?.cancel()
        propPollJob = scope.launch {
            while (isActive && isConnected()) {
                val h = handleProvider()
                if (h != 0L) {
                    try {
                        val vals = withContext(ioDispatcher) {
                            bridge.nativeGetProperties(h, pollPropIds)
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
                    } catch (e: Exception) {
                        // native 调用失败,保持上次值
                    }
                }
                delay(2000)
            }
        }
    }

    /** 停止参数轮询(断连/关闭时)。 */
    fun stopPropertyPolling() {
        propPollJob?.cancel()
    }

    // ─── FTP 自动化 ────────────────────────────────────────────

    /**
     * 将本地文件异步上传到 FTP (先按当前设置下发 FtpConfig, 再导出)。
     * 用户名为空时 C 层回退为 anonymous 登录。
     */
    fun exportToFtp(localPath: String) {
        val h = handleProvider()
        if (h == 0L) return
        scope.launch(ioDispatcher) {
            val s = _settings.value
            bridge.nativeSetFtpConfig(
                h, s.ftpHost, s.ftpPort,
                s.ftpUsername, s.ftpPassword, s.ftpRemotePath,
                s.ftpsEncryption, s.ftpAutoUpload,
            )
            val rc = bridge.nativeExportToFtp(h, localPath)
            if (rc != CameraBridge.CAM_OK) {
                onError("FTP 上传失败 (错误码: $rc)")
            }
        }
    }

    // ─── 参数格式化(native 返回 Long,转为 UI 显示字符串)──────

    fun formatShutter(raw: Long): String {
        if (raw == 0L) return "Bulb"
        if (raw > 0) return "1/${raw}s"
        return "${-raw}s"  // 负数表示慢于 1 秒,如 -5 → "5s"
    }

    fun formatAperture(raw: Long): String = "f/${raw / 10.0}"

    fun formatIso(raw: Long): String = "ISO $raw"

    fun formatEv(raw: Long): String {
        val v = raw / 10.0
        return "${if (v >= 0) "+" else ""}${"%.1f".format(v)}EV"
    }

    fun formatFocusMode(raw: Long): String = when (raw.toInt()) {
        0 -> "MF"
        1 -> "AF-S"
        2 -> "AF-C"
        3 -> "AF-F"
        else -> "AF"
    }

    fun formatWb(raw: Long): String = when (raw.toInt()) {
        0 -> "自动"
        1 -> "白炽灯"
        2 -> "荧光灯"
        3 -> "直射阳光"
        4 -> "闪光灯"
        5 -> "阴天"
        6 -> "阴影"
        else -> "自动"
    }

    fun formatQuality(raw: Long): String = when (raw.toInt()) {
        0 -> "RAW"
        1 -> "JPEG"
        2 -> "RAW + JPEG"
        3 -> "TIFF"
        else -> "RAW"
    }
}