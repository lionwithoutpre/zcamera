// android/app/build.gradle.kts
plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

// 版本集中声明于 gradle.properties(单一事实来源),此处仅引用。
val composeBomVersion = findProperty("COMPOSE_BOM") as String
val ndkVersionValue = findProperty("VERSION_NDK") as String
val kotlinCompilerExt = findProperty("VERSION_KOTLIN_COMPILER_EXT") as String
val compileSdkValue = (findProperty("COMPILE_SDK") as String).toInt()
val minSdkValue = (findProperty("MIN_SDK") as String).toInt()
val targetSdkValue = (findProperty("TARGET_SDK") as String).toInt()

android {
    namespace = "com.nikon.app"
    compileSdk = compileSdkValue
    ndkVersion = ndkVersionValue

    defaultConfig {
        applicationId   = "com.nikon.cameraconnect"
        minSdk          = minSdkValue      // Android 8.0+  USB Host API
        targetSdk       = targetSdkValue
        versionCode     = 1
        versionName     = "1.0.0"

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"

        ndk {
            // 相机 USB/BLE 连接工具仅在 ARM 手机使用, 无需 x86 ChromeOS 支持
            abiFilters += listOf("arm64-v8a", "armeabi-v7a")
        }

        lint {
            // 仅打包 ARM 原生库是刻意选择(相机硬件工具), 忽略 ChromeOS ABI 告警
            disable += "ChromeOsAbiSupport"
            // mipmap-anydpi-v26 的 adaptive-icon 需 v26 目录语义(minSdk 恰好=26),
            // AAPT2 依赖该目录正确处理启动图标, 属平台约定而非冗余
            disable += "ObsoleteSdkInt"
        }

        // 配置 USB host intent-filter 资源
        resValue("string", "app_name", "Nikon Connect")
    }

    buildTypes {
        debug {
            isDebuggable    = true
            isMinifyEnabled = false
        }
        release {
            isMinifyEnabled = true
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }

    buildFeatures {
        compose = true
    }

    // 单元测试为纯 JVM 测试(不依赖 Robolectric / Android 框架),
    // 通过 TestNikonApplication 提供内存版 SharedPreferences 与临时外部目录。
    testOptions {
        unitTests {
            isReturnDefaultValues = true
        }
    }

    // 纯 JVM 测试很轻量, 给一个适度堆即可(无需 Robolectric 的大内存)。
    tasks.withType<Test> {
        maxHeapSize = "1g"
    }

    composeOptions {
        kotlinCompilerExtensionVersion = kotlinCompilerExt
    }

    externalNativeBuild {
        cmake {
            path    = file("src/main/jni/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    packaging {
        resources {
            excludes += "/META-INF/{AL2.0,LGPL2.1}"
            // 多个 JUnit 5 jar(junit-jupiter / junit-platform)各自带 META-INF/LICENSE.md,
            // 打包合并资源时会冲突导致 mergeDebugAndroidTestJavaResource 失败, 排除这些许可证文件。
            excludes += "/META-INF/LICENSE.md"
            excludes += "/META-INF/LICENSE-notice.md"
        }
    }
}

dependencies {
    // 领域模型层 (纯 Kotlin, 无 Android 依赖)
    implementation(project(":core:model"))
    // 数据层 (设置持久化 / 相册存储)
    implementation(project(":core:data"))

    // Kotlin
    implementation("androidx.core:core-ktx:1.12.0")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.7.3")

    // Lifecycle & ViewModel
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.7.0")
    implementation("androidx.lifecycle:lifecycle-runtime-compose:2.7.0")
    implementation("androidx.lifecycle:lifecycle-viewmodel-ktx:2.7.0")
    implementation("androidx.lifecycle:lifecycle-service:2.7.0")

    // Compose
    val composeBom = platform("androidx.compose:compose-bom:$composeBomVersion")
    implementation(composeBom)
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.ui:ui-graphics")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.material:material-icons-extended:1.6.2")
    implementation("androidx.compose.ui:ui-tooling-preview")
    implementation("androidx.activity:activity-compose:1.8.2")

    // Navigation
    implementation("androidx.navigation:navigation-compose:2.7.7")

    // USB (Android 系统 API, 无需额外依赖)
    // android.hardware.usb.UsbManager

    // Testing (纯 JVM 单元测试, 不依赖 Robolectric / Android 框架)
    testImplementation("junit:junit:4.13.2")
    testImplementation("org.jetbrains.kotlinx:kotlinx-coroutines-test:1.7.3")
    testImplementation("app.cash.turbine:turbine:1.0.0")
    testImplementation("io.mockk:mockk:1.13.8")

    // ─── 仪表化 Compose UI 测试(在真机/模拟器运行)──────────────
    // 本机因内存限制无法跑 Robolectric,故 UI 交互测试放在 androidTest,
    // 通过 connectedDebugAndroidTest 在设备/模拟器上执行。
    androidTestImplementation("androidx.test.ext:junit:1.1.5")
    androidTestImplementation("androidx.test:core:1.5.0")
    androidTestImplementation(composeBom)
    androidTestImplementation("androidx.compose.ui:ui-test")
    androidTestImplementation("androidx.compose.ui:ui-test-junit4")
    androidTestImplementation("androidx.compose.ui:ui-test-manifest")
    androidTestImplementation("org.jetbrains.kotlinx:kotlinx-coroutines-test:1.7.3")
    // mockk 会传递引入 org.junit.jupiter(JUnit 5), 与 androidTest 的 JUnit4 体系无关,
    // 且会带来重复的 META-INF/LICENSE.md 导致打包冲突, 故排除整组 jupiter。
    androidTestImplementation("io.mockk:mockk-android:1.13.8") {
        exclude(group = "org.junit.jupiter")
    }

    debugImplementation("androidx.compose.ui:ui-tooling")
}
