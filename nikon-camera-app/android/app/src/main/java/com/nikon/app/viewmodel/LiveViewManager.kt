package com.nikon.app.viewmodel

import com.nikon.app.jni.CameraApi
import com.nikon.app.jni.CameraBridge
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
 * LiveViewManager — 实时取景域
 *
 * 从 CameraViewModel 拆分(阶段4): 负责实时取景的启动 / 停止 / 帧轮询。
 *
 * 帧轮询说明: 曾尝试做"UI 消费完上一帧才取下一帧"的背压节流, 但因依赖 UI 侧回调,
 * 一旦 UI 因任何原因(帧未渲染/组件未挂载/Activity 退后台)未消费, 取帧循环会永久
 * 停摆、LiveView 彻底卡死。故回退为固定 ~30fps 简单轮询, 让 StateFlow 自动合并
 * 中间帧, 兼顾实时性与稳定性。
 */
internal class LiveViewManager(
    private val bridge: CameraApi,
    private val handleProvider: () -> Long,
    private val scope: CoroutineScope,
    private val ioDispatcher: CoroutineDispatcher,
    private val onError: (String) -> Unit,
) {
    private val _liveViewFrame = MutableStateFlow<ByteArray?>(null)
    val liveViewFrame: StateFlow<ByteArray?> = _liveViewFrame.asStateFlow()

    private val _liveViewActive = MutableStateFlow(false)
    val liveViewActive: StateFlow<Boolean> = _liveViewActive.asStateFlow()

    private var lvPollJob: Job? = null

    fun startLiveView() {
        val h = handleProvider()
        if (h == 0L) {
            onError("相机服务未就绪,请稍候")
            return
        }
        if (_liveViewActive.value) return
        scope.launch {
            val rc = withContext(ioDispatcher) {
                bridge.nativeStartLiveView(h)
            }
            if (rc == CameraBridge.CAM_OK) {
                _liveViewActive.value = true
                startLiveViewPolling()
            } else {
                onError("启动实时取景失败 (错误码: $rc)")
            }
        }
    }

    fun stopLiveView() {
        lvPollJob?.cancel()
        if (!_liveViewActive.value) return
        val h = handleProvider()
        if (h != 0L) {
            scope.launch(ioDispatcher) {
                bridge.nativeStopLiveView(h)
            }
        }
        _liveViewActive.value = false
        _liveViewFrame.value = null
    }

    /** 关闭时停止帧轮询(不通知 native —— 由门面 onCleared 统一收尾)。 */
    fun close() {
        lvPollJob?.cancel()
    }

    /**
     * 帧轮询:~30fps,delay(33ms)。nativeGetLiveViewFrame 同步阻塞取一帧 JPEG。
     * 每帧更新 liveViewFrame StateFlow,UI 侧 collect 后 BitmapFactory 解码渲染。
     */
    private fun startLiveViewPolling() {
        lvPollJob?.cancel()
        lvPollJob = scope.launch {
            while (isActive && _liveViewActive.value) {
                val h = handleProvider()
                val frame = if (h == 0L) null else withContext(ioDispatcher) {
                    bridge.nativeGetLiveViewFrame(h)
                }
                if (frame != null && frame.isNotEmpty()) {
                    _liveViewFrame.value = frame
                }
                delay(33)  // 目标 ~30fps
            }
        }
    }
}