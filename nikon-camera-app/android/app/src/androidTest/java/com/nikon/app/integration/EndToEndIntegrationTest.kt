package com.nikon.app.integration

import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.nikon.app.NikonApplication
import com.nikon.app.jni.CameraBridge
import com.nikon.app.viewmodel.CameraViewModel
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import kotlinx.coroutines.test.setMain
import kotlinx.coroutines.test.resetMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/**
 * 集成测试 2 —— 驱动真实 [CameraViewModel](注入真实 [CameraBridge] 与真实句柄)。
 *
 * 验证“ViewModel 状态机 ↔ 真实 native 栈 ↔ 真实硬件”整链路:
 * 连接后的 serviceReady、拍摄、文件列举、实时取景帧直达 StateFlow。
 * 无真实相机时, [requireRealCamera] 抛 AssumptionViolatedException → 用例 SKIP。
 */
@RunWith(AndroidJUnit4::class)
@OptIn(ExperimentalCoroutinesApi::class)
class EndToEndIntegrationTest {

    private lateinit var vm: CameraViewModel
    private var session: RealCameraSession? = null

    @Before
    fun setUp() {
        // 无相机 / 无 .so 时这里直接 skip
        session = requireRealCamera()
        // 把真实句柄注入 Application,使 ViewModel.ensureHandle() 通过
        val app = ApplicationProvider.getApplicationContext<NikonApplication>()
        app.cameraHandle = session!!.handle
        Dispatchers.setMain(UnconfinedTestDispatcher())
        vm = CameraViewModel(
            application = app,
            bridge = CameraBridge, // 真实桥,非替身
            ioDispatcher = Dispatchers.IO,
            enablePolling = false, // 避免无限状态轮询自旋
        )
        vm.refreshServiceReady()
    }

    @After
    fun tearDown() {
        session?.let {
            runCatching { CameraBridge.nativeDisconnect(it.handle) }
            runCatching { CameraBridge.nativeDestroy(it.handle) }
        }
        Dispatchers.resetMain()
    }

    @Test
    fun connect_updatesServiceReady_andHandshakeOk() {
        assertTrue("serviceReady 应为 true(真实句柄已注入)", vm.serviceReady.value)
        assertEquals(
            "native 握手状态应为 STATUS_CONNECTED",
            CameraBridge.STATUS_CONNECTED,
            CameraBridge.nativeGetStatus(session!!.handle),
        )
    }

    @Test
    fun capture_producesNoError() {
        vm.capture()
        assertTrue("拍摄不应产生 error,实际 error=${vm.error.value}", vm.error.value == null)
    }

    @Test
    fun listFiles_populatesRealFileList() {
        vm.listFiles()
        val list = vm.fileList.value
        assertTrue("应列举到真实文件", list.isNotEmpty())
        list.forEach { f ->
            assertNotNull("文件名不应为 null", f.filename)
            assertTrue("文件 size 应为正数,实际=${f.size}", f.size > 0)
        }
    }

    @Test
    fun liveView_deliversRealFrame_toStateFlow() {
        vm.startLiveView()
        assertTrue("LiveView 应已激活", vm.liveViewActive.value)
        // 真实帧经 IO 取回后写入 liveViewFrame,等待至多 10s
        var waited = 0
        while (waited < 10000 && !isJpeg(vm.liveViewFrame.value)) {
            Thread.sleep(300)
            waited += 300
        }
        assertTrue(
            "取景帧应经 StateFlow 抵达且为真实 JPEG,size=${jpegSize(vm.liveViewFrame.value)}",
            isJpeg(vm.liveViewFrame.value),
        )
        vm.stopLiveView()
        assertTrue("停止后 LiveView 应非激活", !vm.liveViewActive.value)
    }
}
