package com.nikon.app

import androidx.test.core.app.ApplicationProvider
import com.nikon.app.jni.CameraApi
import com.nikon.app.viewmodel.CameraViewModel
import io.mockk.mockk
import androidx.compose.runtime.Composable
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.AndroidComposeTestRule
import androidx.compose.ui.test.hasScrollAction
import androidx.compose.ui.semantics.SemanticsActions
import com.nikon.app.ui.theme.NikonTheme
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.setMain

/**
 * 仪表化(androidTest)UI 测试的公共夹具。
 *
 * 设计要点(与单元测试一致,但运行在真机 / 模拟器上):
 *  - bridge 用 relaxed mockk: native 调用不会真实执行,也不会抛 UnsatisfiedLinkError
 *  - ioDispatcher 注入 UnconfinedTestDispatcher: 所有 withContext(ioDispatcher) 同步完成
 *  - Dispatchers.setMain(UnconfinedTestDispatcher()): viewModelScope 同步执行,
 *    使点击 / 状态变化在测试线程内确定性完成(无需 waitForIdle 等待协程)
 *  - cameraHandle 设为 1L: ensureHandle() 通过,scan/connect 等真正走到 bridge
 */
@OptIn(ExperimentalCoroutinesApi::class)
object TestDoubles {

    fun createTestViewModel(): Pair<CameraViewModel, CameraApi> {
        val app = ApplicationProvider.getApplicationContext<NikonApplication>()
        app.cameraHandle = 1L
        val api = mockk<CameraApi>(relaxed = true)
        val vm = CameraViewModel(
            application = app,
            bridge = api,
            ioDispatcher = UnconfinedTestDispatcher(),
            enablePolling = false,
        )
        return vm to api
    }

    /**
     * 安装测试用 Dispatcher, 并关闭动画时钟自动推进。
     *
     * 关闭动画时钟(mainClock.autoAdvance = false)是必须的:
     * 屏幕 Composable 里大量使用不确定(无限循环)动画 —— CircularProgressIndicator、
     * rememberInfiniteTransition(扫描脉冲 / 同步脉冲 / 录制中脉冲)等。Compose UI 测试的
     * waitForIdle() 会等待动画静止, 而不确定动画永不静止, 导致测试永远卡在等待、
     * 模拟器停在某画面不动。关闭自动推进后, waitForIdle 不再被无限动画拖死。
     */
    fun installTestDispatchers(rule: AndroidComposeTestRule<*, *>) {
        Dispatchers.setMain(UnconfinedTestDispatcher())
        rule.mainClock.autoAdvance = false
    }

    fun resetTestDispatchers() {
        Dispatchers.resetMain()
    }

    /**
     * 轻量记录型 CameraApi 实现。
     *
     * 用途: MockK 在 verify/every 作用域内会把 `Any.setProperty` 解析成它自己的
     * 动态属性扩展,从而遮蔽真实的 `CameraApi.setProperty`,导致无法直接 verify。
     * 这里用一个显式记录调用的实现绕过该坑,既能断言 native 调用,又无需 Robolectric。
     */
    fun createRecordingViewModel(): Pair<CameraViewModel, RecordingCameraApi> {
        val app = ApplicationProvider.getApplicationContext<NikonApplication>()
        app.cameraHandle = 1L
        val api = RecordingCameraApi()
        val vm = CameraViewModel(
            application = app,
            bridge = api,
            ioDispatcher = UnconfinedTestDispatcher(),
            enablePolling = false,
        )
        return vm to api
    }
}

/**
 * 记录所有 native 调用的 CameraApi 实现。
 * calls[name] 保存每次调用的实参列表,测试据此断言「点击→native 调用」的接线。
 */
class RecordingCameraApi : CameraApi {

    val calls = mutableMapOf<String, MutableList<List<Any?>>>()
    private fun rec(name: String, vararg args: Any?) {
        calls.getOrPut(name) { mutableListOf() }.add(args.toList())
    }

