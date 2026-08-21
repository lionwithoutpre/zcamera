package com.nikon.app.viewmodel
import com.nikon.model.AppSettings

import android.content.Context
import androidx.test.core.app.ApplicationProvider
import com.nikon.app.FakeCameraApi
import com.nikon.app.NikonApplication
import com.nikon.app.createFakeVm
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import androidx.test.ext.junit.runners.AndroidJUnit4

/**
 * App 设置持久化功能测试:默认值、内存更新 + SharedPreferences 落盘、跨 ViewModel 重新加载、
 * 每个持久化字段的往返(含上一轮新增的 ftpHost/ftpPort/storageTarget/transferBlockSize/
 * wifiPollIntervalMs),以及错误清除。
 * 不依赖真实相机。
 */
@OptIn(ExperimentalCoroutinesApi::class)
@RunWith(AndroidJUnit4::class)
class AppSettingsPersistenceTest {

    @Before
    fun setUp() {
        Dispatchers.setMain(UnconfinedTestDispatcher())
        // 清空旧设置,避免用例间串扰
        val app = ApplicationProvider.getApplicationContext<NikonApplication>()
        app.cameraHandle = 1L
        app.getSharedPreferences("nikon_settings", Context.MODE_PRIVATE).edit().clear().commit()
    }

    @After
    fun tearDown() {
        Dispatchers.resetMain()
    }

    private fun freshVm(): CameraViewModel {
        val app = ApplicationProvider.getApplicationContext<NikonApplication>()
        return CameraViewModel(app, FakeCameraApi(), UnconfinedTestDispatcher(), enablePolling = false)
    }

    @Test
    fun defaults_matchAppSettingsDefaults() {
        val (vm, _) = createFakeVm()
        assertEquals(AppSettings(), vm.settings.value)
    }

    @Test
    fun updateSettings_updatesInMemoryAndPersists() {
        val (vm, _) = createFakeVm()
        vm.updateSettings { it.copy(autoTransfer = false, concurrentJobs = 7) }

        assertEquals(false, vm.settings.value.autoTransfer)
        assertEquals(7, vm.settings.value.concurrentJobs)

        val prefs = ApplicationProvider.getApplicationContext<NikonApplication>()
            .getSharedPreferences("nikon_settings", Context.MODE_PRIVATE)
        assertEquals(false, prefs.getBoolean("autoTransfer", true))
        assertEquals(7, prefs.getInt("concurrentJobs", 3))
    }

    @Test
    fun newViewModel_loadsPersistedSettings() {
        val (vm, _) = createFakeVm()
        vm.updateSettings {
            it.copy(autoTransfer = false, storageTarget = "/Pictures/Nikon", wifiPollIntervalMs = 5000)
        }

        val reloaded = freshVm()
        assertEquals(false, reloaded.settings.value.autoTransfer)
        assertEquals("/Pictures/Nikon", reloaded.settings.value.storageTarget)
        assertEquals(5000, reloaded.settings.value.wifiPollIntervalMs)
    }

    @Test
    fun everyPersistedField_roundTrips() {
        val (vm, _) = createFakeVm()
        val custom = AppSettings(
            autoTransfer = false,
            concurrentJobs = 7,
            formatJpg = false,
            formatNef = false,
            formatMov = true,
            autoChunk = false,
            speedAdaptive = false,
            smallFileFirst = false,
            resumeTransfer = false,
            ftpsEncryption = false,
            ftpAutoUpload = true,
            preferUsb = false,
            notifyComplete = false,
            notifyFail = false,
            ftpHost = "10.0.0.5",
            ftpPort = 990,
            storageTarget = "/Pictures/Nikon",
            transferBlockSize = "4MB",
            wifiPollIntervalMs = 5000,
        )
        vm.updateSettings { custom }

        // 跨 ViewModel 重建后仍相等(全部字段往返)
        assertEquals(custom, freshVm().settings.value)

        // 直接核对 SharedPreferences 落盘
        val prefs = ApplicationProvider.getApplicationContext<NikonApplication>()
            .getSharedPreferences("nikon_settings", Context.MODE_PRIVATE)
        assertEquals(false, prefs.getBoolean("autoTransfer", true))
        assertEquals(7, prefs.getInt("concurrentJobs", 3))
        assertEquals(false, prefs.getBoolean("formatJpg", true))
        assertEquals(true, prefs.getBoolean("formatMov", false))
        assertEquals(false, prefs.getBoolean("resumeTransfer", true))
        assertEquals(true, prefs.getBoolean("ftpAutoUpload", false))
        assertEquals("10.0.0.5", prefs.getString("ftpHost", ""))
        assertEquals(990, prefs.getInt("ftpPort", 0))
        assertEquals("/Pictures/Nikon", prefs.getString("storageTarget", ""))
        assertEquals("4MB", prefs.getString("transferBlockSize", ""))
        assertEquals(5000, prefs.getInt("wifiPollIntervalMs", 0))
    }

    @Test
    fun clearError_resetsErrorState() {
        val (vm, _) = createFakeVm(handle = 0L)
        vm.scan() // handle=0 → 上报错误
        assertTrue(vm.error.value?.isNotEmpty() == true)
        vm.clearError()
        assertNull(vm.error.value)
    }
}
