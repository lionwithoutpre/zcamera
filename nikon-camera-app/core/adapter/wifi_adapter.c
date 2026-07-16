/**
 * adapter/wifi_adapter.c — WiFi 适配器工厂
 *
 * 复用 PTP 适配器 vtable，差异仅在于传输层是 socket 而非 USB。
 * 实际命令执行仍走同一套 ptp_exec() 路径。
 * WiFi 模式下 PtpSession.transport = PTP_TRANSPORT_WIFI，
 * ptp_exec 内部根据 transport 类型选择 hal_wifi_send/recv。
 */

#include "adapter/camera_adapter.h"
#include "protocol/ptp.h"
#include "hal/wifi.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

extern const CameraAdapterVtable kNikonPTPVtable;

typedef struct {
    PtpSession          *session;
    ConnectionStatus     status;
    event_handler_fn     event_handlers[8];
    void                *event_user_data[8];
    char                 ip_addr[64];
    uint16_t             port;
} NikonWiFiCtx;

static const CommandMapping *_find_mapping(CameraCommandType type) {
    extern const CommandMapping kNikonCommandMappings[];
    extern const int kNikonCommandMappingCount;
    for (int i = 0; i < kNikonCommandMappingCount; i++) {
        if (kNikonCommandMappings[i].cmd_type == type)
            return &kNikonCommandMappings[i];
    }
    return NULL;
}

static CameraResult _make_error(int code, const char *msg) {
    CameraResult r;
    memset(&r, 0, sizeof(r));
    r.result_code = code;
    if (msg) strncpy(r.error_msg, msg, sizeof(r.error_msg) - 1);
    return r;
}

static CameraResult _make_ok(void *data, uint32_t size) {
    CameraResult r;
    memset(&r, 0, sizeof(r));
    r.result_code = 0;
    r.data        = data;
    r.data_size   = size;
    return r;
}

