# 协议层规格书 — PTP/MTP 引擎

## 1. PTP 协议基础

### 1.1 标准 PTP 操作码

```c
enum PtpOperationCode {
    PTP_OC_OpenSession           = 0x1002,
    PTP_OC_CloseSession          = 0x1003,
    PTP_OC_GetStorageIDs         = 0x1004,
    PTP_OC_GetStorageInfo        = 0x1005,
    PTP_OC_GetNumObjects         = 0x1006,
    PTP_OC_GetObjectHandles      = 0x1007,
    PTP_OC_GetObjectInfo         = 0x1008,
    PTP_OC_GetObject             = 0x1009,
    PTP_OC_GetThumb              = 0x100A,
    PTP_OC_DeleteObject          = 0x100B,
    PTP_OC_InitiateCapture       = 0x100E,
    PTP_OC_GetDeviceInfo         = 0x1001,
    PTP_OC_GetDevicePropDesc     = 0x1014,
    PTP_OC_GetDevicePropValue    = 0x1015,
    PTP_OC_SetDevicePropValue    = 0x1016,
    PTP_OC_ResetDevice           = 0x1010,
    PTP_OC_SendObject            = 0x100D,
    PTP_OC_GetEvent              = 0x40C7,
};
```

### 1.2 响应码

```c
enum PtpResponseCode {
    PTP_RC_OK                    = 0x2001,
    PTP_RC_GeneralError          = 0x2002,
    PTP_RC_SessionNotOpen        = 0x2003,
    PTP_RC_InvalidTransaction    = 0x2004,
    PTP_RC_OperationNotSupported = 0x2005,
    PTP_RC_ParameterNotSupported = 0x2006,
    PTP_RC_DeviceBusy            = 0x2019,
    PTP_RC_AccessDenied          = 0x200A,
    PTP_RC_NoThumbnailPresent    = 0x200B,
    PTP_RC_StoreFull             = 0x200C,
    PTP_RC_ObjectWriteProtected  = 0x200D,
    PTP_RC_StoreReadOnly         = 0x200E,
};
```

### 1.3 PTP 数据包结构

```c
typedef struct __attribute__((packed)) {
    uint32_t    length;
    uint16_t    packet_type;     // 1=命令, 2=数据, 3=响应, 4=事件
    uint16_t    transaction_id;
} PtpContainerHeader;

typedef struct __attribute__((packed)) {
    PtpContainerHeader header;
    uint16_t    operation_code;
    uint16_t    session_id;
    uint32_t    params[5];
} PtpCommandPacket;

typedef struct __attribute__((packed)) {
    PtpContainerHeader header;
    uint16_t    response_code;
    uint32_t    params[5];
} PtpResponsePacket;

typedef struct __attribute__((packed)) {
    PtpContainerHeader header;
    uint8_t     data[];
} PtpDataPacket;
```

### 1.4 会话建立流程

```
客户端                         相机
  │                             │
  │── PTP_OC_OpenSession ──────►│
  │    (Transaction=1)          │
  │◄── PTP_RC_OK ───────────────│
  │    (SessionID=0x1234)       │
  │                             │
  │── PTP_OC_GetDeviceInfo ────►│
  │◄── (Data Phase) ────────────│
  │◄── PTP_RC_OK ───────────────│
  │                             │
  │── PTP_OC_GetStorageIDs ────►│
  │◄── (Storage IDs) ───────────│
  │◄── PTP_RC_OK ───────────────│
  │                             │
  │── PTP_OC_GetObjectHandles ──►│
  │◄── (Object Handles) ────────│
  │◄── PTP_RC_OK ───────────────│
```

## 2. 尼康私有协议扩展

### 2.1 尼康特定操作码

```c
enum NikonOperationCode {
    NIKON_OC_StartLiveView      = 0x9201,
    NIKON_OC_EndLiveView        = 0x9202,
    NIKON_OC_GetLiveViewImage   = 0x9203,
    NIKON_OC_AutoFocus          = 0x90C0,
    NIKON_OC_Capture            = 0x90C1,
    NIKON_OC_ShutterRelease     = 0x90C2,
    NIKON_OC_ChangeShutterSpeed = 0x90C6,
    NIKON_OC_ChangeAperture     = 0x90C8,
    NIKON_OC_ChangeISO          = 0x90CA,
    NIKON_OC_ChangeWhiteBalance = 0x90CC,
    NIKON_OC_DeviceReady        = 0x90D0,
    NIKON_OC_GetEvent           = 0x40C7,
    NIKON_OC_GetStream          = 0x90E0,
    NIKON_OC_SendFileObject     = 0x90A0,
    NIKON_OC_GetPictCtrl        = 0x9205,
    NIKON_OC_SetPictCtrl        = 0x9206,
};

enum NikonEventCode {
    NIKON_EVENT_ObjectAdded      = 0x40C1,
    NIKON_EVENT_CaptureComplete  = 0x40C3,
    NIKON_EVENT_ImageTransferred = 0x40C4,
    NIKON_EVENT_ShutterSpeed     = 0x40D0,
    NIKON_EVENT_Aperture         = 0x40D1,
    NIKON_EVENT_ISO              = 0x40D2,
};
```

