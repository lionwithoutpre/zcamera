/**
 * adapter/nikon_adapter.h — 尼康 PTP/USB 适配器公开接口
 *
 * 创建 USB 直连 PTP 适配器实例。
 */
#ifndef NIKON_NIKON_ADAPTER_H
#define NIKON_NIKON_ADAPTER_H

#include "adapter/camera_adapter.h"
#include "protocol/ptp.h"

#ifdef __cplusplus
extern "C" {
#endif

CameraAdapter *adapter_create_ptp(PtpSession *session);

#ifdef __cplusplus
}
#endif
#endif /* NIKON_NIKON_ADAPTER_H */
