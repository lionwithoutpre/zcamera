/**
 * adapter/nikon_adapter.c — 尼康 PTP/USB 适配器实现
 *
 * 将 CameraCommand 翻译为具体的 PTP 操作，通过 PtpSession 执行。
 */
#include "adapter/camera_adapter.h"
#include "protocol/ptp.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ─── 适配器私有上下文 ───────────────────────────────────────── */

typedef struct {
    PtpSession          *session;
    ConnectionStatus     status;
    event_handler_fn     event_handlers[8];
    void                *event_user_data[8];
} NikonPTPCtx;

/* ─── 内部辅助 ───────────────────────────────────────────────── */

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

/* ─── 命令分发 ───────────────────────────────────────────────── */

static CameraResult _nikon_ptp_execute(void *ctx, CameraCommand *cmd) {
    NikonPTPCtx *c = (NikonPTPCtx *)ctx;
    if (!c || !cmd) return _make_error(CAM_ERR_INVALID_PARAM, "null arg");

    const CommandMapping *m = _find_mapping(cmd->type);
    if (!m) return _make_error(CAM_ERR_NOT_SUPPORTED, "command not mapped");

    if (m->needs_session && c->session->state < SESSION_OPEN) {
        return _make_error(CAM_ERR_NOT_CONNECTED, "session not open");
    }

    switch (cmd->type) {

    case CAM_CMD_CONNECT: {
        int rc = ptp_session_open(c->session);
        if (rc != 0) {
            c->status = STATUS_ERROR;
            return _make_error(CAM_ERR_PROTOCOL, "OpenSession failed");
        }
        c->status = STATUS_CONNECTED;
        return _make_ok(NULL, 0);
    }

    case CAM_CMD_DISCONNECT: {
        ptp_session_close(c->session);
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

    case CAM_CMD_SET_PICTCTRL: {
        /* PTP 0x90CD: data payload = PictureControl struct */
        uint32_t out_len = 0;
        int rc = ptp_exec(c->session,
                          (uint16_t)NIKON_OC_SetPictCtrlData,
                          NULL, 0,
                          (uint8_t *)&cmd->params.pictctrl,
                          sizeof(PictureControl),
                          NULL, 0, &out_len);
        return rc == (int)PTP_RC_OK
            ? _make_ok(NULL, 0)
            : _make_error(CAM_ERR_PROTOCOL, "SetPictCtrlData failed");
    }

    case CAM_CMD_GET_PICTCTRL: {
        PictureControl *pc = (PictureControl *)malloc(sizeof(PictureControl));
        if (!pc) return _make_error(CAM_ERR_OUT_OF_MEMORY, "oom");
        uint32_t out_len = 0;
        int rc = ptp_exec(c->session,
                          (uint16_t)NIKON_OC_GetPictCtrlData,
                          NULL, 0, NULL, 0,
                          (uint8_t *)pc, sizeof(PictureControl), &out_len);
        if (rc != (int)PTP_RC_OK) {
            free(pc);
            return _make_error(CAM_ERR_PROTOCOL, "GetPictCtrlData failed");
        }
        return _make_ok(pc, sizeof(PictureControl));
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

    /* ── 连拍 / 停止连拍 ──────────────────────────────────── */

    case CAM_CMD_CAPTURE_BURST: {
        /* 发送 Capture 命令, 相机进入连拍模式由参数控制 */
        uint32_t burst_params[5] = {
            (uint32_t)cmd->params.capture.count,
            (uint32_t)cmd->params.capture.interval_ms,
            0, 0, 0
        };
        uint32_t out_len = 0;
        int rc = ptp_exec(c->session,
                          (uint16_t)m->ptp_opcode,
                          burst_params, 2,
                          NULL, 0, NULL, 0, &out_len);
        if (rc != (int)PTP_RC_OK)
            return _make_error(CAM_ERR_BUSY, "burst capture failed");
        return _make_ok(NULL, 0);
    }

    case CAM_CMD_STOP_BURST:
        /* 停止连拍: 发送 ShutterRelease 结束快门保持 */
        {
            uint32_t out_len = 0;
            ptp_exec(c->session, (uint16_t)NIKON_OC_ShutterRelease,
                     NULL, 0, NULL, 0, NULL, 0, &out_len);
            return _make_ok(NULL, 0);
        }

    case CAM_CMD_AUTOFOCUS: {
        uint32_t out_len = 0;
        int rc = ptp_exec(c->session,
                          (uint16_t)NIKON_OC_AutoFocus,
                          NULL, 0, NULL, 0, NULL, 0, &out_len);
        return rc == (int)PTP_RC_OK
            ? _make_ok(NULL, 0)
            : _make_error(CAM_ERR_BUSY, "AutoFocus failed");
    }

    case CAM_CMD_GET_LIVEVIEW: {
        /* 取一帧实时取景 JPEG */
        #define LV_BUF_SZ  (512 * 1024)   /* 512KB 足够一帧 JPEG */
        uint8_t *frame = (uint8_t *)malloc(LV_BUF_SZ);
        if (!frame) return _make_error(CAM_ERR_OUT_OF_MEMORY, "oom");
        uint32_t out_len = 0;
        int rc = ptp_exec(c->session,
                          (uint16_t)NIKON_OC_GetLiveViewImage,
                          NULL, 0, NULL, 0,
                          frame, LV_BUF_SZ, &out_len);
        if (rc != (int)PTP_RC_OK || out_len == 0) {
            free(frame);
            return _make_error(CAM_ERR_PROTOCOL, "GetLiveViewImage failed");
        }
        return _make_ok(frame, out_len);
    }

    /* ── 设备信息 ─────────────────────────────────────────── */

    case CAM_CMD_DEVICE_INFO: {
        #define DEVINFO_BUF_SZ  1024
        uint8_t *info = (uint8_t *)malloc(DEVINFO_BUF_SZ);
        if (!info) return _make_error(CAM_ERR_OUT_OF_MEMORY, "oom");
        uint32_t out_len = 0;
        int rc = ptp_exec(c->session,
                          (uint16_t)PTP_OC_GetDeviceInfo,
                          NULL, 0, NULL, 0,
                          info, DEVINFO_BUF_SZ, &out_len);
        if (rc != (int)PTP_RC_OK || out_len == 0) {
            free(info);
            return _make_error(CAM_ERR_PROTOCOL, "GetDeviceInfo failed");
        }
        return _make_ok(info, out_len);
    }

    /* ── 文件操作 ─────────────────────────────────────────── */

    case CAM_CMD_LIST_FILES: {
        /* Step 1: 获取 object handle 列表 */
        uint32_t storage_id = cmd->params.list_files.storage_id;
        if (storage_id == 0) storage_id = 0xFFFFFFFF;   /* 全部存储 */

        uint32_t lf_params[5] = { storage_id, 0xFFFFFFFF, 0, 0, 0 };
        #define HANDLE_BUF_SZ  (64 * 1024)  /* 64KB ≈ ~16000 handles */
        uint8_t *hb = (uint8_t *)malloc(HANDLE_BUF_SZ);
        if (!hb) return _make_error(CAM_ERR_OUT_OF_MEMORY, "oom");

        uint32_t out_len = 0;
        int rc = ptp_exec(c->session,
                          (uint16_t)PTP_OC_GetObjectHandles,
                          lf_params, 3,
                          NULL, 0,
                          hb, HANDLE_BUF_SZ, &out_len);
        if (rc != (int)PTP_RC_OK || out_len < 4) {
            free(hb);
            return _make_error(CAM_ERR_FILE_NOT_FOUND, "GetObjectHandles failed");
        }

        /* 返回格式: [uint32_t count][uint32_t handle]... */
        uint32_t handle_count = *(uint32_t *)hb;
        uint32_t data_bytes  = 4 + handle_count * sizeof(uint32_t);
        if (data_bytes > out_len) data_bytes = out_len;

        /* 重新分配精确大小的 buffer 返回给调用者 */
        uint8_t *result = (uint8_t *)malloc(data_bytes);
        if (!result) {
            free(hb);
            return _make_error(CAM_ERR_OUT_OF_MEMORY, "oom");
        }
        memcpy(result, hb, data_bytes);
        free(hb);
        return _make_ok(result, data_bytes);
    }

    case CAM_CMD_GET_FILE: {
        uint32_t obj_h = cmd->params.file.object_handle;
        uint32_t gf_params[5] = { obj_h, 0, 0, 0, 0 };

        /* 大文件缓冲: 先获取 ObjectInfo 得知文件大小, 再分块接收 */
        /* Step 1: 获取对象信息 (文件大小) */
        uint8_t  info_buf[512];
        uint32_t info_len = 0;
        int rc = ptp_exec(c->session,
                          (uint16_t)PTP_OC_GetObjectInfo,
                          gf_params, 1,
                          NULL, 0,
                          info_buf, sizeof(info_buf), &info_len);
        if (rc != (int)PTP_RC_OK) {
            return _make_error(CAM_ERR_FILE_NOT_FOUND, "GetObjectInfo failed");
        }

        /* Step 2: 获取文件数据 (上限 64MB, 超出由 transfer engine 处理) */
        #define GET_FILE_MAX_BUF  (64 * 1024 * 1024)
        uint32_t alloc_sz = GET_FILE_MAX_BUF;
        uint8_t *fb = (uint8_t *)malloc(alloc_sz);
        if (!fb) return _make_error(CAM_ERR_OUT_OF_MEMORY, "file buffer oom");

        uint32_t flen = 0;
        rc = ptp_exec(c->session,
                      (uint16_t)PTP_OC_GetObject,
                      gf_params, 1,
                      NULL, 0,
                      fb, alloc_sz, &flen);
        if (rc != (int)PTP_RC_OK || flen == 0) {
            free(fb);
            return _make_error(CAM_ERR_TRANSFER_FAILED, "GetObject failed");
        }

        /* 缩小到实际尺寸再返回 */
        uint8_t *trimmed = (uint8_t *)realloc(fb, flen);
        return _make_ok(trimmed ? trimmed : fb, flen);
    }

    case CAM_CMD_GET_THUMBNAIL: {
        uint32_t obj_h = cmd->params.file.object_handle;
        uint32_t gt_params[5] = { obj_h, 0, 0, 0, 0 };

        #define THUMB_BUF_SZ  (256 * 1024)   /* 256KB max */
        uint8_t *tb = (uint8_t *)malloc(THUMB_BUF_SZ);
        if (!tb) return _make_error(CAM_ERR_OUT_OF_MEMORY, "oom");

        uint32_t out_len = 0;
        int rc = ptp_exec(c->session,
                          (uint16_t)PTP_OC_GetThumb,
                          gt_params, 1,
                          NULL, 0,
                          tb, THUMB_BUF_SZ, &out_len);
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
                          del_params, 1,
                          NULL, 0,
                          NULL, 0, &out_len);
        return rc == (int)PTP_RC_OK
            ? _make_ok(NULL, 0)
            : _make_error(CAM_ERR_FILE_NOT_FOUND, "DeleteObject failed");
    }

    default:
        return _make_error(CAM_ERR_NOT_SUPPORTED, "cmd not implemented");
    }
}

static ConnectionStatus _nikon_ptp_get_status(void *ctx) {
    NikonPTPCtx *c = (NikonPTPCtx *)ctx;
    return c ? c->status : STATUS_DISCONNECTED;
}

static void _nikon_ptp_register_event(void *ctx, EventType type,
                                       event_handler_fn handler, void *user_data)
{
    NikonPTPCtx *c = (NikonPTPCtx *)ctx;
    if (!c || type >= 8) return;
    c->event_handlers[type]   = handler;
    c->event_user_data[type]  = user_data;
}

static void _nikon_ptp_destroy(void *ctx) {
    NikonPTPCtx *c = (NikonPTPCtx *)ctx;
    if (!c) return;
    free(c);
}

/* ─── vtable ─────────────────────────────────────────────────── */

static const CameraAdapterVtable kNikonPTPVtable = {
    .execute        = _nikon_ptp_execute,
    .get_status     = _nikon_ptp_get_status,
    .register_event = _nikon_ptp_register_event,
    .destroy        = _nikon_ptp_destroy,
};

/* ─── 工厂函数 ───────────────────────────────────────────────── */

CameraAdapter *adapter_create_ptp(PtpSession *session) {
    if (!session) return NULL;

    NikonPTPCtx *ctx = (NikonPTPCtx *)calloc(1, sizeof(NikonPTPCtx));
    if (!ctx) return NULL;
    ctx->session = session;
    ctx->status  = STATUS_DISCONNECTED;

    CameraAdapter *adapter = (CameraAdapter *)malloc(sizeof(CameraAdapter));
    if (!adapter) {
        free(ctx);
        return NULL;
    }
    adapter->vtable = &kNikonPTPVtable;
    adapter->ctx    = ctx;
    return adapter;
}
