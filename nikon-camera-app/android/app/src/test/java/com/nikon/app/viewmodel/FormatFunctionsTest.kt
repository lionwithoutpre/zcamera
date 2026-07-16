package com.nikon.app.viewmodel

import com.nikon.app.TestNikonApplication
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Test
import java.lang.reflect.Method

/**
 * 格式化函数测试 — 通过反射调用 ViewModel 的 private 格式化方法
 *
 * 覆盖:
 * - formatShutter: Bulb / 正数分母 / 负数秒数
 * - formatAperture: 整数除以 10
 * - formatIso: 直接拼接
 * - formatEv: 正负 EV 值
 * - formatFocusMode: 0-3 映射
 * - formatWb: 0-6 映射
 * - formatQuality: 0-3 映射
 *
 * 这些方法是纯 Kotlin 逻辑,不涉及 native,因此不需要 mock CameraApi。
 * 通过 [TestNikonApplication] 在纯 JVM 下运行, 不依赖 Robolectric。
 */
class FormatFunctionsTest {

    private lateinit var vm: CameraViewModel
    private val testDispatcher = UnconfinedTestDispatcher()

    @Before
    fun setup() {
        Dispatchers.setMain(testDispatcher)
        vm = CameraViewModel(
            TestNikonApplication(),
            ioDispatcher = testDispatcher,
            enablePolling = false,
        )
    }

    @After
    fun tearDown() {
        Dispatchers.resetMain()
    }

    private fun callFormat(name: String, arg: Long): String {
        val method: Method = vm.javaClass.getDeclaredMethod(name, Long::class.javaPrimitiveType)
        method.isAccessible = true
        return method.invoke(vm, arg) as String
    }

    // ─── formatShutter ──────────────────────────────────────

    @Test fun `shutter 0 is Bulb`() = assertEquals("Bulb", callFormat("formatShutter", 0L))
    @Test fun `shutter 500 is 1_500s`() = assertEquals("1/500s", callFormat("formatShutter", 500L))
    @Test fun `shutter 1000 is 1_1000s`() = assertEquals("1/1000s", callFormat("formatShutter", 1000L))
    @Test fun `shutter -5 is 5s (slow shutter)`() = assertEquals("5s", callFormat("formatShutter", -5L))
    @Test fun `shutter -30 is 30s`() = assertEquals("30s", callFormat("formatShutter", -30L))
    @Test fun `shutter -1 is 1s`() = assertEquals("1s", callFormat("formatShutter", -1L))

    // ─── formatAperture ─────────────────────────────────────

    @Test fun `aperture 28 is f2_8`() = assertEquals("f/2.8", callFormat("formatAperture", 28L))
    @Test fun `aperture 40 is f4_0`() = assertEquals("f/4.0", callFormat("formatAperture", 40L))
    @Test fun `aperture 56 is f5_6`() = assertEquals("f/5.6", callFormat("formatAperture", 56L))
    @Test fun `aperture 80 is f8_0`() = assertEquals("f/8.0", callFormat("formatAperture", 80L))
    @Test fun `aperture 0 is f0_0`() = assertEquals("f/0.0", callFormat("formatAperture", 0L))

    // ─── formatIso ──────────────────────────────────────────

    @Test fun `iso 100`() = assertEquals("ISO 100", callFormat("formatIso", 100L))
    @Test fun `iso 3200`() = assertEquals("ISO 3200", callFormat("formatIso", 3200L))
    @Test fun `iso 0`() = assertEquals("ISO 0", callFormat("formatIso", 0L))

    // ─── formatEv ───────────────────────────────────────────

    @Test fun `ev 0 is +0_0EV`() = assertEquals("+0.0EV", callFormat("formatEv", 0L))
    @Test fun `ev 7 is +0_7EV`() = assertEquals("+0.7EV", callFormat("formatEv", 7L))
    @Test fun `ev -7 is -0_7EV`() = assertEquals("-0.7EV", callFormat("formatEv", -7L))
    @Test fun `ev 10 is +1_0EV`() = assertEquals("+1.0EV", callFormat("formatEv", 10L))
    @Test fun `ev -30 is -3_0EV`() = assertEquals("-3.0EV", callFormat("formatEv", -30L))

    // ─── formatFocusMode ────────────────────────────────────

    @Test fun `focus 0 is MF`() = assertEquals("MF", callFormat("formatFocusMode", 0L))
    @Test fun `focus 1 is AF-S`() = assertEquals("AF-S", callFormat("formatFocusMode", 1L))
    @Test fun `focus 2 is AF-C`() = assertEquals("AF-C", callFormat("formatFocusMode", 2L))
    @Test fun `focus 3 is AF-F`() = assertEquals("AF-F", callFormat("formatFocusMode", 3L))
    @Test fun `focus 99 defaults to AF`() = assertEquals("AF", callFormat("formatFocusMode", 99L))

    // ─── formatWb ───────────────────────────────────────────

    @Test fun `wb 0 is auto`() = assertEquals("自动", callFormat("formatWb", 0L))
    @Test fun `wb 1 is incandescent`() = assertEquals("白炽灯", callFormat("formatWb", 1L))
    @Test fun `wb 2 is fluorescent`() = assertEquals("荧光灯", callFormat("formatWb", 2L))
    @Test fun `wb 3 is direct sunlight`() = assertEquals("直射阳光", callFormat("formatWb", 3L))
    @Test fun `wb 4 is flash`() = assertEquals("闪光灯", callFormat("formatWb", 4L))
    @Test fun `wb 5 is cloudy`() = assertEquals("阴天", callFormat("formatWb", 5L))
    @Test fun `wb 6 is shade`() = assertEquals("阴影", callFormat("formatWb", 6L))
    @Test fun `wb 99 defaults to auto`() = assertEquals("自动", callFormat("formatWb", 99L))

    // ─── formatQuality ──────────────────────────────────────

    @Test fun `quality 0 is RAW`() = assertEquals("RAW", callFormat("formatQuality", 0L))
    @Test fun `quality 1 is JPEG`() = assertEquals("JPEG", callFormat("formatQuality", 1L))
    @Test fun `quality 2 is RAW+JPEG`() = assertEquals("RAW + JPEG", callFormat("formatQuality", 2L))
    @Test fun `quality 3 is TIFF`() = assertEquals("TIFF", callFormat("formatQuality", 3L))
    @Test fun `quality 99 defaults to RAW`() = assertEquals("RAW", callFormat("formatQuality", 99L))
}
