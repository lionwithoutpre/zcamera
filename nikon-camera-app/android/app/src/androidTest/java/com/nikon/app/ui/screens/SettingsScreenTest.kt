package com.nikon.app.ui.screens

import com.nikon.app.TestComponentActivity
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.nikon.app.TestDoubles
import com.nikon.app.setNikonContent
import com.nikon.app.scrollToText
import com.nikon.app.settle
import androidx.compose.ui.semantics.SemanticsActions
import com.nikon.app.viewmodel.CameraViewModel
import io.mockk.verify
import kotlinx.coroutines.ExperimentalCoroutinesApi
import org.junit.After
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

/**
 * SettingsScreen UI 测试。
 *
 * 覆盖设置页操作 bug:
 *  - 「边拍边传」开关 → updateSettings 翻转 autoTransfer
 *  - 「FTP 服务器」展开后「FTPS 加密」开关 → updateSettings 翻转 ftpsEncryption
 *  - 「断开连接」行 → 调 nativeDisconnect
 */
@OptIn(ExperimentalCoroutinesApi::class)
@RunWith(AndroidJUnit4::class)
class SettingsScreenTest {

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

    /**
     * 对话框选项在语义树中位于主内容之后(对话框是独立窗口,挂在根节点末尾),
     * 因此同名文本匹配里 onAllNodesWithText(text).last() 是对话框选项本身,
     * 可避开 NavRow 合并文本带来的歧义。
     */
    private fun dialogOption(text: String) =
        composeTestRule.onAllNodesWithText(text).onLast()

    @Test
    fun autoTransferToggle_updatesSettings() {
        val before = vm.settings.value.autoTransfer // 默认 true
        composeTestRule.setNikonContent { SettingsScreen(vm) }

        composeTestRule.onNodeWithText("边拍边传").performClick()
        composeTestRule.waitForIdle()

        assertTrue(
            "autoTransfer 应翻转: before=$before after=${vm.settings.value.autoTransfer}",
            vm.settings.value.autoTransfer == !before,
        )
    }

    @Test
    fun ftpExpand_toggleFtps_updatesSettings() {
        val before = vm.settings.value.ftpsEncryption // 默认 true
        composeTestRule.setNikonContent { SettingsScreen(vm) }

        composeTestRule.scrollToText("FTP 服务器")
        // 直接触发 OnClick 语义动作,避免 animateContentSize 容器 + LazyColumn 拦截合成 tap 手势
        composeTestRule.onNodeWithText("FTP 服务器").performSemanticsAction(SemanticsActions.OnClick)
        composeTestRule.settle() // 展开后「FTPS 加密」重组需推进时钟才提交
        composeTestRule.scrollToText("FTPS 加密")
        composeTestRule.onNodeWithText("FTPS 加密").performClick()
        composeTestRule.settle() // 提交 updateSettings 重组

        assertTrue(
            "ftpsEncryption 应翻转: before=$before after=${vm.settings.value.ftpsEncryption}",
            vm.settings.value.ftpsEncryption == !before,
        )
    }

    @Test
    fun disconnectRow_callsNativeDisconnect() {
        composeTestRule.setNikonContent { SettingsScreen(vm) }
        // 「断开连接」在 LazyColumn 深处,先滚入视口
        composeTestRule.scrollToText("断开连接")
        composeTestRule.onNodeWithText("断开连接").performClick()
        verify { api.nativeDisconnect(1L) }
    }

    @Test
    fun wifiPollingRow_isDisplayed() {
        composeTestRule.setNikonContent { SettingsScreen(vm) }
        // 「Wi-Fi 轮询间隔」在 LazyColumn「连接偏好」段,深滚入视口
        composeTestRule.scrollToText("Wi-Fi 轮询间隔")
        composeTestRule.onNodeWithText("Wi-Fi 轮询间隔").assertIsDisplayed()
    }

