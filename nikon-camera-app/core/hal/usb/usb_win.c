/**
 * hal/usb/usb_win.c — Windows USB HAL 实现 (libusb-1.0)
 *
 * 三端统一 libusb 后端。
 * Windows 上 libusb 通过 WinUSB/libusbK 驱动工作。
 * 需要在系统上安装 libusb 或通过 vcpkg/MSYS2 获取。
 *
 * 编译 (MSVC): /I<vcpkg>\include\libusb-1.0 libusb-1.0.lib
 * 编译 (MinGW): `pkg-config --cflags --libs libusb-1.0`
 */
#include "hal/usb.h"

#if defined(_WIN32)
#include <libusb-1.0/libusb.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ─── 全局状态 ───────────────────────────────────────────────── */

static libusb_context       *s_ctx       = NULL;
static libusb_device_handle *s_dh        = NULL;
static usb_device_callback   s_hotplug_cb = NULL;
static void                 *s_hotplug_user = NULL;

static int s_next_fd = 1;

/* ─── 辅助: 字符串描述符 ──────────────────────────────────────── */

static void _read_string(libusb_device_handle *dh, uint8_t idx,
                         char *out, size_t out_sz) {
    if (idx == 0 || !out) return;
    libusb_get_string_descriptor_ascii(dh, idx,
                                       (unsigned char *)out, (int)out_sz);
}

/* ─── 热插拔回调 ─────────────────────────────────────────────── */

static int LIBUSB_CALL _hotplug_handler(libusb_context *ctx,
                                         libusb_device *device,
                                         libusb_hotplug_event event,
                                         void *user_data) {
    (void)ctx; (void)user_data;
    if (!s_hotplug_cb) return 0;

    struct libusb_device_descriptor desc;
    if (libusb_get_device_descriptor(device, &desc) != 0) return 0;
    if (desc.idVendor != NIKON_USB_VENDOR_ID) return 0;

    UsbDeviceInfo info;
    memset(&info, 0, sizeof(info));
    info.vendor_id      = desc.idVendor;
    info.product_id     = desc.idProduct;
    info.bus_number     = libusb_get_bus_number(device);
    info.device_address = libusb_get_device_address(device);
    info.fd             = -1;

    bool connected = (event == LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED);
    s_hotplug_cb(&info, connected, s_hotplug_user);
    return 0;
}

/* ─── 查找并打开 ─────────────────────────────────────────────── */

static libusb_device_handle *_find_and_open(libusb_device *dev,
                                             const UsbDeviceInfo *info) {
    struct libusb_device_descriptor desc;
    if (libusb_get_device_descriptor(dev, &desc) != 0) return NULL;
    if (desc.idVendor != NIKON_USB_VENDOR_ID) return NULL;

    if (info && info->serial[0]) {
        libusb_device_handle *tmp = NULL;
        if (libusb_open(dev, &tmp) != 0) return NULL;
        char serial[64] = {0};
        _read_string(tmp, desc.iSerialNumber, serial, sizeof(serial));
        if (strcmp(serial, info->serial) != 0) {
            libusb_close(tmp);
            return NULL;
        }
        return tmp;
    }

    if (info &&
        libusb_get_bus_number(dev)     != info->bus_number &&
        libusb_get_device_address(dev) != info->device_address) {
        return NULL;
    }

    libusb_device_handle *dh = NULL;
    if (libusb_open(dev, &dh) != 0) return NULL;
    return dh;
}

/* ═══════════════════════════════════════════════════════════════
 *  公开接口
 * ══════════════════════════════════════════════════════════════ */

int hal_usb_init(usb_device_callback cb, void *user_data) {
    if (s_ctx) return HAL_USB_OK;

    int rc = libusb_init(&s_ctx);
    if (rc != 0) return HAL_USB_ERR_IO;

    s_hotplug_cb   = cb;
    s_hotplug_user = user_data;

    /* 注册热插拔 (Windows 支持) */
    if (cb && libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG)) {
        libusb_hotplug_register_callback(
            s_ctx,
            LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED |
            LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT,
            LIBUSB_HOTPLUG_ENUMERATE,
            NIKON_USB_VENDOR_ID,
            LIBUSB_HOTPLUG_MATCH_ANY,
            LIBUSB_HOTPLUG_MATCH_ANY,
            _hotplug_handler,
            NULL, NULL);
    }
    return HAL_USB_OK;
}

