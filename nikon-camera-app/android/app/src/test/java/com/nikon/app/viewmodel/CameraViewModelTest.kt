package com.nikon.app.viewmodel

import app.cash.turbine.test
import com.nikon.app.NikonApplication
import com.nikon.app.TestNikonApplication
import com.nikon.app.jni.CameraApi
import com.nikon.app.jni.CameraBridge
import com.nikon.app.jni.TransferProgressCallback
import io.mockk.every
import io.mockk.just
import io.mockk.mockk
import io.mockk.Runs
import io.mockk.verify
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

/**
 * CameraViewModel 单元测试
 *
 * 覆盖:
 * - ensureHandle 守卫 (handle=0 时不调 native + 写 error)
 * - serviceReady 状态同步
 * - 传输并发控制 (WAITING/ACTIVE/DONE 状态流转)
 * - 设置持久化 (SharedPreferences)
 * - 错误 Channel (不丢连续错误)
 * - 路径生成 (Scoped Storage)
 *
 * 测试通过向 ViewModel 注入 mockk 的 [CameraApi] 接口来隔离 native 层,
 * 不再直接 mockkObject(CameraBridge) —— 因为 external 方法无法被 MockK 覆盖,
 * 会在 JVM 下抛 UnsatisfiedLinkError。
 *
 * 不使用 Robolectric: 通过 [TestNikonApplication] 提供内存版 SharedPreferences 与
 * 临时外部存储目录, 在纯 JVM 下即可运行, 更快更稳, 且不受内存受限环境影响。
 */
@OptIn(ExperimentalCoroutinesApi::class)
class CameraViewModelTest {

    private lateinit var app: NikonApplication
    private lateinit var api: CameraApi
    private lateinit var vm: CameraViewModel
    private val testDispatcher = UnconfinedTestDispatcher()

    @Before
    fun setup() {
        Dispatchers.setMain(testDispatcher)
        app = TestNikonApplication()
        // 用 mockk 模拟纯接口(无 native,可安全覆盖)
        api = mockk(relaxed = true)
        every { api.nativeGetStatus(any()) } returns CameraBridge.STATUS_DISCONNECTED
        every { api.nativeRegisterProgressCallback(any(), any()) } just Runs
        vm = CameraViewModel(app, api, ioDispatcher = testDispatcher, enablePolling = false)
    }

    @After
    fun tearDown() {
        Dispatchers.resetMain()
    }

    // ─── 辅助:通过反射触发传输进度回调 ──────────────────────

    private fun triggerProgressCallback(jobId: Int, speed: Double, percent: Int, status: Int) {
        val field = vm.javaClass.getDeclaredField("transferProgressCallback")
        field.isAccessible = true
        val callback = field.get(vm) as TransferProgressCallback
        callback.onProgress(jobId, speed, percent, status)
    }

    // ─── ensureHandle 守卫 ──────────────────────────────────

    @Test
    fun `handle is zero when Service not started`() {
        assertEquals(0L, app.cameraHandle)
        assertFalse(vm.serviceReady.value)
    }

    @Test
    fun `scan does nothing when handle is zero`() = runTest(testDispatcher) {
        vm.scan()
        runCurrent()
        // nativeScan 不应被调用
        verify(exactly = 0) { api.nativeScan(any()) }
        // 应该报错
        assertNotNull(vm.error.value)
        assertTrue(vm.error.value!!.contains("未就绪"))
    }

    @Test
    fun `connect does nothing when handle is zero`() = runTest(testDispatcher) {
        vm.connect("camera-1")
        runCurrent()
        verify(exactly = 0) { api.nativeConnect(any(), any()) }
    }

    @Test
    fun `capture does nothing when handle is zero`() = runTest(testDispatcher) {
        vm.capture()
        runCurrent()
        verify(exactly = 0) { api.nativeCapture(any()) }
    }

    @Test
    fun `startTransfer does nothing when handle is zero`() = runTest(testDispatcher) {
        vm.startTransfer(123L, "/tmp/test.nef")
        runCurrent()
        verify(exactly = 0) { api.nativeStartTransfer(any(), any(), any()) }
        assertTrue(vm.transferJobs.value.isEmpty())
    }

