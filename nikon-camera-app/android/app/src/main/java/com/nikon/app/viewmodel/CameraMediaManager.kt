package com.nikon.app.viewmodel

import android.app.Application
import com.nikon.app.jni.CameraApi
import com.nikon.app.jni.CameraBridge
import com.nikon.model.CameraFile
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/**
 * CameraMediaManager — 相机媒体域
 *
 * 从 CameraViewModel 拆分(阶段4): 负责相机文件列举 / 缩略图(带 LRU 缓存) /
 * 删除 / 传输目标路径生成。持有 [CameraViewModel] 回调, 错误与状态通过回调同步给门面。
 *
 * 职责边界:
 *  - handle 生命周期归 CameraService, 本类只读 [handleProvider]
 *  - 删除文件后更新 [fileList] 由本类维护
 *  - 发起传输走 [onStartTransfer] 回调(门面 → TransferManager), 避免跨域依赖
 */
internal class CameraMediaManager(
    private val application: Application,
    private val bridge: CameraApi,
    private val handleProvider: () -> Long,
    private val scope: CoroutineScope,
    private val ioDispatcher: CoroutineDispatcher,
    private val onError: (String) -> Unit,
    /** 发起传输(门面转发给 TransferManager): 参数为 (objectHandle, destPath) */
    private val onStartTransfer: (Long, String) -> Unit,
) {
    private val _fileList = MutableStateFlow<List<CameraFile>>(emptyList())
    val fileList: StateFlow<List<CameraFile>> = _fileList.asStateFlow()

    private val _fileListLoading = MutableStateFlow(false)
    val fileListLoading: StateFlow<Boolean> = _fileListLoading.asStateFlow()

    /**
     * 缩略图内存缓存 (objectHandle → JPEG 字节)。
     * 相册网格会为同一文件反复请求缩略图, 缓存避免重复走 PTP GetThumb。
     * 访问顺序 LRU, 容量上限 128 项, 超过时移除最久未用。
     */
    private val thumbnailCache = object : LinkedHashMap<Long, ByteArray>(16, 0.75f, true) {
        override fun removeEldestEntry(
            eldest: MutableMap.MutableEntry<Long, ByteArray>?,
        ): Boolean = size > 128
    }

    /**
     * 拉取相机文件列表。
     * @param storageId 0=全部; 1=CF-A; 2=SD-B
     */
    fun listFiles(storageId: Int = CameraBridge.STORAGE_ALL) {
        val h = handleProvider()
        if (h == 0L) {
            onError("相机服务未就绪,请稍候")
            return
        }
        scope.launch {
            _fileListLoading.value = true
            val raw = withContext(ioDispatcher) {
                bridge.nativeListFiles(h, storageId)
            }
            val parsed = raw?.mapNotNull { CameraFile.fromRaw(it) } ?: emptyList()
            _fileList.value = parsed
            _fileListLoading.value = false
        }
    }

    /** 生成传输目标路径(App 私有目录,绕过 Scoped Storage 限制) */
    fun buildDestPath(filename: String): String {
        val dir = application.getExternalFilesDir(android.os.Environment.DIRECTORY_DCIM)
            ?: application.filesDir
        return "${dir.absolutePath}/NikonConnect/$filename"
    }

    /** 便捷方法:用 app 私有目录作为传输目标 */
    fun startTransferToApp(objectHandle: Long, filename: String) {
        onStartTransfer(objectHandle, buildDestPath(filename))
    }

    /** 获取缩略图 JPEG 字节(挂起,UI 侧用 rememberAsyncImage 或 BitmapFactory 解码)。
     *  命中缓存直接返回, 未命中则走 nativeGetThumbnail 并写缓存。 */
    suspend fun getThumbnail(objectHandle: Long): ByteArray? {
        thumbnailCache[objectHandle]?.let { return it }
        val h = handleProvider()
        if (h == 0L) return null
        val bytes = withContext(ioDispatcher) {
            bridge.nativeGetThumbnail(h, objectHandle)
        } ?: return null
        thumbnailCache[objectHandle] = bytes
        return bytes
    }

    fun deleteFile(objectHandle: Long) {
        val h = handleProvider()
        if (h == 0L) {
            onError("相机服务未就绪,请稍候")
            return
        }
        scope.launch(ioDispatcher) {
            val rc = bridge.nativeDeleteFile(h, objectHandle)
            if (rc == CameraBridge.CAM_OK) {
                _fileList.update { it.filterNot { f -> f.objectHandle == objectHandle } }
            } else {
                onError("删除失败 (错误码: $rc)")
            }
        }
    }
}