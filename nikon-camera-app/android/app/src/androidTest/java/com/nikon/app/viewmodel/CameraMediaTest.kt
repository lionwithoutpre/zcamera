package com.nikon.app.viewmodel

import androidx.test.core.app.ApplicationProvider
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
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import androidx.test.ext.junit.runners.AndroidJUnit4

/**
 * 媒体功能测试:相机文件列举 / 缩略图 / 删除 / 实时取景。不依赖真实相机。
 */
@OptIn(ExperimentalCoroutinesApi::class)
@RunWith(AndroidJUnit4::class)
class CameraMediaTest {

    @Before
    fun setUp() {
        Dispatchers.setMain(UnconfinedTestDispatcher())
    }

    @After
    fun tearDown() {
        Dispatchers.resetMain()
    }

    private fun app(): NikonApplication = ApplicationProvider.getApplicationContext()

    private val sampleFile =
        "1|DSC001.NEF|1048576|2026-07-16 10:00|1|0|6000|4000|0"

    // ── 文件列举 ─────────────────────────────────────────

    @Test
    fun listFiles_success_parsesAndClearsLoading() {
        val (vm, api) = createFakeVm()
        api.listFilesResult = arrayOf(sampleFile)
        vm.listFiles()

        assertTrue(api.called("nativeListFiles"))
        assertEquals(1, vm.fileList.value.size)
        assertEquals("DSC001.NEF", vm.fileList.value[0].filename)
        assertEquals("NEF", vm.fileList.value[0].format)
        assertEquals("1.0 MB", vm.fileList.value[0].sizeLabel)
        assertFalse(vm.fileListLoading.value)
    }

    @Test
    fun listFiles_withStorageId_passesArg() {
        val (vm, api) = createFakeVm()
        api.listFilesResult = emptyArray()
        vm.listFiles(CameraBridge.STORAGE_SD)
        assertEquals(CameraBridge.STORAGE_SD, api.argsOf("nativeListFiles")[1])
    }

    @Test
    fun listFiles_nullResult_emptyAndLoadingFalse() {
        val (vm, api) = createFakeVm()
        api.listFilesResult = null
        vm.listFiles()
        assertEquals(0, vm.fileList.value.size)
        assertFalse(vm.fileListLoading.value)
    }

    @Test
    fun listFiles_malformedRawFiltered() {
        val (vm, api) = createFakeVm()
        api.listFilesResult = arrayOf("bad", "x|y|z", sampleFile)
        vm.listFiles()
        assertEquals(1, vm.fileList.value.size)
    }

    @Test
    fun listFiles_noHandle_reportsError_andKeepsPrevious() {
        val (vm, api) = createFakeVm(handle = 0L)
        vm.listFiles()
        assertFalse(api.called("nativeListFiles"))
        assertTrue(vm.error.value?.contains("相机服务未就绪") == true)
        assertEquals(0, vm.fileList.value.size)
    }

    // ── 缩略图 ───────────────────────────────────────────

    @Test
    fun getThumbnail_returnsBytes_whenHandleSet() = runBlocking {
        val (vm, api) = createFakeVm()
        val bytes = vm.getThumbnail(1L)
        assertNotNull(bytes)
        assertTrue(api.called("nativeGetThumbnail"))
    }

    @Test
    fun getThumbnail_returnsNull_whenNoHandle() = runBlocking {
        val (vm, api) = createFakeVm(handle = 0L)
        val bytes = vm.getThumbnail(1L)
        assertNull(bytes)
        assertFalse(api.called("nativeGetThumbnail"))
    }

    // ── 删除 ─────────────────────────────────────────────

    @Test
    fun deleteFile_success_removesFromList() {
        val (vm, api) = createFakeVm()
        api.listFilesResult = arrayOf(sampleFile)
        vm.listFiles()
        assertEquals(1, vm.fileList.value.size)

        vm.deleteFile(1L)
        assertTrue(api.called("nativeDeleteFile"))
        assertEquals(1L, api.argsOf("nativeDeleteFile")[1])
        assertEquals(0, vm.fileList.value.size)
    }

    @Test
    fun deleteFile_failure_reportsError_keepsList() {
        val (vm, api) = createFakeVm()
        api.listFilesResult = arrayOf(sampleFile)
        vm.listFiles()
        api.deleteRc = -1
        vm.deleteFile(1L)

        assertTrue(vm.error.value?.contains("删除失败") == true)
        assertEquals(1, vm.fileList.value.size)
    }

    @Test
    fun deleteFile_noHandle_reportsError() {
        val (vm, api) = createFakeVm(handle = 0L)
        vm.deleteFile(1L)
        assertFalse(api.called("nativeDeleteFile"))
        assertTrue(vm.error.value?.contains("相机服务未就绪") == true)
        // no-handle 下连 listFiles 也不会成功,文件列表保持空,且删除操作不破坏它
        assertEquals(0, vm.fileList.value.size)
    }

    // ── 实时取景 ─────────────────────────────────────────

    @Test
    fun startLiveView_success_setsActive_andFramesArrive() {
        val (vm, api) = createFakeVm()
        api.liveViewStartRc = CameraBridge.CAM_OK
        vm.startLiveView()

        assertTrue(api.called("nativeStartLiveView"))
        assertTrue(vm.liveViewActive.value)
        assertNotNull(vm.liveViewFrame.value) // 帧轮询已取一帧
    }

    @Test
    fun startLiveView_failure_reportsError_notActive() {
        val (vm, api) = createFakeVm()
        api.liveViewStartRc = -1
        vm.startLiveView()
        assertTrue(vm.error.value?.contains("启动实时取景失败") == true)
        assertFalse(vm.liveViewActive.value)
        assertNull(vm.liveViewFrame.value)
    }

    @Test
    fun startLiveView_idempotent_callsNativeOnce() {
        val (vm, api) = createFakeVm()
        api.liveViewStartRc = CameraBridge.CAM_OK
        vm.startLiveView()
        vm.startLiveView()
        assertEquals(1, api.callCount("nativeStartLiveView"))
    }

    @Test
    fun startLiveView_noHandle_reportsError() {
        val (vm, api) = createFakeVm(handle = 0L)
        vm.startLiveView()
        assertFalse(api.called("nativeStartLiveView"))
        assertTrue(vm.error.value?.contains("相机服务未就绪") == true)
        assertFalse(vm.liveViewActive.value)
    }

    @Test
    fun stopLiveView_callsNative_clearsFrame_andActive() {
        val (vm, api) = createFakeVm()
        api.liveViewStartRc = CameraBridge.CAM_OK
        vm.startLiveView()
        assertTrue(vm.liveViewActive.value)

        vm.stopLiveView()
        assertTrue(api.called("nativeStopLiveView"))
        assertFalse(vm.liveViewActive.value)
        assertNull(vm.liveViewFrame.value)
    }
}
