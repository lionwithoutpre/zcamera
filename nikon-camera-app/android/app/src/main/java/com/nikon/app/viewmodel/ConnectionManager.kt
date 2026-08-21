package com.nikon.app.viewmodel

import android.app.Application
import android.content.Context
import com.nikon.app.ble.BleManager
import com.nikon.app.jni.CameraApi
import com.nikon.app.jni.CameraBridge
import com.nikon.app.jni.StatusChangeCallback
import com.nikon.data.settings.SettingsRepository
import com.nikon.model.CameraInfo
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/**
 * ConnectionManager — 连接管理域
 *
 * 从 CameraViewModel 拆分(阶段4): 负责相机扫描 / 连接(USB/Wi-Fi) / 断开 /
 * BLE 发现与唤醒 / 连接状态事件(替代轮询)。持有 [CameraViewModel] 回调,
 * 状态变化通过回调同步给门面。
 *
 * 职责边界:
 *  - handle 生命周期归 CameraService, 本类只读 [handleProvider]
 *  - 连接状态归本类管理, 门面只透出 StateFlow
 *  - 连接成功后的"补注册" (传输进度回调 / 参数轮询 / WiFi 端点持久化)
 *    通过 [onConnected] / [onDisconnected] 回调给门面协调, 避免跨域耦合
 */
