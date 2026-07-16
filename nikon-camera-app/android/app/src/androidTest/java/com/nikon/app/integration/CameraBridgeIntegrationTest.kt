package com.nikon.app.integration

import android.app.Application
import androidx.test.core.app.ApplicationProvider
import com.nikon.app.jni.CameraBridge
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.runner.RunWith
import java.io.File

/**
 * 集成测试 1 —— 直接驱动真实 [CameraBridge] native 层。
 *
 * 验证“真协议握手 + 真字节流”这一功能区,这是功能测试(用 FakeCameraApi 替身)无法覆盖的。
 * 无真实相机时, [requireRealCamera] 抛出 AssumptionViolatedException → 用例 SKIP。
 */
@RunWith(AndroidJUnit4::class)
class CameraBridgeIntegrationTest {

    private var session: RealCameraSession? = null

    @Before
    fun setUp() {
        // 无相机 / 无 .so 时这里直接 skip,不会进入用例体
        session = requireRealCamera()
    }

    @After
    fun tearDown() {
        session?.let {
            runCatching { CameraBridge.nativeDisconnect(it.handle) }
            runCatching { CameraBridge.nativeDestroy(it.handle) }
        }
    }

    @Test
    fun scan_findsAtLeastOneCamera() {
        val found = CameraBridge.nativeScan(session!!.handle)
        assertNotNull("scan 不应返回 null", found)
        assertTrue("应扫描到至少一台相机", found!!.isNotEmpty())
        assertTrue("首行应含 '|' 分隔的相机信息", found.first().contains("|"))
    }

    @Test
    fun connect_establishesSession() {
        assertEquals(
            "连接后状态应为 STATUS_CONNECTED",
            CameraBridge.STATUS_CONNECTED,
            CameraBridge.nativeGetStatus(session!!.handle),
        )
    }

    @Test
    fun capture_succeeds() {
        val rc = CameraBridge.nativeCapture(session!!.handle)
        assertEquals("拍摄应返回 CAM_OK", CameraBridge.CAM_OK, rc)
    }

    @Test
    fun listFiles_returnsParseableEntries() {
        val files = CameraBridge.nativeListFiles(session!!.handle, CameraBridge.STORAGE_ALL)
        assertNotNull("listFiles 不应返回 null", files)
        assertTrue("应列举到文件", files!!.isNotEmpty())
        files.forEach { row ->
            val parts = row.split("|")
            assertTrue("每行应至少 9 个字段,实际: $row", parts.size >= 9)
            assertNotNull("object_handle 应可解析为 Long", parts[0].toLongOrNull())
        }
    }

    @Test
    fun getThumbnail_returnsRealJpeg() {
        val files = CameraBridge.nativeListFiles(session!!.handle, CameraBridge.STORAGE_ALL) ?: return
        val firstHandle = files.first().split("|")[0].toLong()
        val thumb = CameraBridge.nativeGetThumbnail(session!!.handle, firstHandle)
        assertTrue(
            "缩略图应为真实 JPEG (FFD8 开头),size=${jpegSize(thumb)}",
            isJpeg(thumb),
        )
    }

    @Test
    fun liveView_frameIsRealJpeg() {
        val rc = CameraBridge.nativeStartLiveView(session!!.handle)
        assertEquals("启动实时取景应返回 CAM_OK", CameraBridge.CAM_OK, rc)
        try {
            val frame = CameraBridge.nativeGetLiveViewFrame(session!!.handle)
            assertTrue("取景帧应为真实 JPEG (FFD8 开头),size=${jpegSize(frame)}", isJpeg(frame))
        } finally {
            CameraBridge.nativeStopLiveView(session!!.handle)
        }
    }

    @Test
    fun transfer_writesRealFileToDisk() {
        val files = CameraBridge.nativeListFiles(session!!.handle, CameraBridge.STORAGE_ALL) ?: return
        val firstHandle = files.first().split("|")[0].toLong()
        val dest = File(
            ApplicationProvider.getApplicationContext<Application>().getExternalFilesDir(null),
            "integration_${firstHandle}.nef",
        )
        val jobId = CameraBridge.nativeStartTransfer(session!!.handle, firstHandle, dest.absolutePath)
        assertTrue("nativeStartTransfer 应返回有效 jobId(>0),实际=$jobId", jobId > 0)

        // 真实传输较慢,等待落地(集成环境可接受)
        var waited = 0
        while (waited < 15000 && !(dest.exists() && dest.length() > 0)) {
            Thread.sleep(500)
            waited += 500
        }
        assertTrue(
            "传输后目标文件应存在且非空: ${dest.absolutePath} (exists=${dest.exists()}, size=${dest.length()})",
            dest.exists() && dest.length() > 0,
        )
    }
}
