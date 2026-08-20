/**
 * api/camera_api.c — CameraAPI 业务层实现
 *
 * 四层架构的顶层, 对 Android ViewModel / Desktop CLI 暴露统一入口。
 * 职责:
 *   - 管理 CameraAdapter / PtpSession / EventWatcher 生命周期
 *   - 线程安全互斥锁保护所有内部状态
 *   - 事件中继: watcher → _event_relay → 用户回调
 *   - 传输桥接: transfer engine 回调 → TransferProgress → 用户回调
 */
#include "api/camera_api.h"
#include "adapter/camera_adapter.h"
#include "protocol/ptp.h"
#include "transfer/transfer.h"
#include "transfer/ftp_client.h"
#include "event/watcher.h"
#include "hal/usb.h"
#include "hal/wifi.h"

/* mtp.c 前向声明 (无独立头文件) */
extern int mtp_send_file(PtpSession *session, const char *local_path,
                         uint32_t storage_id, uint32_t parent_obj,
                         const char *remote_name);

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <errno.h>

/* ═══════════════════════════════════════════════════════════════
 *  CameraAPI 内部结构体 (不透明, 仅 .c 内可见)
 * ══════════════════════════════════════════════════════════════ */

#define MAX_EVENT_HANDLERS  8

typedef struct CameraAPI {
    /* ── 传输偏好 ── */
    int                 transport;          /* TRANSPORT_AUTO / USB_ONLY / WIFI_ONLY */

    /* ── 核心组件 ── */
    CameraAdapter      *adapter;            /* 当前适配器 (NULL → 未连接) */
    PtpSession          session;            /* PTP 会话 (fd 由 HAL 提供) */
    EventWatcher       *watcher;            /* 三通道事件监听器 */

    /* ── 状态 ── */
    ConnectionStatus    status;
    CameraInfo          current_camera;     /* 仅连接后有效 */

    /* ── FTP 配置 ── */
    FtpConfig           ftp_config;
    bool                ftp_configured;

    /* ── 状态变化回调 ── */
    void (*status_cb)(ConnectionStatus, void *);
    void *status_cb_data;

    /* ── 传输进度回调 ── */
    void (*transfer_cb)(int, TransferProgress *, void *);
    void *transfer_cb_data;

    /* ── 新文件回调 ── */
    void (*new_file_cb)(uint32_t, const char *, void *);
    void *new_file_cb_data;

    /* ── 通用事件处理器 ── */
    event_handler_fn    event_handlers[MAX_EVENT_HANDLERS];
    void               *event_user_data[MAX_EVENT_HANDLERS];

    /* ── USB 设备 (用于 disconnect/destroy 时 hal_usb_close) ── */
    UsbDeviceInfo       dev_info;           /* 连接时填充, 断开时传入 HAL */

    /* ── 同步 ── */
    pthread_mutex_t     mutex;
    bool                usb_inited;
    bool                transfer_inited;
    bool                destroyed;          /* 防止 destroy 后使用 */
} CameraAPI;

/* ═══════════════════════════════════════════════════════════════
 *  内部工具函数
 * ══════════════════════════════════════════════════════════════ */

static void _lock(CameraAPI *api)   { pthread_mutex_lock(&api->mutex); }
static void _unlock(CameraAPI *api) { pthread_mutex_unlock(&api->mutex); }

static void _set_status_locked(CameraAPI *api, ConnectionStatus s) {
    if (api->status == s) return;
    api->status = s;
    void (*cb)(ConnectionStatus, void *) = api->status_cb;
    void *data = api->status_cb_data;
    if (cb) {
        _unlock(api);           /* 回调在锁外执行, 防止死锁 */
        cb(s, data);
        _lock(api);
    }
}

static void _event_relay(CameraEvent *event, void *user_data) {
    CameraAPI *api = (CameraAPI *)user_data;
    if (!api || !event) return;

    /* 新文件事件 → 触发新文件回调 (用于边拍边传) */
    if (event->type == EVENT_NEW_FILE && api->new_file_cb) {
        api->new_file_cb(event->data.new_file.object_handle,
                         event->data.new_file.filename,
                         api->new_file_cb_data);
    }

    /* 通用事件分发 */
    if (event->type < MAX_EVENT_HANDLERS) {
        _lock(api);
        event_handler_fn h = api->event_handlers[event->type];
        void *ud = api->event_user_data[event->type];
        _unlock(api);

        if (h) h(event, ud);
    }
}

/* ── 传输引擎回调桥接 ──────────────────────────────────────── */

static void _transfer_progress_bridge(TransferContext *ctx, int percent,
                                       void *user_data) {
    CameraAPI *api = (CameraAPI *)user_data;
    if (!api || !ctx) return;

    _lock(api);
    void (*cb)(int, TransferProgress *, void *) = api->transfer_cb;
    void *cb_data = api->transfer_cb_data;
    if (!cb) { _unlock(api); return; }

    TransferProgress tp;
    memset(&tp, 0, sizeof(tp));
    tp.job_id        = ctx->job_id;
    tp.object_handle = ctx->object_handle;
    snprintf(tp.filename, sizeof(tp.filename), "0x%08X", ctx->object_handle);
    tp.total_size    = ctx->file_size;
    tp.transferred   = ctx->transferred;
    tp.percent       = percent;
    tp.status        = TRANSFER_STATUS_RUNNING;

    _unlock(api);
    cb(ctx->job_id, &tp, cb_data);
}

static void _transfer_complete_bridge(TransferContext *ctx, int error_code,
                                       void *user_data) {
    CameraAPI *api = (CameraAPI *)user_data;
    if (!api || !ctx) return;

    _lock(api);
    void (*cb)(int, TransferProgress *, void *) = api->transfer_cb;
    void *cb_data = api->transfer_cb_data;
    if (!cb) { _unlock(api); return; }

    TransferProgress tp;
    memset(&tp, 0, sizeof(tp));
    tp.job_id        = ctx->job_id;
    tp.object_handle = ctx->object_handle;
    snprintf(tp.filename, sizeof(tp.filename), "0x%08X", ctx->object_handle);
    tp.total_size    = ctx->file_size;
    tp.transferred   = ctx->transferred;
    tp.percent       = (ctx->file_size > 0)
                         ? (int)(ctx->transferred * 100 / ctx->file_size)
                         : 100;
    tp.status         = (error_code == 0)
                         ? TRANSFER_STATUS_DONE
                         : TRANSFER_STATUS_FAILED;
    tp.error_code     = error_code;

    _unlock(api);
    cb(ctx->job_id, &tp, cb_data);
}