static CameraResult _wifi_execute(void *ctx, CameraCommand *cmd) {
    NikonWiFiCtx *c = (NikonWiFiCtx *)ctx;
    if (!c || !cmd) return _make_error(CAM_ERR_INVALID_PARAM, "null arg");

    const CommandMapping *m = _find_mapping(cmd->type);
    if (!m) return _make_error(CAM_ERR_NOT_SUPPORTED, "command not mapped");

    if (m->needs_session && c->session->state < SESSION_OPEN) {
        return _make_error(CAM_ERR_NOT_CONNECTED, "session not open");
    }

    switch (cmd->type) {

    case CAM_CMD_CONNECT: {
        WifiConnectionInfo wci;
        memset(&wci, 0, sizeof(wci));
        strncpy(wci.ip_address, c->ip_addr, sizeof(wci.ip_address) - 1);
        wci.port = c->port;

        int wifi_rc = hal_wifi_connect(&wci);
        if (wifi_rc != HAL_WIFI_OK) {
            c->status = STATUS_ERROR;
            return _make_error(CAM_ERR_WIFI, "WiFi connect failed");
        }

        int rc = ptp_session_open(c->session);
        if (rc != 0) {
            hal_wifi_disconnect();
            c->status = STATUS_ERROR;
            return _make_error(CAM_ERR_PROTOCOL, "OpenSession failed");
        }
        c->status = STATUS_CONNECTED;
        return _make_ok(NULL, 0);
    }

    case CAM_CMD_DISCONNECT: {
        ptp_session_close(c->session);
        hal_wifi_disconnect();
        c->status = STATUS_DISCONNECTED;
        return _make_ok(NULL, 0);
    }

    case CAM_CMD_CAPTURE: {
        uint32_t out_len = 0;
        int rc = ptp_exec(c->session,
                          (uint16_t)m->ptp_opcode,
                          NULL, 0, NULL, 0, NULL, 0, &out_len);
        if (rc != (int)PTP_RC_OK)
            return _make_error(CAM_ERR_BUSY, "capture failed");
        return _make_ok(NULL, 0);
    }

    case CAM_CMD_START_LIVEVIEW: {
        uint32_t out_len = 0;
        int rc = ptp_exec(c->session,
                          (uint16_t)NIKON_OC_StartLiveView,
                          NULL, 0, NULL, 0, NULL, 0, &out_len);
        return rc == (int)PTP_RC_OK
            ? _make_ok(NULL, 0)
            : _make_error(CAM_ERR_PROTOCOL, "StartLiveView failed");
    }

    case CAM_CMD_STOP_LIVEVIEW: {
        uint32_t out_len = 0;
        ptp_exec(c->session, (uint16_t)NIKON_OC_EndLiveView,
                 NULL, 0, NULL, 0, NULL, 0, &out_len);
        return _make_ok(NULL, 0);
    }

    case CAM_CMD_SET_PROPERTY: {
        int rc = ptp_set_device_prop(c->session,
                                     cmd->params.set_property.property_id,
                                     cmd->params.set_property.value);
        return rc == (int)PTP_RC_OK
            ? _make_ok(NULL, 0)
            : _make_error(CAM_ERR_PROTOCOL, "SetDevicePropValue failed");
    }

    case CAM_CMD_GET_PROPERTY: {
        uint32_t *val = (uint32_t *)malloc(sizeof(uint32_t));
        if (!val) return _make_error(CAM_ERR_OUT_OF_MEMORY, "oom");
        int rc = ptp_get_device_prop(c->session,
                                     cmd->params.get_property.property_id,
                                     val);
        if (rc != (int)PTP_RC_OK) {
            free(val);
            return _make_error(CAM_ERR_PROTOCOL, "GetDevicePropValue failed");
        }
        return _make_ok(val, sizeof(uint32_t));
    }

    case CAM_CMD_EVENT_POLL: {
        uint32_t *events = (uint32_t *)malloc(32 * sizeof(uint32_t));
        if (!events) return _make_error(CAM_ERR_OUT_OF_MEMORY, "oom");
        int count = ptp_get_events(c->session, events, 32);
        if (count < 0) {
            free(events);
            return _make_error(CAM_ERR_PROTOCOL, "GetEvent failed");
        }
        return _make_ok(events, (uint32_t)(count * sizeof(uint32_t)));
    }

    case CAM_CMD_GET_LIVEVIEW: {
        #define WIFI_LV_BUF_SZ  (512 * 1024)
        uint8_t *frame = (uint8_t *)malloc(WIFI_LV_BUF_SZ);
        if (!frame) return _make_error(CAM_ERR_OUT_OF_MEMORY, "oom");
        uint32_t out_len = 0;
        int rc = ptp_exec(c->session,
                          (uint16_t)NIKON_OC_GetLiveViewImage,
                          NULL, 0, NULL, 0,
                          frame, WIFI_LV_BUF_SZ, &out_len);
        if (rc != (int)PTP_RC_OK || out_len == 0) {
            free(frame);
            return _make_error(CAM_ERR_PROTOCOL, "GetLiveViewImage failed");
        }
        return _make_ok(frame, out_len);
    }

    case CAM_CMD_DEVICE_INFO: {
        #define WIFI_DEVINFO_BUF_SZ  1024
        uint8_t *info = (uint8_t *)malloc(WIFI_DEVINFO_BUF_SZ);
        if (!info) return _make_error(CAM_ERR_OUT_OF_MEMORY, "oom");
        uint32_t out_len = 0;
        int rc = ptp_exec(c->session,
                          (uint16_t)PTP_OC_GetDeviceInfo,
                          NULL, 0, NULL, 0,
                          info, WIFI_DEVINFO_BUF_SZ, &out_len);
        if (rc != (int)PTP_RC_OK || out_len == 0) {
            free(info);
            return _make_error(CAM_ERR_PROTOCOL, "GetDeviceInfo failed");
        }
        return _make_ok(info, out_len);
    }

    case CAM_CMD_LIST_FILES: {
        uint32_t storage_id = cmd->params.list_files.storage_id;
        if (storage_id == 0) storage_id = 0xFFFFFFFF;
        uint32_t lf_params[5] = { storage_id, 0xFFFFFFFF, 0, 0, 0 };
        #define WIFI_HANDLE_BUF_SZ  (64 * 1024)
        uint8_t *hb = (uint8_t *)malloc(WIFI_HANDLE_BUF_SZ);
        if (!hb) return _make_error(CAM_ERR_OUT_OF_MEMORY, "oom");
        uint32_t out_len = 0;
        int rc = ptp_exec(c->session,
                          (uint16_t)PTP_OC_GetObjectHandles,
                          lf_params, 3, NULL, 0,
                          hb, WIFI_HANDLE_BUF_SZ, &out_len);
        if (rc != (int)PTP_RC_OK || out_len < 4) {
            free(hb);
            return _make_error(CAM_ERR_FILE_NOT_FOUND, "GetObjectHandles failed");
        }
        uint32_t handle_count = *(uint32_t *)hb;
        uint32_t data_bytes = 4 + handle_count * sizeof(uint32_t);
        if (data_bytes > out_len) data_bytes = out_len;
        uint8_t *result = (uint8_t *)malloc(data_bytes);
        if (!result) { free(hb); return _make_error(CAM_ERR_OUT_OF_MEMORY, "oom"); }
        memcpy(result, hb, data_bytes);
        free(hb);
        return _make_ok(result, data_bytes);
    }

    case CAM_CMD_GET_FILE: {
        uint32_t obj_h = cmd->params.file.object_handle;
        uint32_t gf_params[5] = { obj_h, 0, 0, 0, 0 };
        uint8_t info_buf[512];
        uint32_t info_len = 0;
        int rc = ptp_exec(c->session,
                          (uint16_t)PTP_OC_GetObjectInfo,
                          gf_params, 1, NULL, 0,
                          info_buf, sizeof(info_buf), &info_len);
        if (rc != (int)PTP_RC_OK)
            return _make_error(CAM_ERR_FILE_NOT_FOUND, "GetObjectInfo failed");

        #define WIFI_GET_FILE_MAX_BUF  (64 * 1024 * 1024)
        uint32_t alloc_sz = WIFI_GET_FILE_MAX_BUF;
        uint8_t *fb = (uint8_t *)malloc(alloc_sz);
        if (!fb) return _make_error(CAM_ERR_OUT_OF_MEMORY, "file buffer oom");
        uint32_t flen = 0;
        rc = ptp_exec(c->session,
                      (uint16_t)PTP_OC_GetObject,
                      gf_params, 1, NULL, 0,
                      fb, alloc_sz, &flen);
        if (rc != (int)PTP_RC_OK || flen == 0) {
            free(fb);
            return _make_error(CAM_ERR_TRANSFER_FAILED, "GetObject failed");
        }
        uint8_t *trimmed = (uint8_t *)realloc(fb, flen);
        return _make_ok(trimmed ? trimmed : fb, flen);
    }

    case CAM_CMD_GET_THUMBNAIL: {
        uint32_t obj_h = cmd->params.file.object_handle;
        uint32_t gt_params[5] = { obj_h, 0, 0, 0, 0 };
        #define WIFI_THUMB_BUF_SZ  (256 * 1024)
        uint8_t *tb = (uint8_t *)malloc(WIFI_THUMB_BUF_SZ);
        if (!tb) return _make_error(CAM_ERR_OUT_OF_MEMORY, "oom");
        uint32_t out_len = 0;
        int rc = ptp_exec(c->session,
                          (uint16_t)PTP_OC_GetThumb,
                          gt_params, 1, NULL, 0,
                          tb, WIFI_THUMB_BUF_SZ, &out_len);
        if (rc != (int)PTP_RC_OK || out_len == 0) {
            free(tb);
            return _make_error(CAM_ERR_FILE_NOT_FOUND, "no thumbnail");
        }
        return _make_ok(tb, out_len);
    }

    case CAM_CMD_DELETE_FILE: {
        uint32_t obj_h = cmd->params.file.object_handle;
        uint32_t del_params[5] = { obj_h, 0, 0, 0, 0 };
        uint32_t out_len = 0;
        int rc = ptp_exec(c->session,
                          (uint16_t)PTP_OC_DeleteObject,
                          del_params, 1, NULL, 0, NULL, 0, &out_len);
        return rc == (int)PTP_RC_OK
            ? _make_ok(NULL, 0)
            : _make_error(CAM_ERR_FILE_NOT_FOUND, "DeleteObject failed");
    }

    default:
        return _make_error(CAM_ERR_NOT_SUPPORTED, "cmd not implemented");
    }
}

