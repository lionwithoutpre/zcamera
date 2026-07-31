package com.nikon.app.storage

import android.content.ContentValues
import android.content.Context
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.provider.MediaStore
import android.webkit.MimeTypeMap
import java.io.File

/**
 * StorageManager — 分区存储适配
 *
 * 将传输完成的照片/视频保存到系统相册 (DCIM/NikonConnect)。
 * Android 10+ 使用 MediaStore API,无需 WRITE_EXTERNAL_STORAGE 权限。
 * Android 9 及以下使用传统文件路径写入。
 */
class StorageManager(private val context: Context) {

    companion object {
        private const val ALBUM_DIR = "NikonConnect"
    }

    /**
     * 将文件保存到系统相册。
     *
     * @param sourcePath 源文件路径 (App 私有目录中的已传输文件)
     * @param filename   目标文件名 (e.g. "DSC_0001.NEF")
     * @return 插入的 MediaStore Uri;失败返回 null
     */
    fun saveToGallery(sourcePath: String, filename: String): Uri? {
        val sourceFile = File(sourcePath)
        if (!sourceFile.exists()) return null

        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            saveWithMediaStore(sourceFile, filename)
        } else {
            saveWithLegacy(sourceFile, filename)
        }
    }

    /**
     * Android 10+ (API 29): 通过 MediaStore ContentResolver 插入。
     */
    private fun saveWithMediaStore(sourceFile: File, filename: String): Uri? {
        val mimeType = getMimeType(filename)
        val isVideo = mimeType.startsWith("video/")

        val collection = if (isVideo) {
            MediaStore.Video.Media.EXTERNAL_CONTENT_URI
        } else {
            MediaStore.Images.Media.EXTERNAL_CONTENT_URI
        }

        val relativePath = if (isVideo) {
            "${Environment.DIRECTORY_DCIM}/$ALBUM_DIR"
        } else {
            "${Environment.DIRECTORY_DCIM}/$ALBUM_DIR"
        }

        val values = ContentValues().apply {
            put(MediaStore.MediaColumns.DISPLAY_NAME, filename)
            put(MediaStore.MediaColumns.MIME_TYPE, mimeType)
            put(MediaStore.MediaColumns.RELATIVE_PATH, relativePath)
            put(MediaStore.MediaColumns.IS_PENDING, 1)
        }

        val resolver = context.contentResolver
        val uri = resolver.insert(collection, values) ?: return null

        return try {
            resolver.openOutputStream(uri)?.use { output ->
                sourceFile.inputStream().use { input ->
                    input.copyTo(output, bufferSize = 8192)
                }
            } ?: run {
                resolver.delete(uri, null, null)
                return null
            }

            // 标记写入完成
            values.clear()
            values.put(MediaStore.MediaColumns.IS_PENDING, 0)
            resolver.update(uri, values, null, null)
            uri
        } catch (e: Exception) {
            // 写入失败,清理残留记录
            try { resolver.delete(uri, null, null) } catch (_: Exception) {}
            null
        }
    }

    /**
     * Android 9 及以下: 直接写入公共 DCIM 目录。
     */
    @Suppress("DEPRECATION")
    private fun saveWithLegacy(sourceFile: File, filename: String): Uri? {
        val dcimDir = Environment.getExternalStoragePublicDirectory(
            Environment.DIRECTORY_DCIM
        )
        val albumDir = File(dcimDir, ALBUM_DIR)
        if (!albumDir.exists() && !albumDir.mkdirs()) return null

        val destFile = File(albumDir, filename)
        return try {
            sourceFile.copyTo(destFile, overwrite = true)
            // 通知 MediaScanner 扫描新文件
            val values = ContentValues().apply {
                put(MediaStore.MediaColumns.DATA, destFile.absolutePath)
                put(MediaStore.MediaColumns.DISPLAY_NAME, filename)
                put(MediaStore.MediaColumns.MIME_TYPE, getMimeType(filename))
            }
            val isVideo = getMimeType(filename).startsWith("video/")
            val collection = if (isVideo) {
                MediaStore.Video.Media.EXTERNAL_CONTENT_URI
            } else {
                MediaStore.Images.Media.EXTERNAL_CONTENT_URI
            }
            context.contentResolver.insert(collection, values)
        } catch (e: Exception) {
            null
        }
    }

    /**
     * 批量保存:将目录下所有已传输文件保存到相册。
     * @return 成功保存的文件数
     */
    fun saveAllToGallery(sourceDir: String): Int {
        val dir = File(sourceDir)
        if (!dir.isDirectory) return 0
        var count = 0
        dir.listFiles()?.forEach { file ->
            if (file.isFile && isMediaFile(file.name)) {
                if (saveToGallery(file.absolutePath, file.name) != null) {
                    count++
                }
            }
        }
        return count
    }

    /**
     * 根据文件扩展名推断 MIME 类型。
     */
    private fun getMimeType(filename: String): String {
        val ext = filename.substringAfterLast('.', "").lowercase()
        return MimeTypeMap.getSingleton().getMimeTypeFromExtension(ext)
            ?: when (ext) {
                "nef", "nrw" -> "image/x-nikon-nef"
                "jpg", "jpeg" -> "image/jpeg"
                "png" -> "image/png"
                "mp4" -> "video/mp4"
                "mov" -> "video/quicktime"
                else -> "application/octet-stream"
            }
    }

    /**
     * 判断是否为可保存到相册的媒体文件。
     */
    private fun isMediaFile(filename: String): Boolean {
        val ext = filename.substringAfterLast('.', "").lowercase()
        return ext in setOf("jpg", "jpeg", "png", "nef", "nrw", "mp4", "mov", "tiff", "tif")
    }
}
