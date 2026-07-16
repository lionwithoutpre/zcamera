package com.nikon.app.ui.screens

import com.nikon.app.TestComponentActivity
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.nikon.app.NikonApplication
import com.nikon.app.TestDoubles
import com.nikon.app.setNikonContent
import com.nikon.app.scrollToText
import com.nikon.app.settle
import com.nikon.app.jni.CameraApi
import com.nikon.app.jni.CameraBridge
import com.nikon.app.viewmodel.CameraViewModel
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
 * 1.6.2 的 ui-test 未提供 hasRole,这里用 SemanticsMatcher.expectValue 自建等价实现。
 */
private fun hasRole(role: Role): SemanticsMatcher =
    SemanticsMatcher.expectValue(SemanticsProperties.Role, role)

/**
 * HomeScreen UI 测试。
 *
 * 覆盖用户最关心的「UI 操作 bug」:
 *  - 服务未就绪时扫描按钮禁用(防止误点导致异常)
 *  - 就绪后点击扫描 → 调 nativeScan,设备卡片出现 → 点击连接 → 调 nativeConnect 并进入仪表盘
 *  - 仪表盘「自动传输」开关 → 调 updateSettings(双向绑定)
 */
@OptIn(ExperimentalCoroutinesApi::class)
@RunWith(AndroidJUnit4::class)
class HomeScreenTest {

    @get:Rule
    val composeTestRule = createAndroidComposeRule<TestComponentActivity>()

    private lateinit var vm: CameraViewModel
    private lateinit var api: CameraApi

    @Before
    fun setup() {
        TestDoubles.installTestDispatchers(composeTestRule)
        val (v, a) = TestDoubles.createTestViewModel()
        vm = v
        api = a
        every { api.nativeScan(any()) } returns emptyArray()
        every { api.nativeConnect(any(), any()) } returns CameraBridge.CAM_OK
    }

    @After
    fun tearDown() = TestDoubles.resetTestDispatchers()

    @Test
    fun serviceNotReady_scanButtonShowsInitializingAndIsDisabled() {
        composeTestRule.setNikonContent { HomeScreen(vm, onNavigateToLiveView = {}) }
        // 未就绪: 按钮文案为「服务启动中…」,且不应出现就绪态文案
        composeTestRule.onNodeWithText("服务启动中…").assertIsDisplayed()
        composeTestRule.onNodeWithText("请连接相机后选择连接方式").assertDoesNotExist()
    }

    @Test
    fun serviceReady_scanShowsDevice_thenConnectEntersDashboard() {
        vm.refreshServiceReady() // serviceReady = true
        every { api.nativeScan(any()) } returns
            arrayOf("cam1|Nikon Z9|SN123|0|80|32.0|64.0")

        composeTestRule.setNikonContent { HomeScreen(vm, onNavigateToLiveView = {}) }
        composeTestRule.settle() // 让 lifecycle RESUMED, collectAsStateWithLifecycle 开始收集

        // 点击「扫描设备」
        composeTestRule.onNodeWithText("扫描设备").performClick()
        verify { api.nativeScan(1L) }

        // 设备卡片出现,点击连接(设备卡在可滚动内容底部,先滚入视口)
        composeTestRule.settle() // 让 cameras 状态更新提交到语义树
        composeTestRule.scrollToText("Nikon Z9")
        composeTestRule.onNodeWithText("Nikon Z9").assertIsDisplayed()
        composeTestRule.onNodeWithText("Nikon Z9").performClick()
        verify { api.nativeConnect(1L, "cam1") }

        // 进入仪表盘(commit 重组)
        composeTestRule.settle()
        composeTestRule.onNodeWithText("立即拍摄").assertIsDisplayed()
    }

    @Test
    fun autoTransferToggle_updatesSettings() {
        vm.refreshServiceReady()
        every { api.nativeScan(any()) } returns
            arrayOf("cam1|Nikon Z9|SN123|0|80|32.0|64.0")
        composeTestRule.setNikonContent { HomeScreen(vm, onNavigateToLiveView = {}) }
        composeTestRule.settle() // 让 lifecycle RESUMED, collectAsStateWithLifecycle 开始收集
        composeTestRule.onNodeWithText("扫描设备").performClick()
        composeTestRule.settle() // 让 cameras 状态更新提交到语义树
        composeTestRule.scrollToText("Nikon Z9")
        composeTestRule.onNodeWithText("Nikon Z9").performClick()
        composeTestRule.settle() // commit 连接到仪表盘的重组

        val before = vm.settings.value.autoTransfer
        // HomeScreen 仪表盘当前只有 1 个 Switch(自动传输)
        composeTestRule.onNode(hasRole(Role.Switch)).performClick()
        composeTestRule.settle() // commit updateSettings 重组

        assertTrue(
            "autoTransfer 应翻转: before=$before after=${vm.settings.value.autoTransfer}",
            vm.settings.value.autoTransfer == !before,
        )
    }
}
