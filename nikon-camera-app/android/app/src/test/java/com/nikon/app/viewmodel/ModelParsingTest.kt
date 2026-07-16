package com.nikon.app.viewmodel

import org.junit.Assert.*
import org.junit.Test

/**
 * 纯 JVM 模型解析测试(可立即在本地运行,无需设备或 Robolectric)。
 *
 * CameraInfo.fromRaw / CameraFile.fromRaw 的解析错误会直接表现为 UI 上的
 * 乱码、空白或崩溃,因此这里覆盖常见 / 边界输入。
 */
class ModelParsingTest {

    @Test
    fun cameraInfo_fromRaw_parsesAllFields() {
        val info = CameraInfo.fromRaw("cam1|Nikon Z9|SN123|0|80|32.0|64.0")!!
        assertEquals("cam1", info.id)
        assertEquals("Nikon Z9", info.model)
        assertEquals("SN123", info.serial)
        assertEquals(0, info.transport)
        assertEquals("USB", info.transportLabel)
        assertEquals(80, info.batteryLevel)
        assertEquals(32.0, info.storageFreeGb, 0.001)
        assertEquals(64.0, info.storageTotalGb, 0.001)
    }

    @Test
    fun cameraInfo_fromRaw_tooFewParts_returnsNull() {
        assertNull(CameraInfo.fromRaw("a|b|c"))
    }

    @Test
    fun cameraInfo_fromRaw_malformedNumbers_returnsNull() {
        // transport/battery 不可解析 → runCatching 返回 null
        assertNull(CameraInfo.fromRaw("cam1|Z9|SN|notInt|80|32.0|64.0"))
    }

    @Test
    fun cameraFile_fromRaw_nef_parsesAndDerives() {
        val f = CameraFile.fromRaw("1|DSC_0001.NEF|1048576|2026-06-23T10:00:00|1|0|6000|4000|1")!!
        assertEquals(1L, f.objectHandle)
        assertEquals("DSC_0001.NEF", f.filename)
        assertEquals("NEF", f.format)
        assertTrue(f.isRaw)
        assertFalse(f.isJpeg)
        assertEquals("1.0 MB", f.sizeLabel)
        assertEquals("2026-06-23", f.dateGroup)
        assertEquals(6000, f.width)
        assertEquals(4000, f.height)
    }

    @Test
    fun cameraFile_fromRaw_jpg_parsesAndDerives() {
        val f = CameraFile.fromRaw("2|DSC_0002.JPG|524288|2026-06-23T11:00:00|0|1|4000|3000|2")!!
        assertEquals("JPG", f.format)
        assertTrue(f.isJpeg)
        assertEquals("512.0 KB", f.sizeLabel)
    }

    @Test
    fun cameraFile_fromRaw_tooFewParts_returnsNull() {
        assertNull(CameraFile.fromRaw("1|only|few|parts"))
    }

    @Test
    fun appSettings_defaults() {
        val s = AppSettings()
        assertTrue(s.autoTransfer)
        assertEquals(3, s.concurrentJobs)
        assertTrue(s.formatJpg)
        assertTrue(s.formatNef)
        assertFalse(s.formatMov)
        assertEquals("192.168.1.100", s.ftpHost)
        assertEquals(21, s.ftpPort)
        assertTrue(s.preferUsb)
    }

    @Test
    fun settingsCopy_updatesSingleField() {
        val s = AppSettings()
        val updated = s.copy(autoTransfer = false, concurrentJobs = 5)
        assertFalse(updated.autoTransfer)
        assertEquals(5, updated.concurrentJobs)
        // 其余字段保持不变
        assertTrue(updated.formatJpg)
        assertEquals("192.168.1.100", updated.ftpHost)
    }
}
