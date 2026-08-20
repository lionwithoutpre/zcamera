package com.nikon.app.viewmodel

/**
 * Models.kt — ViewModel 层共享数据类
 *
 * 从 CameraViewModel.kt 拆出,保持原包名 `com.nikon.app.viewmodel`,
 * 所有既有 import / 全限定引用无需改动。
 */

data class CameraInfo(
    val id: String,
    val model: String,
    val serial: String,
    val transport: Int,       // 0=USB 1=Wi-Fi
    val batteryLevel: Int,
    val storageFreeGb: Double,
    val storageTotalGb: Double,
) {
    val transportLabel: String get() = if (transport == 0) "USB" else "Wi-Fi"

    companion object {
        fun fromRaw(raw: String): CameraInfo? {
            val parts = raw.split("|")
            if (parts.size < 7) return null
            return runCatching {
                CameraInfo(
                    id             = parts[0],
                    model          = parts[1],
                    serial         = parts[2],
                    transport      = parts[3].toInt(),
                    batteryLevel   = parts[4].toInt(),
                    storageFreeGb  = parts[5].toDouble(),
                    storageTotalGb = parts[6].toDouble(),
                )
            }.getOrNull()
        }
    }
}

/**
 * 传输任务。
 *
 * id 设计(v3 重构):
 *  - [id] 为稳定的 UI 侧任务 id,单调递增、创建后永不变化 —— 续传/重传
 *    原地更新同一任务,Compose key 不抖动,UI 也不会出现中间重复项。
 *  - [nativeJobId] 是 nativeStartTransfer 成功后返回的 native 任务 id,
 *    仅用于匹配 JNI 进度回调;未启动/启动失败时为 null。
 */
data class TransferJob(
    val id: Int,
    val nativeJobId: Int? = null,
    val objectHandle: Long,
    val destPath: String,
    val filename: String,
    val format: String = "NEF",    // UI 推断或从文件名解析
    val sizeMb: Double = 0.0,
    val status: TransferStatus,
    val percent: Int,
    val speedMbps: Double,
    val note: String,
)

enum class TransferStatus {
    WAITING, ACTIVE, PAUSED, DONE, FAILED, CANCELLED;

    /** 是否终态(不会再回到传输中)。用于幂等保护: 终态任务忽略重复回调。 */
    fun isTerminal(): Boolean = this == DONE || this == FAILED || this == CANCELLED
}

/** 相机拍摄参数(v2) */
data class CameraProperties(
    val shutterSpeed: String = "--",
    val aperture: String = "--",
    val iso: String = "--",
    val ev: String = "--",
    val focusMode: String = "--",
    val whiteBalance: String = "--",
    val imageQuality: String = "--",
)

/**
 * Picture Control 参数 — 与 C 层 `struct PictureControl` 一一对应 (PTP 0x90CC/0x90CD)。
 *
 * 内存布局固定为 9 个 1 字节字段 (无 padding), 顺序:
 *   hue, saturation, contrast, clarity, sharpening, brightness, wb_ab, wb_gm, color_space
 */
data class PictureControl(
    val hue: Int = 0,          /**< 色相     -3 ~ +3 */
    val saturation: Int = 0,   /**< 饱和度   -3 ~ +3 */
    val contrast: Int = 0,     /**< 对比度   -3 ~ +3 */
    val clarity: Int = 0,      /**< 清晰度   -3 ~ +3 */
    val sharpening: Int = 0,   /**< 锐化     0 ~ 9 */
    val brightness: Int = 0,   /**< 亮度     -1 ~ +1 */
    val wbAb: Int = 0,         /**< WB A-B 轴 -6 ~ +6 (琥珀→蓝) */
    val wbGm: Int = 0,         /**< WB G-M 轴 -6 ~ +6 (绿→品红) */
    val colorSpace: Int = 0,   /**< 0=sRGB 1=AdobeRGB */
) {
    /** 序列化为 9 字节, 顺序与 C 结构体一致。 */
    fun toBytes(): ByteArray = byteArrayOf(
        hue.toByte(),
        saturation.toByte(),
        contrast.toByte(),
        clarity.toByte(),
        sharpening.toByte(),
        brightness.toByte(),
        wbAb.toByte(),
        wbGm.toByte(),
        colorSpace.toByte(),
    )

    companion object {
        /** 从 native 返回的 9 字节解码; 长度不足返回 null。 */
        fun fromBytes(b: ByteArray): PictureControl? {
            if (b.size < 9) return null
            return PictureControl(
                hue = b[0].toInt(),
                saturation = b[1].toInt(),
                contrast = b[2].toInt(),
                clarity = b[3].toInt(),
                sharpening = b[4].toInt(),
                brightness = b[5].toInt(),
                wbAb = b[6].toInt(),
                wbGm = b[7].toInt(),
                colorSpace = b[8].toInt() and 0xFF,
            )
        }
    }
}

/** App 设置(跨页面共享,由 SettingsRepository 持久化) */
data class AppSettings(
    val autoTransfer: Boolean = true,
    val concurrentJobs: Int = 3,
    val formatJpg: Boolean = true,
    val formatNef: Boolean = true,
    val formatMov: Boolean = false,
    val autoChunk: Boolean = true,
    val speedAdaptive: Boolean = true,
    val smallFileFirst: Boolean = true,
    val resumeTransfer: Boolean = true,
    val ftpsEncryption: Boolean = true,
    val ftpAutoUpload: Boolean = false,
    val preferUsb: Boolean = true,
    val notifyComplete: Boolean = true,
    val notifyFail: Boolean = true,
    val ftpHost: String = "192.168.1.100",
    val ftpPort: Int = 21,
    val ftpUsername: String = "",
    val ftpPassword: String = "",
    val ftpRemotePath: String = "/",
    val storageTarget: String = "/DCIM/NikonConnect",
    val transferBlockSize: String = "自动",
    val wifiPollIntervalMs: Int = 1000,
)

/** 相机文件(v2,对应 C 层 FileInfo) */
data class CameraFile(
    val objectHandle: Long,
    val filename: String,
    val size: Long,
    val datetime: String,
    val isRaw: Boolean,
    val isJpeg: Boolean,
    val width: Int,
    val height: Int,
    val storageId: Int,
) {
    /** 文件格式标签:NEF / JPG / 其他 */
    val format: String get() = when {
        isRaw -> "NEF"
        isJpeg -> "JPG"
        else -> filename.substringAfterLast('.', "").uppercase()
    }

    /** 人类可读大小 */
    val sizeLabel: String get() = when {
        size >= 1024 * 1024 -> "%.1f MB".format(size / (1024.0 * 1024.0))
        size >= 1024 -> "%.1f KB".format(size / 1024.0)
        else -> "$size B"
    }

    /** 日期分组标签(取 datetime 前 10 位 "YYYY-MM-DD") */
    val dateGroup: String get() = datetime.take(10)

    companion object {
        /** 解析 JNI 返回的 "handle|name|size|datetime|is_raw|is_jpeg|w|h|storage" */
        fun fromRaw(raw: String): CameraFile? {
            val p = raw.split("|")
            if (p.size < 9) return null
            return runCatching {
                CameraFile(
                    objectHandle = p[0].toLong(),
                    filename     = p[1],
                    size         = p[2].toLong(),
                    datetime     = p[3],
                    isRaw        = p[4] == "1",
                    isJpeg       = p[5] == "1",
                    width        = p[6].toInt(),
                    height       = p[7].toInt(),
                    storageId    = p[8].toInt(),
                )
            }.getOrNull()
        }
    }
}
