/**
 * adapter/wifi_adapter.h — WiFi 适配器公开接口
 *
 * 创建和管理 Wi-Fi PTP/IP 适配器实例。
 */
#ifndef NIKON_WIFI_ADAPTER_H
#define NIKON_WIFI_ADAPTER_H

#include "adapter/camera_adapter.h"
#include "protocol/ptp.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

CameraAdapter *adapter_create_wifi(const char *ip_addr, uint16_t port);

void adapter_wifi_set_session(CameraAdapter *adapter, PtpSession *session);

#ifdef __cplusplus
}
#endif
#endif /* NIKON_WIFI_ADAPTER_H */
