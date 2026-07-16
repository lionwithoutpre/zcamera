package com.nikon.app.ui.screens

import com.nikon.app.TestComponentActivity
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.nikon.app.TestDoubles
import com.nikon.app.setNikonContent
import com.nikon.app.settle
import com.nikon.app.clickText
import com.nikon.app.RecordingCameraApi
import com.nikon.app.jni.CameraBridge
import com.nikon.app.viewmodel.CameraViewModel
import kotlinx.coroutines.ExperimentalCoroutinesApi
import org.junit.After
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

/**
 * LiveViewScreen UI 测试。
 *
 * 覆盖实时取景页操作 bug:
 *  - 进入页面自动调 nativeStartLiveView(生命周期副作用)
 *  - AF 模式切换 → 调 setProperty(PROP_FOCUS_MODE)
 *  - 变焦 + → 调 setProperty(ZOOM) 且标签 1.0× → 1.1×(夹紧不越界)
 *  - 点击快门 → 调 nativeCapture
 *
 * 使用 RecordingCameraApi(见 TestDoubles)记录 native 调用,MockK 在 verify 作用域内会
 * 把 setProperty 解析成自己的动态属性扩展,无法直接断言,故改用显式记录。
 */
@OptIn(ExperimentalCoroutinesApi::class)
@RunWith(AndroidJUnit4::class)
class LiveViewScreenTest {

    @get:Rule
    val composeTestRule = createAndroidComposeRule<TestComponentActivity>()

    private lateinit var vm: CameraViewModel
    private lateinit var api: RecordingCameraApi

    private fun called(name: String, vararg args: Any?): Boolean =
        api.calls[name]?.any { it == args.toList() } == true

    @Before
    fun setup() {
        TestDoubles.installTestDispatchers(composeTestRule)
        val (v, a) = TestDoubles.createRecordingViewModel()
        vm = v
        api = a
    }

    @After
    fun tearDown() = TestDoubles.resetTestDispatchers()

    @Test
    fun onLaunch_callsStartLiveView() {
        composeTestRule.setNikonContent {
            LiveViewScreen(vm, onBack = {}, onNavigateToGallery = {})
        }
        assertTrue("进入 LiveView 应调 nativeStartLiveView(1)", called("nativeStartLiveView", 1L))
    }

    @Test
    fun afModeClick_callsSetProperty() {
        composeTestRule.setNikonContent {
            LiveViewScreen(vm, onBack = {}, onNavigateToGallery = {})
        }
        composeTestRule.onNodeWithText("MF").performClick()
        assertTrue(
            "选择 MF 应调 setProperty(PROP_FOCUS_MODE=0xD014, 0)",
            called("setProperty", 1L, CameraBridge.PROP_FOCUS_MODE, 0L),
        )
    }

    @Test
    fun zoomIn_clampsAndCallsSetProperty() {
        composeTestRule.setNikonContent {
            LiveViewScreen(vm, onBack = {}, onNavigateToGallery = {})
        }
        composeTestRule.settle()
        // 初始 0.5f → 标签 "1.0×"
        composeTestRule.onNodeWithText("1.0×").assertIsDisplayed()
        composeTestRule.onNodeWithText("＋").performClick()
        composeTestRule.settle()
        // 0.6f → "1.1×"
        composeTestRule.onNodeWithText("1.1×").assertIsDisplayed()
        // ZOOM = 0x5010, (0.6 * 10) = 6
        assertTrue(
            "变焦 + 应调 setProperty(ZOOM=0x5010, 6)",
            called("setProperty", 1L, 0x5010, 6L),
        )
    }

    @Test
    fun shutterClick_callsCapture() {
        composeTestRule.setNikonContent {
            LiveViewScreen(vm, onBack = {}, onNavigateToGallery = {})
        }
        composeTestRule.onNodeWithContentDescription("快门").performClick()
        assertTrue("点击快门应调 nativeCapture(1)", called("nativeCapture", 1L))
    }

    @Test
    fun zoomOut_clampsAndCallsSetProperty() {
        composeTestRule.setNikonContent {
            LiveViewScreen(vm, onBack = {}, onNavigateToGallery = {})
        }
        composeTestRule.settle()
        // 初始 0.5f → 标签 "1.0×"
        composeTestRule.onNodeWithText("1.0×").assertIsDisplayed()
        // 点击缩小(－)
        composeTestRule.onNodeWithText("－").performClick()
        composeTestRule.settle()
        // 0.4f → "0.9×"
        composeTestRule.onNodeWithText("0.9×").assertIsDisplayed()
        // ZOOM = 0x5010, (0.4 * 10) = 4
        assertTrue(
            "变焦 - 应调 setProperty(ZOOM=0x5010, 4)",
            called("setProperty", 1L, 0x5010, 4L),
        )
    }

    @Test
    fun exitLiveView_triggersOnBack() {
        var backCalled = false
        composeTestRule.setNikonContent {
            LiveViewScreen(vm, onBack = { backCalled = true }, onNavigateToGallery = {})
        }
        composeTestRule.settle()
        // 点击「退出 LV」胶囊 → 触发 onBack
        composeTestRule.clickText("退出 LV")
        assertTrue("点击退出 LV 应触发 onBack 回调", backCalled)
    }

    @Test
    fun paramAdjust_openAperture_overlayAndIncrementCallsSetProperty() {
        composeTestRule.setNikonContent {
            LiveViewScreen(vm, onBack = {}, onNavigateToGallery = {})
        }
        composeTestRule.settle()
        // 点击底部参数行的「光圈」(默认 active, 展示文案为 "光圈 ▸")→ 打开参数调节浮层
        composeTestRule.onNodeWithText("光圈 ▸").performClick()
        composeTestRule.settle()
        // 浮层打开: 浮层内 param 标签为纯 "光圈"(与展示行的 "光圈 ▸" 区分, 精确匹配)
        composeTestRule.onNodeWithText("光圈").assertIsDisplayed()
        // 点击浮层「＋」(onAllNodesWithText("＋")[1] = 浮层增量键, [0] = 变焦+)
        composeTestRule.onAllNodesWithText("＋")[1].performClick()
        composeTestRule.settle()
        // 光圈 PTP 码 = 0x500E; adjustProperty 先 get(返回 0) 再 set(+1) → setProperty(1, 0x500E, 1)
        assertTrue(
            "点击光圈 + 应调 setProperty(APERTURE=0x500E, 1)",
            called("setProperty", 1L, 0x500E, 1L),
        )
    }
}
