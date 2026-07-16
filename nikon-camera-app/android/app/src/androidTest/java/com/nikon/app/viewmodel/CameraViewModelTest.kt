package com.nikon.app.viewmodel

import androidx.test.core.app.ApplicationProvider
import com.nikon.app.FakeCameraApi
import com.nikon.app.NikonApplication
import com.nikon.app.createFakeVm
import com.nikon.app.jni.CameraBridge
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.junit.runners.JUnit4
import androidx.test.ext.junit.runners.AndroidJUnit4

/**
 * CameraViewModel 功能(业务)测试 —— 不依赖真实相机。
 *
 * 通过 createFakeVm() 注入可配置的 FakeCameraApi + 同步 UnconfinedTestDispatcher,
 * 并以 handle=1L 模拟 CameraService 已就绪(ensureHandle 通过)。所有 native 调用
 * 既不会被真实执行,也由 FakeCameraApi 记录,可断言「状态/UI 动作 → native 接线」。
 *
 * 覆盖:服务就绪守卫、扫描解析、连接成功/失败、断开、拍摄、相机参数 get/set/adjust、
 * 参数格式化、错误上报。
 */
@OptIn(ExperimentalCoroutinesApi::class)
@RunWith(AndroidJUnit4::class)
class CameraViewModelTest {

    @Before
    fun setUp() {
        Dispatchers.setMain(UnconfinedTestDispatcher())
    }

    @After
    fun tearDown() {
        Dispatchers.resetMain()
    }

    private fun app(): NikonApplication = ApplicationProvider.getApplicationContext()

    // ── 服务就绪守卫 ──────────────────────────────────────

    @Test
    fun serviceReady_initialFalse_whenHandleZero() {
        val (vm, _) = createFakeVm(handle = 0L)
        assertFalse(vm.serviceReady.value)
    }

    @Test
    fun refreshServiceReady_reflectsHandle() {
        val (vm, _) = createFakeVm(handle = 1L)
        vm.refreshServiceReady()
        assertTrue(vm.serviceReady.value)

        app().cameraHandle = 0L
        vm.refreshServiceReady()
        assertFalse(vm.serviceReady.value)
    }

    // ── 扫描 ─────────────────────────────────────────────

    @Test
    fun scan_success_parsesCamerasAndKeepsDisconnected() {
        val (vm, api) = createFakeVm()
        api.scanResult = arrayOf("c1|Z 9|SN001|0|90|200.5|256.0")
        vm.scan()

        assertTrue(api.called("nativeScan"))
        assertEquals(1, vm.cameras.value.size)
        val cam = vm.cameras.value[0]
        assertEquals("c1", cam.id)
        assertEquals("Z 9", cam.model)
        assertEquals("SN001", cam.serial)
        assertEquals(0, cam.transport)
        assertEquals("USB", cam.transportLabel)
        assertEquals(90, cam.batteryLevel)
        assertEquals(200.5, cam.storageFreeGb, 0.001)
        assertEquals(256.0, cam.storageTotalGb, 0.001)
        // 扫描后不自动连接,仍停留在 DISCONNECTED
        assertEquals(CameraBridge.STATUS_DISCONNECTED, vm.status.value)
    }

    @Test
    fun scan_emptyResult_noCameras() {
        val (vm, api) = createFakeVm()
        api.scanResult = emptyArray()
        vm.scan()
        assertEquals(0, vm.cameras.value.size)
    }

    @Test
    fun scan_malformedRawFilteredOut() {
        val (vm, api) = createFakeVm()
        api.scanResult = arrayOf("too|few", "a|b|c|d|e|f", "valid|Z6|SN|1|80|10|20")
        vm.scan()
        assertEquals(1, vm.cameras.value.size)
        assertEquals("valid", vm.cameras.value[0].id)
    }

    @Test
    fun scan_noHandle_reportsErrorAndKeepsState() {
        val (vm, api) = createFakeVm(handle = 0L)
        vm.scan()
        assertFalse(api.called("nativeScan"))
        assertTrue(vm.error.value?.contains("相机服务未就绪") == true)
        assertEquals(0, vm.cameras.value.size)
    }

    // ── 连接 ─────────────────────────────────────────────

    @Test
    fun connect_success_setsConnected_registersCallback_startsPolling() {
        val (vm, api) = createFakeVm()
        api.connectRc = CameraBridge.CAM_OK
        vm.connect("c1")

        assertEquals(CameraBridge.STATUS_CONNECTED, vm.status.value)
        assertEquals("c1", api.argsOf("nativeConnect")[1])
        assertTrue(api.called("nativeRegisterProgressCallback"))
    }

    @Test
    fun connect_failure_setsErrorStatus_andReports() {
        val (vm, api) = createFakeVm()
        api.connectRc = -1
        vm.connect("c1")

        assertEquals(CameraBridge.STATUS_ERROR, vm.status.value)
        assertTrue(vm.error.value?.contains("连接失败") == true)
    }

    @Test
    fun connect_noHandle_reportsError() {
        val (vm, api) = createFakeVm(handle = 0L)
        vm.connect("c1")
        assertFalse(api.called("nativeConnect"))
        assertTrue(vm.error.value?.contains("相机服务未就绪") == true)
    }

    @Test
    fun disconnect_callsNativeAndResetsStatus() {
        val (vm, api) = createFakeVm()
        api.connectRc = CameraBridge.CAM_OK
        vm.connect("c1")
        vm.disconnect()

        assertTrue(api.called("nativeDisconnect"))
        assertEquals(CameraBridge.STATUS_DISCONNECTED, vm.status.value)
    }

    // ── 拍摄 ─────────────────────────────────────────────

