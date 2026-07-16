/**
 * hal/usb/usb_macos.c — macOS USB HAL 实现 (libusb-1.0)
 *
 * 三端统一 libusb 后端。macOS 上 libusb 不支持热插拔回调，
 * 因此 hal_usb_init() 仅走枚举模式，热插拔由上层轮询驱动。
 *
 * 依赖: brew install libusb
 * 编译: -I/opt/homebrew/include/libusb-1.0 -L/opt/homebrew/lib -lusb-1.0
 */
#include "hal/usb.h"

#if defined(__APPLE__)
#include <libusb-1.0/libusb.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ─── 全局状态 ───────────────────────────────────────────────── */

static libusb_context       *s_ctx       = NULL;
static libusb_device_handle *s_dh        = NULL;   /* 当前打开的句柄 (单设备) */
static usb_device_callback   s_hotplug_cb = NULL;
static void                 *s_hotplug_user = NULL;

/* ─── fd → handle 映射 (简易: 单设备模式) ──────────────────────── */

static int s_next_fd = 1;

/* ─── 辅助: 读取字符串描述符 ──────────────────────────────────── */

static void _read_string(libusb_device_handle *dh, uint8_t idx,
                         char *out, size_t out_sz) {
    if (idx == 0 || !out) return;
    libusb_get_string_descriptor_ascii(dh, idx,
                                       (unsigned char *)out, (int)out_sz);
}

/* ─── 辅助: 查设备 handle ─────────────────────────────────────── */

static libusb_device_handle *_find_and_open(libusb_device *dev,
                                             const UsbDeviceInfo *info) {
    struct libusb_device_descriptor desc;
    if (libusb_get_device_descriptor(dev, &desc) != 0) return NULL;
    if (desc.idVendor != NIKON_USB_VENDOR_ID) return NULL;

    /* 有序列号则精确匹配 */
    if (info && info->serial[0]) {
        libusb_device_handle *tmp = NULL;
        if (libusb_open(dev, &tmp) != 0) return NULL;
        char serial[64] = {0};
        _read_string(tmp, desc.iSerialNumber, serial, sizeof(serial));
        if (strcmp(serial, info->serial) != 0) {
            libusb_close(tmp);
            return NULL;
        }
        return tmp;  /* 已打开, 调用者接管 */
    }

    /* 无序列号: 匹配 bus+address */
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

    /* macOS libusb 不支持热插拔 —— 由上层定时 enumerate 替代 */
    (void)cb;

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
    /* macOS IOKit 自动管理 USB 权限，无需运行时弹框 */
    return HAL_USB_OK;
}

int hal_usb_open(UsbDeviceInfo *dev) {
    if (!dev || !s_ctx) return HAL_USB_ERR_NOT_FOUND;
    if (s_dh) return HAL_USB_ERR_BUSY;   /* 单设备模式, 先关再开 */

    libusb_device **list = NULL;
    ssize_t count = libusb_get_device_list(s_ctx, &list);
    if (count < 0) return HAL_USB_ERR_IO;

    for (ssize_t i = 0; i < count; i++) {
        s_dh = _find_and_open(list[i], dev);
        if (s_dh) break;
    }
    libusb_free_device_list(list, 1);

    if (!s_dh) return HAL_USB_ERR_NOT_FOUND;

    /* 声明接口 0 (PTP) */
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
    if (dev) {
        dev->priv = NULL;
        dev->fd   = -1;
    }
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
    if (rc == LIBUSB_ERROR_NO_MEM)      return HAL_USB_ERR_NO_MEM;
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
    if (s_ctx) {
        libusb_exit(s_ctx);
        s_ctx = NULL;
    }
    s_hotplug_cb   = NULL;
    s_hotplug_user = NULL;
}

const char *hal_usb_strerror(int error_code) {
    switch (error_code) {
    case HAL_USB_OK:              return "USB 正常";
    case HAL_USB_ERR_NOT_FOUND:   return "设备未找到 — 请确认相机已开启并连接 USB";
    case HAL_USB_ERR_PERMISSION:  return "权限被拒绝 — 请检查 USB 接口是否被其他程序占用";
    case HAL_USB_ERR_IO:          return "I/O 错误 — 请重新插拔 USB 线缆";
    case HAL_USB_ERR_TIMEOUT:     return "传输超时";
    case HAL_USB_ERR_BUSY:        return "设备忙 — 请等待当前操作完成";
    case HAL_USB_ERR_NO_MEM:      return "内存不足";
    case HAL_USB_ERR_UNSUPPORTED: return "不支持的操作";
    default:                      return "未知 USB 错误";
    }
}

#endif /* __APPLE__ */
