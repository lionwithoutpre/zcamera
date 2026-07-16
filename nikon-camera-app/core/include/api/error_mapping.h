/**
 * api/error_mapping.h — 错误码三层统一映射
 *
 * HAL 层 (wifi.h / bluetooth.h)  →  PTP 层 (ptp_types)  →  CameraAPI 层 (camera_api.h)
 *
 * 映射规则:
 *   HAL_WIFI_ERR_*     → CAM_ERR_WIFI / CAM_ERR_BLUETOOTH
 *   HAL_BLE_ERR_*      → CAM_ERR_BLUETOOTH
 *   PTP_RC_*           → CAM_ERR_PROTOCOL / CAM_ERR_BUSY / CAM_OK
 *   未知               → CAM_ERR_PROTOCOL
 */
#ifndef NIKON_ERROR_MAPPING_H
#define NIKON_ERROR_MAPPING_H

#include "api/camera_api.h"
#include "hal/wifi.h"
#include "hal/bluetooth.h"
#include "protocol/ptp.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline int hal_wifi_to_cam(int wifi_err) {
    switch (wifi_err) {
    case HAL_WIFI_OK:               return CAM_OK;
    case HAL_WIFI_ERR_NOT_FOUND:    return CAM_ERR_WIFI;
    case HAL_WIFI_ERR_AUTH:         return CAM_ERR_PERMISSION_DENIED;
    case HAL_WIFI_ERR_TIMEOUT:      return CAM_ERR_TIMEOUT;
    case HAL_WIFI_ERR_IO:           return CAM_ERR_WIFI;
    case HAL_WIFI_ERR_DISCONNECTED: return CAM_ERR_NOT_CONNECTED;
    default:                        return CAM_ERR_WIFI;
    }
}

static inline int hal_ble_to_cam(int ble_err) {
    switch (ble_err) {
    case HAL_BLE_OK:                return CAM_OK;
    case HAL_BLE_ERR_NOT_FOUND:     return CAM_ERR_BLUETOOTH;
    case HAL_BLE_ERR_PERMISSION:    return CAM_ERR_PERMISSION_DENIED;
    case HAL_BLE_ERR_TIMEOUT:       return CAM_ERR_TIMEOUT;
    case HAL_BLE_ERR_IO:            return CAM_ERR_BLUETOOTH;
    case HAL_BLE_ERR_NOT_SUPPORTED: return CAM_ERR_NOT_SUPPORTED;
    default:                        return CAM_ERR_BLUETOOTH;
    }
}

static inline int ptp_to_cam(int ptp_rc) {
    if (ptp_rc == (int)PTP_RC_OK) return CAM_OK;
    switch (ptp_rc) {
    case 0x2001: return CAM_ERR_NOT_SUPPORTED;
    case 0x2002: return CAM_ERR_BUSY;
    case 0x2006: return CAM_ERR_BUSY;
    case 0x200A: return CAM_ERR_INVALID_PARAM;
    case 0x200C: return CAM_ERR_NOT_SUPPORTED;
    case 0x2014: return CAM_ERR_FILE_NOT_FOUND;
    case 0x2019: return CAM_ERR_NOT_CONNECTED;
    case 0x201A: return CAM_ERR_INVALID_PARAM;
    case 0x201B: return CAM_ERR_BUSY;
    case 0xA001: return CAM_ERR_BUSY;
    case 0xA002: return CAM_ERR_NOT_SUPPORTED;
    case 0xA003: return CAM_ERR_BUSY;
    default:     return CAM_ERR_PROTOCOL;
    }
}

static inline int any_to_cam(int err, int layer) {
    switch (layer) {
    case 0: return hal_wifi_to_cam(err);
    case 1: return hal_ble_to_cam(err);
    case 2: return ptp_to_cam(err);
    default: return CAM_ERR_PROTOCOL;
    }
}

#ifdef __cplusplus
}
#endif
#endif /* NIKON_ERROR_MAPPING_H */