/* ── 确保 USB HAL 初始化 (幂等) ──────────────────────────────── */

static int _ensure_usb_init(CameraAPI *api) {
    if (api->usb_inited) return HAL_USB_OK;
    int rc = hal_usb_init(NULL, NULL);
    if (rc == HAL_USB_OK) api->usb_inited = true;
    return rc;
}

/* ── 确保传输引擎初始化 (幂等) ──────────────────────────────── */

static int _ensure_transfer_init(CameraAPI *api) {
    if (api->transfer_inited) return 0;
    int rc = transfer_engine_init(/* worker_threads = */ 4);
    if (rc == 0) api->transfer_inited = true;
    return rc;
}

/* ── MTP ObjectInfo 固定部分 (packed, 与 mtp.c 一致) ──────── */
typedef struct __attribute__((packed)) {
    uint32_t    storage_id;
    uint16_t    object_format;
    uint16_t    protection_status;
    uint32_t    object_compressed_size;
    uint16_t    thumb_format;
    uint32_t    thumb_compressed_size;
    uint32_t    thumb_offset;
    uint32_t    thumb_pix_width;
    uint32_t    thumb_pix_height;
    uint32_t    image_pix_width;
    uint32_t    image_pix_height;
    uint32_t    image_bit_depth;
    uint32_t    parent_object;
    uint16_t    association_type;
    uint32_t    association_desc;
    uint32_t    sequence_number;
    /* 变长 UTF-16LE 字符串: filename, capture_date, modification_date */
} MtpObjInfoHdr;

#define MTP_OBJ_HDR_SIZE        56      /* sizeof(MtpObjInfoHdr) */

/* MTP 对象格式码 (与 mtp.c 保持一致) */
#define MTP_OBJ_FMT_JPEG        0x3801u
#define MTP_OBJ_FMT_NEF         0x3805u
#define MTP_OBJ_FMT_NRW         0x3806u
#define MTP_OBJ_FMT_MP4         0xB982u
#define MTP_OBJ_FMT_MOV         0xB984u

/** 从 UTF-16LE 字节流解析为 UTF-8 (最大 max_utf8 字节, 不含 \\0) */
static int _utf16le_to_utf8(const uint8_t *utf16, int utf16_bytes,
                            char *utf8, int max_utf8) {
    if (!utf16 || !utf8 || utf16_bytes < 2) return 0;
    int out = 0;
    for (int i = 0; i < utf16_bytes - 1 && out < max_utf8 - 1; i += 2) {
        uint16_t ch = (uint16_t)utf16[i] | ((uint16_t)utf16[i + 1] << 8);
        if (ch == 0) break;
        if (ch < 0x80) {
            if (out < max_utf8 - 1) utf8[out++] = (char)ch;
        } else if (ch < 0x800) {
            if (out < max_utf8 - 2) {
                utf8[out++] = (char)(0xC0 | (ch >> 6));
                utf8[out++] = (char)(0x80 | (ch & 0x3F));
            }
        } else {
            if (out < max_utf8 - 3) {
                utf8[out++] = (char)(0xE0 | (ch >> 12));
                utf8[out++] = (char)(0x80 | ((ch >> 6) & 0x3F));
                utf8[out++] = (char)(0x80 | (ch & 0x3F));
            }
        }
    }
    utf8[out] = '\0';
    return out;
}

CameraAPI *camera_api_create(int transport) {
    CameraAPI *api = (CameraAPI *)calloc(1, sizeof(CameraAPI));
    if (!api) return NULL;

    api->transport               = transport;
    api->status                  = STATUS_DISCONNECTED;
    api->adapter                 = NULL;
    api->watcher                 = NULL;
    api->usb_inited              = false;
    api->transfer_inited         = false;
    api->destroyed               = false;
    api->session.fd              = -1;

    if (pthread_mutex_init(&api->mutex, NULL) != 0) {
        free(api);
        return NULL;
    }

    return api;
}

void camera_api_destroy(CameraAPI *api) {
    if (!api) return;

    _lock(api);

    if (api->destroyed) { _unlock(api); return; }
    api->destroyed = true;

    /* 先断开 */
    if (api->adapter) {
        CameraCommand cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type = CAM_CMD_DISCONNECT;
        cmd.timeout_ms = 2000;
        adapter_execute(api->adapter, &cmd);
    }

    /* 停止事件监听 */
    if (api->watcher) {
        watcher_stop(api->watcher);
        watcher_destroy(api->watcher);
        api->watcher = NULL;
    }

    /* 关闭会话 */
    if (api->session.transport == PTP_TRANSPORT_USB && api->session.fd >= 0) {
        hal_usb_close(&api->dev_info);
        api->session.fd = -1;
    } else if (api->session.transport == PTP_TRANSPORT_WIFI) {
        hal_wifi_disconnect();
    }

    /* 销毁适配器 */
    if (api->adapter) {
        adapter_destroy(api->adapter);
        free(api->adapter);
        api->adapter = NULL;
    }

    _unlock(api);

    pthread_mutex_destroy(&api->mutex);
    free(api);
}

/* ═══════════════════════════════════════════════════════════════
 *  连接管理
 * ══════════════════════════════════════════════════════════════ */

