/**
 * hal/usb/usb_stub.c — USB HAL 桩实现 (测试/不支持平台)
 *
 * 全部返回 HAL_USB_ERR_UNSUPPORTED, 用于单元测试和平台占位。
 */
#include "hal/usb.h"
#include <string.h>

int hal_usb_init(usb_device_callback cb, void *user_data) {
    (void)cb; (void)user_data;
    return HAL_USB_ERR_UNSUPPORTED;
}

int hal_usb_enumerate(UsbDeviceInfo *devices, int max_count) {
    (void)devices; (void)max_count;
    return 0;   /* 返回 0 设备, 不报错 */
}

int hal_usb_request_permission(UsbDeviceInfo *dev) {
    (void)dev;
    return HAL_USB_OK;
}

int hal_usb_open(UsbDeviceInfo *dev) {
    (void)dev;
    return HAL_USB_ERR_UNSUPPORTED;
}

void hal_usb_close(UsbDeviceInfo *dev) {
    (void)dev;
}

int hal_usb_bulk_transfer(int fd, uint8_t endpoint,
                          uint8_t *buf, int length, int timeout_ms) {
    (void)fd; (void)endpoint; (void)buf; (void)length; (void)timeout_ms;
    return HAL_USB_ERR_UNSUPPORTED;
}

int hal_usb_control_transfer(int fd,
                             uint8_t bmRequestType, uint8_t bRequest,
                             uint16_t wValue, uint16_t wIndex,
                             uint8_t *data, uint16_t wLength,
                             int timeout_ms) {
    (void)fd; (void)bmRequestType; (void)bRequest;
    (void)wValue; (void)wIndex; (void)data; (void)wLength; (void)timeout_ms;
    return HAL_USB_ERR_UNSUPPORTED;
}

void hal_usb_shutdown(void) {}

const char *hal_usb_strerror(int error_code) {
    if (error_code == HAL_USB_OK) return "OK";
    return "unsupported";
}
