# 适配层规格书 — CameraCommand 抽象与尼康适配

## 1. 统一命令接口

```c
typedef enum {
    CAM_CMD_CONNECT = 0,
    CAM_CMD_DISCONNECT,
    CAM_CMD_CAPTURE,
    CAM_CMD_CAPTURE_BURST,
    CAM_CMD_STOP_BURST,          // 停止连拍
    CAM_CMD_START_LIVEVIEW,
    CAM_CMD_STOP_LIVEVIEW,
    CAM_CMD_GET_LIVEVIEW,
    CAM_CMD_AUTOFOCUS,
    CAM_CMD_SET_PROPERTY,
    CAM_CMD_GET_PROPERTY,
    CAM_CMD_GET_PICTCTRL,        // 获取照片调控参数
    CAM_CMD_SET_PICTCTRL,        // 设置照片调控参数
    CAM_CMD_LIST_FILES,
    CAM_CMD_GET_FILE,
    CAM_CMD_DELETE_FILE,
    CAM_CMD_GET_THUMBNAIL,
    CAM_CMD_SEND_FILE,
    CAM_CMD_FORMAT_STORAGE,
    CAM_CMD_DEVICE_INFO,
    CAM_CMD_EVENT_POLL,
} CameraCommandType;

typedef struct {
    CameraCommandType   type;
    union {
        struct {
            uint16_t    property_id;
            uint32_t    value;
        } set_property;
        struct {
            uint16_t    property_id;
        } get_property;
        struct {
            uint32_t    object_handle;
            char        path[512];
        } file;
        struct {
            int         count;
            int         interval_ms;
        } capture;
        struct {
            int         transport;       // 0=USB, 1=Wi-Fi
            char        params[256];
        } connect;
        struct {
            char        local_path[512];
            uint32_t    storage_id;
            char        remote_path[256];
        } send_file;
    } params;
    uint32_t            timeout_ms;
    int                 priority;
} CameraCommand;

typedef struct {
    int             result_code;
    uint32_t        data_size;
    void*           data;
    char            error_msg[256];
} CameraResult;

typedef CameraResult (*command_handler_fn)(CameraCommand* cmd, void* context);

typedef struct {
    CameraCommandType   cmd_type;
    uint16_t            ptp_opcode;
    int                 nikon_private;
    int                 needs_session;
    int                 timeout_default_ms;
} CommandMapping;
```

## 2. 适配器虚表接口

```c
// 适配器虚表 (函数指针表模式)
typedef struct {
    CameraResult (*execute)(void* ctx, const CameraCommand* cmd);
    int          (*get_status)(void* ctx);
    int          (*register_event)(void* ctx, event_handler_fn handler, void* user_data);
    void         (*destroy)(void* ctx);
} CameraAdapterVtable;

typedef struct {
    void*                   ctx;       // 适配器私有上下文
    const CameraAdapterVtable* vtable; // 虚表指针
} CameraAdapter;

// 内联操作
static inline CameraResult adapter_execute(CameraAdapter* a, const CameraCommand* cmd) {
    return a->vtable->execute(a->ctx, cmd);
}
static inline int adapter_get_status(CameraAdapter* a) {
    return a->vtable->get_status(a->ctx);
}
static inline int adapter_register_event(CameraAdapter* a, event_handler_fn h, void* ud) {
    return a->vtable->register_event(a->ctx, h, ud);
}
static inline void adapter_destroy(CameraAdapter* a) {
    if (a) { a->vtable->destroy(a->ctx); free(a); }
}
```

## 3. 尼康命令映射表

```c
static CommandMapping kNikonCommandMappings[] = {
    { CAM_CMD_CONNECT,          PTP_OC_OpenSession,   0, 0, 5000 },
    { CAM_CMD_DISCONNECT,       PTP_OC_CloseSession,  0, 1, 2000 },
    { CAM_CMD_DEVICE_INFO,      PTP_OC_GetDeviceInfo, 0, 1, 3000 },
    { CAM_CMD_LIST_FILES,       PTP_OC_GetObjectHandles, 0, 1, 5000 },
    { CAM_CMD_GET_FILE,         PTP_OC_GetObject,     0, 1, 30000 },
    { CAM_CMD_GET_THUMBNAIL,    PTP_OC_GetThumb,      0, 1, 5000 },
    { CAM_CMD_DELETE_FILE,      PTP_OC_DeleteObject,  0, 1, 3000 },
    { CAM_CMD_CAPTURE,          NIKON_OC_Capture,      1, 1, 10000 },
    { CAM_CMD_CAPTURE_BURST,    NIKON_OC_Capture,      1, 1, 30000 },
    { CAM_CMD_STOP_BURST,       NIKON_OC_EndLiveView,  1, 1, 3000 },
    { CAM_CMD_START_LIVEVIEW,   NIKON_OC_StartLiveView,1, 1, 5000 },
    { CAM_CMD_STOP_LIVEVIEW,    NIKON_OC_EndLiveView,  1, 1, 3000 },
    { CAM_CMD_GET_LIVEVIEW,     NIKON_OC_GetLiveViewImage,1,1,3000 },
    { CAM_CMD_AUTOFOCUS,        NIKON_OC_AutoFocus,    1, 1, 5000 },
    { CAM_CMD_SET_PROPERTY,     PTP_OC_SetDevicePropValue, 0, 1, 3000 },
    { CAM_CMD_GET_PROPERTY,     PTP_OC_GetDevicePropValue, 0, 1, 3000 },
    { CAM_CMD_GET_PICTCTRL,     NIKON_OC_GetPictCtrl,  1, 1, 3000 },
    { CAM_CMD_SET_PICTCTRL,     NIKON_OC_SetPictCtrl,  1, 1, 3000 },
    { CAM_CMD_EVENT_POLL,       NIKON_OC_GetEvent,     1, 1, 3000 },
};
```