internal class ConnectionManager(
    private val application: Application,
    private val bridge: CameraApi,
    private val handleProvider: () -> Long,
    private val scope: CoroutineScope,
    private val ioDispatcher: CoroutineDispatcher,
    private val enablePolling: Boolean,
    private val settingsRepo: SettingsRepository,
    /** 连接进入 CONNECTED 时回调(手动 connect/connectWifi 与 native 自动连接共用) */
    private val onConnected: (Long) -> Unit,
    /** 从 CONNECTED/TRANSFERRING 跌出时回调(门面据此停参数轮询等) */
    private val onDisconnected: () -> Unit,
    private val onError: (String) -> Unit,
    /** BLE 唤醒成功事件(走错误事件 Channel, 非错误, 仅提示) */
    private val onBleWakeEvent: (String) -> Unit,
) {
    private val _status = MutableStateFlow(CameraBridge.STATUS_DISCONNECTED)
    val status: StateFlow<Int> = _status.asStateFlow()

    private val _serviceReady = MutableStateFlow(false)
    val serviceReady: StateFlow<Boolean> = _serviceReady.asStateFlow()

    private val _cameras = MutableStateFlow<List<CameraInfo>>(emptyList())
    val cameras: StateFlow<List<CameraInfo>> = _cameras.asStateFlow()

    private val _bleDevices = MutableStateFlow<List<BleManager.Device>>(emptyList())
    val bleDevices: StateFlow<List<BleManager.Device>> = _bleDevices.asStateFlow()

    private val _bleScanning = MutableStateFlow(false)
    val bleScanning: StateFlow<Boolean> = _bleScanning.asStateFlow()

    private val bleManager = BleManager(application)

    val bleSupported: Boolean get() = bleManager.isBleSupported
    val bleEnabled: Boolean get() = bleManager.isBluetoothEnabled

    private var statusPollJob: kotlinx.coroutines.Job? = null

    init {
        if (enablePolling) startServiceReadyPolling()
    }

    fun refreshServiceReady() {
        _serviceReady.value = handleProvider() != 0L
    }

    /** 注册连接状态回调(幂等: 重复注册会替换旧回调)。 */
    fun registerStatusCallback() {
        val h = handleProvider()
        if (h == 0L) return
        try {
            bridge.nativeSetStatusCallback(h, statusCallback)
        } catch (_: Exception) { }
    }

    /**
     * native 状态机回调入口(可能来自 USB 拔出/自动连接成功等 native 侧状态变化)。
     * 逻辑与原 ViewModel 的 onNativeStatusChanged 一致, 只是触发方式从轮询变为事件。
     */
    fun onNativeStatusChanged(nativeStatus: Int) {
        when (nativeStatus) {
            CameraBridge.STATUS_CONNECTED -> {
                val local = _status.value
                if (local != CameraBridge.STATUS_CONNECTED &&
                    local != CameraBridge.STATUS_TRANSFERRING &&
                    local != CameraBridge.STATUS_CONNECTING &&
                    local != CameraBridge.STATUS_SCANNING) {
                    _status.value = CameraBridge.STATUS_CONNECTED
                    onConnected(handleProvider())
                }
            }
            CameraBridge.STATUS_DISCONNECTED,
            CameraBridge.STATUS_ERROR -> {
                if (_status.value == CameraBridge.STATUS_CONNECTED ||
                    _status.value == CameraBridge.STATUS_TRANSFERRING) {
                    _status.value = nativeStatus
                    onDisconnected()
                }
            }
            else -> Unit
        }
    }

    fun scan() {
        val h = handleProvider()
        if (h == 0L) {
            onError("相机服务未就绪,请稍候")
            return
        }
        scope.launch {
            _status.value = CameraBridge.STATUS_SCANNING
            _cameras.value = emptyList()
            // 保证扫描动画至少显示 3 秒,避免一闪而过
            val scanStart = System.currentTimeMillis()

            // Android 默认关闭 Wi-Fi 组播接收, mDNS 发现相机需要持 MulticastLock
            val wifiManager = application
                .getSystemService(Context.WIFI_SERVICE) as? android.net.wifi.WifiManager
            val multicastLock = wifiManager?.createMulticastLock("nikon-connect-scan")
            multicastLock?.setReferenceCounted(false)
            try { multicastLock?.acquire() } catch (_: Exception) {}

            val rawList = try {
                withContext(ioDispatcher) {
                    bridge.nativeScan(h) ?: emptyArray()
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
        val h = handleProvider()
        if (h == 0L) {
            onError("相机服务未就绪,请稍候")
            return
        }
        scope.launch {
            _status.value = CameraBridge.STATUS_CONNECTING
            val rc = withContext(ioDispatcher) {
                bridge.nativeConnect(h, cameraId)
            }
            if (rc == CameraBridge.CAM_OK) {
                _status.value = CameraBridge.STATUS_CONNECTED
                registerStatusCallback()
                onConnected(h)
            } else {
                _status.value = CameraBridge.STATUS_ERROR
                onError("连接失败 (错误码: $rc)")
            }
        }
    }

    /**
     * Wi-Fi 直连相机 PTP/IP 服务。
     * 手机需已连接到相机热点 (系统设置里连接), 相机 IP 通常为 192.168.1.1, 端口 15740。
     */
    fun connectWifi(ip: String, port: Int) {
        val h = handleProvider()
        if (h == 0L) {
            onError("相机服务未就绪,请稍候")
            return
        }
        scope.launch {
            _status.value = CameraBridge.STATUS_CONNECTING
            val rc = withContext(ioDispatcher) {
                bridge.nativeConnectWifi(h, ip, port)
            }
            if (rc == CameraBridge.CAM_OK) {
                _status.value = CameraBridge.STATUS_CONNECTED
                registerStatusCallback()
                // 持久化本次连接端点: 供 CameraService WiFi 断连后真正重连使用
                settingsRepo.saveLastWifiEndpoint(ip, port)
                // 填充设备信息供仪表盘显示 (Android 上 USB 扫描恒为空)。
                // 电量/存储用 -1 表示"未知/读取中": Wi-Fi 连接不返回真实值,
                // 不能填硬编码假数据(如 100%/0GB)误导用户, UI 层对负值显示占位符。
                _cameras.value = listOf(
                    CameraInfo("$ip|wifi|$port", "NIKON (WiFi)", ip, 1, -1, -1.0, -1.0)
                )
                onConnected(h)
            } else {
                _status.value = CameraBridge.STATUS_ERROR
                onError("Wi-Fi 连接失败 (错误码: $rc)")
            }
        }
    }

    /** 传输开始/结束时由门面调用: 切 TRANSFERRING / 回落 CONNECTED。 */
    fun onTransferActiveChanged(active: Boolean) {
        _status.value = if (active) CameraBridge.STATUS_TRANSFERRING
                        else CameraBridge.STATUS_CONNECTED
    }

    fun disconnect() {
        scope.launch(ioDispatcher) {
            bridge.nativeDisconnect(handleProvider())
        }
        _status.value = CameraBridge.STATUS_DISCONNECTED
        onDisconnected()
    }

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
            onError("BLE 扫描启动失败, 请检查蓝牙是否开启")
        }
    }

    /** 停止 BLE 扫描。 */
    fun stopBleScan() {
        bleManager.stopScan()
        _bleScanning.value = false
    }

    /** 连接 BLE 设备并尝试唤醒 (结果通过事件/错误通道反馈)。 */
    fun connectBleWake(address: String) {
        bleManager.connectAndWake(address) { ok, msg ->
            if (ok) {
                onBleWakeEvent("BLE: $msg")
            } else {
                onError("BLE 唤醒失败: $msg")
            }
        }
    }

    /** 关闭时释放资源(BLE 连接 + 就绪轮询)。 */
    fun close() {
        bleManager.close()
        statusPollJob?.cancel()
    }

    /** native 状态回调实现: 事件化推送(替代 1s 轮询 nativeGetStatus)。 */
    private val statusCallback = object : StatusChangeCallback {
        override fun onStatusChanged(status: Int) = onNativeStatusChanged(status)
    }

    /**
     * 每 1s 轻量轮询 Service 生命周期(handle 0↔非0), 并在就绪时注册状态回调。
     * 注意: 不做 nativeGetStatus 轮询 —— 状态变化由 native 回调驱动, 消除轮询延迟。
     */
    private fun startServiceReadyPolling() {
        statusPollJob?.cancel()
        statusPollJob = scope.launch {
            while (isActive) {
                val h = handleProvider()
                if (h != 0L) {
                    if (!_serviceReady.value) {
                        _serviceReady.value = true
                        // Service 就绪时注册状态回调(覆盖自动连接成功场景)
                        registerStatusCallback()
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
}