static ConnectionStatus _wifi_get_status(void *ctx) {
    NikonWiFiCtx *c = (NikonWiFiCtx *)ctx;
    return c ? c->status : STATUS_DISCONNECTED;
}

static void _wifi_register_event(void *ctx, EventType type,
                                  event_handler_fn handler, void *user_data)
{
    NikonWiFiCtx *c = (NikonWiFiCtx *)ctx;
    if (!c || type >= 8) return;
    c->event_handlers[type]   = handler;
    c->event_user_data[type]  = user_data;
}

static void _wifi_destroy(void *ctx) {
    NikonWiFiCtx *c = (NikonWiFiCtx *)ctx;
    if (!c) return;
    free(c);
}

static const CameraAdapterVtable kNikonWiFiVtable = {
    .execute        = _wifi_execute,
    .get_status     = _wifi_get_status,
    .register_event = _wifi_register_event,
    .destroy        = _wifi_destroy,
};

CameraAdapter *adapter_create_wifi(const char *ip_addr, uint16_t port) {
    if (!ip_addr) return NULL;

    NikonWiFiCtx *ctx = (NikonWiFiCtx *)calloc(1, sizeof(NikonWiFiCtx));
    if (!ctx) return NULL;
    strncpy(ctx->ip_addr, ip_addr, sizeof(ctx->ip_addr) - 1);
    ctx->port    = (port > 0) ? port : NIKON_WIFI_DEFAULT_PORT;
    ctx->status  = STATUS_DISCONNECTED;

    CameraAdapter *adapter = (CameraAdapter *)malloc(sizeof(CameraAdapter));
    if (!adapter) {
        free(ctx);
        return NULL;
    }
    adapter->vtable = &kNikonWiFiVtable;
    adapter->ctx    = ctx;
    return adapter;
}

void adapter_wifi_set_session(CameraAdapter *adapter, PtpSession *session) {
    if (!adapter || !session) return;
    NikonWiFiCtx *ctx = (NikonWiFiCtx *)adapter->ctx;
    if (ctx) ctx->session = session;
}
