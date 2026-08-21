package com.nikon.app

import androidx.test.core.app.ApplicationProvider
import com.nikon.app.jni.CameraApi
import com.nikon.app.jni.CameraBridge
import com.nikon.app.jni.TransferProgressCallback
import com.nikon.app.viewmodel.CameraViewModel
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.UnconfinedTestDispatcher

/**
 * 可配置 + 记录调用的 CameraApi 实现,供 ViewModel 功能(业务)测试使用。
 *
 * 与 TestDoubles.RecordingCameraApi 的区别:
 *  - RecordingCameraApi 返回值固定(connect=0/CAM_OK、getProperty=0、startTransfer=0…),
 *    只能断言「是否被调用」;
 *  - FakeCameraApi 的每个返回值都可按用例配置(scan 列表、connect 返回码、各属性值、
 *    传输 jobId、LiveView 帧…),从而驱动「成功 / 失败 / 边界」等各种分支,
 *    并同时记录每次调用以便断言「点击/状态变化 → native 调用的接线」。
 *
 * 不依赖真实 .so,也不抛 UnsatisfiedLinkError —— 所有方法直接返回配置值。
 */
class FakeCameraApi : CameraApi {

    // ── 可配置返回值 ───────────────────────────────────────
    var scanResult: Array<String>? = emptyArray()
    var connectRc: Int = CameraBridge.CAM_OK
    var captureRc: Int = CameraBridge.CAM_OK
    /** nativeStartTransfer 返回的 jobId;返回 <0 表示启动失败 */
    var startTransferRc: Int = 1
    var listFilesResult: Array<String>? = emptyArray()
    var getThumbnailResult: ByteArray? = byteArrayOf(0xFF.toByte(), 0xD8.toByte(), 0xFF.toByte(), 0xD9.toByte())
    var liveViewStartRc: Int = CameraBridge.CAM_OK
    var liveViewFrame: ByteArray? = byteArrayOf(0xFF.toByte(), 0xD8.toByte())
    var deleteRc: Int = CameraBridge.CAM_OK
    /** propId → 当前属性值;未配置的 prop 返回 0 */
    val propertyValues = mutableMapOf<Int, Long>()

    // ── 调用记录 ───────────────────────────────────────────
    private val calls = mutableMapOf<String, MutableList<List<Any?>>>()
    private fun rec(name: String, vararg args: Any?) {
        calls.getOrPut(name) { mutableListOf() }.add(args.toList())
    }

    // ── 断言辅助 ───────────────────────────────────────────
    fun called(name: String): Boolean = calls.containsKey(name)
    fun callCount(name: String): Int = calls[name]?.size ?: 0
    /** 取某方法第 n 次调用的参数列表(从 0 开始) */
    fun argsOf(name: String, n: Int = 0): List<Any?> = calls[name]?.get(n) ?: emptyList()

