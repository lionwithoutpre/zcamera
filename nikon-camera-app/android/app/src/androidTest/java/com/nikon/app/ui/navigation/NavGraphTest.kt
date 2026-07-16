package com.nikon.app.ui.navigation

import com.nikon.app.TestComponentActivity
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.navigation.compose.rememberNavController
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.nikon.app.TestDoubles
import com.nikon.app.setNikonContent
import com.nikon.app.settle
import com.nikon.app.scrollToText
import com.nikon.app.jni.CameraApi
import com.nikon.app.viewmodel.CameraViewModel
import io.mockk.every
import kotlinx.coroutines.ExperimentalCoroutinesApi
import org.junit.After
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

/**
 * NavGraph UI 测试。
 *
 * 覆盖底部导航与页面切换 bug:
 *  - 点击「设置」底部 tab → SettingsScreen 出现
 *  - 点击「相册」底部 tab → GalleryScreen 出现(未连接空态)
 *  - 主页点击「实时取景」→ 进入 LiveView 且底部导航栏隐藏
 */
@OptIn(ExperimentalCoroutinesApi::class)
@RunWith(AndroidJUnit4::class)
class NavGraphTest {

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
        every { api.nativeConnect(any(), any()) } returns com.nikon.app.jni.CameraBridge.CAM_OK
    }

    @After
    fun tearDown() = TestDoubles.resetTestDispatchers()

    @Test
    fun bottomNav_switchesToSettings() {
        composeTestRule.setNikonContent {
            NikonNavGraph(viewModel = vm, navController = rememberNavController())
        }
        composeTestRule.onNodeWithText("设置").performClick()
        // NavHost 进入转场被 autoAdvance=false 冻结,推进时钟让其落地
        composeTestRule.settle()
        // 「断开连接」在 Settings LazyColumn 深处,滚到可见再断言
        composeTestRule.scrollToText("断开连接")
        composeTestRule.onNodeWithText("断开连接").assertIsDisplayed()
    }

    @Test
    fun bottomNav_switchesToGallery_showsNotConnected() {
        composeTestRule.setNikonContent {
            NikonNavGraph(viewModel = vm, navController = rememberNavController())
        }
        composeTestRule.onNodeWithText("相册").performClick()
        composeTestRule.settle()
        composeTestRule.onNodeWithText("未连接相机").assertIsDisplayed()
    }

    @Test
    fun liveViewRoute_hidesBottomBar() {
        vm.connect("cam1") // 进入已连接态,主页显示仪表盘与「实时取景」入口
        composeTestRule.setNikonContent {
            NikonNavGraph(viewModel = vm, navController = rememberNavController())
        }
        composeTestRule.onNodeWithText("实时取景").performClick()
        composeTestRule.settle()
        composeTestRule.onNodeWithText("退出 LV").assertIsDisplayed()
        // LiveView 为全屏页,底部导航栏应隐藏 → 「主页」tab 不应存在
        composeTestRule.onNodeWithText("主页").assertDoesNotExist()
    }

    @Test
    fun bottomNav_switchesToGallery_connected_showsFiles() {
        val files = arrayOf(
            "1|DSC_0001.NEF|1048576|2026-06-23T10:00:00|1|0|6000|4000|1",
            "2|DSC_0002.JPG|524288|2026-06-23T11:00:00|0|1|4000|3000|1",
        )
        every { api.nativeListFiles(any(), any()) } returns files
        vm.connect("cam1")
        composeTestRule.setNikonContent {
            NikonNavGraph(viewModel = vm, navController = rememberNavController())
        }
        composeTestRule.onNodeWithText("相册").performClick()
        composeTestRule.settle()
        // 已连接: 不应再显示未连接空态
        composeTestRule.onNodeWithText("未连接相机").assertDoesNotExist()
        // 文件网格渲染(NEF 格式标签出现在缩略图, 在栅格内, 滚入视口再断言)
        composeTestRule.scrollToText("NEF")
        composeTestRule.onNodeWithText("NEF").assertIsDisplayed()
    }

    @Test
    fun bottomNav_switchesToTransfer_showsTransferScreen() {
        vm.connect("cam1")
        composeTestRule.setNikonContent {
            NikonNavGraph(viewModel = vm, navController = rememberNavController())
        }
        composeTestRule.onNodeWithText("传输").performClick()
        composeTestRule.settle()
        composeTestRule.onNodeWithText("文件传输").assertIsDisplayed()
    }

    @Test
    fun bottomNav_switchesToPreset_showsPresets() {
        composeTestRule.setNikonContent {
            NikonNavGraph(viewModel = vm, navController = rememberNavController())
        }
        composeTestRule.onNodeWithText("预设").performClick()
        composeTestRule.settle()
        // 预设卡片在 LazyColumn 内,滚入可视区后断言(兼容冻结转场下双屏共存)
        composeTestRule.scrollToText("风景A")
        composeTestRule.onNodeWithText("风景A").assertIsDisplayed()
    }
}
