# Android 端规格书

## 1. Android 专属架构

```
┌─────────────────────────────────────┐
│         UI Layer (Jetpack Compose)   │
│  ┌─────────┐ ┌──────────┐ ┌──────┐ │
│  │ 相机列表  │ │ 实时取景  │ │传输列表│ │
│  └────┬────┘ └────┬─────┘ └──┬───┘ │
├───────┼───────────┼──────────┼──────┤
│  ┌────┴───────────┴──────────┴────┐ │
│  │     ViewModel Layer             │ │
│  │  CameraViewModel               │ │
│  │  TransferViewModel             │ │
│  └──────────────────────────────┬─┘ │
├─────────────────────────────────┼───┤
│  ┌──────────────────────────────┴─┐ │
│  │   NikonCameraService (前台服务)  │ │
│  │   • 保持连接                    │ │
│  │   • 后台传输                    │ │
│  │   • 事件监听                    │ │
│  └────────────────────────────┬───┘ │
├───────────────────────────────┼─────┤
│  ┌────────────────────────────┴───┐ │
│  │   JNI Bridge (nikon_bridge.so) │ │
│  │   Java ←→ C/C++ Core          │ │
│  └────────────────────────────┬───┘ │
├───────────────────────────────┼─────┤
│  ┌────────────────────────────┴───┐ │
│  │   Core (C/C++ 静态库)           │ │
│  │   PTP · 传输 · 事件            │ │
│  └────────────────────────────────┘ │
└─────────────────────────────────────┘
```

## 2. Android 清单配置

```xml
<manifest xmlns:android="http://schemas.android.com/apk/res/android">
    <uses-feature android:name="android.hardware.usb.host" android:required="false" />
    <uses-permission android:name="android.permission.INTERNET" />
    <uses-permission android:name="android.permission.ACCESS_NETWORK_STATE" />
    <uses-permission android:name="android.permission.ACCESS_WIFI_STATE" />
    <uses-permission android:name="android.permission.CHANGE_WIFI_STATE" />
    <uses-permission android:name="android.permission.BLUETOOTH" />
    <uses-permission android:name="android.permission.BLUETOOTH_ADMIN" />
    <uses-permission android:name="android.permission.ACCESS_FINE_LOCATION" />
    <uses-permission android:name="android.permission.BLUETOOTH_SCAN" />
    <uses-permission android:name="android.permission.BLUETOOTH_CONNECT" />
    <uses-permission android:name="android.permission.FOREGROUND_SERVICE" />
    <uses-permission android:name="android.permission.FOREGROUND_SERVICE_DATA_SYNC" />
    <uses-permission android:name="android.permission.POST_NOTIFICATIONS" />
    <uses-permission android:name="android.permission.REQUEST_IGNORE_BATTERY_OPTIMIZATIONS" />
    <uses-permission android:name="android.permission.READ_EXTERNAL_STORAGE"
        android:maxSdkVersion="32" />
    <uses-permission android:name="android.permission.WRITE_EXTERNAL_STORAGE"
        android:maxSdkVersion="29" />

    <application>
        <usb-device vendor-id="1200" />
        <service
            android:name=".service.NikonCameraService"
            android:foregroundServiceType="dataSync"
            android:exported="false" />
        <receiver
            android:name=".hal.UsbDeviceReceiver"
            android:exported="true">
            <intent-filter>
                <action android:name="android.hardware.usb.action.USB_DEVICE_ATTACHED" />
                <action android:name="android.hardware.usb.action.USB_DEVICE_DETACHED" />
            </intent-filter>
        </receiver>
        <receiver android:name=".hal.WifiReceiver">
            <intent-filter>
                <action android:name="android.net.wifi.STATE_CHANGE" />
                <action android:name="android.net.conn.CONNECTIVITY_CHANGE" />
            </intent-filter>
        </receiver>
    </application>
</manifest>
```

## 3. 前台服务设计