CameraInfo *camera_api_scan(CameraAPI *api, int *count) {
    if (!api || !count) return NULL;
    *count = 0;

    _lock(api);

    /* 限扫一次 */
    if (api->status >= STATUS_CONNECTED) {
        _unlock(api);
        return NULL;
    }

    _set_status_locked(api, STATUS_SCANNING);

    /* USB 扫描 */
    UsbDeviceInfo usb_devs[16];
    int usb_count = 0;
    if (api->transport != TRANSPORT_WIFI_ONLY) {
        (void)_ensure_usb_init(api);
        usb_count = hal_usb_enumerate(usb_devs, 16);
        if (usb_count < 0) usb_count = 0;
    }

    /* WiFi 扫描 */
    WifiConnectionInfo wifi_devs[16];
    int wifi_count = 0;
    if (api->transport != TRANSPORT_USB_ONLY) {
        wifi_count = hal_wifi_scan(NIKON_WIFI_SSID_PREFIX, wifi_devs, 16);
        if (wifi_count < 0) wifi_count = 0;
    }

    int total = usb_count + wifi_count;
    if (total == 0) {
        _set_status_locked(api, STATUS_DISCONNECTED);
        _unlock(api);
        *count = 0;
        return NULL;
    }

    CameraInfo *infos = (CameraInfo *)calloc((size_t)total, sizeof(CameraInfo));
    if (!infos) {
        _set_status_locked(api, STATUS_DISCONNECTED);
        _unlock(api);
        return NULL;
    }

    /* 填充 USB 相机 */
    for (int i = 0; i < usb_count; i++) {
        CameraInfo *ci = &infos[i];
        snprintf(ci->id, sizeof(ci->id), "%s|usb", usb_devs[i].serial);
        snprintf(ci->model, sizeof(ci->model), "%s", usb_devs[i].product_name);
        snprintf(ci->serial, sizeof(ci->serial), "%s", usb_devs[i].serial);
        ci->transport     = 0;
        ci->battery_level = -1;          /* 未连接时未知 */
    }

    /* 填充 WiFi 相机 */
    for (int i = 0; i < wifi_count; i++) {
        CameraInfo *ci = &infos[usb_count + i];
        snprintf(ci->id, sizeof(ci->id), "%s|wifi|%d",
                 wifi_devs[i].ip_address, wifi_devs[i].port);
        snprintf(ci->model, sizeof(ci->model), "%s (WiFi)", wifi_devs[i].ssid);
        snprintf(ci->serial, sizeof(ci->serial), "%s", wifi_devs[i].ssid);
        ci->transport     = 1;
        ci->battery_level = wifi_devs[i].rssi; /* 近似: 信号强度 */
    }

    *count = total;
    _set_status_locked(api, STATUS_DISCONNECTED);
    _unlock(api);
    return infos;
}

int camera_api_connect(CameraAPI *api, const char *camera_id) {
    if (!api || !camera_id) return CAM_ERR_INVALID_PARAM;

    _lock(api);

    if (api->status >= STATUS_CONNECTED) {
        _unlock(api);
        return CAM_ERR_ALREADY_CONNECTED;
    }

    _set_status_locked(api, STATUS_CONNECTING);

    bool is_usb = (strstr(camera_id, "|usb") != NULL);
    int  rc     = CAM_OK;

    if (is_usb) {
        /* ── USB 连接路径 ──────────────────────────────────── */

        /* 1) 枚举设备, 匹配 ID */
        _ensure_usb_init(api);
        UsbDeviceInfo devs[16];
        int dev_count = hal_usb_enumerate(devs, 16);
        UsbDeviceInfo *target = NULL;

        for (int i = 0; i < dev_count; i++) {
            char tmp_id[128];
            snprintf(tmp_id, sizeof(tmp_id), "%s|usb", devs[i].serial);
            if (strcmp(tmp_id, camera_id) == 0) {
                target = &devs[i];
                break;
            }
        }

        if (!target) {
            rc = CAM_ERR_NOT_CONNECTED;
            goto connect_error;
        }

        /* 保存设备信息 (用于后续 hal_usb_close) */
        api->dev_info = *target;

        /* 2) 请求权限 (Android OTG 弹框, 其他平台直接过) */
        int perm = hal_usb_request_permission(target);
        if (perm < 0) {
            rc = CAM_ERR_PERMISSION_DENIED;
            goto connect_error;
        }
        /* perm == 1 表示 Android 等待用户授权, 先视为临时错误 */
        if (perm == 1) {
            rc = CAM_ERR_PERMISSION_DENIED;
            goto connect_error;
        }

        /* 3) 打开设备, 获取 fd */
        int fd = hal_usb_open(target);
        if (fd < 0) {
            rc = CAM_ERR_USB;
            goto connect_error;
        }

        /* 4) 初始化 PTP 会话 */
        ptp_session_init(&api->session, fd, PTP_TRANSPORT_USB,
                         PTP_USB_EP_OUT_DEFAULT, PTP_USB_EP_IN_DEFAULT);

        /* 5) 创建 PTP 适配器 */
        api->adapter = adapter_create_ptp(&api->session);
        if (!api->adapter) {
            rc = CAM_ERR_OUT_OF_MEMORY;
            goto connect_error;
        }

        /* 填充基本信息 (结构体, 后续从 GetDeviceInfo 获取更精确值) */
        snprintf(api->current_camera.id, sizeof(api->current_camera.id),
                 "%s", camera_id);
        snprintf(api->current_camera.model, sizeof(api->current_camera.model),
                 "%s", target->product_name);
        snprintf(api->current_camera.serial, sizeof(api->current_camera.serial),
                 "%s", target->serial);
        api->current_camera.transport = 0;

    } else {
        /* ── WiFi 连接路径 ─────────────────────────────────── */

        /* 解析 camera_id: "IP|wifi|PORT" */
        WifiConnectionInfo wci;
        memset(&wci, 0, sizeof(wci));
        {
            char id_copy[256];
            strncpy(id_copy, camera_id, sizeof(id_copy) - 1);
            char *ip   = strtok(id_copy, "|");
            char *tag  = strtok(NULL, "|");
            char *port = strtok(NULL, "|");
            (void)tag; /* "wifi" */
            if (!ip || !port) {
                rc = CAM_ERR_INVALID_PARAM;
                goto connect_error;
            }
            strncpy(wci.ip_address, ip, sizeof(wci.ip_address) - 1);
            wci.port = (uint16_t)atoi(port);
        }
        if (wci.port == 0) wci.port = NIKON_WIFI_DEFAULT_PORT;

        int wifi_rc = hal_wifi_connect(&wci);
        if (wifi_rc != HAL_WIFI_OK) {
            rc = CAM_ERR_WIFI;
            goto connect_error;
        }

        /* WiFi HAL 不暴露 fd, 使用 -1 标记 */
        int wifi_fd = -1;
        ptp_session_init(&api->session, wifi_fd, PTP_TRANSPORT_WIFI, 0, 0);

        api->adapter = adapter_create_ptp(&api->session);
        if (!api->adapter) {
            rc = CAM_ERR_OUT_OF_MEMORY;
            goto connect_error;
        }

        snprintf(api->current_camera.id, sizeof(api->current_camera.id),
                 "%s", camera_id);
        snprintf(api->current_camera.model, sizeof(api->current_camera.model),
                 "NIKON (WiFi)");
    }

    /* ── 打开 PTP 会话 ──────────────────────────────────────── */
    {
        CameraCommand cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type       = CAM_CMD_CONNECT;
        cmd.timeout_ms = 5000;
        CameraResult res = adapter_execute(api->adapter, &cmd);
        if (res.result_code != 0) {
            rc = res.result_code;
            goto connect_error;
        }
    }

    /* ── 启动事件监听 ──────────────────────────────────────── */
    {
        api->watcher = watcher_create(&api->session, WATCHER_CHANNEL_PTP_POLL);
        if (api->watcher) {
            watcher_on_event(api->watcher, EVENT_NEW_FILE,
                             _event_relay, api);
            watcher_on_event(api->watcher, EVENT_CAPTURE_COMPLETE,
                             _event_relay, api);
            watcher_on_event(api->watcher, EVENT_CONNECTION_CHANGED,
                             _event_relay, api);
            watcher_on_event(api->watcher, EVENT_PROPERTY_CHANGED,
                             _event_relay, api);
            watcher_on_event(api->watcher, EVENT_STORAGE_CHANGED,
                             _event_relay, api);
            watcher_on_event(api->watcher, EVENT_ERROR,
                             _event_relay, api);
            watcher_start(api->watcher);
        }
    }

    /* ── 初始化传输引擎 ────────────────────────────────────── */
    _ensure_transfer_init(api);

    _set_status_locked(api, STATUS_CONNECTED);
    _unlock(api);
    return CAM_OK;

connect_error:
    if (api->watcher)   { watcher_destroy(api->watcher); api->watcher = NULL; }
    if (api->adapter)   { adapter_destroy(api->adapter); free(api->adapter); api->adapter = NULL; }
    if (api->session.fd >= 0) { /* hal_usb_close */ api->session.fd = -1; }
    _set_status_locked(api, STATUS_ERROR);
    _unlock(api);
    return rc;
}

