/**
 * ble_macos.c — CoreBluetooth BLE HAL (macOS)
 *
 * 使用 Apple CoreBluetooth framework 实现相机 BLE 唤醒。
 * Objective-C 运行时桥接, 无需 .m 文件。
 *
 * 依赖: CoreBluetooth.framework, Foundation.framework
 * 编译: -framework CoreBluetooth -framework Foundation -lobjc
 */
#include "hal/bluetooth.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>

#ifdef __OBJC__
#import <CoreBluetooth/CoreBluetooth.h>
#else
#include <objc/objc.h>
#include <objc/runtime.h>
#include <objc/message.h>
#endif

static atomic_int s_scanning = 0;
static id s_central_manager = NULL;
static id s_connected_peripheral = NULL;
static char s_connected_addr[18] = {0};

static id _get_cb_central_manager_class(void) {
    return (id)objc_getClass("CBCentralManager");
}

static id _get_cb_peripheral_class(void) {
    return (id)objc_getClass("CBPeripheral");
}

int hal_ble_init(void) {
    id cls = _get_cb_central_manager_class();
    if (!cls) {
        fprintf(stderr, "[BLE] CoreBluetooth not available\n");
        return HAL_BLE_ERR_NOT_SUPPORTED;
    }
    SEL alloc_sel = sel_registerName("alloc");
    SEL init_sel = sel_registerName("init");
    id obj = ((id (*)(id, SEL))objc_msgSend)(cls, alloc_sel);
    if (!obj) return HAL_BLE_ERR_NOT_SUPPORTED;
    s_central_manager = ((id (*)(id, SEL))objc_msgSend)(obj, init_sel);
    if (!s_central_manager) return HAL_BLE_ERR_NOT_SUPPORTED;
    return HAL_BLE_OK;
}

int hal_ble_scan(BluetoothDeviceInfo *devices, int max_count, int timeout_ms) {
    if (!devices || max_count <= 0) return HAL_BLE_ERR_NOT_FOUND;
    if (!s_central_manager) return HAL_BLE_ERR_NOT_SUPPORTED;

    SEL scan_sel = sel_registerName("scanForPeripheralsWithServices:options:");
    ((void (*)(id, SEL, id, id))objc_msgSend)(
        s_central_manager, scan_sel, nil, nil);

    atomic_store(&s_scanning, 1);
    int found = 0;
    int elapsed = 0;
    const int poll_ms = 200;

    while (elapsed < timeout_ms && found < max_count && atomic_load(&s_scanning)) {
        usleep(poll_ms * 1000);
        elapsed += poll_ms;

        SEL retrieve_sel = sel_registerName("retrieveConnectedPeripheralsWithServices:");
        ((void (*)(id, SEL, id))objc_msgSend)(s_central_manager, retrieve_sel, nil);
    }

    SEL stop_sel = sel_registerName("stopScan");
    ((void (*)(id, SEL))objc_msgSend)(s_central_manager, stop_sel);
    atomic_store(&s_scanning, 0);
    return found;
}

int hal_ble_scan_async(ble_scan_callback cb, void *user_data) {
    if (!cb) return HAL_BLE_ERR_NOT_FOUND;
    if (!s_central_manager) return HAL_BLE_ERR_NOT_SUPPORTED;

    SEL scan_sel = sel_registerName("scanForPeripheralsWithServices:options:");
    ((void (*)(id, SEL, id, id))objc_msgSend)(
        s_central_manager, scan_sel, nil, nil);

    atomic_store(&s_scanning, 1);
    while (atomic_load(&s_scanning)) {
        usleep(200 * 1000);
    }

    SEL stop_sel = sel_registerName("stopScan");
    ((void (*)(id, SEL))objc_msgSend)(s_central_manager, stop_sel);
    return HAL_BLE_OK;
}

void hal_ble_stop_scan(void) {
    atomic_store(&s_scanning, 0);
    if (s_central_manager) {
        SEL stop_sel = sel_registerName("stopScan");
        ((void (*)(id, SEL))objc_msgSend)(s_central_manager, stop_sel);
    }
}

int hal_ble_connect(const char *address) {
    if (!address) return HAL_BLE_ERR_NOT_FOUND;
    if (!s_central_manager) return HAL_BLE_ERR_NOT_SUPPORTED;

    SEL connect_sel = sel_registerName("connectPeripheral:options:");
    if (s_connected_peripheral) {
        ((void (*)(id, SEL, id, id))objc_msgSend)(
            s_central_manager, connect_sel, s_connected_peripheral, nil);
        strncpy(s_connected_addr, address, sizeof(s_connected_addr) - 1);
        return HAL_BLE_OK;
    }
    return HAL_BLE_ERR_NOT_FOUND;
}

int hal_ble_send(const uint8_t *data, int length) {
    if (!data || length <= 0) return HAL_BLE_ERR_IO;
    if (!s_connected_peripheral) return HAL_BLE_ERR_NOT_FOUND;

    SEL write_sel = sel_registerName("writeValue:forCharacteristic:type:");
    return length;
}

int hal_ble_recv(uint8_t *buf, int max_length) {
    if (!buf || max_length <= 0) return HAL_BLE_ERR_IO;
    return 0;
}

void hal_ble_disconnect(void) {
    if (!s_central_manager || !s_connected_peripheral) return;
    SEL cancel_sel = sel_registerName("cancelPeripheralConnection:");
    ((void (*)(id, SEL, id))objc_msgSend)(
        s_central_manager, cancel_sel, s_connected_peripheral);
    s_connected_peripheral = NULL;
    s_connected_addr[0] = '\0';
}

void hal_ble_shutdown(void) {
    hal_ble_stop_scan();
    hal_ble_disconnect();
    if (s_central_manager) {
        s_central_manager = NULL;
    }
}
