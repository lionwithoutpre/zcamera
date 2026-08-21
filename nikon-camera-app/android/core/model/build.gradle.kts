// core/model/build.gradle.kts — 领域模型层
// 纯 Kotlin JVM 库: 不依赖 Android, 便于快速单元测试与多端复用
// (桌面端未来也可直接复用同一套模型定义)。
plugins {
    id("org.jetbrains.kotlin.jvm")
}

java {
    sourceCompatibility = JavaVersion.VERSION_17
    targetCompatibility = JavaVersion.VERSION_17
}

kotlin {
    jvmToolchain(17)
}

dependencies {
    testImplementation("junit:junit:4.13.2")
}