### 2.2 尼康设备属性

```c
enum NikonDeviceProperty {
    NIKON_PROP_WhiteBalance      = 0xD00A,
    NIKON_PROP_ShutterSpeed      = 0xD00C,
    NIKON_PROP_Aperture          = 0xD00E,
    NIKON_PROP_ISO               = 0xD010,
    NIKON_PROP_ExposureComp      = 0xD012,
    NIKON_PROP_FocusMode         = 0xD014,
    NIKON_PROP_FlashMode         = 0xD016,
    NIKON_PROP_ImageQuality      = 0xD01A,
    NIKON_PROP_ImageSize         = 0xD01C,
};

#define NIKON_STORAGE_CF  0x00010001
#define NIKON_STORAGE_SD  0x00010002
```

## 3. 会话管理与心跳

### 3.1 传输层类型

```c
typedef enum {
    PTP_TRANSPORT_USB  = 0,
    PTP_TRANSPORT_WIFI = 1,
} PtpTransport;
```

### 3.2 会话结构

```c
enum SessionState {
    SESSION_DISCONNECTED = 0,
    SESSION_CONNECTING,
    SESSION_OPEN,
    SESSION_ACTIVE,
    SESSION_ERROR,
    SESSION_CLOSING,
};

typedef struct {
    enum SessionState   state;
    uint32_t            session_id;
    uint16_t            transaction_id;
    int                 fd;
    PtpTransport        transport;          // 传输层类型 (USB/Wi-Fi)
    uint8_t             ep_out;             // USB 输出端点号
    uint8_t             ep_in;              // USB 输入端点号
    uint64_t            last_heartbeat;
    uint64_t            heartbeat_interval_ms;
    void*               transport_ctx;
} PtpSession;
```

### 3.3 会话接口

```c
// 初始化会话 (参数化: 不再硬编码端点)
int ptp_session_init(PtpSession* session, int fd,
                     PtpTransport transport, uint8_t ep_out, uint8_t ep_in);

int ptp_session_open(PtpSession* session);
int ptp_session_heartbeat(PtpSession* session);
int ptp_session_close(PtpSession* session);
uint16_t ptp_session_next_transaction(PtpSession* session);
```

### 3.4 托管会话 (含心跳线程 + 重连回调)

```c
// 心跳重连回调
typedef void (*session_reconnect_cb)(PtpSession* session, void* user_data);

typedef struct {
    PtpSession          session;
    pthread_t           heartbeat_thread;
    _Atomic int         stop;              // 原子停止标志
    session_reconnect_cb reconnect_cb;     // 重连回调
    void*               reconnect_user_data;
} ManagedSession;

// 创建托管会话 (参数化)
ManagedSession* managed_session_create(int fd, PtpTransport transport,
                                        uint8_t ep_out, uint8_t ep_in);

// 设置重连回调 (心跳连续失败 3 次触发)
void managed_session_set_reconnect_cb(ManagedSession* ms,
                                       session_reconnect_cb cb,
                                       void* user_data);

void managed_session_destroy(ManagedSession* ms);
```

## 4. 数据传输

### 4.1 分块传输参数

```c
#define CHUNK_SIZE_DEFAULT     (1024 * 1024)
#define CHUNK_SIZE_MAX         (4 * 1024 * 1024)
#define CHUNK_SIZE_MIN         (64 * 1024)
#define MAX_CONCURRENT_CHUNKS  4

typedef struct {
    uint32_t        object_handle;
    uint64_t        file_size;
    uint64_t        offset;
    uint32_t        chunk_size;
    uint32_t        chunk_index;
    uint64_t        transferred;
    uint64_t        timestamp_start;
    uint8_t         md5[16];
} TransferContext;

typedef void (*transfer_progress_cb)(TransferContext* ctx, int percent, void* user_data);
typedef void (*transfer_complete_cb)(TransferContext* ctx, int error_code, void* user_data);
```

## 5. MTP 扩展 (文件传输)

```c
typedef struct __attribute__((packed)) {
    uint32_t    storage_id;
    uint16_t    object_format;    // 0x3801=JPEG, 0x3805=NEF(RAW)
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
    char        parent_object;
    uint16_t    association_type;
    uint32_t    association_desc;
    uint32_t    sequence_number;
    char        filename[256];
    char        capture_date[32];
    char        modification_date[32];
    uint32_t    keywords[5];
} MtpObjectInfo;
```
