package com.nikon.app

import android.app.Application
import com.nikon.app.jni.CameraBridge

/**
 * NikonApplication — 应用入口
 *
 * 职责:
 *   1. 预加载 native 库 (libnikon_bridge.so)
 *   2. 持有全局 native handle(由 CameraService 写入,ViewModel 读取)
 *   3. 持有全局单例引用
 */
open class NikonApplication : Application() {

    companion object {
        lateinit var instance: NikonApplication
            private set
    }

    /**
     * 全局 native handle。
     * - 由 CameraService.onCreate 调 nativeCreate 后写入
     * - 由 CameraService.onDestroy 调 nativeDestroy 后置 0
     * - ViewModel 所有 nativeXxx 调用读此值
     * - 值为 0 表示 Service 未启动或已销毁,调用方应判断
     */
    @Volatile
    var cameraHandle: Long = 0L
        internal set

    override fun onCreate() {
        super.onCreate()
        instance = this

        // 预加载 native 库; 失败(无 .so)由 CameraBridge 的 try-catch 容忍,
        // App 进入"无相机"待机模式而非崩溃。真正的 native 调用异常由 CameraService 捕获。
        CameraBridge // 触发 companion object init → System.loadLibrary
    }
}
