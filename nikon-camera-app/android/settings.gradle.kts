// android/settings.gradle.kts
pluginManagement {
    plugins {
        // 插件版本集中声明于此;根 build.gradle.kts 的 plugins 块不再写版本。
        // 修改时请与 gradle.properties 的 VERSION_AGP / VERSION_KOTLIN 及 docs/VERSIONS.md 保持一致。
        id("com.android.application") version "8.2.2" apply false
        id("org.jetbrains.kotlin.android") version "1.9.22" apply false
    }
    repositories {
        maven("https://mirrors.cloud.tencent.com/nexus/repository/maven-public/")
        maven("https://maven.aliyun.com/repository/gradle-plugin")
        maven("https://maven.aliyun.com/repository/google")
        maven("https://maven.aliyun.com/repository/public")
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}
dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        maven("https://mirrors.cloud.tencent.com/nexus/repository/maven-public/")
        maven("https://maven.aliyun.com/repository/google")
        maven("https://maven.aliyun.com/repository/public")
        google()
        mavenCentral()
    }
}
rootProject.name = "NikonConnect"
include(":app")