/* ── 预开 USB fd 连接 (Android 专用) ──────────────────────── */

int camera_api_connect_usb_fd(CameraAPI *api, int fd, const char *serial,
                               uint8_t ep_out, uint8_t ep_in) {
    if (!api || fd < 0) return CAM_ERR_INVALID_PARAM;

    _lock(api);

    if (api->status >= STATUS_CONNECTED) {
        _unlock(api);
        return CAM_ERR_ALREADY_CONNECTED;
    }

    _set_status_locked(api, STATUS_CONNECTING);

    /* 1) 初始化 PTP 会话 (跳过 HAL 枚举/打开) */
    ptp_session_init(&api->session, fd, PTP_TRANSPORT_USB, ep_out, ep_in);

    /* 2) 创建适配器 */
    api->adapter = adapter_create_ptp(&api->session);
    if (!api->adapter) {
        _set_status_locked(api, STATUS_ERROR);
        _unlock(api);
        return CAM_ERR_OUT_OF_MEMORY;
    }

    /* 3) 填充基本信息 */
    snprintf(api->current_camera.id, sizeof(api->current_camera.id),
             "%s|usb", serial ? serial : "unknown");
    snprintf(api->current_camera.model, sizeof(api->current_camera.model),
             "NIKON (USB)");
    snprintf(api->current_camera.serial, sizeof(api->current_camera.serial),
             "%s", serial ? serial : "");
    api->current_camera.transport = 0;

    /* 4) 打开 PTP 会话 */
    {
        CameraCommand cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type       = CAM_CMD_CONNECT;
        cmd.timeout_ms = 5000;
        CameraResult res = adapter_execute(api->adapter, &cmd);
        if (res.result_code != 0) {
            _set_status_locked(api, STATUS_ERROR);
            _unlock(api);
            return res.result_code;
        }
    }

    /* 5) 启动事件监听 */
    {
        api->watcher = watcher_create(&api->session, WATCHER_CHANNEL_PTP_POLL);
        if (api->watcher) {
            watcher_on_event(api->watcher, EVENT_NEW_FILE,
                             _event_relay, api);
            watcher_on_event(api->watcher, EVENT_CAPTURE_COMPLETE,
                             _event_relay, api);
            watcher_on_event(api->watcher, EVENT_CONNECTION_CHANGED,
                             _event_relay, api);
            watcher_on_event(api->watcher, EVENT_PROPERTY_CHANGED,
                             _event_relay, api);
            watcher_on_event(api->watcher, EVENT_STORAGE_CHANGED,
                             _event_relay, api);
            watcher_on_event(api->watcher, EVENT_ERROR,
                             _event_relay, api);
            watcher_start(api->watcher);
        }
    }

    /* 6) 初始化传输引擎 */
    _ensure_transfer_init(api);

    _set_status_locked(api, STATUS_CONNECTED);
    _unlock(api);
    return CAM_OK;
}

void camera_api_disconnect(CameraAPI *api) {
    if (!api) return;

    _lock(api);

    /* 停止事件监听 */
    if (api->watcher) {
        watcher_stop(api->watcher);
        watcher_destroy(api->watcher);
        api->watcher = NULL;
    }

    /* 发送 Disconnect 命令 */
    if (api->adapter && api->status >= STATUS_CONNECTED) {
        CameraCommand cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.type       = CAM_CMD_DISCONNECT;
        cmd.timeout_ms = 2000;
        adapter_execute(api->adapter, &cmd);
    }

    /* 关闭传输层 */
    if (api->session.transport == PTP_TRANSPORT_USB && api->session.fd >= 0) {
        hal_usb_close(&api->dev_info);
        api->session.fd = -1;
    } else if (api->session.transport == PTP_TRANSPORT_WIFI) {
        hal_wifi_disconnect();
        api->session.fd = -1;
    }

    /* 销毁适配器 */
    if (api->adapter) {
        adapter_destroy(api->adapter);
        free(api->adapter);
        api->adapter = NULL;
    }

    memset(&api->session, 0, sizeof(api->session));
    api->session.fd = -1;

    _set_status_locked(api, STATUS_DISCONNECTED);
    _unlock(api);
}

ConnectionStatus camera_api_get_status(CameraAPI *api) {
    if (!api) return STATUS_DISCONNECTED;
    _lock(api);
    ConnectionStatus s = api->status;
    _unlock(api);
    return s;
}

void camera_api_on_status_change(CameraAPI *api,
    void (*cb)(ConnectionStatus status, void *), void *user_data) {
    if (!api) return;
    _lock(api);
    api->status_cb      = cb;
    api->status_cb_data = user_data;
    _unlock(api);
}

