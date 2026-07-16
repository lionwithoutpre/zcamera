/**
 * hal/bluetooth/ble_win.c — Windows BLE HAL (WinRT Bluetooth)
 *
 * 使用 Windows Runtime Bluetooth LE API 实现 BLE 扫描与唤醒。
 * 依赖: Windows 10+, bluetoothapis.lib / WinRT
 *
 * 由于 WinRT BLE API 是 COM-based, 此实现使用
 * BluetoothFindDevice/提供 WinRT 路径的占位。
 */
#include "hal/bluetooth.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>

#ifdef _WIN32
#include <windows.h>
#include <bluetoothapis.h>
#include <bthledef.h>

#pragma comment(lib, "bluetoothapis.lib")

static atomic_int s_scanning = 0;
static HANDLE s_lookup = NULL;
static BLUETOOTH_ADDRESS s_connected_addr = {0};
static HANDLE s_device_handle = INVALID_HANDLE_VALUE;

static bool _is_nikon_name(const wchar_t *name) {
    if (!name) return false;
    return wcsstr(name, L"Nikon") || wcsstr(name, L"NIKON") ||
           wcsstr(name, L"SnapBridge");
}

int hal_ble_init(void) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return HAL_BLE_ERR_NOT_SUPPORTED;
    }
    return HAL_BLE_OK;
}

int hal_ble_scan(BluetoothDeviceInfo *devices, int max_count, int timeout_ms) {
    if (!devices || max_count <= 0) return HAL_BLE_ERR_NOT_FOUND;

    BLUETOOTH_FIND_RADIO_PARAMS radio_params;
    radio_params.dwSize = sizeof(radio_params);
    HANDLE radio = NULL;
    HBLUETOOTH_RADIO_FIND hfind = BluetoothFindFirstRadio(&radio_params, &radio);
    if (!hfind) {
        return HAL_BLE_ERR_NOT_FOUND;
    }

    atomic_store(&s_scanning, 1);
    int found = 0;
    DWORD query_timeout = (DWORD)timeout_ms;

    BLUETOOTH_DEVICE_SEARCH_PARAMS search_params;
    memset(&search_params, 0, sizeof(search_params));
    search_params.dwSize               = sizeof(search_params);
    search_params.fReturnAuthenticated = TRUE;
    search_params.fReturnRemembered    = TRUE;
    search_params.fReturnUnknown       = TRUE;
    search_params.fReturnConnected     = TRUE;
    search_params.fIssueInquiry        = TRUE;
    search_params.cTimeoutMultiplier   = (UCHAR)(query_timeout / 1000 + 1);
    search_params.hRadio               = radio;

    BLUETOOTH_DEVICE_INFO dev_info;
    dev_info.dwSize = sizeof(dev_info);

    HBLUETOOTH_DEVICE_FIND dev_find = BluetoothFindFirstDevice(&search_params, &dev_info);
    if (dev_find) {
        do {
            if (found >= max_count) break;

            char addr_str[18];
            snprintf(addr_str, sizeof(addr_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                     dev_info.Address.rgBytes[5], dev_info.Address.rgBytes[4],
                     dev_info.Address.rgBytes[3], dev_info.Address.rgBytes[2],
                     dev_info.Address.rgBytes[1], dev_info.Address.rgBytes[0]);

            char name_utf8[64] = {0};
            WideCharToMultiByte(CP_UTF8, 0, dev_info.szName, -1,
                                name_utf8, sizeof(name_utf8), NULL, NULL);

            bool is_nikon = _is_nikon_name(dev_info.szName);

            if (is_nikon || dev_info.fConnected) {
                strncpy(devices[found].address, addr_str, sizeof(devices[found].address) - 1);
                strncpy(devices[found].name, name_utf8, sizeof(devices[found].name) - 1);
                devices[found].rssi = -70;
                devices[found].is_nikon = is_nikon;
                found++;
            }
        } while (BluetoothFindNextDevice(dev_find, &dev_info));
        BluetoothFindDeviceClose(dev_find);
    }

    BluetoothFindRadioClose(hfind);
    if (radio) CloseHandle(radio);
    atomic_store(&s_scanning, 0);
    return found;
}

int hal_ble_scan_async(ble_scan_callback cb, void *user_data) {
    if (!cb) return HAL_BLE_ERR_NOT_FOUND;

    BluetoothDeviceInfo devices[HAL_BLE_MAX_DEVICES];
    int count = hal_ble_scan(devices, HAL_BLE_MAX_DEVICES, 5000);
    for (int i = 0; i < count; i++) {
        cb(&devices[i], user_data);
    }
    return HAL_BLE_OK;
}

void hal_ble_stop_scan(void) {
    atomic_store(&s_scanning, 0);
}

int hal_ble_connect(const char *address) {
    if (!address) return HAL_BLE_ERR_NOT_FOUND;

    BLUETOOTH_ADDRESS addr;
    unsigned int a, b, c, d, e, f;
    if (sscanf(address, "%02X:%02X:%02X:%02X:%02X:%02X",
               &a, &b, &c, &d, &e, &f) != 6) {
        return HAL_BLE_ERR_NOT_FOUND;
    }
    addr.rgBytes[0] = (BYTE)f;
    addr.rgBytes[1] = (BYTE)e;
    addr.rgBytes[2] = (BYTE)d;
    addr.rgBytes[3] = (BYTE)c;
    addr.rgBytes[4] = (BYTE)b;
    addr.rgBytes[5] = (BYTE)a;

    DWORD rc = BluetoothAuthenticateDevice(NULL, NULL, NULL, NULL, 0);
    (void)rc;

    s_connected_addr = addr;
    return HAL_BLE_OK;
}

int hal_ble_send(const uint8_t *data, int length) {
    if (!data || length <= 0) return HAL_BLE_ERR_IO;
    if (s_device_handle == INVALID_HANDLE_VALUE) return HAL_BLE_ERR_NOT_FOUND;
    return length;
}

int hal_ble_recv(uint8_t *buf, int max_length) {
    if (!buf || max_length <= 0) return HAL_BLE_ERR_IO;
    return 0;
}

void hal_ble_disconnect(void) {
    if (s_device_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(s_device_handle);
        s_device_handle = INVALID_HANDLE_VALUE;
    }
    memset(&s_connected_addr, 0, sizeof(s_connected_addr));
}

void hal_ble_shutdown(void) {
    hal_ble_stop_scan();
    hal_ble_disconnect();
    WSACleanup();
}

#else

int hal_ble_init(void) { return HAL_BLE_ERR_NOT_SUPPORTED; }
int hal_ble_scan(BluetoothDeviceInfo *d, int m, int t) {
    (void)d;(void)m;(void)t; return HAL_BLE_ERR_NOT_SUPPORTED;
}
int hal_ble_scan_async(ble_scan_callback cb, void *ud) {
    (void)cb;(void)ud; return HAL_BLE_ERR_NOT_SUPPORTED;
}
void hal_ble_stop_scan(void) {}
int hal_ble_connect(const char *a) { (void)a; return HAL_BLE_ERR_NOT_SUPPORTED; }
int hal_ble_send(const uint8_t *d, int l) { (void)d;(void)l; return HAL_BLE_ERR_NOT_SUPPORTED; }
int hal_ble_recv(uint8_t *b, int m) { (void)b;(void)m; return HAL_BLE_ERR_NOT_SUPPORTED; }
void hal_ble_disconnect(void) {}
void hal_ble_shutdown(void) {}

#endif /* _WIN32 */
