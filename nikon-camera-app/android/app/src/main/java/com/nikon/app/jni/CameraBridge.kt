package com.nikon.app.jni

/**
 * CameraBridge — Kotlin JNI 桥接包装类
 *
 * 封装 native 方法, 提供 Kotlin 友好的 API。
 * 所有耗时操作应在协程 IO 调度器上调用。
 */
object CameraBridge : CameraApi {

    init {
        try {
            System.loadLibrary("nikon_bridge")
        } catch (_: UnsatisfiedLinkError) {
            // 测试环境(Robolectric/JVM)无 .so,忽略;
            // native 方法调用会被 mockk 拦截或返回默认值
        }
    }

    // ─── 生命周期 ────────────────────────────────────────────
    // 注意: 所有 external fun 都声明为 open。这样在单元测试中 MockK 才能
    // 用子类覆盖这些 native 方法, 否则 native 方法是 final 的, mockkObject 时会
    // 直接调用真实 native 实现而抛出 UnsatisfiedLinkError (JVM 下没有 .so)。
    override external fun nativeCreate(transport: Int): Long
    override external fun nativeDestroy(handle: Long)

    // ─── 连接管理 ─────────────────────────────────────────────
    /** 返回格式: "id|model|serial|transport|battery|free_gb|total_gb" */
    override external fun nativeScan(handle: Long): Array<String>?
    override external fun nativeConnect(handle: Long, cameraId: String): Int
    /** 使用已获取权限的 USB fd 连接 (Android 专用, 跳过 scan/open) */
    override external fun nativeConnectUsbFd(handle: Long, fd: Int, serial: String): Int
    /** 通过 Wi-Fi 直连相机 PTP/IP 服务 (ip + 端口 15740)。JNI 已实现但此前从未声明, 导致 Wi-Fi 无法连接 */
    override external fun nativeConnectWifi(handle: Long, ip: String, port: Int): Int
    override external fun nativeDisconnect(handle: Long)
    /** @return ConnectionStatus 枚举序数 */
    override external fun nativeGetStatus(handle: Long): Int

    // ─── 拍摄控制 ─────────────────────────────────────────────
    override external fun nativeCapture(handle: Long): Int
    override external fun nativeCaptureBurst(handle: Long, count: Int, intervalMs: Int): Int

    // ─── 相机参数 ─────────────────────────────────────────────
    override external fun nativeSetProperty(handle: Long, propId: Int, value: Long): Int
    override external fun nativeGetProperty(handle: Long, propId: Int): Long
    /** 批量读取属性: 一次 JNI 调用取多个, 供参数轮询减少跨边界开销 */
    override external fun nativeGetProperties(handle: Long, propIds: IntArray): LongArray?

    // ─── 文件传输 ─────────────────────────────────────────────
    override external fun nativeStartTransfer(handle: Long, objectHandle: Long, destPath: String): Int
    override external fun nativeCancelTransfer(handle: Long, jobId: Int): Int

    // ─── Picture Control ──────────────────────────────────────
    /** 返回 sizeof(PictureControl) 字节数组 */
    override external fun nativeGetPictCtrl(handle: Long): ByteArray?
    override external fun nativeSetPictCtrl(handle: Long, data: ByteArray): Int

    // ─── 文件列举 / 缩略图 / 删除 ────────────────────────────
    /**
     * 列举相机文件。返回 String[],每行格式:
     * "object_handle|filename|size|datetime|is_raw|is_jpeg|width|height|storage_id"
     * @param storageId 0=全部存储卡; 1=CF-A; 2=SD-B
     */
    override external fun nativeListFiles(handle: Long, storageId: Int): Array<String>?

    /** 获取缩略图 JPEG 字节数据 */
    override external fun nativeGetThumbnail(handle: Long, objectHandle: Long): ByteArray?

    /** 删除相机内文件 */
    override external fun nativeDeleteFile(handle: Long, objectHandle: Long): Int

    // ─── 实时取景 ─────────────────────────────────────────────
    override external fun nativeStartLiveView(handle: Long): Int
    override external fun nativeStopLiveView(handle: Long): Int
    /** 同步取一帧 JPEG,返回 ByteArray;无帧或失败返回 null */
    override external fun nativeGetLiveViewFrame(handle: Long): ByteArray?

    // ─── 传输进度回调 ───────────────────────────────────────────
    // 注意: TransferProgressCallback 已抽到顶层接口 (CameraApi.kt),
    // 以便 ViewModel 依赖接口而非具体 object。

    /** 注册传输进度回调(连接成功后调一次) */
    override external fun nativeRegisterProgressCallback(handle: Long, callback: TransferProgressCallback)

    // ─── FTP 自动化 ─────────────────────────────────────────────
    /** 配置 FTP 服务器 (不立即连接)。字段对应 C 层 FtpConfig */
    override external fun nativeSetFtpConfig(
        handle: Long, host: String, port: Int,
        username: String, password: String, remotePath: String,
        useTls: Boolean, autoUpload: Boolean,
    ): Int

    /** 将本地文件异步导出到 FTP (需先 nativeSetFtpConfig) */
    override external fun nativeExportToFtp(handle: Long, localPath: String): Int

    // ─── 常量 (同 camera_api.h) ───────────────────────────────
    const val TRANSPORT_AUTO      = 0
    const val TRANSPORT_USB_ONLY  = 1
    const val TRANSPORT_WIFI_ONLY = 2

    const val CAM_OK                    =  0
    const val CAM_ERR_NOT_CONNECTED     = -100
    const val CAM_ERR_ALREADY_CONNECTED = -101
    const val CAM_ERR_TIMEOUT           = -102
    const val CAM_ERR_BUSY              = -104

    const val STATUS_DISCONNECTED  = 0
    const val STATUS_SCANNING      = 1
    const val STATUS_CONNECTING    = 2
    const val STATUS_CONNECTED     = 3
    const val STATUS_TRANSFERRING  = 4
    const val STATUS_ERROR         = 5

    /** 存储卡 ID(传给 nativeListFiles) */
    const val STORAGE_ALL = 0
    const val STORAGE_CF  = 1
    const val STORAGE_SD  = 2

    // 尼康属性码 (与 SPEC_PROTOCOL.md 一致)
    const val PROP_WHITE_BALANCE = 0xD00A
    const val PROP_SHUTTER_SPEED = 0xD00C
    const val PROP_APERTURE      = 0xD00E
    const val PROP_ISO           = 0xD010
    const val PROP_EXPOSURE_COMP = 0xD012
    const val PROP_FOCUS_MODE    = 0xD014
    const val PROP_IMAGE_QUALITY = 0xD01A
    const val PROP_IMAGE_SIZE    = 0xD01C
}