/* ═══════════════════════════════════════════════════════════════
 *  拍摄控制
 * ══════════════════════════════════════════════════════════════ */

int camera_api_capture(CameraAPI *api) {
    if (!api) return CAM_ERR_INVALID_PARAM;
    _lock(api);
    if (!api->adapter) { _unlock(api); return CAM_ERR_NOT_CONNECTED; }

    CameraCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type       = CAM_CMD_CAPTURE;
    cmd.timeout_ms = 10000;

    CameraResult res = adapter_execute(api->adapter, &cmd);
    _unlock(api);

    if (res.data) free(res.data);
    return res.result_code;
}

int camera_api_capture_burst(CameraAPI *api, int count, int interval_ms) {
    if (!api) return CAM_ERR_INVALID_PARAM;
    _lock(api);
    if (!api->adapter) { _unlock(api); return CAM_ERR_NOT_CONNECTED; }

    CameraCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type                 = CAM_CMD_CAPTURE_BURST;
    cmd.params.capture.count = count;
    cmd.params.capture.interval_ms = interval_ms;
    cmd.timeout_ms           = (count == 0) ? 0 : count * (interval_ms + 5000);

    CameraResult res = adapter_execute(api->adapter, &cmd);
    _unlock(api);

    if (res.data) free(res.data);
    return res.result_code;
}

int camera_api_stop_burst(CameraAPI *api) {
    if (!api) return CAM_ERR_INVALID_PARAM;
    _lock(api);
    if (!api->adapter) { _unlock(api); return CAM_ERR_NOT_CONNECTED; }

    CameraCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type       = CAM_CMD_STOP_BURST;
    cmd.timeout_ms = 3000;

    CameraResult res = adapter_execute(api->adapter, &cmd);
    _unlock(api);

    if (res.data) free(res.data);
    return res.result_code;
}

int camera_api_autofocus(CameraAPI *api) {
    if (!api) return CAM_ERR_INVALID_PARAM;
    _lock(api);
    if (!api->adapter) { _unlock(api); return CAM_ERR_NOT_CONNECTED; }

    CameraCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type       = CAM_CMD_AUTOFOCUS;
    cmd.timeout_ms = 5000;

    CameraResult res = adapter_execute(api->adapter, &cmd);
    _unlock(api);

    if (res.data) free(res.data);
    return res.result_code;
}

/* ═══════════════════════════════════════════════════════════════
 *  实时取景
 * ══════════════════════════════════════════════════════════════ */

int camera_api_start_liveview(CameraAPI *api) {
    if (!api) return CAM_ERR_INVALID_PARAM;
    _lock(api);
    if (!api->adapter) { _unlock(api); return CAM_ERR_NOT_CONNECTED; }

    CameraCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type       = CAM_CMD_START_LIVEVIEW;
    cmd.timeout_ms = 5000;

    CameraResult res = adapter_execute(api->adapter, &cmd);
    _unlock(api);

    if (res.data) free(res.data);
    return res.result_code;
}

int camera_api_stop_liveview(CameraAPI *api) {
    if (!api) return CAM_ERR_INVALID_PARAM;
    _lock(api);
    if (!api->adapter) { _unlock(api); return CAM_ERR_NOT_CONNECTED; }

    CameraCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type       = CAM_CMD_STOP_LIVEVIEW;
    cmd.timeout_ms = 3000;

    CameraResult res = adapter_execute(api->adapter, &cmd);
    _unlock(api);

    if (res.data) free(res.data);
    return res.result_code;
}

int camera_api_get_liveview_frame(CameraAPI *api,
    void (*cb)(const uint8_t *jpeg_data, int size, void *), void *user_data) {
    if (!api || !cb) return CAM_ERR_INVALID_PARAM;
    _lock(api);
    if (!api->adapter) { _unlock(api); return CAM_ERR_NOT_CONNECTED; }

    CameraCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type       = CAM_CMD_GET_LIVEVIEW;
    cmd.timeout_ms = 3000;

    CameraResult res = adapter_execute(api->adapter, &cmd);
    _unlock(api);

    if (res.result_code == 0 && res.data && res.data_size > 0) {
        cb((const uint8_t *)res.data, (int)res.data_size, user_data);
    }
    if (res.data) free(res.data);
    return res.result_code;
}

/* ═══════════════════════════════════════════════════════════════
 *  相机参数读写
 * ══════════════════════════════════════════════════════════════ */

int camera_api_set_property(CameraAPI *api, uint16_t prop_id, uint32_t value) {
    if (!api) return CAM_ERR_INVALID_PARAM;
    _lock(api);
    if (!api->adapter) { _unlock(api); return CAM_ERR_NOT_CONNECTED; }

    CameraCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type                         = CAM_CMD_SET_PROPERTY;
    cmd.params.set_property.property_id = prop_id;
    cmd.params.set_property.value       = value;
    cmd.timeout_ms                   = 3000;

    CameraResult res = adapter_execute(api->adapter, &cmd);
    _unlock(api);

    if (res.data) free(res.data);
    return res.result_code;
}

int camera_api_get_property(CameraAPI *api, uint16_t prop_id, uint32_t *value) {
    if (!api || !value) return CAM_ERR_INVALID_PARAM;
    _lock(api);
    if (!api->adapter) { _unlock(api); return CAM_ERR_NOT_CONNECTED; }

    CameraCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type                        = CAM_CMD_GET_PROPERTY;
    cmd.params.get_property.property_id = prop_id;
    cmd.timeout_ms                  = 3000;

    CameraResult res = adapter_execute(api->adapter, &cmd);
    _unlock(api);

    if (res.result_code == 0 && res.data) {
        *value = *(uint32_t *)res.data;
    } else if (res.result_code == 0) {
        res.result_code = CAM_ERR_PROTOCOL;
    }
    if (res.data) free(res.data);
    return res.result_code;
}

int camera_api_get_properties(CameraAPI *api, const uint16_t *prop_ids,
                              uint32_t *values, int count) {
    if (!api || !prop_ids || !values || count <= 0) return CAM_ERR_INVALID_PARAM;

    /* 逐项读取; 单项失败保留 values[i] 原值, 累计失败数返回 */
    int failures = 0;
    for (int i = 0; i < count; i++) {
        int rc = camera_api_get_property(api, prop_ids[i], &values[i]);
        if (rc != CAM_OK) failures++;
    }
    return failures;
}

