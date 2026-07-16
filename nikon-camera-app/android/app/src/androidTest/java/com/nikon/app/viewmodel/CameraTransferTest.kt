package com.nikon.app.viewmodel

import android.content.Context
import androidx.test.core.app.ApplicationProvider
import com.nikon.app.NikonApplication
import com.nikon.app.createFakeVm
import com.nikon.app.jni.CameraBridge
import kotlinx.coroutines.ExperimentalCoroutinesApi
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
 * 传输任务管理功能测试:并发控制、启动成功/失败、取消、暂停、全部取消、断点续传、重传。
 * 不依赖真实相机(假 handle + FakeCameraApi)。
 */
@OptIn(ExperimentalCoroutinesApi::class)
@RunWith(AndroidJUnit4::class)
class CameraTransferTest {

    @Before
    fun setUp() {
        Dispatchers.setMain(UnconfinedTestDispatcher())
        // 清空设置,避免并发上限等被其他用例落盘的值污染(本类多个用例依赖默认 concurrentJobs=3)
        val app = ApplicationProvider.getApplicationContext<NikonApplication>()
        app.getSharedPreferences("nikon_settings", Context.MODE_PRIVATE).edit().clear().commit()
    }

    @After
    fun tearDown() {
        Dispatchers.resetMain()
    }

    private fun app(): NikonApplication = ApplicationProvider.getApplicationContext()
    private fun activeCount(vm: CameraViewModel) =
        vm.transferJobs.value.count { it.status == TransferStatus.ACTIVE }
    private fun waitingCount(vm: CameraViewModel) =
        vm.transferJobs.value.count { it.status == TransferStatus.WAITING }

    @Test
    fun startTransfer_success_createsActiveJob_andSetsTransferring() {
        val (vm, api) = createFakeVm()
        vm.startTransfer(10L, "/dest/a.nef")

        assertEquals(CameraBridge.STATUS_TRANSFERRING, vm.status.value)
        assertEquals(1, activeCount(vm))
        assertEquals(10L, vm.transferJobs.value[0].objectHandle)
        assertEquals("/dest/a.nef", vm.transferJobs.value[0].destPath)
        assertTrue(api.called("nativeStartTransfer"))
        assertEquals(10L, api.argsOf("nativeStartTransfer")[1])
    }

    @Test
    fun startTransfer_failure_createsFailedJob_andReports() {
        val (vm, api) = createFakeVm()
        api.startTransferRc = -1
        vm.startTransfer(10L, "/dest/a.nef")

        assertEquals(1, vm.transferJobs.value.size)
        assertEquals(TransferStatus.FAILED, vm.transferJobs.value[0].status)
        assertTrue(vm.error.value?.contains("传输失败") == true)
    }

    @Test
    fun startTransfer_noHandle_reportsError_andNoJob() {
        val (vm, api) = createFakeVm(handle = 0L)
        vm.startTransfer(10L, "/dest/a.nef")
        assertFalse(api.called("nativeStartTransfer"))
        assertTrue(vm.error.value?.contains("相机服务未就绪") == true)
        assertEquals(0, vm.transferJobs.value.size)
    }

    @Test
    fun concurrency_singleActiveThenWaiting_promotesOnCancel() {
        val (vm, api) = createFakeVm()
        vm.updateSettings { it.copy(concurrentJobs = 1) }

        vm.startTransfer(1L, "/a.nef")   // 立即启动 → ACTIVE
        vm.startTransfer(2L, "/b.nef")   // 超过并发上限 → WAITING

        assertEquals(1, activeCount(vm))
        assertEquals(1, waitingCount(vm))
        assertEquals(1, api.callCount("nativeStartTransfer")) // 仅第一个真正下发

        // 取消活跃任务 → 排队任务被提升
        vm.cancelTransfer(1)
        assertEquals(0, waitingCount(vm))
        assertEquals(1, activeCount(vm))
        assertEquals(2, api.callCount("nativeStartTransfer"))
    }

    @Test
    fun cancelTransfer_callsNative_andMarksCancelled() {
        val (vm, api) = createFakeVm()
        vm.startTransfer(1L, "/a.nef")
        vm.cancelTransfer(1)

        assertTrue(api.called("nativeCancelTransfer"))
        assertEquals(1, api.argsOf("nativeCancelTransfer")[1])
        assertEquals(
            1,
            vm.transferJobs.value.count { it.status == TransferStatus.CANCELLED },
        )
    }

    @Test
    fun pauseAllTransfers_marksPaused_andCallsNativePerActive() {
        val (vm, api) = createFakeVm()
        vm.startTransfer(1L, "/a.nef")
        vm.startTransfer(2L, "/b.nef")
        vm.pauseAllTransfers()

        assertEquals(
            2,
            vm.transferJobs.value.count { it.status == TransferStatus.PAUSED },
        )
        assertEquals(2, api.callCount("nativeCancelTransfer"))
    }

    @Test
    fun cancelAllTransfers_marksAllCancelled() {
        val (vm, api) = createFakeVm()
        vm.startTransfer(1L, "/a.nef")
        vm.startTransfer(2L, "/b.nef")
        vm.pauseAllTransfers()
        vm.cancelAllTransfers()

        val remaining = vm.transferJobs.value.filter {
            it.status == TransferStatus.ACTIVE ||
                it.status == TransferStatus.PAUSED ||
                it.status == TransferStatus.WAITING
        }
        assertTrue(remaining.isEmpty())
        // 暂停时按活跃任务数取消 2 次 + 取消时按可取消任务数取消 2 次
        assertEquals(4, api.callCount("nativeCancelTransfer"))
    }

    @Test
    fun resumeTransfer_reTransfersAndMarksActive() {
        val (vm, api) = createFakeVm()
        // 先制造一个失败任务
        api.startTransferRc = -1
        vm.startTransfer(1L, "/a.nef")
        assertEquals(TransferStatus.FAILED, vm.transferJobs.value[0].status)
        // 再续传
        api.startTransferRc = 1
        vm.resumeTransfer(1)

        assertEquals(1, activeCount(vm))
        assertEquals(2, api.callCount("nativeStartTransfer")) // 失败的那次 + 续传这次
    }

    @Test
    fun retryTransfer_resetsPercentAndReTransfers() {
        val (vm, api) = createFakeVm()
        api.startTransferRc = -1
        vm.startTransfer(1L, "/a.nef")
        api.startTransferRc = 1
        vm.retryTransfer(1)

        val job = vm.transferJobs.value.first { it.status == TransferStatus.ACTIVE }
        assertEquals(0, job.percent)
        assertEquals(2, api.callCount("nativeStartTransfer"))
    }
}
