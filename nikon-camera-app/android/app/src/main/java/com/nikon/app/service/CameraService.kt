package com.nikon.app.service

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import android.os.Build
import android.os.IBinder
import android.util.Log
import androidx.core.app.NotificationCompat
import androidx.core.content.ContextCompat
import com.nikon.app.jni.CameraBridge
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch

/**
 * CameraService — Android 前台服务
 *
 * 职责:
 *   1. 持有与相机的长连接 (跨 Activity 生命周期)
 *   2. USB 设备热插拔监听 + 权限请求 + 设备打开
 *   3. 执行边拍边传后台任务
 *   4. 维护前台通知 (Android 8+ 强制要求)
 *
 * USB 权限流程:
 *   设备插入 → 检查 Vendor ID (0x04B0) → requestPermission()
 *   → BroadcastReceiver 收到授权 → openDevice() → getFileDescriptor()
 *   → nativeConnectUsbFd(fd) → PTP 会话建立
 */
class CameraService : Service() {

    companion object {
        const val ACTION_CONNECT    = "com.nikon.app.CONNECT"
        const val ACTION_DISCONNECT = "com.nikon.app.DISCONNECT"
        const val EXTRA_CAMERA_ID   = "camera_id"
        const val NIkon_VENDOR_ID   = 0x04B0

        private const val CHANNEL_ID   = "nikon_camera_channel"
        private const val NOTIF_ID     = 1001
        private const val ACTION_USB_PERMISSION =
            "com.nikon.app.USB_PERMISSION"
        private const val TAG = "CameraService"
    }

    private val serviceScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    // 当前正在等待权限的 USB 设备 (仅一个)
    private var pendingUsbDevice: UsbDevice? = null

    // 持有 USB 连接引用,防止 GC 关闭 fd 导致 native 读写失败
    private var usbConnection: android.hardware.usb.UsbDeviceConnection? = null

    /**
     * native handle — 唯一真源在 NikonApplication.cameraHandle。
     * 不再维护 Service 私有 _handle,消除双写不一致风险。
     * Service.onCreate 写入(非0),cleanup 置0。
     */
    private val app: com.nikon.app.NikonApplication
        get() = application as com.nikon.app.NikonApplication
    private val handle: Long get() = app.cameraHandle

    // ─── USB 广播接收器 ─────────────────────────────────────

    private val usbReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            when (intent.action) {
                UsbManager.ACTION_USB_DEVICE_ATTACHED -> {
                    val device: UsbDevice? =
                        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                            intent.getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
                        } else {
                            @Suppress("DEPRECATION")
                            intent.getParcelableExtra(UsbManager.EXTRA_DEVICE)
                        }
                    onUsbDeviceAttached(device)
                }