    @Test
    fun `startLiveView does nothing when handle is zero`() = runTest(testDispatcher) {
        vm.startLiveView()
        runCurrent()
        verify(exactly = 0) { api.nativeStartLiveView(any()) }
        assertFalse(vm.liveViewActive.value)
    }

    @Test
    fun `listFiles does nothing when handle is zero`() = runTest(testDispatcher) {
        vm.listFiles()
        runCurrent()
        verify(exactly = 0) { api.nativeListFiles(any(), any()) }
    }

    @Test
    fun `deleteFile does nothing when handle is zero`() = runTest(testDispatcher) {
        vm.deleteFile(456L)
        runCurrent()
        verify(exactly = 0) { api.nativeDeleteFile(any(), any()) }
    }

    // ─── serviceReady 状态 ──────────────────────────────────

    @Test
    fun `serviceReady becomes true when handle is set`() = runTest(testDispatcher) {
        app.cameraHandle = 12345L
        vm.refreshServiceReady()
        assertTrue(vm.serviceReady.value)
    }

    @Test
    fun `serviceReady becomes false when handle is cleared`() = runTest(testDispatcher) {
        app.cameraHandle = 12345L
        vm.refreshServiceReady()
        app.cameraHandle = 0L
        vm.refreshServiceReady()
        assertFalse(vm.serviceReady.value)
    }

    // ─── 传输并发控制 ───────────────────────────────────────

    @Test
    fun `startTransfer with handle creates ACTIVE job`() = runTest(testDispatcher) {
        app.cameraHandle = 1L
        every { api.nativeStartTransfer(any(), any(), any()) } returns 100
        vm.startTransfer(0xABCDEF, "/tmp/test.nef")
        runCurrent()
        assertEquals(1, vm.transferJobs.value.size)
        val job = vm.transferJobs.value.first()
        // v3: 稳定 UI id 与 native 任务 id 分离,native 返回值存 nativeJobId
        assertEquals(100, job.nativeJobId)
        assertEquals(TransferStatus.ACTIVE, job.status)
    }

    @Test
    fun `transfers beyond concurrentJobs limit are WAITING`() = runTest(testDispatcher) {
        app.cameraHandle = 1L
        // 设置并发上限为 2
        vm.updateSettings { it.copy(concurrentJobs = 2) }
        // 启动 3 个传输,第 3 个应该排队
        every { api.nativeStartTransfer(any(), any(), any()) } returns 1
        vm.startTransfer(0x01, "/tmp/a.nef")
        runCurrent()
        every { api.nativeStartTransfer(any(), any(), any()) } returns 2
        vm.startTransfer(0x02, "/tmp/b.nef")
        runCurrent()
        every { api.nativeStartTransfer(any(), any(), any()) } returns 3
        vm.startTransfer(0x03, "/tmp/c.nef")
        runCurrent()
        val jobs = vm.transferJobs.value
        assertEquals(3, jobs.size)
        val active = jobs.filter { it.status == TransferStatus.ACTIVE }
        val waiting = jobs.filter { it.status == TransferStatus.WAITING }
        assertEquals(2, active.size)
        assertEquals(1, waiting.size)
    }

    @Test
    fun `progress callback updates job percent and speed`() = runTest(testDispatcher) {
        app.cameraHandle = 1L
        every { api.nativeStartTransfer(any(), any(), any()) } returns 42
        vm.startTransfer(0x01, "/tmp/test.nef")
        runCurrent()
        // 模拟 native 回调:50%, 30 MB/s, 传输中
        triggerProgressCallback(42, 30.0, 50, 1)
        runCurrent()
        val job = vm.transferJobs.value.first { it.nativeJobId == 42 }
        assertEquals(50, job.percent)
        assertEquals(30.0, job.speedMbps, 0.01)
        assertEquals(TransferStatus.ACTIVE, job.status)
    }

    @Test
    fun `progress callback status 2 marks job DONE`() = runTest(testDispatcher) {
        app.cameraHandle = 1L
        every { api.nativeStartTransfer(any(), any(), any()) } returns 10
        vm.startTransfer(0x01, "/tmp/test.nef")
        runCurrent()
        triggerProgressCallback(10, 40.0, 100, 2)
        runCurrent()
        val job = vm.transferJobs.value.first { it.nativeJobId == 10 }
        assertEquals(TransferStatus.DONE, job.status)
        assertEquals(100, job.percent)
    }

