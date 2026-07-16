package com.nikon.app.integration

import com.nikon.app.jni.CameraBridge
import org.junit.AssumptionViolatedException

/**
 * 相机集成测试公共前置。
 *
 * 这些用例驱动“真实 native 栈 + 真实相机硬件”,需要:
 *   1) APK 已打包 libnikon_bridge.so(native 层已加载);
 *   2) 一台通过 USB/Wi-Fi 连接的真实尼康相机,且处于可连接状态。
 *
 * 任一条件不满足 → 抛出 [AssumptionViolatedException],用例以 SKIP 结束,
 * 不污染“无相机环境”下功能测试套件的全绿。
 */
data class RealCameraSession(val handle: Long, val cameraId: String)

fun requireRealCamera(transport: Int = CameraBridge.TRANSPORT_USB_ONLY): RealCameraSession {
    val handle = try {
        CameraBridge.nativeCreate(transport)
    } catch (e: UnsatisfiedLinkError) {
        throw AssumptionViolatedException("native 库未加载(无 .so),跳过集成测试", e)
    }
    if (handle == 0L) {
        throw AssumptionViolatedException("nativeCreate 返回 0,无法创建句柄,跳过集成测试")
    }

    val found = try {
        CameraBridge.nativeScan(handle)
    } catch (e: UnsatisfiedLinkError) {
        CameraBridge.nativeDestroy(handle)
        throw AssumptionViolatedException("native 库未加载,跳过集成测试", e)
    }
    if (found.isNullOrEmpty()) {
        CameraBridge.nativeDestroy(handle)
        throw AssumptionViolatedException("未扫描到相机,跳过集成测试(请用 USB/Wi-Fi 连接真实尼康相机)")
    }

    val cameraId = found.first().substringBefore("|")
    val rc = CameraBridge.nativeConnect(handle, cameraId)
    if (rc != CameraBridge.CAM_OK) {
        CameraBridge.nativeDestroy(handle)
        throw AssumptionViolatedException("连接相机失败 rc=$rc,跳过集成测试")
    }
    return RealCameraSession(handle, cameraId)
}

/** JPEG 起始标记 SOI = 0xFFD8 */
fun isJpeg(bytes: ByteArray?): Boolean =
    bytes != null && bytes.size >= 2 &&
        bytes[0] == 0xFF.toByte() && bytes[1] == 0xD8.toByte()

fun jpegSize(bytes: ByteArray?): Int = bytes?.size ?: 0
