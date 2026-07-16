// Top-level build file
// AGP / Kotlin 版本统一在 settings.gradle.kts 的 pluginManagement.plugins 中声明
// (对应 gradle.properties 的 VERSION_AGP / VERSION_KOTLIN)。
plugins {
    id("com.android.application") apply false
    id("org.jetbrains.kotlin.android") apply false
}