    // ── CameraApi 实现 ─────────────────────────────────────
    override fun nativeCreate(transport: Int): Long = 0L.also { rec("nativeCreate", transport) }
    override fun nativeDestroy(handle: Long) { rec("nativeDestroy", handle) }
    override fun nativeScan(handle: Long): Array<String>? = scanResult.also { rec("nativeScan", handle) }
    override fun nativeConnect(handle: Long, cameraId: String): Int = connectRc.also { rec("nativeConnect", handle, cameraId) }
    override fun nativeConnectUsbFd(handle: Long, fd: Int, serial: String): Int =
        connectRc.also { rec("nativeConnectUsbFd", handle, fd, serial) }
    override fun nativeConnectWifi(handle: Long, ip: String, port: Int): Int =
        connectRc.also { rec("nativeConnectWifi", handle, ip, port) }
    override fun nativeDisconnect(handle: Long) { rec("nativeDisconnect", handle) }
    override fun nativeGetStatus(handle: Long): Int = 0.also { rec("nativeGetStatus", handle) }
    override fun nativeCapture(handle: Long): Int = captureRc.also { rec("nativeCapture", handle) }
    override fun nativeCaptureBurst(handle: Long, count: Int, intervalMs: Int): Int =
        captureRc.also { rec("nativeCaptureBurst", handle, count, intervalMs) }
    override fun nativeSetProperty(handle: Long, propId: Int, value: Long): Int {
        rec("setProperty", handle, propId, value); return 0
    }
    override fun nativeGetProperty(handle: Long, propId: Int): Long =
        (propertyValues[propId] ?: 0L).also { rec("nativeGetProperty", handle, propId) }
    private var nextTransferId = 1
    override fun nativeStartTransfer(handle: Long, objectHandle: Long, destPath: String): Int {
        // startTransferRc < 0 表示启动失败(返回错误码);否则返回自增的唯一 jobId,
        // 保证连续多次 startTransfer 得到不同 id,使 transferJobs 的 id 唯一、可正确取消/暂停。
        val rc = if (startTransferRc < 0) startTransferRc else nextTransferId++
        return rc.also { rec("nativeStartTransfer", handle, objectHandle, destPath) }
    }
    override fun nativeCancelTransfer(handle: Long, jobId: Int): Int {
        rec("nativeCancelTransfer", handle, jobId); return 0
    }
    override fun nativeGetPictCtrl(handle: Long): ByteArray? = null.also { rec("nativeGetPictCtrl", handle) }
    override fun nativeSetPictCtrl(handle: Long, data: ByteArray): Int = 0.also { rec("nativeSetPictCtrl", handle, data) }
    override fun nativeListFiles(handle: Long, storageId: Int): Array<String>? =
        listFilesResult.also { rec("nativeListFiles", handle, storageId) }
    override fun nativeGetThumbnail(handle: Long, objectHandle: Long): ByteArray? =
        getThumbnailResult.also { rec("nativeGetThumbnail", handle, objectHandle) }
    override fun nativeDeleteFile(handle: Long, objectHandle: Long): Int =
        deleteRc.also { rec("nativeDeleteFile", handle, objectHandle) }
    override fun nativeStartLiveView(handle: Long): Int = liveViewStartRc.also { rec("nativeStartLiveView", handle) }
    override fun nativeStopLiveView(handle: Long): Int { rec("nativeStopLiveView", handle); return 0 }
    override fun nativeGetLiveViewFrame(handle: Long): ByteArray? = liveViewFrame.also { rec("nativeGetLiveViewFrame", handle) }
    override fun nativeRegisterProgressCallback(handle: Long, callback: TransferProgressCallback) {
        rec("nativeRegisterProgressCallback", handle)
    }
    override fun nativeGetProperties(handle: Long, propIds: IntArray): LongArray? =
        LongArray(propIds.size) { i -> propertyValues[propIds[i]] ?: 0L }
            .also { rec("nativeGetProperties", handle, propIds) }
    override fun nativeSetFtpConfig(
        handle: Long, host: String, port: Int,
        username: String, password: String, remotePath: String,
        useTls: Boolean, autoUpload: Boolean,
    ): Int = 0.also { rec("nativeSetFtpConfig", handle, host, port, username, password, remotePath, useTls, autoUpload) }
    override fun nativeExportToFtp(handle: Long, localPath: String): Int =
        0.also { rec("nativeExportToFtp", handle, localPath) }
}

/**
 * 构造一个「假相机」ViewModel:bridge 用可配置的 FakeCameraApi,ioDispatcher 用同步的
 * UnconfinedTestDispatcher,且默认关闭后台轮询(enablePolling=false,避免虚拟时钟下自旋)。
 * handle 默认 1L(模拟 CameraService 已就绪);传 0 可测试「服务未就绪」分支。
 */
@OptIn(ExperimentalCoroutinesApi::class)
fun createFakeVm(handle: Long = 1L): Pair<CameraViewModel, FakeCameraApi> {
    val app = ApplicationProvider.getApplicationContext<com.nikon.app.NikonApplication>()
    app.cameraHandle = handle
    val api = FakeCameraApi()
    val vm = CameraViewModel(
        application = app,
        bridge = api,
        ioDispatcher = UnconfinedTestDispatcher(),
        enablePolling = false,
    )
    return vm to api
}