    @Test
    fun `progress callback status -1 marks job FAILED`() = runTest(testDispatcher) {
        app.cameraHandle = 1L
        every { api.nativeStartTransfer(any(), any(), any()) } returns 10
        vm.startTransfer(0x01, "/tmp/test.nef")
        runCurrent()
        triggerProgressCallback(10, 0.0, 30, -1)
        runCurrent()
        val job = vm.transferJobs.value.first { it.nativeJobId == 10 }
        assertEquals(TransferStatus.FAILED, job.status)
    }

    @Test
    fun `cancelTransfer marks job CANCELLED`() = runTest(testDispatcher) {
        app.cameraHandle = 1L
        every { api.nativeStartTransfer(any(), any(), any()) } returns 5
        vm.startTransfer(0x01, "/tmp/test.nef")
        runCurrent()
        // v3: cancelTransfer 按稳定 UI id(首个任务为 1),内部解析 nativeJobId 通知 native
        vm.cancelTransfer(1)
        runCurrent()
        val job = vm.transferJobs.value.first { it.nativeJobId == 5 }
        assertEquals(TransferStatus.CANCELLED, job.status)
    }

    // ─── 设置持久化 ─────────────────────────────────────────

    @Test
    fun `settings persist to SharedPreferences`() {
        vm.updateSettings { it.copy(concurrentJobs = 5, autoTransfer = false) }
        val prefs = app.getSharedPreferences("nikon_settings", android.content.Context.MODE_PRIVATE)
        assertEquals(5, prefs.getInt("concurrentJobs", 0))
        assertFalse(prefs.getBoolean("autoTransfer", true))
    }

    @Test
    fun `settings loaded from SharedPreferences on init`() {
        // 先写入设置
        val prefs = app.getSharedPreferences("nikon_settings", android.content.Context.MODE_PRIVATE)
        prefs.edit()
            .putInt("concurrentJobs", 7)
            .putBoolean("autoTransfer", false)
            .putString("ftpHost", "10.0.0.1")
            .apply()
        // 新 ViewModel 实例应该读回这些值
        val vm2 = CameraViewModel(app, api, ioDispatcher = testDispatcher, enablePolling = false)
        assertEquals(7, vm2.settings.value.concurrentJobs)
        assertFalse(vm2.settings.value.autoTransfer)
        assertEquals("10.0.0.1", vm2.settings.value.ftpHost)
    }

    @Test
    fun `ftp settings persist`() {
        vm.updateSettings {
            it.copy(ftpHost = "192.168.1.50", ftpPort = 2121, ftpAutoUpload = true)
        }
        val prefs = app.getSharedPreferences("nikon_settings", android.content.Context.MODE_PRIVATE)
        assertEquals("192.168.1.50", prefs.getString("ftpHost", ""))
        assertEquals(2121, prefs.getInt("ftpPort", 0))
        assertTrue(prefs.getBoolean("ftpAutoUpload", false))
    }

    // ─── 错误 Channel ───────────────────────────────────────

    @Test
    fun `error events are delivered via Channel without loss`() = runTest(testDispatcher) {
        vm.errorEvents.test {
            // handle=0 时调 scan → reportError("未就绪")
            vm.scan()
            runCurrent()
            val event = awaitItem()
            assertTrue(event.contains("未就绪"))
            cancelAndIgnoreRemainingEvents()
        }
    }

    // ─── 路径生成 (Scoped Storage) ──────────────────────────

    @Test
    fun `startTransferToApp uses app-private DCIM directory`() {
        val expectedDir = app.getExternalFilesDir(android.os.Environment.DIRECTORY_DCIM)
        assertNotNull(expectedDir!!)
        // buildDestPath 是 private,通过反射验证
        val method = vm.javaClass.getDeclaredMethod("buildDestPath", String::class.java)
        method.isAccessible = true
        val path = method.invoke(vm, "DSC_0001.NEF") as String
        assertTrue(path.contains("NikonConnect"))
        assertTrue(path.contains("DSC_0001.NEF"))
        assertTrue(path.startsWith(expectedDir.absolutePath))
    }
}
