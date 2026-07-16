# desktop/CMakeLists.txt — 桌面应用

project(nikon-desktop LANGUAGES C CXX)

option(USE_QT  "Build Qt6 desktop UI"  OFF)
option(USE_GTK "Build GTK3 desktop UI" OFF)

# ─── CLI 工具 ──────────────────────────────────────────────────
add_executable(nikon-cli cli/main.c)
target_link_libraries(nikon-cli PRIVATE core)
target_include_directories(nikon-cli PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include)

install(TARGETS nikon-cli RUNTIME DESTINATION bin)

# ─── Qt6 UI ────────────────────────────────────────────────────
if(USE_QT)
    find_package(Qt6 REQUIRED COMPONENTS Widgets)

    # Qt 需要 MOC 预处理
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
    set(CMAKE_AUTOUIC ON)

    # 收集所有 Qt GUI 源文件
    file(GLOB_RECURSE QT_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/api/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/api/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/pages/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/pages/*.h
    )

    add_executable(nikon-qt ${QT_SOURCES})

    target_include_directories(nikon-qt PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/core/include
    )

    target_link_libraries(nikon-qt PRIVATE
        core
        Qt6::Widgets
    )

    # macOS: 打包为 .app bundle
    if(APPLE)
        set_target_properties(nikon-qt PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_GUI_IDENTIFIER "com.nikon.cameraconnect"
            MACOSX_BUNDLE_BUNDLE_NAME "Nikon Camera Connect"
            MACOSX_BUNDLE_BUNDLE_VERSION "1.0.0"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
        )
    endif()

    install(TARGETS nikon-qt RUNTIME DESTINATION bin BUNDLE DESTINATION .)
    message(STATUS "[desktop] Qt6 UI enabled (${QT_SOURCES} sources)")
endif()

# ─── GTK3 UI (可选) ────────────────────────────────────────────
if(USE_GTK)
    find_package(GTK3 QUIET)
    if(GTK3_FOUND)
        add_executable(nikon-gtk src/ui/gtk_main.c)
        target_link_libraries(nikon-gtk PRIVATE core ${GTK3_LIBRARIES})
        target_include_directories(nikon-gtk PRIVATE ${GTK3_INCLUDE_DIRS})
        install(TARGETS nikon-gtk RUNTIME DESTINATION bin)
        message(STATUS "[desktop] GTK3 UI enabled")
    else()
        message(WARNING "[desktop] GTK3 not found, skipping GTK UI")
    endif()
endif()
