package com.nikon.app.ui.screens

import com.nikon.app.TestComponentActivity
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.nikon.app.TestDoubles
import com.nikon.app.setNikonContent
import com.nikon.app.jni.CameraBridge
import com.nikon.app.viewmodel.CameraViewModel
import com.nikon.app.viewmodel.TransferStatus
import io.mockk.every
import io.mockk.verify
import kotlinx.coroutines.ExperimentalCoroutinesApi
import org.junit.After
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

/**
 * TransferScreen UI 测试。
 *
 * 覆盖文件传输页最常见的操作 bug:
 *  - 失败任务正确显示「断点续传 / 重新传输」,点击「断点续传」再次调 nativeStartTransfer
 *  - 「全部取消」将所有进行中任务标记为 CANCELLED(并发/状态机)
 */
@OptIn(ExperimentalCoroutinesApi::class)
@RunWith(AndroidJUnit4::class)
class TransferScreenTest {

    @get:Rule
    val composeTestRule = createAndroidComposeRule<TestComponentActivity>()

    private lateinit var vm: CameraViewModel
    private lateinit var api: com.nikon.app.jni.CameraApi

    @Before
    fun setup() {
        TestDoubles.installTestDispatchers(composeTestRule)
        val (v, a) = TestDoubles.createTestViewModel()
        vm = v
        api = a
    }

    @After
    fun tearDown() = TestDoubles.resetTestDispatchers()

    @Test
    fun failedJob_showsResumeRetry_andResumeCallsNativeAgain() {
        // nativeStartTransfer 返回 -1 → FAILED 任务
        every { api.nativeStartTransfer(any(), any(), any()) } returns -1
        vm.startTransfer(1L, "/DCIM/NikonConnect/DSC_0001.NEF")

        composeTestRule.setNikonContent { TransferScreen(vm) }

        composeTestRule.onNodeWithText("断点续传").assertIsDisplayed()
        composeTestRule.onNodeWithText("重新传输").assertIsDisplayed()

        composeTestRule.onNodeWithText("断点续传").performClick()

        // 初始 startTransfer 调用 1 次;点击断点续传再调用 1 次
        verify(exactly = 2) { api.nativeStartTransfer(any(), any(), any()) }
    }

    @Test
    fun activeJob_cancelAll_marksAllCancelled() {
        // nativeStartTransfer relaxed 返回 0 → ACTIVE 任务
        vm.startTransfer(2L, "/DCIM/NikonConnect/DSC_0002.NEF")

        composeTestRule.setNikonContent { TransferScreen(vm) }
        composeTestRule.onNodeWithText("全部取消").performClick()
        composeTestRule.waitForIdle()

        assertTrue(
            "全部取消后所有任务应为 CANCELLED, 实际=${vm.transferJobs.value}",
            vm.transferJobs.value.all { it.status == TransferStatus.CANCELLED },
        )
    }

    @Test
    fun pauseAll_marksActivePaused() {
        vm.startTransfer(3L, "/DCIM/NikonConnect/DSC_0003.NEF")
        composeTestRule.setNikonContent { TransferScreen(vm) }
        composeTestRule.onNodeWithText("全部暂停").performClick()
        composeTestRule.waitForIdle()

        assertTrue(
            "全部暂停后所有活跃任务应为 PAUSED, 实际=${vm.transferJobs.value}",
            vm.transferJobs.value.all { it.status == TransferStatus.PAUSED },
        )
    }
}
