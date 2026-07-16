# 跨平台构建系统规格书

## 1. 构建系统总览

```
nikon-camera-app/
├── CMakeLists.txt              # 根 CMake (统一入口)
├── core/                       # C/C++ 核心库
│   ├── CMakeLists.txt          # libcore 静态库
│   ├── hal/
│   │   ├── CMakeLists.txt
│   │   ├── usb/
│   │   │   ├── usb_linux.c
│   │   │   ├── usb_macos.c
│   │   │   ├── usb_win.c
│   │   │   └── usb_stub.c
│   │   ├── wifi/
│   │   │   ├── wifi_posix.c    # POSIX socket 实现 (Linux/macOS)
│   │   │   └── wifi_win.c
│   │   └── bluetooth/
│   │       ├── ble_linux.c
│   │       ├── ble_macos.c
│   │       └── ble_win.c
│   ├── protocol/
│   │   ├── ptp.c
│   │   ├── mtp.c
│   │   └── session.c
│   ├── adapter/
│   │   ├── nikon_adapter.c
│   │   ├── wifi_adapter.c      # Wi-Fi 适配器实现
│   │   └── command_map.c
│   ├── transfer/
│   │   ├── zerocopy.c
│   │   └── chunked.c
│   └── event/
│       └── watcher.c
├── android/                    # Android 项目
│   ├── CMakeLists.txt
│   ├── app/
│   │   ├── build.gradle.kts
│   │   └── src/main/
│   │       ├── jni/
│   │       │   ├── jni_bridge.c
│   │       │   └── Android.mk
│   │       └── java/com/nikon/app/
│   └── settings.gradle.kts
├── desktop/                    # 桌面应用
│   ├── CMakeLists.txt
│   ├── src/
│   │   ├── py_gui/
│   │   │   └── desktop_api.py  # Python GUI (PySide6 + ctypes)
│   │   └── ui/
│   │       └── pages/
│   │           ├── liveview_page.cpp
│   │           ├── dashboard_page.cpp
│   │           └── file_browser_page.cpp
│   └── cli/
├── tests/                      # 测试
│   ├── CMakeLists.txt          # 测试注册 (8 个测试)
│   ├── test_ptp.c
│   ├── test_mtp.c
│   ├── test_session.c          # 会话管理测试
│   ├── test_watcher.c          # 事件监听测试
│   ├── test_wifi_adapter.c     # Wi-Fi 适配器测试
│   ├── test_chunked.c          # 分块传输测试
│   ├── test_camera_api.c       # CameraAPI 测试
│   └── test_common.h           # CHECK 宏框架
└── common/                     # 公共代码 (预留)
    ├── CMakeLists.txt
    └── src/
```

## 2. 根 CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.20)
project(nikon-camera VERSION 1.0.0 LANGUAGES C CXX)

option(BUILD_ANDROID "Build Android JNI library" OFF)
option(BUILD_DESKTOP "Build desktop application" OFF)
option(BUILD_TESTING "Build tests" ON)
option(ENABLE_ZEROCOPY "Enable zero-copy optimization" ON)
option(ENABLE_EVENT_DRIVEN "Enable event-driven monitoring" ON)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(PLATFORM_ANDROID TRUE)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(PLATFORM_LINUX TRUE)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(PLATFORM_MACOS TRUE)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(PLATFORM_WINDOWS TRUE)
endif()

add_subdirectory(core)

if(BUILD_DESKTOP)
    add_subdirectory(desktop)
endif()

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
```

## 3. 核心库 CMakeLists.txt

```cmake
project(core LANGUAGES C)

set(CORE_SOURCES
    protocol/ptp.c
    protocol/mtp.c
    protocol/session.c
    adapter/nikon_adapter.c
    adapter/wifi_adapter.c
    adapter/command_map.c
    transfer/zerocopy.c
    transfer/chunked.c
    event/watcher.c
)

if(PLATFORM_LINUX)
    list(APPEND CORE_SOURCES hal/usb/usb_linux.c hal/wifi/wifi_posix.c)
    list(APPEND CORE_PUBLIC_LIBS usb-1.0)
elseif(PLATFORM_MACOS)
    list(APPEND CORE_SOURCES hal/usb/usb_macos.c hal/wifi/wifi_posix.c)
    find_library(IOKIT_FRAMEWORK IOKit)
    find_library(COREFOUNDATION CoreFoundation)
    list(APPEND CORE_PUBLIC_LIBS ${IOKIT_FRAMEWORK} ${COREFOUNDATION})
elseif(PLATFORM_WINDOWS)
    list(APPEND CORE_SOURCES hal/usb/usb_win.c hal/wifi/wifi_win.c)
    list(APPEND CORE_PUBLIC_LIBS setupapi.lib winusb.lib)
elseif(PLATFORM_ANDROID)
    list(APPEND CORE_SOURCES hal/usb/usb_linux.c hal/wifi/wifi_posix.c)
endif()

