/**
 * hal/bluetooth.h — BLE 蓝牙硬件抽象层接口
 *
 * 主要用途: 相机发现 + 唤醒 (SnapBridge BLE)，不做大数据传输。
 * 平台实现:
 *   Android : hal/bluetooth/ble_android.c (android.bluetooth.le)
 *   Linux   : hal/bluetooth/ble_linux.c   (BlueZ D-Bus)
 *   macOS   : hal/bluetooth/ble_macos.c   (CoreBluetooth)
 *   Windows : hal/bluetooth/ble_win.c     (WinRT Bluetooth)
 */
#ifndef NIKON_HAL_BLUETOOTH_H
#define NIKON_HAL_BLUETOOTH_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ─── 常量 ───────────────────────────────────────────────────── */

#define HAL_BLE_MAX_DEVICES         32
#define HAL_BLE_MAX_SCAN_TIMEOUT_MS 10000

/* 错误码 */
#define HAL_BLE_OK                  0
#define HAL_BLE_ERR_NOT_FOUND       (-1)
#define HAL_BLE_ERR_PERMISSION      (-2)
#define HAL_BLE_ERR_TIMEOUT         (-3)
#define HAL_BLE_ERR_IO              (-4)
#define HAL_BLE_ERR_NOT_SUPPORTED   (-5)

/* ─── 数据结构 ───────────────────────────────────────────────── */

/** BLE 设备信息 */
typedef struct {
    char    address[18];    /**< MAC 地址 (AA:BB:CC:DD:EE:FF) */
    char    name[64];       /**< 设备名称 */
    int     rssi;           /**< 信号强度 dBm */
    bool    is_nikon;       /**< 是否识别为尼康相机 */
} BluetoothDeviceInfo;

/** BLE 扫描结果回调 */
typedef void (*ble_scan_callback)(BluetoothDeviceInfo *dev, void *user_data);

/* ─── 接口 ───────────────────────────────────────────────────── */

/**
 * 初始化 BLE 子系统。
 * @return HAL_BLE_OK; HAL_BLE_ERR_NOT_SUPPORTED 如硬件不支持
 */
int hal_ble_init(void);

/**
 * 扫描 BLE 设备 (同步, 阻塞直到超时或找到足够设备)。
 *
 * @param devices       输出缓冲区
 * @param max_count     最大设备数
 * @param timeout_ms    扫描超时 (ms)
 * @return >= 0 找到的设备数; < 0 错误码
 */
int hal_ble_scan(BluetoothDeviceInfo *devices, int max_count, int timeout_ms);

/**
 * 异步扫描 BLE 设备 (每发现一个触发回调)。
 * 调用 hal_ble_stop_scan() 停止。
 */
int hal_ble_scan_async(ble_scan_callback cb, void *user_data);

/** 停止正在进行的异步扫描。 */
void hal_ble_stop_scan(void);

/**
 * 连接 BLE 设备。
 *
 * @param address MAC 地址字符串
 * @return HAL_BLE_OK; < 0 错误码
 */
int hal_ble_connect(const char *address);

/**
 * 向相机发送 BLE 唤醒指令。
 * 用于 SnapBridge 远程唤醒场景。
 *
 * @return 实际发送字节数; < 0 错误码
 */
int hal_ble_send(const uint8_t *data, int length);

/**
 * 接收 BLE 通知数据。
 *
 * @return 接收字节数; 0=无数据; < 0 错误码
 */
int hal_ble_recv(uint8_t *buf, int max_length);

/** 断开 BLE 连接。 */
void hal_ble_disconnect(void);

/** 释放 BLE 子系统。 */
void hal_ble_shutdown(void);

#ifdef __cplusplus
}
#endif
#endif /* NIKON_HAL_BLUETOOTH_H */