                UsbManager.ACTION_USB_DEVICE_DETACHED -> {
                    val device: UsbDevice? =
                        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                            intent.getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
                        } else {
                            @Suppress("DEPRECATION")
                            intent.getParcelableExtra(UsbManager.EXTRA_DEVICE)
                        }
                    onUsbDeviceDetached(device)
                }

                ACTION_USB_PERMISSION -> {
                    val device: UsbDevice? =
                        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                            intent.getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
                        } else {
                            @Suppress("DEPRECATION")
                            intent.getParcelableExtra(UsbManager.EXTRA_DEVICE)
                        }
                    val granted = intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)
                    onUsbPermissionResult(device, granted)
                }
            }
        }
    }

    // ─── 生命周期 ───────────────────────────────────────────

    override fun onCreate() {
        super.onCreate()

        // 前台通知必须最先就位:Android 8+ 要求 startForeground 在 onCreate 后 5 秒内
        // 调用,否则系统抛 RemoteServiceException 直接杀进程。放在 nativeCreate 之前,
        // 即使 native 初始化耗时/阻塞也不会触发超时闪退。
        createNotificationChannel()
        startForeground(NOTIF_ID, buildNotification("待机中"))

        // native 初始化:可能失败(无 .so / 无相机硬件 / 异常)。必须捕获,
        // 否则会让整个 App 进程崩溃。失败时进入"待机"模式,UI 显示未连接,
        // 用户插入相机后仍可正常工作。
        try {
            val h = CameraBridge.nativeCreate(CameraBridge.TRANSPORT_AUTO)
            if (h != 0L) {
                app.cameraHandle = h
                Log.i(TAG, "nativeCreate ok handle=$h")
            } else {
                Log.w(TAG, "nativeCreate 返回 0:无相机或未初始化,进入待机模式")
            }
        } catch (t: Throwable) {
            Log.e(TAG, "nativeCreate 失败,进入待机模式", t)
        }

        registerUsbReceiver()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_CONNECT -> {
                val cameraId = intent.getStringExtra(EXTRA_CAMERA_ID)
                    ?: return START_STICKY
                serviceScope.launch {
                    val rc = CameraBridge.nativeConnect(handle, cameraId)
                    if (rc == CameraBridge.CAM_OK) {
                        updateNotification("已连接")
                    } else {
                        updateNotification("连接失败")
                    }
                }
            }
            ACTION_DISCONNECT -> {
                serviceScope.launch {
                    CameraBridge.nativeDisconnect(handle)
                    updateNotification("已断开")
                }
            }
        }
        return START_STICKY
    }

    override fun onDestroy() {
        cleanup()
        super.onDestroy()
    }

    override fun onTaskRemoved(rootIntent: Intent?) {
        // 用户从最近任务划掉 App 时,系统不保证调 onDestroy,这里兜底清理
        cleanup()
        super.onTaskRemoved(rootIntent)
    }

    private fun cleanup() {
        unregisterUsbReceiver()
        serviceScope.cancel()
        if (handle != 0L) {
            CameraBridge.nativeDestroy(handle)
            app.cameraHandle = 0L
        }
        // 关闭 USB 连接(nativeDisconnect 之后)
        try { usbConnection?.close() } catch (_: Exception) {}
        usbConnection = null
    }

    override fun onBind(intent: Intent?): IBinder? = null

    // ─── USB 广播注册 / 注销 ────────────────────────────────

    private fun registerUsbReceiver() {
        val filter = IntentFilter().apply {
            addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED)
            addAction(UsbManager.ACTION_USB_DEVICE_DETACHED)
            addAction(ACTION_USB_PERMISSION)
        }
        // USB 权限广播由系统 UsbManager 经本应用的 PendingIntent 回送, 仅来自系统/本应用,
        // 不需对其他应用导出。统一用 ContextCompat 声明 RECEIVER_NOT_EXPORTED 以兼容 Android U+。
        ContextCompat.registerReceiver(
            this,
            usbReceiver,
            filter,
            ContextCompat.RECEIVER_NOT_EXPORTED
        )
    }

    private fun unregisterUsbReceiver() {
        try {
            unregisterReceiver(usbReceiver)
        } catch (_: IllegalArgumentException) {
            // 未注册或已注销
        }
    }

    // ─── USB 设备附加 ───────────────────────────────────────

    /**
     * USB 设备插入: 过滤尼康相机, 请求权限。
     */
    private fun onUsbDeviceAttached(device: UsbDevice?) {
        if (device == null) return
        if (device.vendorId != NIkon_VENDOR_ID) return

        val name = device.productName ?: "Nikon Camera"
        updateNotification("发现相机: $name")

        // 请求 USB 权限 (弹出系统对话框)
        val usbManager = getSystemService(Context.USB_SERVICE) as? UsbManager ?: return

        if (usbManager.hasPermission(device)) {
            // 已有权限, 直接打开
            openAndConnectDevice(usbManager, device)
        } else {
            // 请求权限 (结果通过 BroadcastReceiver 返回)
            pendingUsbDevice = device
            val permissionIntent = Intent(ACTION_USB_PERMISSION).apply {
                setPackage(packageName)
            }
            val pi = PendingIntent.getBroadcast(
                this, 0, permissionIntent,
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                    PendingIntent.FLAG_IMMUTABLE
                } else {
                    0
                }
            )
            usbManager.requestPermission(device, pi)
            updateNotification("请求 USB 权限...")
        }
    }

    /**
     * USB 设备拔出。
     */
    private fun onUsbDeviceDetached(device: UsbDevice?) {
        if (device == null) return
        if (device.vendorId != NIkon_VENDOR_ID) return

        serviceScope.launch {
            if (handle != 0L) {
                CameraBridge.nativeDisconnect(handle)
            }
            // 通知 ViewModel 层状态变更(通过 Application 单例广播)
            // ViewModel 的 status 轮询或下次操作会感知到断开
            updateNotification("USB 相机已断开")
        }
        pendingUsbDevice = null
        try { usbConnection?.close() } catch (_: Exception) {}
        usbConnection = null
    }

    /**
     * USB 权限请求结果回调。
     */
    private fun onUsbPermissionResult(device: UsbDevice?, granted: Boolean) {
        if (device == null) return
        pendingUsbDevice = null

        if (!granted) {
            updateNotification("USB 权限被拒绝")
            return
        }

        val usbManager = getSystemService(Context.USB_SERVICE) as? UsbManager ?: return
        openAndConnectDevice(usbManager, device)
    }

    // ─── 打开设备并建立 PTP 连接 ─────────────────────────────

    /**
     * 打开 UsbDeviceConnection, 提取原生 fd, 注入 native 层。
     */
    private fun openAndConnectDevice(usbManager: UsbManager, device: UsbDevice) {
        val name = device.productName ?: "Nikon Camera"
        val serial = device.serialNumber ?: "unknown"
        updateNotification("正在连接 $name...")

        serviceScope.launch(Dispatchers.IO) {
            try {
                // 1) 打开 USB 设备连接
                val connection = usbManager.openDevice(device)
                if (connection == null) {
                    updateNotification("打开设备失败: $name")
                    return@launch
                }

                // 2) 尝试声明 PTP 接口
                val claimed = tryClaimPtpInterface(connection, device)
                if (!claimed) {
                    connection.close()
                    updateNotification("PTP 接口声明失败")
                    return@launch
                }

                // 3) 获取原生文件描述符
                val fd = extractNativeFd(connection)
                if (fd < 0) {
                    connection.close()
                    updateNotification("获取文件描述符失败")
                    return@launch
                }

                // 4) 注入 native 层
                val rc = CameraBridge.nativeConnectUsbFd(handle, fd, serial)
                if (rc == CameraBridge.CAM_OK) {
                    updateNotification("USB 已连接: $name")
                } else {
                    updateNotification("PTP 会话建立失败 (code=$rc)")
                    // 关闭 fd 会由 native disconnect 处理
                }

            } catch (e: Exception) {
                updateNotification("连接异常: ${e.message}")
            }
        }
    }

    /**
     * 声明 PTP (Still Image) 接口。
     * 尼康相机通常暴露两个接口:
     *   - 接口 0: MTP (Mass Storage)
     *   - 接口 1: PTP (Still Image) ← 我们需要这个
     * 优先声明 class=6 (Still Imaging) 的接口;
     * 如果没有,回退到 class=255 (Vendor Specific,部分尼康型号用);
     * 最后才尝试声明所有接口(兼容极端情况)。
     *
     * @return true 如果成功声明至少一个接口
     */
    private fun tryClaimPtpInterface(
        connection: android.hardware.usb.UsbDeviceConnection,
        device: UsbDevice
    ): Boolean {
        // 第一优先级:class=6 (Still Imaging), subclass=1 (PTP)
        for (i in 0 until device.interfaceCount) {
            val iface = device.getInterface(i)
            if (iface.interfaceClass == 6 && connection.claimInterface(iface, true)) {
                return true
            }
        }
        // 第二优先级:class=255 (Vendor Specific,部分尼康型号)
        for (i in 0 until device.interfaceCount) {
            val iface = device.getInterface(i)
            if (iface.interfaceClass == 255 && connection.claimInterface(iface, true)) {
                return true
            }
        }
        // 最后回退:声明第一个可用接口(极端兼容)
        for (i in 0 until device.interfaceCount) {
            val iface = device.getInterface(i)
            if (connection.claimInterface(iface, true)) {
                return true
            }
        }
        return false
    }

    /**
     * 从 UsbDeviceConnection 提取原生 fd。
     * 使用反射访问隐藏的 getFileDescriptor() 方法,
     * 或通过 ParcelFileDescriptor 包装。
     *
     * @return >= 0 原生 fd; < 0 失败
     */
    private fun extractNativeFd(connection: android.hardware.usb.UsbDeviceConnection): Int {
        // 持有 connection 引用防止 GC 关 fd
        usbConnection = connection
        // connection.fileDescriptor 返回原始 fd (Int)
        return try {
            connection.fileDescriptor
        } catch (e: Exception) {
            try {
                val method = connection.javaClass.getMethod("getFileDescriptor")
                method.invoke(connection) as Int
            } catch (_: Exception) {
                -1
            }
        }
    }

    // ─── 通知 ────────────────────────────────────────────────

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                "Nikon 相机连接",
                NotificationManager.IMPORTANCE_LOW
            ).apply { description = "相机连接与传输状态" }
            getSystemService(NotificationManager::class.java)
                .createNotificationChannel(channel)
        }
    }

    private fun buildNotification(status: String): Notification {
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("Nikon Camera Connect")
            .setContentText(status)
            .setSmallIcon(android.R.drawable.ic_menu_camera)
            .setOngoing(true)
            .build()
    }

    private fun updateNotification(status: String) {
        val nm = getSystemService(NotificationManager::class.java)
        nm.notify(NOTIF_ID, buildNotification(status))
    }
}
