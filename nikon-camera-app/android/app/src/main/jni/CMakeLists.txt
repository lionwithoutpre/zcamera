cmake_minimum_required(VERSION 3.22)
project(nikon_jni LANGUAGES C)

# 声明目标平台 (顶层 CMakeLists.txt 不会被执行,需在此设置)
set(PLATFORM_ANDROID TRUE)

# 引入 core 静态库 (相对于 JNI 目录: jni→main→src→app→android→nikon-camera-app, 共 5 级)
add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../core
    ${CMAKE_BINARY_DIR}/core
)

# JNI 桥接共享库
add_library(nikon_bridge SHARED jni_bridge.c)

target_include_directories(nikon_bridge PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../core/include
)

target_link_libraries(nikon_bridge
    core
    jnigraphics   # Android Bitmap 操作 (实时取景渲染)
    log           # __android_log_print
)