## 4. 适配器实现

### 4.1 USB PTP 适配器

```c
// NikonPTPCtx — USB PTP 适配器私有上下文
typedef struct {
    PtpSession*     session;
    int             status;          // ConnectionStatus
    event_handler_fn event_handler;
    void*           event_user_data;
} NikonPTPCtx;

// kNikonPTPVtable — USB PTP 虚表
static const CameraAdapterVtable kNikonPTPVtable = {
    .execute        = nikon_ptp_execute,
    .get_status     = nikon_ptp_get_status,
    .register_event = nikon_ptp_register_event,
    .destroy        = nikon_ptp_destroy,
};

// 工厂函数
CameraAdapter* adapter_create_ptp(PtpSession *session);
```

### 4.2 Wi-Fi 适配器

```c
// NikonWiFiCtx — Wi-Fi 适配器私有上下文 (独立于 PTP)
typedef struct {
    char            ip_addr[64];
    uint16_t        port;
    int             socket_fd;
    PtpSession*     session;         // 可选, 通过 set_session 注入
    int             status;
    event_handler_fn event_handler;
    void*           event_user_data;
} NikonWiFiCtx;

// kNikonWiFiVtable — Wi-Fi 虚表 (独立实现, 不复用 PTP)
static const CameraAdapterVtable kNikonWiFiVtable = {
    .execute        = nikon_wifi_execute,
    .get_status     = nikon_wifi_get_status,
    .register_event = nikon_wifi_register_event,
    .destroy        = nikon_wifi_destroy,
};

// 工厂函数
CameraAdapter* adapter_create_wifi(const char *ip_addr, uint16_t port);

// 注入外部 PtpSession (用于 Wi-Fi 模式下复用 PTP 协议)
void adapter_wifi_set_session(CameraAdapter *adapter, PtpSession *session);
```

### 4.3 适配器工厂

```
┌──────────────────────────────────────────────────┐
│                  CameraAdapter                     │
│  ctx: void*                                       │
│  vtable: const CameraAdapterVtable*               │
└──────────────────┬───────────────────────────────┘
                   │
        ┌──────────┴──────────┐
        ▼                     ▼
┌─────────────────┐  ┌─────────────────┐
│ NikonPTPAdapter  │  │ NikonWiFiAdapter │
│ ctx=NikonPTPCtx  │  │ ctx=NikonWiFiCtx │
│ vtable=PTP       │  │ vtable=WiFi      │
│ • USB 直连       │  │ • HTTP API 封装  │
│ • 零拷贝传输     │  │ • keep-alive     │
│ • 实时控制       │  │ • 可注入 Session │
└─────────────────┘  └─────────────────┘
```

## 5. 连接状态机

```
                  ┌─────────────┐
                  │  DISCONNECT │
                  └──────┬──────┘
                         │ connect()
                    ┌────▼────┐
                    │CONNECTING│
                    └────┬────┘
               ┌─────────┼──────────┐
               ▼                    ▼
        ┌────────────┐    ┌───────────────┐
        │ PTP_ERROR   │    │ PTP_CONNECTED │
        └────────────┘    └───────┬───────┘
                                  │ heartbeat OK
                           ┌──────▼──────┐
                           │ PTP_ACTIVE  │
                           └──────┬──────┘
                       ┌──────────┼──────────┐
                       ▼          ▼          ▼
                 ┌────────┐ ┌──────────┐ ┌────────┐
                 │TRANSFER│ │ CAPTURE  │ │SET_PROP│
                 └───┬────┘ └────┬─────┘ └───┬────┘
                     └───────────┼───────────┘
                                 │
                          ┌──────▼──────┐
                          │ PTP_ACTIVE  │
                          └─────────────┘
```

## 6. 错误码与重连策略

| 错误码 | 含义 | 重连动作 |
|--------|------|---------|
| ERR_PTP_SESSION_CLOSED | 会话被相机关闭 | 自动重连，最多 3 次，间隔 1s |
| ERR_USB_DISCONNECTED | USB 线缆断开 | 等待重新插入，回调通知 |
| ERR_WIFI_DISCONNECTED | Wi-Fi 断开 | 自动重连，最多 5 次，间隔 3s |
| ERR_TRANSFER_TIMEOUT | 传输超时 | 断点续传，从失败位置继续 |
| ERR_DEVICE_BUSY | 相机忙碌 | 等待 2s 后重试，最多 3 次 |
| ERR_OUT_OF_SPACE | 存储空间不足 | 通知用户 |
| CAM_ERR_INVALID_PARAM | 无效参数 | 立即返回错误，不重试 |