/* ═══════════════════════════════════════════════════════════════
 *  Picture Control (PTP 0x90CC / 0x90CD)
 * ══════════════════════════════════════════════════════════════ */

int camera_api_get_pictctrl(CameraAPI *api, PictureControl *ctrl) {
    if (!api || !ctrl) return CAM_ERR_INVALID_PARAM;
    _lock(api);
    if (!api->adapter) { _unlock(api); return CAM_ERR_NOT_CONNECTED; }

    CameraCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type       = CAM_CMD_GET_PICTCTRL;
    cmd.timeout_ms = 3000;

    CameraResult res = adapter_execute(api->adapter, &cmd);
    _unlock(api);

    if (res.result_code == 0 && res.data) {
        memcpy(ctrl, res.data, sizeof(PictureControl));
    }
    if (res.data) free(res.data);
    return res.result_code;
}

int camera_api_set_pictctrl(CameraAPI *api, const PictureControl *ctrl) {
    if (!api || !ctrl) return CAM_ERR_INVALID_PARAM;
    _lock(api);
    if (!api->adapter) { _unlock(api); return CAM_ERR_NOT_CONNECTED; }

    CameraCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type              = CAM_CMD_SET_PICTCTRL;
    cmd.params.pictctrl   = *ctrl;
    cmd.timeout_ms        = 3000;

    CameraResult res = adapter_execute(api->adapter, &cmd);
    _unlock(api);

    if (res.data) free(res.data);
    return res.result_code;
}

/* ═══════════════════════════════════════════════════════════════
 *  文件操作
 * ══════════════════════════════════════════════════════════════ */

FileInfo *camera_api_list_files(CameraAPI *api, uint32_t storage_id, int *count) {
    if (!api || !count) return NULL;
    *count = 0;

    _lock(api);
    if (!api->adapter) { _unlock(api); return NULL; }

    /* 1) 获取 object handles (直接走 PTP, 不走 adapter 命令分发) */
    uint32_t params[5] = { storage_id, 0xFFFFFFFF, 0, 0, 0 };
    uint8_t  buf[1024 * 16]; /* 足够容纳 ~1000 个 handle */
    uint32_t out_len = 0;

    int rc = ptp_exec(&api->session,
                      (uint16_t)PTP_OC_GetObjectHandles,
                      params, 3,
                      NULL, 0,
                      buf, sizeof(buf), &out_len);
    if (rc != (int)PTP_RC_OK || out_len < 4) {
        _unlock(api);
        return NULL;
    }

    /* 返回格式: [uint32_t count][uint32_t handle]...  */
    uint32_t handle_count = *(uint32_t *)buf;
    if (handle_count == 0) { _unlock(api); return NULL; }

    /* 限制最大返回数 */
    #define MAX_FILE_LIST 1000
    if (handle_count > MAX_FILE_LIST) handle_count = MAX_FILE_LIST;

    FileInfo *files = (FileInfo *)calloc(handle_count, sizeof(FileInfo));
    if (!files) { _unlock(api); return NULL; }

    uint32_t *handles = (uint32_t *)(buf + 4);
    int valid = 0;

    /* 2) 逐个获取对象信息 */
    for (uint32_t i = 0; i < handle_count && i < MAX_FILE_LIST; i++) {
        uint32_t info_params[5] = { handles[i], 0, 0, 0, 0 };
        uint8_t  info_buf[1024];  /* 增大缓冲区以容纳变长 UTF-16LE 字符串 */
        uint32_t info_len = 0;

        int info_rc = ptp_exec(&api->session,
                               (uint16_t)PTP_OC_GetObjectInfo,
                               info_params, 1,
                               NULL, 0,
                               info_buf, sizeof(info_buf), &info_len);
        if (info_rc != (int)PTP_RC_OK || info_len < MTP_OBJ_HDR_SIZE) continue;

        /* 解析固定头部 */
        MtpObjInfoHdr *hdr = (MtpObjInfoHdr *)info_buf;
        FileInfo *fi = &files[valid];
        fi->object_handle = handles[i];
        fi->size          = (uint64_t)hdr->object_compressed_size;
        fi->width         = (int)hdr->image_pix_width;
        fi->height        = (int)hdr->image_pix_height;
        fi->storage_id    = hdr->storage_id;

        /* 格式判断 */
        switch (hdr->object_format) {
        case MTP_OBJ_FMT_JPEG: fi->is_jpeg = true; fi->is_raw = false; break;
        case MTP_OBJ_FMT_NEF:
        case MTP_OBJ_FMT_NRW: fi->is_raw = true; fi->is_jpeg = false; break;
        default:              fi->is_raw = false; fi->is_jpeg = false; break;
        }

        /* 解析变长 UTF-16LE 字符串: filename */
        int str_offset = MTP_OBJ_HDR_SIZE;
        if ((uint32_t)str_offset < info_len) {
            int utf16_bytes = (int)(info_len - (uint32_t)str_offset);
            _utf16le_to_utf8(info_buf + str_offset, utf16_bytes,
                             fi->filename, (int)sizeof(fi->filename));
            /* 跳过 filename 找 capture_date */
            int fn_end = 0;
            while (fn_end < utf16_bytes - 1) {
                uint16_t ch = (uint16_t)info_buf[str_offset + fn_end]
                            | ((uint16_t)info_buf[str_offset + fn_end + 1] << 8);
                fn_end += 2;
                if (ch == 0) break;
            }
            /* 解析 capture_date */
            int cd_offset = str_offset + fn_end;
            if (cd_offset + 2 <= (int)info_len) {
                char tmp_date[64];
                _utf16le_to_utf8(info_buf + cd_offset,
                                 (int)(info_len - cd_offset),
                                 tmp_date, (int)sizeof(tmp_date));
                /* MTP datetime format: "YYYYMMDDThhmmss.s" → "YYYY-MM-DDThh:mm:ss" */
                if (strlen(tmp_date) >= 14) {
                    snprintf(fi->datetime, sizeof(fi->datetime),
                             "%.4s-%.2s-%.2sT%.2s:%.2s:%.2s",
                             tmp_date, tmp_date + 4, tmp_date + 6,
                             tmp_date + 9, tmp_date + 11, tmp_date + 13);
                }
            }
        }

        /* 文件名回退: 如果 UTF-16LE 解析失败, 用 handle 构造 */
        if (fi->filename[0] == '\0') {
            const char *ext = fi->is_raw ? "NEF" : fi->is_jpeg ? "JPG" : "DAT";
            snprintf(fi->filename, sizeof(fi->filename),
                     "DSC_%04u.%s", handles[i] % 10000, ext);
        }

        valid++;
    }

    _unlock(api);
    *count = valid;
    return (valid > 0) ? files : (free(files), NULL);
}