int hal_usb_enumerate(UsbDeviceInfo *devices, int max_count) {
    if (!s_ctx || !devices || max_count <= 0) return HAL_USB_ERR_NOT_FOUND;

    libusb_device **list = NULL;
    ssize_t count = libusb_get_device_list(s_ctx, &list);
    if (count < 0) return HAL_USB_ERR_IO;

    int found = 0;
    for (ssize_t i = 0; i < count && found < max_count; i++) {
        struct libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(list[i], &desc) != 0) continue;
        if (desc.idVendor != NIKON_USB_VENDOR_ID) continue;

        libusb_device_handle *dh = NULL;
        if (libusb_open(list[i], &dh) != 0) continue;

        UsbDeviceInfo *info = &devices[found];
        memset(info, 0, sizeof(*info));
        info->vendor_id      = desc.idVendor;
        info->product_id     = desc.idProduct;
        info->bus_number     = libusb_get_bus_number(list[i]);
        info->device_address = libusb_get_device_address(list[i]);
        info->fd             = -1;

        _read_string(dh, desc.iManufacturer, info->manufacturer, sizeof(info->manufacturer));
        _read_string(dh, desc.iProduct,      info->product_name, sizeof(info->product_name));
        _read_string(dh, desc.iSerialNumber, info->serial,       sizeof(info->serial));

        libusb_close(dh);
        found++;
    }
    libusb_free_device_list(list, 1);
    return found;
}

int hal_usb_request_permission(UsbDeviceInfo *dev) {
    (void)dev;
    /* Windows: WinUSB 驱动安装后自动授权 */
    return HAL_USB_OK;
}

int hal_usb_open(UsbDeviceInfo *dev) {
    if (!dev || !s_ctx) return HAL_USB_ERR_NOT_FOUND;
    if (s_dh) return HAL_USB_ERR_BUSY;

    libusb_device **list = NULL;
    ssize_t count = libusb_get_device_list(s_ctx, &list);
    if (count < 0) return HAL_USB_ERR_IO;

    for (ssize_t i = 0; i < count; i++) {
        s_dh = _find_and_open(list[i], dev);
        if (s_dh) break;
    }
    libusb_free_device_list(list, 1);

    if (!s_dh) return HAL_USB_ERR_NOT_FOUND;

    if (libusb_claim_interface(s_dh, 0) != 0) {
        libusb_close(s_dh);
        s_dh = NULL;
        return HAL_USB_ERR_PERMISSION;
    }

    dev->priv = s_dh;
    dev->fd   = s_next_fd++;
    return dev->fd;
}

void hal_usb_close(UsbDeviceInfo *dev) {
    if (!s_dh) return;
    libusb_release_interface(s_dh, 0);
    libusb_close(s_dh);
    s_dh = NULL;
    if (dev) { dev->priv = NULL; dev->fd = -1; }
}

int hal_usb_bulk_transfer(int fd, uint8_t endpoint,
                          uint8_t *buf, int length, int timeout_ms) {
    (void)fd;
    if (!s_dh) return HAL_USB_ERR_IO;

    int transferred = 0;
    int rc = libusb_bulk_transfer(s_dh, endpoint, buf, length,
                                  &transferred, timeout_ms);
    if (rc == LIBUSB_SUCCESS)           return transferred;
    if (rc == LIBUSB_ERROR_TIMEOUT)     return HAL_USB_ERR_TIMEOUT;
    if (rc == LIBUSB_ERROR_BUSY)        return HAL_USB_ERR_BUSY;
    if (rc == LIBUSB_ERROR_NO_DEVICE)   return HAL_USB_ERR_NOT_FOUND;
    return HAL_USB_ERR_IO;
}

int hal_usb_control_transfer(int fd,
                             uint8_t bmRequestType, uint8_t bRequest,
                             uint16_t wValue, uint16_t wIndex,
                             uint8_t *data, uint16_t wLength,
                             int timeout_ms) {
    (void)fd;
    if (!s_dh) return HAL_USB_ERR_IO;

    int rc = libusb_control_transfer(s_dh, bmRequestType, bRequest,
                                     wValue, wIndex, data, wLength,
                                     (unsigned int)timeout_ms);
    if (rc >= 0) return rc;
    if (rc == LIBUSB_ERROR_TIMEOUT)     return HAL_USB_ERR_TIMEOUT;
    if (rc == LIBUSB_ERROR_NO_DEVICE)   return HAL_USB_ERR_NOT_FOUND;
    return HAL_USB_ERR_IO;
}

void hal_usb_shutdown(void) {
    if (s_dh) {
        libusb_release_interface(s_dh, 0);
        libusb_close(s_dh);
        s_dh = NULL;
    }
    if (s_ctx) { libusb_exit(s_ctx); s_ctx = NULL; }
    s_hotplug_cb   = NULL;
    s_hotplug_user = NULL;
}

const char *hal_usb_strerror(int error_code) {
    switch (error_code) {
    case HAL_USB_OK:              return "USB OK";
    case HAL_USB_ERR_NOT_FOUND:   return "Device not found — connect camera via USB";
    case HAL_USB_ERR_PERMISSION:  return "Permission denied — install WinUSB driver";
    case HAL_USB_ERR_IO:          return "I/O error — replug USB cable";
    case HAL_USB_ERR_TIMEOUT:     return "Transfer timeout";
    case HAL_USB_ERR_BUSY:        return "Device busy";
    case HAL_USB_ERR_NO_MEM:      return "Out of memory";
    case HAL_USB_ERR_UNSUPPORTED: return "Unsupported";
    default:                      return "Unknown USB error";
    }
}

#endif /* _WIN32 */
