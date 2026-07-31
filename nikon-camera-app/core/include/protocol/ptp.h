/**
 * protocol/ptp.h — PTP/MTP 协议层接口
 *
 * 实现 ISO 15740 标准 PTP 协议 + 尼康私有扩展 (0x90xx / 0x40xx)。
 */
#ifndef NIKON_PROTOCOL_PTP_H
#define NIKON_PROTOCOL_PTP_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ─── 标准 PTP 操作码 ────────────────────────────────────────── */

typedef enum {
    PTP_OC_GetDeviceInfo         = 0x1001,
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
    PTP_OC_SendObjectInfo        = 0x100C,
    PTP_OC_SendObject            = 0x100D,
    PTP_OC_InitiateCapture       = 0x100E,
    PTP_OC_ResetDevice           = 0x1010,
    PTP_OC_GetDevicePropDesc     = 0x1014,
    PTP_OC_GetDevicePropValue    = 0x1015,
    PTP_OC_SetDevicePropValue    = 0x1016,
} PtpOperationCode;

/* ─── 标准 PTP 响应码 ────────────────────────────────────────── */

typedef enum {
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
} PtpResponseCode;

/* ─── 尼康私有操作码 ─────────────────────────────────────────── */

typedef enum {
    /* 实时取景 */
    NIKON_OC_StartLiveView       = 0x9201,
    NIKON_OC_EndLiveView         = 0x9202,
    NIKON_OC_GetLiveViewImage    = 0x9203,

    /* 拍摄控制 */
    NIKON_OC_AutoFocus           = 0x90C0,
    NIKON_OC_Capture             = 0x90C1,
    NIKON_OC_ShutterRelease      = 0x90C2,
    NIKON_OC_ChangeShutterSpeed  = 0x90C6,
    NIKON_OC_ChangeAperture      = 0x90C8,
    NIKON_OC_ChangeISO           = 0x90CA,
    NIKON_OC_GetPictCtrlData     = 0x90CC,  /* 读取 Picture Control */
    NIKON_OC_SetPictCtrlData     = 0x90CD,  /* 写入 Picture Control */

    /* 事件与状态 */
    NIKON_OC_DeviceReady         = 0x90D0,
    NIKON_OC_GetEvent            = 0x40C7,  /* 获取尼康事件队列 */
    NIKON_OC_GetStream           = 0x90E0,

    /* 文件传输 */
    NIKON_OC_SendFileObject      = 0x90A0,
} NikonOperationCode;

/* ─── 尼康事件码 ────────────────────────────────────────────── */

typedef enum {
    NIKON_EVENT_ObjectAdded      = 0x40C1,  /* 新文件产生 */
    NIKON_EVENT_CaptureComplete  = 0x40C3,  /* 拍摄完成 */
    NIKON_EVENT_ImageTransferred = 0x40C4,  /* 图片传输完毕 */
    NIKON_EVENT_ShutterSpeed     = 0x40D0,  /* 快门速度变化 */
    NIKON_EVENT_Aperture         = 0x40D1,  /* 光圈变化 */
    NIKON_EVENT_ISO              = 0x40D2,  /* ISO 变化 */
    NIKON_EVENT_ExposureComp     = 0x40D3,  /* 曝光补偿变化 */
    NIKON_EVENT_FocusMode        = 0x40D4,  /* 对焦模式变化 */
} NikonEventCode;

/* ─── 尼康设备属性码 ─────────────────────────────────────────── */

typedef enum {
    NIKON_PROP_WhiteBalance      = 0xD00A,
    NIKON_PROP_ShutterSpeed      = 0xD00C,
    NIKON_PROP_Aperture          = 0xD00E,
    NIKON_PROP_ISO               = 0xD010,
    NIKON_PROP_ExposureComp      = 0xD012,
    NIKON_PROP_FocusMode         = 0xD014,
    NIKON_PROP_FlashMode         = 0xD016,
    NIKON_PROP_ImageQuality      = 0xD01A,  /* 0=RAW 1=JPEG 2=RAW+JPEG */
    NIKON_PROP_ImageSize         = 0xD01C,  /* 0=L 1=M 2=S */
    NIKON_PROP_AutoISO           = 0xD054,
} NikonDeviceProperty;

/* ─── 存储 ID 常量 ────────────────────────────────────────────── */

#define NIKON_STORAGE_CF    0x00010001u     /* CompactFlash */
#define NIKON_STORAGE_SD    0x00010002u     /* SD Card */

/* ─── 数据包结构 (packed) ────────────────────────────────────── */

/** PTP 容器头 (所有数据包共享) */
typedef struct __attribute__((packed)) {
    uint32_t    length;         /**< 包总长度 (字节, 含此头) */
    uint16_t    packet_type;    /**< 1=命令 2=数据 3=响应 4=事件 */
    uint16_t    code;           /**< 操作码/响应码/事件码 */
    uint32_t    transaction_id; /**< 事务 ID (单调递增) */
} PtpContainerHeader;

#define PTP_PKT_TYPE_COMMAND    1u
#define PTP_PKT_TYPE_DATA       2u
#define PTP_PKT_TYPE_RESPONSE   3u
#define PTP_PKT_TYPE_EVENT      4u