    @Test
    fun capture_success_noError_andCallsNative() {
        val (vm, api) = createFakeVm()
        api.captureRc = CameraBridge.CAM_OK
        vm.capture()
        assertTrue(api.called("nativeCapture"))
        assertNull(vm.error.value)
    }

    @Test
    fun capture_failure_reportsError() {
        val (vm, api) = createFakeVm()
        api.captureRc = -1
        vm.capture()
        assertTrue(vm.error.value?.contains("拍摄失败") == true)
    }

    @Test
    fun capture_noHandle_reportsError() {
        val (vm, api) = createFakeVm(handle = 0L)
        vm.capture()
        assertFalse(api.called("nativeCapture"))
        assertTrue(vm.error.value?.contains("相机服务未就绪") == true)
    }

    @Test
    fun captureBurst_callsNativeWithArgs() {
        val (vm, api) = createFakeVm()
        vm.captureBurst(5, 100)
        assertTrue(api.called("nativeCaptureBurst"))
        val args = api.argsOf("nativeCaptureBurst")
        assertEquals(5, args[1])
        assertEquals(100, args[2])
    }

    // ── 相机参数 ─────────────────────────────────────────

    @Test
    fun setProperty_callsNativeWithValue() {
        val (vm, api) = createFakeVm()
        vm.setProperty(CameraBridge.PROP_ISO, 400)
        assertTrue(api.called("setProperty"))
        assertEquals(400L, api.argsOf("setProperty")[2])
    }

    @Test
    fun getProperty_returnsBridgeValue() = runBlocking {
        val (vm, api) = createFakeVm()
        api.propertyValues[CameraBridge.PROP_ISO] = 800
        assertEquals(800L, vm.getProperty(CameraBridge.PROP_ISO))
    }

    @Test
    fun getProperty_noHandle_returnsZero() = runBlocking {
        val (vm, api) = createFakeVm(handle = 0L)
        assertEquals(0L, vm.getProperty(CameraBridge.PROP_ISO))
    }

    @Test
    fun adjustProperty_getsThenSets() {
        val (vm, api) = createFakeVm()
        api.propertyValues[CameraBridge.PROP_ISO] = 400
        vm.adjustProperty(CameraBridge.PROP_ISO, 200)
        assertTrue(api.called("nativeGetProperty"))
        assertTrue(api.called("setProperty"))
        // 400 + 200 = 600
        assertEquals(600L, api.argsOf("setProperty")[2])
    }

    @Test
    fun adjustProperty_noHandle_noCalls() {
        val (vm, api) = createFakeVm(handle = 0L)
        vm.adjustProperty(CameraBridge.PROP_ISO, 200)
        assertFalse(api.called("nativeGetProperty"))
        assertFalse(api.called("setProperty"))
    }

    // ── 参数格式化(经连接后的参数轮询观察 cameraProperties)──

    @Test
    fun connect_pollsAndFormatsAllProperties() {
        val (vm, api) = createFakeVm()
        api.connectRc = CameraBridge.CAM_OK
        api.propertyValues[CameraBridge.PROP_SHUTTER_SPEED] = 200
        api.propertyValues[CameraBridge.PROP_APERTURE] = 28
        api.propertyValues[CameraBridge.PROP_ISO] = 400
        api.propertyValues[CameraBridge.PROP_EXPOSURE_COMP] = 5
        api.propertyValues[CameraBridge.PROP_FOCUS_MODE] = 2
        api.propertyValues[CameraBridge.PROP_WHITE_BALANCE] = 3
        api.propertyValues[CameraBridge.PROP_IMAGE_QUALITY] = 2
        vm.connect("c1")

        val p = vm.cameraProperties.value
        assertEquals("1/200s", p.shutterSpeed)
        assertEquals("f/2.8", p.aperture)
        assertEquals("ISO 400", p.iso)
        assertEquals("+0.5EV", p.ev)
        assertEquals("AF-C", p.focusMode)
        assertEquals("直射阳光", p.whiteBalance)
        assertEquals("RAW + JPEG", p.imageQuality)
    }

    @Test
    fun propertyFormatting_edgeCases() {
        val (vm, api) = createFakeVm()
        api.connectRc = CameraBridge.CAM_OK
        // 首次连接得到一组基线值
        api.propertyValues[CameraBridge.PROP_SHUTTER_SPEED] = 0   // → Bulb
        api.propertyValues[CameraBridge.PROP_APERTURE] = 28
        api.propertyValues[CameraBridge.PROP_ISO] = 100
        api.propertyValues[CameraBridge.PROP_EXPOSURE_COMP] = -23 // → -2.3EV
        api.propertyValues[CameraBridge.PROP_FOCUS_MODE] = 0      // → MF
        api.propertyValues[CameraBridge.PROP_WHITE_BALANCE] = 0   // → 自动
        api.propertyValues[CameraBridge.PROP_IMAGE_QUALITY] = 0   // → RAW
        vm.connect("c1")
        var p = vm.cameraProperties.value
        assertEquals("Bulb", p.shutterSpeed)
        assertEquals("-2.3EV", p.ev)
        assertEquals("MF", p.focusMode)
        assertEquals("自动", p.whiteBalance)
        assertEquals("RAW", p.imageQuality)

        // 再次连接,改变快门为慢门负值(-5 → "5s")、光圈为 35(→ f/3.5)
        api.propertyValues[CameraBridge.PROP_SHUTTER_SPEED] = -5
        api.propertyValues[CameraBridge.PROP_APERTURE] = 35
        vm.connect("c1")
        p = vm.cameraProperties.value
        assertEquals("5s", p.shutterSpeed)
        assertEquals("f/3.5", p.aperture)
    }
}
