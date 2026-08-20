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
class StorageManager(
    private val context: Context,
    /** 保存到系统相册的子目录名 (DCIM 下)。默认 NikonConnect,可经 settings.storageTarget 覆盖。 */
    private val albumDir: String = "NikonConnect",
) {

    companion object {
        /** 默认相册子目录 */
        const val DEFAULT_ALBUM_DIR = "NikonConnect"
    }

    /**
     * 将文件保存到系统相册。
     *
     * @param sourcePath 源文件路径 (App 私有目录中的已传输文件)
     * @param filename   目标文件名 (e.g. "DSC_0001.NEF")
     * @param albumOverride 相册子目录 (DCIM 下);为空则用构造默认值, 可覆盖 settings.storageTarget
     * @return 插入的 MediaStore Uri;失败返回 null
     */
    fun saveToGallery(sourcePath: String, filename: String, albumOverride: String? = null): Uri? {
        val sourceFile = File(sourcePath)
        if (!sourceFile.exists()) return null
        val targetAlbum = albumOverride?.trim()?.takeIf { it.isNotEmpty() } ?: albumDir

        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            saveWithMediaStore(sourceFile, filename, targetAlbum)
        } else {
            saveWithLegacy(sourceFile, filename, targetAlbum)
        }
    }

    /**
     * Android 10+ (API 29): 通过 MediaStore ContentResolver 插入。
     * 若目标相册中已存在同名文件(DISPLAY_NAME 冲突), 追加序号避免覆盖/重复。
     */
    private fun saveWithMediaStore(sourceFile: File, filename: String, targetAlbum: String): Uri? {
        val mimeType = getMimeType(filename)
        val isVideo = mimeType.startsWith("video/")

        val collection = if (isVideo) {
            MediaStore.Video.Media.EXTERNAL_CONTENT_URI
        } else {
            MediaStore.Images.Media.EXTERNAL_CONTENT_URI
        }

        val relativePath = "${Environment.DIRECTORY_DCIM}/$targetAlbum"
        val resolver = context.contentResolver

        // 解决同名冲突: 相同 (RELATIVE_PATH, DISPLAY_NAME) 已存在时追加 _1/_2/…
        val finalName = resolveUniqueName(resolver, collection, relativePath, filename)
        val values = ContentValues().apply {
            put(MediaStore.MediaColumns.DISPLAY_NAME, finalName)
            put(MediaStore.MediaColumns.MIME_TYPE, mimeType)
            put(MediaStore.MediaColumns.RELATIVE_PATH, relativePath)
            put(MediaStore.MediaColumns.IS_PENDING, 1)
        }

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
     * 已存在同名文件时追加序号,避免覆盖用户已有文件。
     */
    @Suppress("DEPRECATION")
    private fun saveWithLegacy(sourceFile: File, filename: String, targetAlbum: String): Uri? {
        val dcimDir = Environment.getExternalStoragePublicDirectory(
            Environment.DIRECTORY_DCIM
        )
        val albumDirPath = File(dcimDir, targetAlbum)
        if (!albumDirPath.exists() && !albumDirPath.mkdirs()) return null

        val destFile = resolveUniqueLegacyName(albumDirPath, filename)
        return try {
            sourceFile.copyTo(destFile, overwrite = false)
            // 通知 MediaScanner 扫描新文件
            val values = ContentValues().apply {
                put(MediaStore.MediaColumns.DATA, destFile.absolutePath)
                put(MediaStore.MediaColumns.DISPLAY_NAME, destFile.name)
                put(MediaStore.MediaColumns.MIME_TYPE, getMimeType(destFile.name))
            }
            val isVideo = getMimeType(destFile.name).startsWith("video/")
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

    /** MediaStore: 在目标相册中检测同名并返回唯一文件名 (DISPLAY_NAME 冲突时加 _N)。 */
    private fun resolveUniqueName(
        resolver: android.content.ContentResolver,
        collection: Uri,
        relativePath: String,
        filename: String,
    ): String {
        if (!hasMediaStoreName(resolver, collection, relativePath, filename)) return filename
        val base = filename.substringBeforeLast('.', filename)
        val ext = filename.substringAfterLast('.', "").let { if (it == filename) "" else ".$it" }
        for (i in 1..999) {
            val candidate = "${base}_$i$ext"
            if (!hasMediaStoreName(resolver, collection, relativePath, candidate)) return candidate
        }
        return "${base}_${System.currentTimeMillis()}$ext"
    }

    private fun hasMediaStoreName(
        resolver: android.content.ContentResolver,
        collection: Uri,
        relativePath: String,
        filename: String,
    ): Boolean {
        val selection = "${MediaStore.MediaColumns.RELATIVE_PATH}=? AND ${MediaStore.MediaColumns.DISPLAY_NAME}=?"
        val args = arrayOf(relativePath, filename)
        return try {
            resolver.query(collection, arrayOf(MediaStore.MediaColumns._ID), selection, args, null)
                ?.use { it.moveToFirst() } ?: false
        } catch (_: Exception) {
            // 查询失败时保守假设无冲突, 让后续 insert 尝试 (MediaStore 对冲突有兜底策略)
            false
        }
    }

    /** Legacy: 若目录中已存在同名文件, 返回追加序号的路径。 */
    private fun resolveUniqueLegacyName(dir: File, filename: String): File {
        if (!File(dir, filename).exists()) return File(dir, filename)
        val base = filename.substringBeforeLast('.', filename)
        val ext = filename.substringAfterLast('.', "").let { if (it == filename) "" else ".$it" }
        for (i in 1..999) {
            val candidate = File(dir, "${base}_$i$ext")
            if (!candidate.exists()) return candidate
        }
        return File(dir, "${base}_${System.currentTimeMillis()}$ext")
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