int camera_api_get_thumbnail(CameraAPI *api, uint32_t object_handle,
                              uint8_t **data, uint32_t *size) {
    if (!api || !data || !size) return CAM_ERR_INVALID_PARAM;
    *data = NULL;
    *size = 0;

    _lock(api);
    if (!api->adapter) { _unlock(api); return CAM_ERR_NOT_CONNECTED; }

    uint32_t params[5] = { object_handle, 0, 0, 0, 0 };
    uint8_t  *buf = (uint8_t *)malloc(256 * 1024); /* 256KB max thumbnail */
    if (!buf) { _unlock(api); return CAM_ERR_OUT_OF_MEMORY; }

    uint32_t out_len = 0;
    int rc = ptp_exec(&api->session,
                      (uint16_t)PTP_OC_GetThumb,
                      params, 1,
                      NULL, 0,
                      buf, 256 * 1024, &out_len);
    _unlock(api);

    if (rc != (int)PTP_RC_OK || out_len == 0) {
        free(buf);
        return CAM_ERR_FILE_NOT_FOUND;
    }

    *data = buf;
    *size = out_len;
    return CAM_OK;
}

int camera_api_delete_file(CameraAPI *api, uint32_t object_handle) {
    if (!api) return CAM_ERR_INVALID_PARAM;
    _lock(api);
    if (!api->adapter) { _unlock(api); return CAM_ERR_NOT_CONNECTED; }

    uint32_t params[5] = { object_handle, 0, 0, 0, 0 };
    uint32_t out_len = 0;
    int rc = ptp_exec(&api->session,
                      (uint16_t)PTP_OC_DeleteObject,
                      params, 1,
                      NULL, 0,
                      NULL, 0, &out_len);
    _unlock(api);

    return (rc == (int)PTP_RC_OK) ? CAM_OK : CAM_ERR_FILE_NOT_FOUND;
}

/* ═══════════════════════════════════════════════════════════════
 *  文件传输 (异步, 分块 + 断点续传)
 * ══════════════════════════════════════════════════════════════ */

int camera_api_start_transfer(CameraAPI *api,
                               uint32_t object_handle, const char *dest_path) {
    if (!api || !dest_path) return CAM_ERR_INVALID_PARAM;

    _lock(api);
    if (!api->adapter) { _unlock(api); return CAM_ERR_NOT_CONNECTED; }

    if (_ensure_transfer_init(api) != 0) {
        _unlock(api);
        return CAM_ERR_OUT_OF_MEMORY;
    }

    _set_status_locked(api, STATUS_TRANSFERRING);

    int job_id = transfer_submit(&api->session,
                                  object_handle, dest_path,
                                  _transfer_progress_bridge,
                                  _transfer_complete_bridge,
                                  api);
    _unlock(api);

    return (job_id >= 0) ? job_id : CAM_ERR_TRANSFER_FAILED;
}

int camera_api_start_batch_transfer(CameraAPI *api,
                                     const uint32_t *handles, int count,
                                     const char *dest_dir) {
    if (!api || !handles || count <= 0 || !dest_dir) return CAM_ERR_INVALID_PARAM;

    _lock(api);
    if (!api->adapter) { _unlock(api); return CAM_ERR_NOT_CONNECTED; }

    if (_ensure_transfer_init(api) != 0) {
        _unlock(api);
        return CAM_ERR_OUT_OF_MEMORY;
    }

    _set_status_locked(api, STATUS_TRANSFERRING);

    int first_job = -1;
    for (int i = 0; i < count; i++) {
        char filepath[512];
        snprintf(filepath, sizeof(filepath), "%s/%08X.NEF",
                 dest_dir, handles[i]);

        /* 跳过已存在的文件 (断点续传由 transfer engine 处理) */
        int job_id = transfer_submit(&api->session,
                                      handles[i], filepath,
                                      _transfer_progress_bridge,
                                      _transfer_complete_bridge,
                                      api);
        if (first_job < 0) first_job = job_id;
    }

    _unlock(api);
    return first_job;
}

int camera_api_cancel_transfer(CameraAPI *api, int job_id) {
    (void)api;
    return (transfer_cancel(job_id) == 0) ? CAM_OK : CAM_ERR_TRANSFER_FAILED;
}

int camera_api_get_progress(CameraAPI *api, int job_id, TransferProgress *out) {
    (void)api;
    return (transfer_get_progress(job_id, out) == 0) ? CAM_OK : CAM_ERR_TRANSFER_FAILED;
}

void camera_api_on_transfer_progress(CameraAPI *api,
    void (*cb)(int job_id, TransferProgress *progress, void *),
    void *user_data) {
    if (!api) return;
    _lock(api);
    api->transfer_cb      = cb;
    api->transfer_cb_data = user_data;
    _unlock(api);
}

void camera_api_on_new_file(CameraAPI *api,
    void (*cb)(uint32_t object_handle, const char *filename, void *),
    void *user_data) {
    if (!api) return;
    _lock(api);
    api->new_file_cb      = cb;
    api->new_file_cb_data = user_data;
    _unlock(api);
}

/* ═══════════════════════════════════════════════════════════════
 *  FTP 自动化 (真实实现: POSIX socket FTP 客户端)
 * ══════════════════════════════════════════════════════════════ */

int camera_api_set_ftp_config(CameraAPI *api, const FtpConfig *config) {
    if (!api || !config) return CAM_ERR_INVALID_PARAM;
    _lock(api);
    api->ftp_config     = *config;
    api->ftp_configured = true;
    _unlock(api);
    return CAM_OK;
}

/** 异步 FTP 上传任务 (拷贝配置与路径, 供分离线程使用)。 */
typedef struct {
    FtpConfig cfg;
    char      local_path[512];
} FtpUploadJob;

static void *_ftp_upload_thread(void *arg) {
    FtpUploadJob *job = (FtpUploadJob *)arg;
    int rc = ftp_client_upload(&job->cfg, job->local_path);
    fprintf(stderr, "[FTP] upload %s -> %s:%u rc=%d\n",
            job->local_path, job->cfg.host, (unsigned)job->cfg.port, rc);
    free(job);
    return NULL;
}