add_library(core STATIC ${CORE_SOURCES})

target_include_directories(core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(core PRIVATE
    $<$<BOOL:${ENABLE_ZEROCOPY}>:ENABLE_ZEROCOPY>
    $<$<BOOL:${ENABLE_EVENT_DRIVEN}>:ENABLE_EVENT_DRIVEN>
)

if(CORE_PUBLIC_LIBS)
    target_link_libraries(core PUBLIC ${CORE_PUBLIC_LIBS})
endif()
```

## 4. 测试 CMakeLists.txt

```cmake
# tests/CMakeLists.txt
project(tests LANGUAGES C)

# CHECK 宏测试框架 (无外部依赖)
add_executable(test_ptp test_ptp.c)
add_executable(test_mtp test_mtp.c)
add_executable(test_session test_session.c)
add_executable(test_watcher test_watcher.c)
add_executable(test_wifi_adapter test_wifi_adapter.c)
add_executable(test_chunked test_chunked.c)
add_executable(test_camera_api test_camera_api.c)

foreach(test IN ITEMS test_ptp test_mtp test_session test_watcher
                      test_wifi_adapter test_chunked test_camera_api)
    target_link_libraries(${test} core)
    target_include_directories(${test} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/core/include
    )
    add_test(NAME ${test} COMMAND ${test})
endforeach()
```

## 5. Android 构建配置

```kotlin
// android/settings.gradle.kts
pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}
dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}
rootProject.name = "NikonConnect"
include(":app")
```

```groovy
// android/app/build.gradle.kts
plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.nikon.app"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.nikon.cameraconnect"
        minSdk = 26
        targetSdk = 34
        versionCode = 1
        versionName = "1.0.0"

        ndk {
            abiFilters += listOf("arm64-v8a", "armeabi-v7a")
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"))
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/jni/CMakeLists.txt")
            version = "3.22.1"
        }
    }
}

dependencies {
    implementation("androidx.core:core-ktx:1.12.0")
    implementation("androidx.lifecycle:lifecycle-service:2.7.0")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.7.3")
}
```

```cmake
# android/app/src/main/jni/CMakeLists.txt
cmake_minimum_required(VERSION 3.22)
project(nikon_jni LANGUAGES C)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../core
                 ${CMAKE_BINARY_DIR}/core)

add_library(nikon_bridge SHARED jni_bridge.c)

target_include_directories(nikon_bridge PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../core/include
)

target_link_libraries(nikon_bridge core jnigraphics log)
```

## 6. 桌面端 CMakeLists.txt

```cmake
# desktop/CMakeLists.txt
project(nikon-desktop LANGUAGES C CXX)

option(USE_QT "Use Qt for desktop UI" OFF)
option(USE_GTK "Use GTK for desktop UI" OFF)

set(DESKTOP_SOURCES
    src/main.c
    src/hal/usb_desktop.c
)

add_executable(nikon-cli src/cli/main.c)
target_link_libraries(nikon-cli core)

if(USE_QT)
    find_package(Qt6 COMPONENTS Widgets Network QUIET)
    if(Qt6_FOUND)
        add_executable(nikon-qt src/ui/qt_main.cpp)
        target_link_libraries(nikon-qt core Qt6::Widgets Qt6::Network)
    endif()
elseif(USE_GTK)
    find_package(GTK3 COMPONENTS gtk gdk)
    if(GTK3_FOUND)
        add_executable(nikon-gtk src/ui/gtk_main.c)
        target_link_libraries(nikon-gtk core ${GTK3_LIBRARIES})
    endif()
endif()
```

## 7. 依赖清单

| 依赖 | 版本 | 平台 | 用途 |
|------|------|------|------|
| libusb-1.0 | >=1.0.24 | Linux/macOS/Windows | USB 设备枚举与通信 |
| libgphoto2 | >=2.5.31 | Linux/macOS (参考) | 协议参考实现 (可选) |
| pthreads | 系统自带 | 所有 | 线程、并发控制 |
| Android NDK | r26+ | Android | JNI 与原生编译 |
| Kotlin | 1.9+ | Android | UI 层 |
| CMake | >=3.20 | 所有 | 构建系统 |
| Gradle | 8.x | Android | Android 构建 |
| Qt6 / GTK3 | 可选 | Desktop | 桌面 UI |
| PySide6 | 可选 | Desktop | Python GUI |
| ctypes | 标准库 | Desktop | Python FFI |

## 8. 构建命令

```bash
# Linux/macOS 桌面
mkdir -p build/desktop && cd build/desktop
cmake ../.. -DBUILD_DESKTOP=ON -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)

# Android
cd android
./gradlew assembleDebug

# Android NDK 单独构建
mkdir -p build/android && cd build/android
cmake ../.. -DANDROID=ON \
    -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI=arm64-v8a
cmake --build .

# 运行测试
cd build/desktop && ctest --output-on-failure

# 清理
cmake --build . --target clean
```
