cmake_minimum_required(VERSION 3.20)
project(nikon-camera VERSION 1.0.0 LANGUAGES C CXX)

# ─── 全局选项 ──────────────────────────────────────────────────
option(BUILD_ANDROID       "Build Android JNI library"        OFF)
option(BUILD_DESKTOP       "Build desktop application"        OFF)
option(BUILD_TESTING       "Build unit tests"                  ON)
option(ENABLE_ZEROCOPY     "Enable zero-copy file transfer"    ON)
option(ENABLE_EVENT_DRIVEN "Enable event-driven monitoring"    ON)
option(ENABLE_ASAN         "Enable AddressSanitizer (debug)"  OFF)

# ─── C/C++ 标准 ────────────────────────────────────────────────
set(CMAKE_C_STANDARD            11)
set(CMAKE_C_STANDARD_REQUIRED   ON)
set(CMAKE_CXX_STANDARD          17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ─── 编译选项 ──────────────────────────────────────────────────
add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Wno-unused-parameter
)

if(ENABLE_ASAN)
    add_compile_options(-fsanitize=address -g)
    add_link_options(-fsanitize=address)
endif()

# ─── 平台检测 ──────────────────────────────────────────────────
if(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(PLATFORM_ANDROID TRUE)
    message(STATUS "[nikon] Platform: Android")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(PLATFORM_LINUX TRUE)
    message(STATUS "[nikon] Platform: Linux")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(PLATFORM_MACOS TRUE)
    message(STATUS "[nikon] Platform: macOS")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(PLATFORM_WINDOWS TRUE)
    message(STATUS "[nikon] Platform: Windows")
else()
    message(WARNING "[nikon] Unknown platform, using stub HAL")
    set(PLATFORM_STUB TRUE)
endif()

# ─── 子目录 ────────────────────────────────────────────────────
add_subdirectory(core)

if(BUILD_DESKTOP)
    add_subdirectory(desktop)
endif()

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# ─── 摘要 ──────────────────────────────────────────────────────
message(STATUS "")
message(STATUS "══ Nikon Camera Connect ══════════════════════")
message(STATUS "  Version       : ${PROJECT_VERSION}")
message(STATUS "  Zero-copy     : ${ENABLE_ZEROCOPY}")
message(STATUS "  Event-driven  : ${ENABLE_EVENT_DRIVEN}")
message(STATUS "  Build tests   : ${BUILD_TESTING}")
message(STATUS "  Build desktop : ${BUILD_DESKTOP}")
message(STATUS "══════════════════════════════════════════════")
message(STATUS "")
