package com.nikon.app.ui.screens

import com.nikon.app.TestComponentActivity
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.nikon.app.TestDoubles
import com.nikon.app.setNikonContent
import com.nikon.app.settle
import com.nikon.app.jni.CameraBridge
import com.nikon.app.viewmodel.CameraViewModel
import io.mockk.every
import io.mockk.verify
import kotlinx.coroutines.ExperimentalCoroutinesApi
import org.junit.After
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

/**
 * GalleryScreen UI 测试。
 *
 * 覆盖相册页常见操作 bug:
 *  - 未连接相机显示空态提示
 *  - 连接后拉取文件列表并渲染缩略图
 *  - 点击缩略图进入多选,删除按钮调 nativeDeleteFile
 *  - RAW(NEF) 过滤器正确隐藏 JPG
 */
@OptIn(ExperimentalCoroutinesApi::class)
@RunWith(AndroidJUnit4::class)
class GalleryScreenTest {

    @get:Rule
    val composeTestRule = createAndroidComposeRule<TestComponentActivity>()

    private lateinit var vm: CameraViewModel
    private lateinit var api: com.nikon.app.jni.CameraApi

    private val files = arrayOf(
        "1|DSC_0001.NEF|1048576|2026-06-23T10:00:00|1|0|6000|4000|1",
        "2|DSC_0002.JPG|524288|2026-06-23T11:00:00|0|1|4000|3000|1",
    )

    @Before
    fun setup() {
        TestDoubles.installTestDispatchers(composeTestRule)
        val (v, a) = TestDoubles.createTestViewModel()
        vm = v
        api = a
        every { api.nativeConnect(any(), any()) } returns CameraBridge.CAM_OK
        every { api.nativeListFiles(any(), any()) } returns files
        every { api.nativeStartTransfer(any(), any(), any()) } returns 1
    }

    @After
    fun tearDown() = TestDoubles.resetTestDispatchers()

    @Test
    fun notConnected_showsEmptyState() {
        composeTestRule.setNikonContent { GalleryScreen(vm) }
        composeTestRule.onNodeWithText("未连接相机").assertIsDisplayed()
    }

    @Test
    fun connected_rendersFiles_andDeleteCallsNative() {
        vm.connect("cam1") // status -> CONNECTED,触发 listFiles
        composeTestRule.setNikonContent { GalleryScreen(vm) }
        // LaunchedEffect(listFiles) 与首帧布局需要时钟推进才完成
        composeTestRule.settle()

        // 缩略图格式标签出现(NEF / JPG)
        composeTestRule.onNodeWithText("NEF").assertIsDisplayed()
        composeTestRule.onNodeWithText("JPG").assertIsDisplayed()

        // 点击 NEF 缩略图选中(该卡片展示格式标签 "NEF")
        composeTestRule.onNodeWithText("NEF").performClick()
        composeTestRule.settle() // 选中态重组需推进时钟才提交
        composeTestRule.onNodeWithText("已选 1 张").assertIsDisplayed()

        // 删除
        composeTestRule.onNodeWithContentDescription("删除").performClick()
        verify { api.nativeDeleteFile(1L, 1L) }
    }

    @Test
    fun filterNef_hidesJpg() {
        vm.connect("cam1")
        composeTestRule.setNikonContent { GalleryScreen(vm) }
        composeTestRule.settle()

        // 默认全部:两类都显示
        composeTestRule.onNodeWithText("NEF").assertIsDisplayed()
        composeTestRule.onNodeWithText("JPG").assertIsDisplayed()

        // 应用 RAW(NEF) 过滤
        composeTestRule.onNodeWithText("RAW (NEF)").performClick()
        composeTestRule.settle() // 过滤后重组需推进时钟才提交

        composeTestRule.onNodeWithText("NEF").assertIsDisplayed()
        composeTestRule.onNodeWithText("JPG").assertDoesNotExist()
    }

    @Test
    fun multiSelect_twoFiles_showsCount() {
        vm.connect("cam1")
        composeTestRule.setNikonContent { GalleryScreen(vm) }
        composeTestRule.settle()

        // 长按 NEF 选中 → "已选 1 张"
        composeTestRule.onNodeWithText("NEF").performTouchInput { longClick(center) }
        composeTestRule.settle()
        composeTestRule.onNodeWithText("已选 1 张").assertIsDisplayed()

        // 再长按 JPG 选中 → "已选 2 张"
        composeTestRule.onNodeWithText("JPG").performTouchInput { longClick(center) }
        composeTestRule.settle()
        composeTestRule.onNodeWithText("已选 2 张").assertIsDisplayed()
    }

    @Test
    fun transferSelected_callsNativeStartTransfer() {
        vm.connect("cam1")
        composeTestRule.setNikonContent { GalleryScreen(vm) }
        composeTestRule.settle()

        // 长按选中 NEF(objectHandle=1)
        composeTestRule.onNodeWithText("NEF").performTouchInput { longClick(center) }
        composeTestRule.settle()
        composeTestRule.onNodeWithText("已选 1 张").assertIsDisplayed()

        // 点击多选工具栏的「传输」→ 调 startTransferToApp → nativeStartTransfer
        composeTestRule.onNodeWithText("传输").performClick()
        composeTestRule.settle()

        verify { api.nativeStartTransfer(1L, 1L, any()) }
    }

    @Test
    fun viewToggle_gridToList_showsFilenames() {
        vm.connect("cam1")
        composeTestRule.setNikonContent { GalleryScreen(vm) }
        composeTestRule.settle()

        // 默认网格: 显示格式标签, 不显示文件名
        composeTestRule.onNodeWithText("NEF").assertIsDisplayed()
        composeTestRule.onNodeWithText("DSC_0001.NEF").assertDoesNotExist()

        // 点击「列表」视图切换图标(clickable 在父 Box, contentDescription 在 Icon 上)
        composeTestRule.onNodeWithContentDescription("列表").performClick()
        composeTestRule.settle()

        // 列表模式: 文件名可见(网格模式不显示文件名)
        composeTestRule.onNodeWithText("DSC_0001.NEF").assertIsDisplayed()
    }

    @Test
    fun clickThumbnail_opensDetailCard() {
        vm.connect("cam1")
        composeTestRule.setNikonContent { GalleryScreen(vm) }
        composeTestRule.settle()

        // 单击缩略图打开详情卡(常见相册交互); 长按才是多选
        composeTestRule.onNodeWithText("NEF").performClick()
        composeTestRule.settle()

        // 文件详情卡标题出现
        composeTestRule.onNodeWithText("文件详情").assertIsDisplayed()
    }

    @Test
    fun longPressThumbnail_selectsInsteadOfOpen() {
        vm.connect("cam1")
        composeTestRule.setNikonContent { GalleryScreen(vm) }
        composeTestRule.settle()

        // 长按 → 多选, 不应打开详情卡
        composeTestRule.onNodeWithText("NEF").performTouchInput { longClick(center) }
        composeTestRule.settle()
        composeTestRule.onNodeWithText("已选 1 张").assertIsDisplayed()
        composeTestRule.onNodeWithText("文件详情").assertDoesNotExist()
    }
}
