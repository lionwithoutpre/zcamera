package com.nikon.app.jni

/**
 * 传输进度回调 — native 层通过 JNI 回调。
 *
 * 抽成顶层接口(原本嵌套在 CameraBridge 内), 便于 ViewModel 依赖 CameraApi 接口
 * 而非具体的 native object, 从而在单元测试中用 mockk 注入纯接口实现, 彻底避开
 * external 方法无法被 MockK 覆盖(UnsatisfiedLinkError)的问题。
 */
interface TransferProgressCallback {
    /**
     * @param jobId    native 层 jobId
     * @param speedMbps 当前速度
     * @param percent   0-100
     * @param status    0=等待 1=传输中 2=完成 -1=失败
     */
    fun onProgress(jobId: Int, speedMbps: Double, percent: Int, status: Int)
}

/**
 * CameraBridge 的抽象接口。ViewModel 依赖此接口而非具体的 CameraBridge object,
 * 以便单元测试注入 mock。
 *
 * 所有方法签名与 CameraBridge 的 external 方法一一对应。JNI 仍然按 CameraBridge 类
 * 查找 native 实现(接口方法只是抽象声明, external 实现声明在 CameraBridge 上), 因此
 * 不影响原生库的加载与调用。
 */
interface CameraApi {
    fun nativeCreate(transport: Int): Long
    fun nativeDestroy(handle: Long)

    fun nativeScan(handle: Long): Array<String>?
    fun nativeConnect(handle: Long, cameraId: String): Int
    /** 使用已获取权限的 USB fd 连接 (Android 专用, 跳过 scan/open) */
    fun nativeConnectUsbFd(handle: Long, fd: Int, serial: String): Int
    fun nativeDisconnect(handle: Long)
    /** @return ConnectionStatus 枚举序数 */
    fun nativeGetStatus(handle: Long): Int

    fun nativeCapture(handle: Long): Int
    fun nativeCaptureBurst(handle: Long, count: Int, intervalMs: Int): Int

    fun nativeSetProperty(handle: Long, propId: Int, value: Long): Int
    fun nativeGetProperty(handle: Long, propId: Int): Long

    fun nativeStartTransfer(handle: Long, objectHandle: Long, destPath: String): Int
    fun nativeCancelTransfer(handle: Long, jobId: Int): Int

    /** 返回 sizeof(PictureControl) 字节数组 */
    fun nativeGetPictCtrl(handle: Long): ByteArray?
    fun nativeSetPictCtrl(handle: Long, data: ByteArray): Int

    /**
     * 列举相机文件。返回 String[],每行格式:
     * "object_handle|filename|size|datetime|is_raw|is_jpeg|width|height|storage_id"
     * @param storageId 0=全部存储卡; 1=CF-A; 2=SD-B
     */
    fun nativeListFiles(handle: Long, storageId: Int): Array<String>?

    /** 获取缩略图 JPEG 字节数据 */
    fun nativeGetThumbnail(handle: Long, objectHandle: Long): ByteArray?

    /** 删除相机内文件 */
    fun nativeDeleteFile(handle: Long, objectHandle: Long): Int

    fun nativeStartLiveView(handle: Long): Int
    fun nativeStopLiveView(handle: Long): Int
    /** 同步取一帧 JPEG,返回 ByteArray;无帧或失败返回 null */
    fun nativeGetLiveViewFrame(handle: Long): ByteArray?

    /** 注册传输进度回调(连接成功后调一次) */
    fun nativeRegisterProgressCallback(handle: Long, callback: TransferProgressCallback)
}
