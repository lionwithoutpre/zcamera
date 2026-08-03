/**
 * adapter/wifi_adapter.c — WiFi 适配器实现
 *
 * 复用 nikon_common_execute() 处理所有 PTP 命令，
 * 仅在 CONNECT/DISCONNECT 中增加 WiFi 连接管理逻辑。
 */

#include "adapter/camera_adapter.h"
#include "protocol/ptp.h"
#include "hal/wifi.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* nikon_adapter.c 导出的共享命令执行函数 */
extern CameraResult nikon_common_execute(PtpSession *session, CameraCommand *cmd);

/* ─── WiFi 适配器私有上下文 ──────────────────────────────────── */

typedef struct {
    PtpSession          *session;
    ConnectionStatus     status;
    event_handler_fn     event_handlers[8];
    void                *event_user_data[8];
    char                 ip_addr[64];
    uint16_t             port;
} NikonWiFiCtx;

/* ─── 辅助 ───────────────────────────────────────────────────── */

static CameraResult _wifi_make_error(int code, const char *msg) {
    CameraResult r;
    memset(&r, 0, sizeof(r));
    r.result_code = code;
    if (msg) strncpy(r.error_msg, msg, sizeof(r.error_msg) - 1);
    return r;
}

static CameraResult _wifi_make_ok(void) {
    CameraResult r;
    memset(&r, 0, sizeof(r));
    r.result_code = 0;
    return r;
}

/* ─── 命令执行 ───────────────────────────────────────────────── */

static CameraResult _wifi_execute(void *ctx, CameraCommand *cmd) {
    NikonWiFiCtx *c = (NikonWiFiCtx *)ctx;
    if (!c || !cmd) return _wifi_make_error(CAM_ERR_INVALID_PARAM, "null arg");

    /* CONNECT/DISCONNECT 需要管理 WiFi 连接 */
    switch (cmd->type) {

    case CAM_CMD_CONNECT: {
        if (!c->session) return _wifi_make_error(CAM_ERR_NOT_CONNECTED, "no session");

        WifiConnectionInfo wci;
        memset(&wci, 0, sizeof(wci));
        strncpy(wci.ip_address, c->ip_addr, sizeof(wci.ip_address) - 1);
        wci.port = c->port;

        int wifi_rc = hal_wifi_connect(&wci);
        if (wifi_rc != HAL_WIFI_OK) {
            c->status = STATUS_ERROR;
            return _wifi_make_error(CAM_ERR_WIFI, "WiFi connect failed");
        }

        int rc = ptp_session_open(c->session);
        if (rc != 0) {
            hal_wifi_disconnect();
            c->status = STATUS_ERROR;
            return _wifi_make_error(CAM_ERR_PROTOCOL, "OpenSession failed");
        }
        c->status = STATUS_CONNECTED;
        return _wifi_make_ok();
    }

    case CAM_CMD_DISCONNECT: {
        if (c->session) ptp_session_close(c->session);
        hal_wifi_disconnect();
        c->status = STATUS_DISCONNECTED;
        return _wifi_make_ok();
    }

    default:
        break;
    }

    /* 其他命令委托给共享 PTP 实现 */
    if (!c->session) return _wifi_make_error(CAM_ERR_NOT_CONNECTED, "no session");
    return nikon_common_execute(c->session, cmd);
}

/* ─── vtable 方法 ────────────────────────────────────────────── */

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

/* ─── 工厂函数 ───────────────────────────────────────────────── */

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
