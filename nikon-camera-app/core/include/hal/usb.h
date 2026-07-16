/**
 * hal/usb.h — USB 硬件抽象层接口
 *
 * 平台实现:
 *   Android  : hal/usb/usb_android.c  (android.hardware.usb.UsbManager via JNI)
 *   Linux    : hal/usb/usb_linux.c    (libusb-1.0)
 *   macOS    : hal/usb/usb_macos.c    (IOUSBKit / libusb)
 *   Windows  : hal/usb/usb_win.c      (WinUSB / libusb)
 */
#ifndef NIKON_HAL_USB_H
#define NIKON_HAL_USB_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ─── 常量 ───────────────────────────────────────────────────── */

#define NIKON_USB_VENDOR_ID     0x04B0u   /* 尼康 Vendor ID */

/* 端点方向 */
#define USB_EP_DIR_IN           0x80u
#define USB_EP_DIR_OUT          0x00u

/* ─── 错误码 ─────────────────────────────────────────────────── */

#define HAL_USB_OK              0
#define HAL_USB_ERR_NOT_FOUND   (-1)
#define HAL_USB_ERR_PERMISSION  (-2)
#define HAL_USB_ERR_IO          (-3)
#define HAL_USB_ERR_TIMEOUT     (-4)
#define HAL_USB_ERR_BUSY        (-5)
#define HAL_USB_ERR_NO_MEM      (-6)
#define HAL_USB_ERR_UNSUPPORTED (-7)

/* ─── 数据结构 ───────────────────────────────────────────────── */

/** USB 设备信息 */
typedef struct {
    uint16_t    vendor_id;          /**< 厂商 ID (Nikon: 0x04B0) */
    uint16_t    product_id;         /**< 产品 ID */
    char        serial[64];         /**< 序列号 */
    char        manufacturer[64];   /**< 制造商名称 */
    char        product_name[128];  /**< 产品名称 */
    uint8_t     bus_number;         /**< USB 总线号 */
    uint8_t     device_address;     /**< 设备地址 */
    int         fd;                 /**< 设备文件描述符 (-1 表示未打开) */
    void       *priv;               /**< 平台私有数据 (不可直接访问) */
} UsbDeviceInfo;

/** USB 热插拔事件回调
 *  @param dev       发生变化的设备信息
 *  @param connected true=插入 false=拔出
 *  @param user_data 注册时传入的用户数据
 */
typedef void (*usb_device_callback)(UsbDeviceInfo *dev, bool connected, void *user_data);

/* ─── 接口 ───────────────────────────────────────────────────── */

/**
 * 初始化 USB 子系统并注册热插拔回调。
 * 必须在调用其他 hal_usb_* 之前调用。
 *
 * @param cb        热插拔回调 (可为 NULL)
 * @param user_data 回调的用户数据
 * @return HAL_USB_OK 成功; < 0 错误码
 */
int hal_usb_init(usb_device_callback cb, void *user_data);

/**
 * 枚举当前已连接的尼康相机设备。
 *
 * @param devices   输出缓冲区
 * @param max_count 缓冲区最大容量
 * @return >= 0 实际设备数量; < 0 错误码
 */
int hal_usb_enumerate(UsbDeviceInfo *devices, int max_count);

/**
 * 请求设备访问权限 (Android OTG 必须调用)。
 * 其他平台此函数直接返回 HAL_USB_OK。
 *
 * @return 0=已授权可直接打开; 1=已弹出授权对话框需等待回调; < 0 错误
 */
int hal_usb_request_permission(UsbDeviceInfo *dev);

/**
 * 打开 USB 设备并声明 PTP 接口。
 * 成功后 dev->fd 被赋值为有效描述符。
 *
 * @return >= 0 fd; < 0 错误码
 */
int hal_usb_open(UsbDeviceInfo *dev);

/** 释放接口并关闭设备。 */
void hal_usb_close(UsbDeviceInfo *dev);

/**
 * USB 批量传输 (同步阻塞)。
 *
 * @param fd         hal_usb_open 返回的 fd
 * @param endpoint   端点地址 (含方向位, e.g. 0x81=EP1 IN)
 * @param buf        数据缓冲区
 * @param length     期望传输字节数
 * @param timeout_ms 超时毫秒 (0=无限等待)
 * @return >= 0 实际传输字节数; < 0 错误码
 */
int hal_usb_bulk_transfer(int fd, uint8_t endpoint,
                          uint8_t *buf, int length, int timeout_ms);

/**
 * USB 控制传输。
 * 参数语义与 USB 规范一致。
 *
 * @return >= 0 传输字节数; < 0 错误码
 */
int hal_usb_control_transfer(int fd,
                             uint8_t bmRequestType, uint8_t bRequest,
                             uint16_t wValue, uint16_t wIndex,
                             uint8_t *data, uint16_t wLength,
                             int timeout_ms);

/** 释放 USB 子系统并注销热插拔监听。 */
void hal_usb_shutdown(void);

/** 将错误码转换为可读字符串。 */
const char *hal_usb_strerror(int error_code);

#ifdef __cplusplus
}
#endif
#endif /* NIKON_HAL_USB_H */