    @Test
    fun versionInfo_isDisplayed() {
        composeTestRule.setNikonContent { SettingsScreen(vm) }
        // 版本信息在 LazyColumn 底部
        composeTestRule.scrollToText("Nikon Camera Connect v2.0.0")
        composeTestRule.onNodeWithText("Nikon Camera Connect v2.0.0").assertIsDisplayed()
    }

    @Test
    fun storageTargetRow_opensDialog_andSelectsUpdatesSettings() {
        composeTestRule.setNikonContent { SettingsScreen(vm) }
        val before = vm.settings.value.storageTarget // 默认 "/DCIM/NikonConnect"

        composeTestRule.scrollToText("存储目标")
        // 深滚项用坐标无关的 OnClick 语义动作(FTP 测试同款),performClick 坐标注入会落空
        composeTestRule.onNodeWithText("存储目标")
            .performSemanticsAction(SemanticsActions.OnClick)
        composeTestRule.settle() // 提交对话框进入重组
        composeTestRule.onNodeWithText("选择存储位置").assertIsDisplayed()

        // 选一个与默认值不同的路径,用 Role.Button 过滤唯一定位对话框选项
        dialogOption("/Pictures/Nikon").performSemanticsAction(SemanticsActions.OnClick)
        composeTestRule.settle()

        assertTrue(
            "storageTarget 应更新: before=$before after=${vm.settings.value.storageTarget}",
            vm.settings.value.storageTarget == "/Pictures/Nikon",
        )
    }

    @Test
    fun blockSizeRow_opensDialog_andSelectsUpdatesSettings() {
        composeTestRule.setNikonContent { SettingsScreen(vm) }
        val before = vm.settings.value.transferBlockSize // 默认 "自动"

        composeTestRule.scrollToText("传输块大小")
        composeTestRule.onNodeWithText("传输块大小")
            .performSemanticsAction(SemanticsActions.OnClick)
        composeTestRule.settle()
        // 用对话框专属选项确认已打开(NavRow 副标题含 "1MB~4MB",用 Role.Button 过滤唯一定位)
        dialogOption("2MB").assertIsDisplayed()

        dialogOption("2MB").performSemanticsAction(SemanticsActions.OnClick)
        composeTestRule.settle()

        assertTrue(
            "transferBlockSize 应更新: before=$before after=${vm.settings.value.transferBlockSize}",
            vm.settings.value.transferBlockSize == "2MB",
        )
    }

    @Test
    fun wifiIntervalRow_opensDialog_andSelectsUpdatesSettings() {
        composeTestRule.setNikonContent { SettingsScreen(vm) }
        val before = vm.settings.value.wifiPollIntervalMs // 默认 1000

        composeTestRule.scrollToText("Wi-Fi 轮询间隔")
        composeTestRule.onNodeWithText("Wi-Fi 轮询间隔")
            .performSemanticsAction(SemanticsActions.OnClick)
        composeTestRule.settle()
        dialogOption("2000ms").assertIsDisplayed() // 对话框专属选项(Role.Button 唯一定位)

        // 选 2000ms(不等于默认 1000ms,避免与 NavRow 值文本歧义)
        dialogOption("2000ms").performSemanticsAction(SemanticsActions.OnClick)
        composeTestRule.settle()

        assertTrue(
            "wifiPollIntervalMs 应更新: before=$before after=${vm.settings.value.wifiPollIntervalMs}",
            vm.settings.value.wifiPollIntervalMs == 2000,
        )
    }

    @Test
    fun notificationPermissionRow_opensDialog() {
        composeTestRule.setNikonContent { SettingsScreen(vm) }

        composeTestRule.scrollToText("通知权限")
        composeTestRule.onNodeWithText("通知权限")
            .performSemanticsAction(SemanticsActions.OnClick)
        composeTestRule.settle()

        // 对话框专属文本「打开系统设置」出现,说明原本点不动的入口已接上交互
        composeTestRule.onNodeWithText("打开系统设置").assertIsDisplayed()
    }
}
