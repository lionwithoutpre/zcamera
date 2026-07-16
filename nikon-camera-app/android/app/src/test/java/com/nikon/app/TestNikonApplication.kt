package com.nikon.app

import android.app.Application
import android.content.SharedPreferences
import java.io.File
import java.util.concurrent.ConcurrentHashMap

/**
 * 单元测试用的轻量 Application。
 *
 * 不使用 Robolectric —— 直接继承 NikonApplication 并覆盖两个需要 Context 的方法:
 * - getSharedPreferences → 进程内内存实现, 避免依赖 Android 框架
 * - getExternalFilesDir → 临时目录, 供 buildDestPath 验证
 *
 * 注意: 不调用 super.onCreate(), 因此不会触发 CameraBridge 的 loadLibrary
 * (即使触发也会被 CameraBridge.init 的 try-catch 兜住)。
 */
open class TestNikonApplication : NikonApplication() {

    private val prefs = InMemorySharedPreferences()
    private val externalDir =
        File(System.getProperty("java.io.tmpdir"), "nikon_test_files").apply { mkdirs() }

    override fun getSharedPreferences(name: String?, mode: Int): SharedPreferences = prefs

    override fun getExternalFilesDir(type: String?): File = externalDir
}

/**
 * 极简进程内 SharedPreferences 实现, 仅覆盖 ViewModel 用到的 API。
 * 未用到的方法返回安全默认值, 足够单元测试。
 */
class InMemorySharedPreferences : SharedPreferences {

    private val map = ConcurrentHashMap<String, Any?>()

    override fun getAll(): MutableMap<String, *> = HashMap(map)

    override fun getString(key: String?, defValue: String?): String? =
        map[key] as? String ?: defValue

    override fun getStringSet(key: String?, defValue: MutableSet<String>?): MutableSet<String>? =
        (map[key] as? MutableSet<String>) ?: defValue

    override fun getInt(key: String?, defValue: Int): Int =
        (map[key] as? Int) ?: defValue

    override fun getLong(key: String?, defValue: Long): Long =
        (map[key] as? Long) ?: defValue

    override fun getFloat(key: String?, defValue: Float): Float =
        (map[key] as? Float) ?: defValue

    override fun getBoolean(key: String?, defValue: Boolean): Boolean =
        (map[key] as? Boolean) ?: defValue

    override fun contains(key: String?): Boolean = map.containsKey(key)

    override fun edit(): SharedPreferences.Editor = InMemoryEditor(map, this)

    override fun registerOnSharedPreferenceChangeListener(
        listener: SharedPreferences.OnSharedPreferenceChangeListener?,
    ) = Unit

    override fun unregisterOnSharedPreferenceChangeListener(
        listener: SharedPreferences.OnSharedPreferenceChangeListener?,
    ) = Unit
}

/**
 * 极简 Editor 实现, apply() 同步提交到内存 Map。
 */
class InMemoryEditor(
    private val map: MutableMap<String, Any?>,
    private val prefs: InMemorySharedPreferences,
) : SharedPreferences.Editor {

    override fun putString(key: String?, value: String?): SharedPreferences.Editor {
        if (key != null) map[key] = value
        return this
    }

    override fun putStringSet(
        key: String?,
        value: MutableSet<String>?,
    ): SharedPreferences.Editor {
        if (key != null) map[key] = value
        return this
    }

    override fun putInt(key: String?, value: Int): SharedPreferences.Editor {
        if (key != null) map[key] = value
        return this
    }

    override fun putLong(key: String?, value: Long): SharedPreferences.Editor {
        if (key != null) map[key] = value
        return this
    }

    override fun putFloat(key: String?, value: Float): SharedPreferences.Editor {
        if (key != null) map[key] = value
        return this
    }

    override fun putBoolean(key: String?, value: Boolean): SharedPreferences.Editor {
        if (key != null) map[key] = value
        return this
    }

    override fun remove(key: String?): SharedPreferences.Editor {
        if (key != null) map.remove(key)
        return this
    }

    override fun clear(): SharedPreferences.Editor {
        map.clear()
        return this
    }

    override fun commit(): Boolean {
        prefs.notifyChanged()
        return true
    }

    override fun apply() {
        commit()
    }
}

/** 测试用: 触发监听器(本实现无监听器, 仅为接口完整) */
private fun InMemorySharedPreferences.notifyChanged() = Unit