    override fun nativeCreate(transport: Int): Long = 0L.also { rec("nativeCreate", transport) }
    override fun nativeDestroy(handle: Long) { rec("nativeDestroy", handle) }
    override fun nativeScan(handle: Long): Array<String>? = emptyArray<String>().also { rec("nativeScan", handle) }
    override fun nativeConnect(handle: Long, cameraId: String): Int = 0.also { rec("nativeConnect", handle, cameraId) }
    override fun nativeConnectUsbFd(handle: Long, fd: Int, serial: String): Int = 0.also { rec("nativeConnectUsbFd", handle, fd, serial) }
    override fun nativeConnectWifi(handle: Long, ip: String, port: Int): Int = 0.also { rec("nativeConnectWifi", handle, ip, port) }
    override fun nativeDisconnect(handle: Long) { rec("nativeDisconnect", handle) }
    override fun nativeGetStatus(handle: Long): Int = 0.also { rec("nativeGetStatus", handle) }
    override fun nativeCapture(handle: Long): Int = 0.also { rec("nativeCapture", handle) }
    override fun nativeCaptureBurst(handle: Long, count: Int, intervalMs: Int): Int = 0.also { rec("nativeCaptureBurst", handle, count, intervalMs) }
    override fun nativeSetProperty(handle: Long, propId: Int, value: Long): Int { rec("setProperty", handle, propId, value); return 0 }
    override fun nativeGetProperty(handle: Long, propId: Int): Long = 0L.also { rec("nativeGetProperty", handle, propId) }
    override fun nativeStartTransfer(handle: Long, objectHandle: Long, destPath: String): Int = 0.also { rec("nativeStartTransfer", handle, objectHandle, destPath) }
    override fun nativeCancelTransfer(handle: Long, jobId: Int): Int { rec("nativeCancelTransfer", handle, jobId); return 0 }
    override fun nativeGetPictCtrl(handle: Long): ByteArray? = null.also { rec("nativeGetPictCtrl", handle) }
    override fun nativeSetPictCtrl(handle: Long, data: ByteArray): Int = 0.also { rec("nativeSetPictCtrl", handle, data) }
    override fun nativeListFiles(handle: Long, storageId: Int): Array<String>? = emptyArray<String>().also { rec("nativeListFiles", handle, storageId) }
    override fun nativeGetThumbnail(handle: Long, objectHandle: Long): ByteArray? = null.also { rec("nativeGetThumbnail", handle, objectHandle) }
    override fun nativeDeleteFile(handle: Long, objectHandle: Long): Int = 0.also { rec("nativeDeleteFile", handle, objectHandle) }
    override fun nativeStartLiveView(handle: Long): Int = 0.also { rec("nativeStartLiveView", handle) }
    override fun nativeStopLiveView(handle: Long): Int { rec("nativeStopLiveView", handle); return 0 }
    override fun nativeGetLiveViewFrame(handle: Long): ByteArray? = null.also { rec("nativeGetLiveViewFrame", handle) }
    override fun nativeRegisterProgressCallback(handle: Long, callback: com.nikon.app.jni.TransferProgressCallback) { rec("nativeRegisterProgressCallback", handle) }
    override fun nativeSetStatusCallback(handle: Long, callback: com.nikon.app.jni.StatusChangeCallback) { rec("nativeSetStatusCallback", handle) }
    override fun nativeGetProperties(handle: Long, propIds: IntArray): LongArray? =
        LongArray(propIds.size).also { rec("nativeGetProperties", handle, propIds) }
    override fun nativeSetFtpConfig(
        handle: Long, host: String, port: Int,
        username: String, password: String, remotePath: String,
        useTls: Boolean, autoUpload: Boolean,
    ): Int = 0.also { rec("nativeSetFtpConfig", handle, host, port, username, password, remotePath, useTls, autoUpload) }
    override fun nativeExportToFtp(handle: Long, localPath: String): Int =
        0.also { rec("nativeExportToFtp", handle, localPath) }
}

/**
 * 测试专用 setContent: 包一层生产同款 NikonTheme(MaterialTheme)。
 *
 * 屏幕 Composable(HomeScreen/LiveViewScreen/...) 内部使用 MaterialTheme 颜色与组件
 * (Button/Switch/Surface), 但自身不包主题 —— 生产代码由 MainActivity 的 NikonTheme 包裹。
 * 测试里若直接 setContent { Screen(...) } 会抛
 * `IllegalStateException: No Material theme provided`, 导致整个组合阶段崩溃。
 * 故统一用 setNikonContent 包裹, 与生产行为一致。
 */
fun AndroidComposeTestRule<*, *>.setNikonContent(content: @Composable () -> Unit) {
    setContent { NikonTheme(content) }
}

/**
 * 推进动画时钟,让挂起的副作用 / 转场完成。
 *
 * 测试里 `mainClock.autoAdvance = false`(避免无限动画拖死 waitForIdle),但副作用有二:
 *  1) NavHost 进入转场(默认 enterTransition)被冻结在初始帧,目标内容停在视口外 →
 *     `assertIsDisplayed()` 报 "The component is not displayed!"。推进足够时长让转场落地。
 *  2) LaunchedEffect(如 GalleryScreen.listFiles)/初始布局需要至少一帧才完成组合。
 *
 * 所以凡是「点击导航 / setContent 后依赖状态变化」的断言前,调一次 settle() 即可。
 * 700ms 足以覆盖 NavHost 默认转场(~300–500ms)。
 */