int camera_api_export_to_ftp(CameraAPI *api, const char *local_path) {
    if (!api || !local_path) return CAM_ERR_INVALID_PARAM;

    _lock(api);
    bool configured = api->ftp_configured;
    FtpConfig cfg   = api->ftp_config;
    _unlock(api);

    if (!configured) return CAM_ERR_NOT_CONNECTED;
    if (cfg.host[0] == '\0') return CAM_ERR_INVALID_PARAM;

    FtpUploadJob *job = (FtpUploadJob *)malloc(sizeof(FtpUploadJob));
    if (!job) return CAM_ERR_OUT_OF_MEMORY;
    job->cfg = cfg;
    snprintf(job->local_path, sizeof(job->local_path), "%s", local_path);

    pthread_t t;
    if (pthread_create(&t, NULL, _ftp_upload_thread, job) != 0) {
        free(job);
        return CAM_ERR_OUT_OF_MEMORY;
    }
    pthread_detach(t);
    return CAM_OK;
}

/* ─── WiFi 直连 + 文件发送 ─────────────────────────────────── */

int camera_api_connect_wifi(CameraAPI *api, const char *ip_addr, uint16_t port) {
    if (!api || !ip_addr) return CAM_ERR_INVALID_PARAM;

    _lock(api);

    if (api->status >= STATUS_CONNECTED) {
        _unlock(api);
        return CAM_ERR_ALREADY_CONNECTED;
    }

    _set_status_locked(api, STATUS_CONNECTING);

    /* 初始化 PTP 会话 (WiFi 模式) */
    ptp_session_init(&api->session, -1, PTP_TRANSPORT_WIFI, 0, 0);

    /* 创建 WiFi 适配器 */
    api->adapter = adapter_create_wifi(ip_addr, port);
    if (!api->adapter) {
        _set_status_locked(api, STATUS_ERROR);
        _unlock(api);
        return CAM_ERR_OUT_OF_MEMORY;
    }

    /* 注入 session 到 WiFi 适配器 */
    adapter_wifi_set_session(api->adapter, &api->session);

    /* 执行连接 */
    CameraCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type       = CAM_CMD_CONNECT;
    cmd.timeout_ms = 5000;
    CameraResult res = adapter_execute(api->adapter, &cmd);
    if (res.result_code != 0) {
        adapter_destroy(api->adapter);
        free(api->adapter);
        api->adapter = NULL;
        _set_status_locked(api, STATUS_ERROR);
        _unlock(api);
        return res.result_code;
    }

    snprintf(api->current_camera.id, sizeof(api->current_camera.id),
             "%s|wifi|%d", ip_addr, port);
    snprintf(api->current_camera.model, sizeof(api->current_camera.model),
             "NIKON (WiFi)");
    api->current_camera.transport = 1;

    /* 启动事件监听 (HTTP 轮询) */
    api->watcher = watcher_create(&api->session, WATCHER_CHANNEL_HTTP_POLL);
    if (api->watcher) {
        watcher_set_wifi_endpoint(api->watcher, ip_addr,
                                  port > 0 ? port : NIKON_WIFI_DEFAULT_PORT);
        watcher_on_event(api->watcher, EVENT_NEW_FILE, _event_relay, api);
        watcher_on_event(api->watcher, EVENT_CAPTURE_COMPLETE, _event_relay, api);
        watcher_on_event(api->watcher, EVENT_ERROR, _event_relay, api);
        watcher_start(api->watcher);
    }

    _ensure_transfer_init(api);

    _set_status_locked(api, STATUS_CONNECTED);
    _unlock(api);
    return CAM_OK;
}

int camera_api_send_file(CameraAPI *api, const char *local_path,
                         uint32_t storage_id, const char *remote_name) {
    if (!api || !local_path) return CAM_ERR_INVALID_PARAM;

    _lock(api);
    if (!api->adapter || api->session.state < SESSION_OPEN) {
        _unlock(api);
        return CAM_ERR_NOT_CONNECTED;
    }

    int rc = mtp_send_file(&api->session, local_path, storage_id, 0, remote_name);
    _unlock(api);

    return (rc == 0) ? CAM_OK : CAM_ERR_TRANSFER_FAILED;
}

/* ═══════════════════════════════════════════════════════════════
 *  通用事件监听
 * ══════════════════════════════════════════════════════════════ */

int camera_api_on_event(CameraAPI *api, EventType type,
                         event_handler_fn handler, void *user_data) {
    if (!api || type >= MAX_EVENT_HANDLERS) return CAM_ERR_INVALID_PARAM;
    _lock(api);
    api->event_handlers[type]  = handler;
    api->event_user_data[type] = user_data;
    _unlock(api);
    return CAM_OK;
}

void camera_api_remove_handler(CameraAPI *api, EventType type) {
    if (!api || type >= MAX_EVENT_HANDLERS) return;
    _lock(api);
    api->event_handlers[type]  = NULL;
    api->event_user_data[type] = NULL;
    _unlock(api);
}

/* ═══════════════════════════════════════════════════════════════
 *  错误码 → 人类可读字符串
 * ══════════════════════════════════════════════════════════════ */

const char *camera_api_strerror(int error_code) {
    switch (error_code) {
    case CAM_OK:                      return "操作成功";
    case CAM_ERR_NOT_CONNECTED:       return "相机未连接";
    case CAM_ERR_ALREADY_CONNECTED:   return "相机已连接";
    case CAM_ERR_TIMEOUT:             return "操作超时";
    case CAM_ERR_NOT_SUPPORTED:       return "不支持的操作";
    case CAM_ERR_BUSY:                return "相机正忙, 请稍后重试";
    case CAM_ERR_TRANSFER_FAILED:     return "文件传输失败";
    case CAM_ERR_FILE_NOT_FOUND:      return "文件不存在";
    case CAM_ERR_OUT_OF_MEMORY:       return "内存不足";
    case CAM_ERR_PERMISSION_DENIED:   return "权限被拒绝 (请检查 USB 授权)";
    case CAM_ERR_USB:                 return "USB 通信错误";
    case CAM_ERR_WIFI:                return "Wi-Fi 通信错误";
    case CAM_ERR_BLUETOOTH:           return "蓝牙通信错误";
    case CAM_ERR_PROTOCOL:            return "PTP/MTP 协议错误";
    case CAM_ERR_INVALID_PARAM:       return "无效参数";
    default:                          return "未知错误";
    }
}
