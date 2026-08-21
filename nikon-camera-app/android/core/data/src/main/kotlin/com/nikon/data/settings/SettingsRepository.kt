package com.nikon.data.settings

import android.content.Context
import android.content.SharedPreferences
import android.util.Log
import androidx.security.crypto.EncryptedSharedPreferences
import androidx.security.crypto.MasterKey
import com.nikon.model.AppSettings

/**
 * SettingsRepository — App 设置的唯一持久化入口
 *
 * 重构前 load/save 逻辑内联在 CameraViewModel 里,且默认值在
 * data class / load / save 三处重复;现在:
 *  - 默认值唯一来源是 [AppSettings] 的构造默认值(load 时以 d 兜底)
 *  - 键名集中在 [K],ViewModel 与 CameraService 共用同一仓库
 */
class SettingsRepository(context: Context) {

    private object K {
        const val AUTO_TRANSFER      = "autoTransfer"
        const val CONCURRENT_JOBS    = "concurrentJobs"
        const val FORMAT_JPG         = "formatJpg"
        const val FORMAT_NEF         = "formatNef"
        const val FORMAT_MOV         = "formatMov"
        const val AUTO_CHUNK         = "autoChunk"
        const val SPEED_ADAPTIVE     = "speedAdaptive"
        const val SMALL_FILE_FIRST   = "smallFileFirst"
        const val RESUME_TRANSFER    = "resumeTransfer"
        const val FTPS_ENCRYPTION    = "ftpsEncryption"
        const val FTP_AUTO_UPLOAD    = "ftpAutoUpload"
        const val PREFER_USB         = "preferUsb"
        const val NOTIFY_COMPLETE    = "notifyComplete"
        const val NOTIFY_FAIL        = "notifyFail"
        const val FTP_HOST           = "ftpHost"
        const val FTP_PORT           = "ftpPort"
        const val FTP_USERNAME       = "ftpUsername"
        const val FTP_PASSWORD       = "ftpPassword"
        const val FTP_REMOTE_PATH    = "ftpRemotePath"
        const val STORAGE_TARGET     = "storageTarget"
        const val TRANSFER_BLOCK_SIZE = "transferBlockSize"
        const val WIFI_POLL_INTERVAL_MS = "wifiPollIntervalMs"
        // 上次成功连接的 WiFi 相机端点(CameraService 断连重连用)
        const val LAST_WIFI_IP       = "lastWifiIp"
        const val LAST_WIFI_PORT     = "lastWifiPort"
    }

    companion object {
        private const val PREFS_NAME = "nikon_settings"
        /** 敏感字段(密码)专用加密存储文件名 */
        private const val SECRET_PREFS_NAME = "nikon_secrets"
        private const val TAG = "SettingsRepository"
        const val DEFAULT_WIFI_PORT = 15740
    }

    private val prefs: SharedPreferences =
        context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

    /**
     * 加密存储(Keystore 支撑)。初始化失败(无 Keystore / 纯 JVM 测试环境)
     * 时为 null, 密码回退明文存储 — 可用性优先, 不因加密失败阻断功能。
     */
    private val secretPrefs: SharedPreferences? by lazy {
        try {
            val masterKey = MasterKey.Builder(context)
                .setKeyScheme(MasterKey.KeyScheme.AES256_GCM)
                .build()
            EncryptedSharedPreferences.create(
                context, SECRET_PREFS_NAME, masterKey,
                EncryptedSharedPreferences.PrefKeyEncryptionScheme.AES256_SIV,
                EncryptedSharedPreferences.PrefValueEncryptionScheme.AES256_GCM,
            )
        } catch (t: Throwable) {
            Log.w(TAG, "EncryptedSharedPreferences 不可用, 敏感设置回退明文", t)
            null
        }
    }

    /**
     * 加密存储是否可用。
     * 不可用时 FTP 密码会明文落盘 —— UI 层可据此显示安全警告条。
     */
    fun isSecretStorageAvailable(): Boolean = secretPrefs != null