fun AndroidComposeTestRule<*, *>.settle(millis: Long = 700) {
    mainClock.advanceTimeBy(millis)
}

/**
 * 在 LazyColumn 等惰性列表中"滚到某个文本真正落在可视区域"。
 *
 * `performScrollTo()` 只能作用于「已在语义树中」的节点;而 LazyColumn 视口外的 item
 * 根本不会被组合进语义树,`performScrollTo` 会报 "could not find"。本助手改为对可滚动
 * 容器反复 swipeUp,直到目标文本真正进入可视区域(assertIsDisplayed 通过),再返回。
 *
 * 为什么用 `assertIsDisplayed()` 而非 `assertExists()`:`assertExists` 只要节点被「组合进
 * 语义树」就通过,但 LazyColumn 会在视口外预组合一层缓冲 item —— 这种节点虽存在却不在屏幕
 * 上,后续 `performClick()` 点不到(合成手势落在视口外被忽略),导致点击类断言静默失败。
 * 必须要求真正可视(assertIsDisplayed)。
 *
 * `mainClock.autoAdvance = false` 时,swipeUp 更新了滚动偏移,但 LazyColumn 的重组(把
 * 视口外 item 组合进语义树)需要一帧才提交。autoAdvance 关闭后没有自动帧,所以每次 swipeUp
 * 后必须手动 `advanceTimeBy` 推进一小段时钟,新 item 才会进入语义树。这里用固定 32ms
 * (而非 waitForIdle),是因为本助手也用于 HomeScreen —— 那里有无限动画(扫描脉冲),
 * waitForIdle 会永远卡死。
 *
 * 为什么遍历「所有」可滚动节点而非 `onNode(hasScrollAction())`:NavHost 默认转场
 * (fade+scale)在 autoAdvance=false 下被冻结在初始帧,导致「被退出」的 HomeScreen 与
 * 「进入中」的 SettingsScreen 同时在语义树里共存。`onNode(hasScrollAction())` 只返回第一个
 * (往往是 Home 的 verticalScroll,maxValue=0 无法滚动),swipe 永远没效果。遍历全部并逐个
 * swipe:maxValue=0 的 Home 节点是无副作用空操作,真正可滚动的 Settings LazyColumn 会被滚动。
 *
 * 用法:先 `settle()` 让目标内容进入组合(若有状态前置),再 `scrollToText("断开连接")`,
 * 随后即可 `performClick()`。
 */
fun AndroidComposeTestRule<*, *>.scrollToText(text: String, maxSwipes: Int = 40) {
    repeat(maxSwipes) {
        try {
            // 必须真正落在可视区域,否则后续 performClick 点不到(懒列表视口外 item 虽已组合但不可点)
            onNodeWithText(text).assertIsDisplayed()
            return
        } catch (_: AssertionError) { }
        try {
            val scrollables = onAllNodes(hasScrollAction())
            val n = scrollables.fetchSemanticsNodes().size
            for (i in 0 until n) {
                try {
                    scrollables[i].performTouchInput { swipeUp(durationMillis = 60) }
                } catch (_: AssertionError) { }
            }
            // 提交一帧,让 LazyColumn 把新进入视口的 item 组合进语义树
            mainClock.advanceTimeBy(32)
        } catch (_: AssertionError) { }
    }
    onNodeWithText(text).assertIsDisplayed() // 仍找不到/不可见则明确报错
}

/**
 * 确定性地点击「含指定文本的节点」(取第一个匹配)。
 *
 * 用 `performSemanticsAction(SemanticsActions.OnClick)` 而非 `performClick()`:
 * 某些节点(手风琴卡片头部 `Row.clickable` 包在 `animateContentSize` Surface + LazyColumn 里)
 * 的合成 tap 手势会被 `animateContentSize`/滚动容器拦截,导致 `onToggle`/`onExpand` 不触发,
 * 表现为「点击成功但状态不变」。直接触发 OnClick 语义动作可确定性调用 handler,与测试环境
 * 的冻结时钟( autoAdvance=false)完全解耦,稳定可靠。
 *
 * 多匹配时(如展开后存在多个「应用到相机」)取 [0],即语义树中第一个。
 */
fun AndroidComposeTestRule<*, *>.clickText(text: String) {
    onAllNodesWithText(text)[0].performSemanticsAction(SemanticsActions.OnClick)
}

