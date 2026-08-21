// core/data/build.gradle.kts — 数据层
// Android 库模块: 依赖 :core:model 领域模型, 提供持久化(设置/相册)能力。
// 说明: TransferManager 因依赖 app 层 CameraApi(JNI 绑定 CameraBridge 不可移动),
// 暂留在 app 模块, 待阶段3 JNI 事件化重构时随 CameraApi 一并下沉。
plugins {
    id("com.android.library")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.nikon.data"
    compileSdk = 34

    defaultConfig {
        minSdk = 26
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }
}

dependencies {
    implementation(project(":core:model"))
    implementation("androidx.core:core-ktx:1.12.0")
    implementation("androidx.security:security-crypto:1.1.0-alpha06")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.7.3")
}