```kotlin
class NikonCameraService : Service() {
    companion object {
        const val NOTIFICATION_ID = 1001
        const val CHANNEL_ID = "nikon_camera_service"
    }

    private val binder = LocalBinder()
    private var cameraApiPtr: Long = 0

    inner class LocalBinder : Binder() {
        fun getService(): NikonCameraService = this@NikonCameraService
    }

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
        startForeground(NOTIFICATION_ID, buildNotification("正在初始化..."))
        initNativeCore()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        return START_STICKY
    }

    override fun onBind(intent: Intent?): IBinder = binder

    override fun onDestroy() {
        releaseNativeCore()
        super.onDestroy()
    }

    // 后台保活策略:
    // 1. 前台服务 + 高优先级通知
    // 2. 请求电池优化白名单
    // 3. 系统广播监听 (开机自启、网络变化)
    // 4. JobScheduler 周期性检查连接状态
    // 5. WorkManager 保活任务 (每 15 分钟)
}
```

## 4. USB 设备连接流程

```kotlin
class UsbConnectionManager(private val context: Context) {
    private val usbManager = context.getSystemService(Context.USB_SERVICE) as UsbManager

    fun scanNikonCameras(): List<UsbDevice> {
        return usbManager.deviceList.values.filter { device ->
            device.vendorId == NIKON_VENDOR_ID
        }
    }

    fun requestPermission(device: UsbDevice, callback: (Boolean) -> Unit) {
        if (usbManager.hasPermission(device)) {
            callback(true)
            return
        }
        permissionCallback = callback
        val filter = IntentFilter(ACTION_USB_PERMISSION)
        context.registerReceiver(permissionReceiver, filter)
        usbManager.requestPermission(device, pendingIntent)
    }

    fun openDevice(device: UsbDevice): ParcelFileDescriptor? {
        return usbManager.openDevice(device)?.let { connection ->
            connection.fileDescriptor?.let { fd ->
                nativeUsbOpen(fd, device.vendorId, device.productId)
            }
            connection.close()
            null
        }
    }

    companion object {
        const val NIKON_VENDOR_ID = 0x04B0
        const val ACTION_USB_PERMISSION = "com.nikon.app.USB_PERMISSION"
    }
}
```

## 5. 分区存储适配

```kotlin
class StorageManager(private val context: Context) {
    fun saveImageToGallery(sourcePath: String, filename: String): Uri? {
        val values = ContentValues().apply {
            put(MediaStore.Images.Media.DISPLAY_NAME, filename)
            put(MediaStore.Images.Media.MIME_TYPE, getMimeType(filename))
            put(MediaStore.Images.Media.RELATIVE_PATH,
                Environment.DIRECTORY_DCIM + "/NikonConnect")
        }
        val uri = context.contentResolver.insert(
            MediaStore.Images.Media.EXTERNAL_CONTENT_URI, values
        ) ?: return null

        context.contentResolver.openOutputStream(uri)?.use { output ->
            File(sourcePath).inputStream().use { input ->
                input.copyTo(output)
            }
        }
        return uri
    }

    fun requestSaveDirectory(activity: Activity, requestCode: Int) {
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT_TREE).apply {
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            addFlags(Intent.FLAG_GRANT_WRITE_URI_PERMISSION)
        }
        activity.startActivityForResult(intent, requestCode)
    }
}
```

## 6. 错误恢复机制

```
USB断连检测 ← UsbManager.ACTION_USB_DEVICE_DETACHED
    │
    ▼
启动重连 ──► 等待设备重新插入 (30s)
                    │
                    ▼
            重试 PTP 会话建立 (最多 3 次)
                    │
                    ▼
            恢复未完成传输 (读取 .resume)

Wi-Fi断连 ← ConnectivityManager.CONNECTIVITY_ACTION
    │
    ▼
自动重连 ──► 扫描 Wi-Fi 重连 (5次, 间隔 3s)
                    │
                    ▼
            恢复 PTP 会话
```