/** PTP 命令包 */
typedef struct __attribute__((packed)) {
    PtpContainerHeader header;
    uint16_t    operation_code;
    uint16_t    session_id;         /**< 仅 OpenSession 使用 */
    uint32_t    params[5];          /**< 最多 5 个操作参数 */
} PtpCommandPacket;

/** PTP 响应包 */
typedef struct __attribute__((packed)) {
    PtpContainerHeader header;
    uint16_t    response_code;
    uint32_t    params[5];
} PtpResponsePacket;

/** PTP 数据包头 (后接变长 payload) */
typedef struct __attribute__((packed)) {
    PtpContainerHeader header;
    /* payload bytes follow immediately */
} PtpDataPacketHeader;

/* ─── 会话状态 ───────────────────────────────────────────────── */

typedef enum {
    SESSION_DISCONNECTED = 0,
    SESSION_CONNECTING,
    SESSION_OPEN,       /**< PTP 会话已建立 */
    SESSION_ACTIVE,     /**< 数据传输中 */
    SESSION_ERROR,
    SESSION_CLOSING,
} SessionState;

/** 传输层类型 */
typedef enum {
    PTP_TRANSPORT_USB  = 0,
    PTP_TRANSPORT_WIFI = 1,
} PtpTransport;

/** PTP 会话上下文 */
typedef struct {
    SessionState    state;
    uint32_t        session_id;             /**< PTP Session ID */
    uint32_t        transaction_id;         /**< 当前事务 ID (自增) */
    int             fd;                     /**< 传输层 fd (USB fd or socket) */
    PtpTransport    transport;              /**< 传输层类型 */
    uint8_t         ep_out;                 /**< USB Bulk OUT 端点 (WiFi 忽略) */
    uint8_t         ep_in;                  /**< USB Bulk IN 端点 (WiFi 忽略) */
    uint64_t        last_heartbeat_ms;      /**< 最后一次心跳时间 (ms) */
    uint64_t        heartbeat_interval_ms;  /**< 心跳间隔 (默认 5000) */
    void           *transport_ctx;          /**< 传输层私有上下文 */
} PtpSession;

/** USB 默认端点地址 (尼康相机常见配置) */
#define PTP_USB_EP_OUT_DEFAULT  0x02u
#define PTP_USB_EP_IN_DEFAULT   0x81u

/* ─── 会话接口 ───────────────────────────────────────────────── */

/**
 * 初始化会话结构体 (不建立连接)。
 *
 * @param fd        传输层文件描述符 (USB: hal_usb_open 返回; WiFi: 任意负值)
 * @param transport 传输类型
 * @param ep_out    USB Bulk OUT 端点 (WiFi 传 0)
 * @param ep_in     USB Bulk IN 端点 (WiFi 传 0)
 */
int  ptp_session_init(PtpSession *session, int fd,
                      PtpTransport transport, uint8_t ep_out, uint8_t ep_in);

/** 执行 OpenSession + GetDeviceInfo 流程。 */
int  ptp_session_open(PtpSession *session);

/** 发送心跳包 (GetEvent 或 GetDevicePropDesc)。 */
int  ptp_session_heartbeat(PtpSession *session);

/** 关闭 PTP 会话 (CloseSession)。 */
int  ptp_session_close(PtpSession *session);

/** 获取并递增事务 ID。 */
uint32_t ptp_session_next_transaction(PtpSession *session);

/* ─── 底层传输接口 ───────────────────────────────────────────── */

/**
 * 发送 PTP 命令包并接收响应。
 * 如有数据阶段 data_buf/data_len 非 NULL。
 *
 * @param session        会话
 * @param opcode         操作码
 * @param params         操作参数 (最多 5 个, 不足补 0)
 * @param param_count    有效参数个数
 * @param data_buf       发送数据 (可 NULL)
 * @param data_len       数据长度
 * @param out_buf        接收数据缓冲区 (可 NULL)
 * @param out_max        接收缓冲区大小
 * @param out_len        [out] 实际接收长度
 * @return PTP_RC_OK; 或 PTP_RC_* / 负错误码
 */
int ptp_exec(PtpSession *session,
             uint16_t opcode,
             const uint32_t *params, int param_count,
             const uint8_t *data_buf, uint32_t data_len,
             uint8_t *out_buf, uint32_t out_max, uint32_t *out_len);

/**
 * 获取尼康事件队列。
 *
 * @param session    会话
 * @param events     [out] 事件码数组
 * @param max_events 数组容量
 * @return >= 0 事件数量; < 0 错误
 */
int ptp_get_events(PtpSession *session, uint32_t *events, int max_events);

/**
 * 读取设备属性值。
 *
 * @param prop_id    NikonDeviceProperty
 * @param value      [out]
 * @return PTP_RC_OK; < 0 错误
 */
int ptp_get_device_prop(PtpSession *session, uint16_t prop_id, uint32_t *value);

/**
 * 写入设备属性值。
 */
int ptp_set_device_prop(PtpSession *session, uint16_t prop_id, uint32_t value);

#ifdef __cplusplus
}
#endif
#endif /* NIKON_PROTOCOL_PTP_H */
