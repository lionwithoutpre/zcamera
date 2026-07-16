package com.nikon.app.ui.screens

import com.nikon.app.TestComponentActivity
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.nikon.app.TestDoubles
import com.nikon.app.clickText
import com.nikon.app.setNikonContent
import com.nikon.app.settle
import com.nikon.app.scrollToText
import com.nikon.app.RecordingCameraApi
import com.nikon.app.viewmodel.CameraViewModel
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.SemanticsMatcher
import kotlinx.coroutines.ExperimentalCoroutinesApi
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

/**
 * PresetScreen UI 测试(此前整屏零覆盖)。
 *
 * 覆盖预设中心常见交互 bug:
 *  - 顶部「新建预设」→ 新增自定义卡片(本地状态)
 *  - 折叠卡片头部点击 → 手风琴展开(面板出现)
 *  - 「激活此预设」→ 标记为激活态(按钮变「已激活」,头部出现「激活」徽标)
 *  - 「应用到相机」→ 调 setProperty 下发 Picture Control / WB 参数(8 次)
 *  - 参数滑块拖动 → 本地状态值变化(用 SetProgress 语义动作,确定性)
 *
 * 预设数据由 PresetScreen 本地 remember 维护(不经 ViewModel),故 VM 仅用于「应用到相机」
 * 的 setProperty 下发断言,用 RecordingCameraApi 记录 native 调用。
 */
@OptIn(ExperimentalCoroutinesApi::class)
@RunWith(AndroidJUnit4::class)
class PresetScreenTest {

    /** 匹配带有 ProgressBarRangeInfo 语义(即滑杆)的节点 */
    private val sliderMatcher = SemanticsMatcher("is slider") { node ->
        node.config.contains(SemanticsProperties.ProgressBarRangeInfo)
    }

    @get:Rule
    val composeTestRule = createAndroidComposeRule<TestComponentActivity>()

    private lateinit var vm: CameraViewModel
    private lateinit var api: RecordingCameraApi

    private fun called(name: String, vararg args: Any?): Boolean =
        api.calls[name]?.any { it == args.toList() } == true

    @Before
    fun setup() {
        TestDoubles.installTestDispatchers(composeTestRule)
        // 本屏卡片展开已改为「即时展开」(无尺寸动画),故沿用统一的 autoAdvance=false,
        // 仅依赖 settle() 推进时钟提交重组,与其他测试保持一致。
        val (v, a) = TestDoubles.createRecordingViewModel()
        vm = v
        api = a
    }

    @Test
    fun newPreset_addsCustomCard() {
        composeTestRule.setNikonContent { PresetScreen(vm) }
        composeTestRule.settle()
        // 初始三张预设(风景A 默认展开且较高,会占满首屏,故用首屏可见的「风景A」确认渲染)
        composeTestRule.onNodeWithText("风景A").assertIsDisplayed()

        // 点击「新建预设」(IconButton, contentDescription="新建预设",位于顶部栏常驻可见)
        composeTestRule.onNodeWithContentDescription("新建预设").performClick()
        composeTestRule.settle()

        // 新卡片 name = "自定义 4"(size=3 + 1),默认 expanded=true
        composeTestRule.scrollToText("自定义 4")
        composeTestRule.onNodeWithText("自定义 4").assertIsDisplayed()
    }

    @Test
    fun expandCollapsedCard_showsPanel() {
        composeTestRule.setNikonContent { PresetScreen(vm) }
        composeTestRule.settle()
        // 默认仅 风景A 展开,故「应用到相机」应只出现 1 次
        assertTrue(
            "默认仅风景A 展开,应只有 1 个「应用到相机」按钮",
            composeTestRule.onAllNodesWithText("应用到相机").fetchSemanticsNodes().size == 1,
        )

        // 点击折叠的「夜景高感」头部 → 展开其编辑面板
        composeTestRule.scrollToText("夜景高感")
        composeTestRule.clickText("夜景高感")
        composeTestRule.settle()

        // 展开后「应用到相机」出现 2 次(风景A + 夜景高感)
        assertTrue(
            "展开夜景高感后,应出现 2 个「应用到相机」按钮",
            composeTestRule.onAllNodesWithText("应用到相机").fetchSemanticsNodes().size == 2,
        )
    }

    @Test
    fun activatePreset_showsActivatedState() {
        composeTestRule.setNikonContent { PresetScreen(vm) }
        composeTestRule.settle()

        // 展开「人像柔光」(默认折叠)
        composeTestRule.scrollToText("人像柔光")
        composeTestRule.clickText("人像柔光")
        composeTestRule.settle()

        // 人像柔光面板展开后,其「激活此预设」按钮在面板底部,需先滚入视口
        composeTestRule.scrollToText("激活此预设")
        composeTestRule.onNodeWithText("激活此预设").assertIsDisplayed()

        // 点击激活 → 人像柔光变为激活,按钮变「已激活」
        composeTestRule.clickText("激活此预设")
        composeTestRule.settle()
        composeTestRule.scrollToText("已激活")
        composeTestRule.onNodeWithText("已激活").assertIsDisplayed()
    }

    @Test
    fun applyToCamera_callsSetProperty() {
        composeTestRule.setNikonContent { PresetScreen(vm) }
        composeTestRule.settle()
        // 风景A 默认展开且激活,其「应用到相机」是第一个匹配
        composeTestRule.clickText("应用到相机")
        composeTestRule.settle()

        // onApply 下发 8 次 setProperty:0x5020~0x5025 + 0x5030/0x5031
        assertTrue(
            "应用到相机应下发 8 次 setProperty(0x5020~0x5025, 0x5030/0x5031)",
            api.calls["setProperty"]?.size == 8,
        )
        // 风景A:hue=0, saturation=65 → 校验其中两项
        assertTrue("应下发 setProperty(0x5020, 0)", called("setProperty", 1L, 0x5020, 0L))
        assertTrue("应下发 setProperty(0x5021, 65)", called("setProperty", 1L, 0x5021, 65L))
    }

    @Test
    fun sliderAdjust_updatesValue() {
        composeTestRule.setNikonContent { PresetScreen(vm) }
        composeTestRule.settle()
        // 风景A 展开,第一个滑块是「色相」(value=0, range -6..6)
        val sliders = composeTestRule.onAllNodes(sliderMatcher)
        assertTrue("应至少存在一个参数滑块", sliders.fetchSemanticsNodes().size >= 1)
        val before = sliders[0]
            .fetchSemanticsNode().config[SemanticsProperties.ProgressBarRangeInfo].current

        // 手动 down→move(超过 touch slop)→up 确定性拖动。
        // 用 duration-free 手势以兼容冻结时钟(autoAdvance=false),
        // 避免 swipeRight(duration) 因时钟不推进而卡死。
        sliders[0].performTouchInput {
            val c = center
            val rightEdge = right - 1f
            down(c)
            moveTo(Offset(rightEdge, c.y))
            up()
        }
        composeTestRule.settle()

        val after = composeTestRule.onAllNodes(sliderMatcher)[0]
            .fetchSemanticsNode().config[SemanticsProperties.ProgressBarRangeInfo].current
        assertTrue("色相滑杆应从 $before 增大,实际 $after", after > before)
    }
}
