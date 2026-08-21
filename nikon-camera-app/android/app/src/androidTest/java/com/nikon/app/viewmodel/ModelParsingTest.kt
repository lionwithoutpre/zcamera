package com.nikon.app.viewmodel
import com.nikon.model.CameraInfo
import com.nikon.model.CameraFile

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import org.junit.runner.RunWith
import androidx.test.ext.junit.runners.AndroidJUnit4

/**
 * 数据模型解析功能测试:CameraInfo.fromRaw / CameraFile.fromRaw 的契约与边界,
 * 以及派生 getter(format / sizeLabel / dateGroup / transportLabel)。纯逻辑,不依赖相机/Android。
 */
@RunWith(AndroidJUnit4::class)
class ModelParsingTest {

    // ── CameraInfo.fromRaw ───────────────────────────────

    @Test
    fun cameraInfo_fromRaw_valid() {
        val c = CameraInfo.fromRaw("c1|Z 9|SN001|0|90|200.5|256.0")
        assertEquals("c1", c!!.id)
        assertEquals("Z 9", c.model)
        assertEquals("SN001", c.serial)
        assertEquals(0, c.transport)
        assertEquals(90, c.batteryLevel)
        assertEquals(200.5, c.storageFreeGb, 0.001)
        assertEquals(256.0, c.storageTotalGb, 0.001)
    }

    @Test
    fun cameraInfo_fromRaw_tooFewParts_returnsNull() {
        assertNull(CameraInfo.fromRaw("a|b|c|d|e|f")) // 6 < 7
    }

    @Test
    fun cameraInfo_fromRaw_nonNumeric_returnsNull() {
        assertNull(CameraInfo.fromRaw("c1|Z9|SN|0|abc|1|2")) // batteryLevel 非数字
    }

    @Test
    fun cameraInfo_transportLabel() {
        assertEquals("USB", CameraInfo.fromRaw("c1|Z9|SN|0|90|1|2")!!.transportLabel)
        assertEquals("Wi-Fi", CameraInfo.fromRaw("c1|Z9|SN|1|90|1|2")!!.transportLabel)
    }

    // ── CameraFile.fromRaw ───────────────────────────────

    @Test
    fun cameraFile_fromRaw_valid_nef() {
        val f = CameraFile.fromRaw("1|DSC001.NEF|1048576|2026-07-16 10:00|1|0|6000|4000|0")
        assertEquals(1L, f!!.objectHandle)
        assertEquals("DSC001.NEF", f.filename)
        assertEquals(1048576L, f.size)
        assertEquals("2026-07-16 10:00", f.datetime)
        assertEquals(true, f.isRaw)
        assertEquals(false, f.isJpeg)
        assertEquals(6000, f.width)
        assertEquals(4000, f.height)
        assertEquals(0, f.storageId)
        assertEquals("NEF", f.format)
        assertEquals("1.0 MB", f.sizeLabel)
        assertEquals("2026-07-16", f.dateGroup)
    }

    @Test
    fun cameraFile_fromRaw_valid_jpeg() {
        val f = CameraFile.fromRaw("2|DSC002.JPG|524288|2026-07-16 11:00|0|1|4000|3000|1")
        assertEquals(false, f!!.isRaw)
        assertEquals(true, f.isJpeg)
        assertEquals("JPG", f.format)
        assertEquals("512.0 KB", f.sizeLabel)
        assertEquals(1, f.storageId)
    }

    @Test
    fun cameraFile_fromRaw_otherFormat_usesExtension() {
        val f = CameraFile.fromRaw("3|VID.MOV|2097152|2026-07-16 12:00|0|0|1920|1080|2")
        assertEquals("MOV", f!!.format)
    }

    @Test
    fun cameraFile_fromRaw_sizeLabel_small() {
        assertEquals("500 B", CameraFile.fromRaw("3|a.NEF|500|d|0|0|1|1|0")!!.sizeLabel)
        assertEquals("1.5 KB", CameraFile.fromRaw("3|a.NEF|1536|d|0|0|1|1|0")!!.sizeLabel)
    }

    @Test
    fun cameraFile_fromRaw_tooFewParts_returnsNull() {
        assertNull(CameraFile.fromRaw("1|DSC|1048576|2026-07-16 10:00|1|0|6000|4000")) // 8 < 9
    }

    @Test
    fun cameraFile_fromRaw_nonNumeric_returnsNull() {
        assertNull(CameraFile.fromRaw("x|DSC|1048576|d|1|0|6000|4000|0")) // objectHandle 非数字
    }
}
