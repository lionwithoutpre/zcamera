# core/CMakeLists.txt — libcore 静态库 (三端统一 libusb 后端)

project(core LANGUAGES C)

# ─── 公共源文件 ────────────────────────────────────────────────
set(CORE_SOURCES
    api/camera_api.c
    protocol/ptp.c
    protocol/mtp.c
    protocol/session.c
    adapter/nikon_adapter.c
    adapter/command_map.c
    adapter/wifi_adapter.c
    transfer/zerocopy.c
    transfer/chunked.c
    transfer/ftp_client.c
    event/watcher.c
    hal/wifi/wifi_pw_obfuscate.c
)

# ─── 查找 libusb (三端共用) ─────────────────────────────────────
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LIBUSB libusb-1.0 QUIET)
endif()
if(NOT LIBUSB_FOUND)
    find_library(LIBUSB_LIBRARIES
        NAMES usb-1.0 libusb-1.0
        PATHS /opt/homebrew/lib /usr/local/lib /usr/lib
        HINTS $ENV{VCPKG_ROOT}/installed/$ENV{VCPKG_TRIPLET}/lib
              $ENV{VCPKG_ROOT}/packages/libusb_$ENV{VCPKG_TRIPLET}/lib)
    find_path(LIBUSB_INCLUDE_DIRS
        NAMES libusb-1.0/libusb.h
        PATHS /opt/homebrew/include /usr/local/include /usr/include
        HINTS $ENV{VCPKG_ROOT}/installed/$ENV{VCPKG_TRIPLET}/include
              $ENV{VCPKG_ROOT}/packages/libusb_$ENV{VCPKG_TRIPLET}/include)
    if(LIBUSB_LIBRARIES AND LIBUSB_INCLUDE_DIRS)
        set(LIBUSB_FOUND TRUE)
    endif()
endif()

# ─── 平台 HAL 源文件 ───────────────────────────────────────────
if(PLATFORM_ANDROID)
    # Android 用 fd 直连(JNI 层处理 USB),core 不需要 USB HAL
    # Android bionic libc 内置 pthread,不需单独链接
    list(APPEND CORE_SOURCES
        hal/usb/usb_stub.c
        hal/wifi/wifi_posix.c)

elseif(PLATFORM_LINUX)
    list(APPEND CORE_SOURCES
        hal/usb/usb_linux.c
        hal/wifi/wifi_posix.c
        hal/bluetooth/ble_linux.c)
    if(LIBUSB_FOUND)
        list(APPEND CORE_PUBLIC_LIBS ${LIBUSB_LIBRARIES})
        list(APPEND CORE_PUBLIC_INCLUDE_DIRS ${LIBUSB_INCLUDE_DIRS})
    endif()
    list(APPEND CORE_PUBLIC_LIBS pthread)

elseif(PLATFORM_MACOS)
    list(APPEND CORE_SOURCES
        hal/usb/usb_macos.c
        hal/wifi/wifi_posix.c
        hal/bluetooth/ble_macos.c)
    if(LIBUSB_FOUND)
        list(APPEND CORE_PUBLIC_LIBS ${LIBUSB_LIBRARIES})
        list(APPEND CORE_PUBLIC_INCLUDE_DIRS ${LIBUSB_INCLUDE_DIRS})
    else()
        message(WARNING "libusb not found — macOS USB HAL will fail to link.")
        message(WARNING "Install: brew install libusb pkg-config")
    endif()
    list(APPEND CORE_PUBLIC_LIBS pthread)

elseif(PLATFORM_WINDOWS)
    list(APPEND CORE_SOURCES
        hal/usb/usb_win.c
        hal/wifi/wifi_posix.c
        hal/bluetooth/ble_win.c)
    if(LIBUSB_FOUND)
        list(APPEND CORE_PUBLIC_LIBS ${LIBUSB_LIBRARIES})
        list(APPEND CORE_PUBLIC_INCLUDE_DIRS ${LIBUSB_INCLUDE_DIRS})
    else()
        message(WARNING "libusb not found — install via vcpkg: vcpkg install libusb")
    endif()
    list(APPEND CORE_PUBLIC_LIBS ws2_32)

else()
    # stub (fallback: 无 USB 后端)
    list(APPEND CORE_SOURCES
        hal/usb/usb_stub.c
        hal/wifi/wifi_posix.c
        hal/bluetooth/ble_linux.c)
    list(APPEND CORE_PUBLIC_LIBS pthread)
endif()

# ─── libcore 静态库 ────────────────────────────────────────────
add_library(core STATIC ${CORE_SOURCES})

target_include_directories(core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CORE_PUBLIC_INCLUDE_DIRS}
)

target_compile_definitions(core
    PRIVATE
        $<$<BOOL:${ENABLE_ZEROCOPY}>:ENABLE_ZEROCOPY>
        $<$<BOOL:${ENABLE_EVENT_DRIVEN}>:ENABLE_EVENT_DRIVEN>
        $<$<BOOL:${PLATFORM_ANDROID}>:PLATFORM_ANDROID>
        $<$<BOOL:${PLATFORM_LINUX}>:PLATFORM_LINUX>
        $<$<BOOL:${PLATFORM_MACOS}>:PLATFORM_MACOS>
        $<$<BOOL:${PLATFORM_WINDOWS}>:PLATFORM_WINDOWS>
)

if(CORE_PUBLIC_LIBS)
    target_link_libraries(core PUBLIC ${CORE_PUBLIC_LIBS})
endif()

# ─── 安装规则 ──────────────────────────────────────────────────
install(TARGETS core ARCHIVE DESTINATION lib)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
        DESTINATION include)