    /** 从持久化读取设置;缺省字段回退到 [AppSettings] 构造默认值 */
    fun load(): AppSettings {
        val d = AppSettings()
        return AppSettings(
            autoTransfer       = prefs.getBoolean(K.AUTO_TRANSFER, d.autoTransfer),
            concurrentJobs     = prefs.getInt(K.CONCURRENT_JOBS, d.concurrentJobs),
            formatJpg          = prefs.getBoolean(K.FORMAT_JPG, d.formatJpg),
            formatNef          = prefs.getBoolean(K.FORMAT_NEF, d.formatNef),
            formatMov          = prefs.getBoolean(K.FORMAT_MOV, d.formatMov),
            autoChunk          = prefs.getBoolean(K.AUTO_CHUNK, d.autoChunk),
            speedAdaptive      = prefs.getBoolean(K.SPEED_ADAPTIVE, d.speedAdaptive),
            smallFileFirst     = prefs.getBoolean(K.SMALL_FILE_FIRST, d.smallFileFirst),
            resumeTransfer     = prefs.getBoolean(K.RESUME_TRANSFER, d.resumeTransfer),
            ftpsEncryption     = prefs.getBoolean(K.FTPS_ENCRYPTION, d.ftpsEncryption),
            ftpAutoUpload      = prefs.getBoolean(K.FTP_AUTO_UPLOAD, d.ftpAutoUpload),
            preferUsb          = prefs.getBoolean(K.PREFER_USB, d.preferUsb),
            notifyComplete     = prefs.getBoolean(K.NOTIFY_COMPLETE, d.notifyComplete),
            notifyFail         = prefs.getBoolean(K.NOTIFY_FAIL, d.notifyFail),
            ftpHost            = prefs.getString(K.FTP_HOST, d.ftpHost) ?: d.ftpHost,
            ftpPort            = prefs.getInt(K.FTP_PORT, d.ftpPort),
            ftpUsername        = prefs.getString(K.FTP_USERNAME, d.ftpUsername) ?: d.ftpUsername,
            // 密码优先从加密存储读; 兼容旧版明文(读后由 save 迁移)
            ftpPassword        = secretPrefs?.getString(K.FTP_PASSWORD, null)
                ?: prefs.getString(K.FTP_PASSWORD, d.ftpPassword)
                ?: d.ftpPassword,
            ftpRemotePath      = prefs.getString(K.FTP_REMOTE_PATH, d.ftpRemotePath) ?: d.ftpRemotePath,
            storageTarget      = prefs.getString(K.STORAGE_TARGET, d.storageTarget) ?: d.storageTarget,
            transferBlockSize  = prefs.getString(K.TRANSFER_BLOCK_SIZE, d.transferBlockSize) ?: d.transferBlockSize,
            wifiPollIntervalMs = prefs.getInt(K.WIFI_POLL_INTERVAL_MS, d.wifiPollIntervalMs),
        )
    }

    fun save(s: AppSettings) {
        prefs.edit().apply {
            putBoolean(K.AUTO_TRANSFER, s.autoTransfer)
            putInt(K.CONCURRENT_JOBS, s.concurrentJobs)
            putBoolean(K.FORMAT_JPG, s.formatJpg)
            putBoolean(K.FORMAT_NEF, s.formatNef)
            putBoolean(K.FORMAT_MOV, s.formatMov)
            putBoolean(K.AUTO_CHUNK, s.autoChunk)
            putBoolean(K.SPEED_ADAPTIVE, s.speedAdaptive)
            putBoolean(K.SMALL_FILE_FIRST, s.smallFileFirst)
            putBoolean(K.RESUME_TRANSFER, s.resumeTransfer)
            putBoolean(K.FTPS_ENCRYPTION, s.ftpsEncryption)
            putBoolean(K.FTP_AUTO_UPLOAD, s.ftpAutoUpload)
            putBoolean(K.PREFER_USB, s.preferUsb)
            putBoolean(K.NOTIFY_COMPLETE, s.notifyComplete)
            putBoolean(K.NOTIFY_FAIL, s.notifyFail)
            putString(K.FTP_HOST, s.ftpHost)
            putInt(K.FTP_PORT, s.ftpPort)
            putString(K.FTP_USERNAME, s.ftpUsername)
            // 密码只写加密存储; 同时清除旧版明文残留(完成迁移)
            remove(K.FTP_PASSWORD)
            putString(K.FTP_REMOTE_PATH, s.ftpRemotePath)
            putString(K.STORAGE_TARGET, s.storageTarget)
            putString(K.TRANSFER_BLOCK_SIZE, s.transferBlockSize)
            putInt(K.WIFI_POLL_INTERVAL_MS, s.wifiPollIntervalMs)
        }.apply()
        // 密码写入加密存储; 不可用时回退明文(已在 save 主体中 remove, 这里补写)
        val sp = secretPrefs
        if (sp != null) {
            sp.edit().putString(K.FTP_PASSWORD, s.ftpPassword).apply()
        } else {
            prefs.edit().putString(K.FTP_PASSWORD, s.ftpPassword).apply()
        }
    }

    // ─── 上次 WiFi 连接端点(CameraService 重连用)─────────────

    fun saveLastWifiEndpoint(ip: String, port: Int) {
        prefs.edit()
            .putString(K.LAST_WIFI_IP, ip)
            .putInt(K.LAST_WIFI_PORT, port)
            .apply()
    }

    fun lastWifiIp(): String? = prefs.getString(K.LAST_WIFI_IP, null)

    fun lastWifiPort(): Int = prefs.getInt(K.LAST_WIFI_PORT, DEFAULT_WIFI_PORT)
}
