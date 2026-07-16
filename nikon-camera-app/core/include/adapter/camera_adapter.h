/**
 * adapter/camera_adapter.h — 相机适配器抽象接口
 *
 * 定义统一命令接口 (CameraCommand / CameraResult) 和命令映射表。
 * 具体实现:
 *   NikonPTPAdapter  — USB 直连, adapter/nikon_adapter.c
 *   NikonWiFiAdapter — Wi-Fi 连接, adapter/wifi_adapter.c
 */
#ifndef NIKON_CAMERA_ADAPTER_H
#define NIKON_CAMERA_ADAPTER_H

#include <stdint.h>
#include "api/camera_api.h"
#include "protocol/ptp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ─── 命令类型 ───────────────────────────────────────────────── */

typedef enum {
    CAM_CMD_CONNECT = 0,
    CAM_CMD_DISCONNECT,
    CAM_CMD_CAPTURE,
    CAM_CMD_CAPTURE_BURST,
    CAM_CMD_STOP_BURST,
    CAM_CMD_START_LIVEVIEW,
    CAM_CMD_STOP_LIVEVIEW,
    CAM_CMD_GET_LIVEVIEW,
    CAM_CMD_AUTOFOCUS,
    CAM_CMD_SET_PROPERTY,
    CAM_CMD_GET_PROPERTY,
    CAM_CMD_GET_PICTCTRL,
    CAM_CMD_SET_PICTCTRL,
    CAM_CMD_LIST_FILES,
    CAM_CMD_GET_FILE,
    CAM_CMD_GET_THUMBNAIL,
    CAM_CMD_DELETE_FILE,
    CAM_CMD_SEND_FILE,
    CAM_CMD_FORMAT_STORAGE,
    CAM_CMD_DEVICE_INFO,
    CAM_CMD_EVENT_POLL,
} CameraCommandType;

/** 统一相机命令结构体 */
typedef struct {
    CameraCommandType type;
    union {
        struct {
            int     transport;          /**< 0=USB 1=Wi-Fi */
            char    params[256];        /**< IP:port 或 USB path */
        } connect;
        struct {
            int     count;
            int     interval_ms;
        } capture;
        struct {
            uint16_t    property_id;
            uint32_t    value;
        } set_property;
        struct {
            uint16_t    property_id;
        } get_property;
        struct {
            uint32_t    object_handle;
            char        dest_path[512]; /**< 本地目标路径 */
        } file;
        struct {
            char        local_path[512];
            uint32_t    storage_id;
            char        remote_path[256];
        } send_file;
        struct {
            uint32_t    storage_id;     /**< 0=全部 */
        } list_files;
        PictureControl  pictctrl;
    } params;

    uint32_t    timeout_ms;             /**< 0=使用默认值 */
    int         priority;               /**< 0=低 1=普通 2=高 */
} CameraCommand;

/** 命令执行结果 */
typedef struct {
    int         result_code;    /**< 0=成功; < 0=CAM_ERR_* */
    uint32_t    data_size;      /**< 返回数据字节数 */
    void       *data;           /**< 返回数据 (调用者 free()) */
    char        error_msg[256];
} CameraResult;

/* ─── 命令映射表条目 ─────────────────────────────────────────── */

typedef struct {
    CameraCommandType   cmd_type;
    uint16_t            ptp_opcode;         /**< 对应 PTP/尼康操作码 */
    int                 nikon_private;       /**< 1=尼康私有指令 */
    int                 needs_session;       /**< 1=需要已建立会话 */
    int                 timeout_default_ms;
} CommandMapping;

/** 尼康 Z/D 系命令映射表 (在 command_map.c 中定义)。 */
extern const CommandMapping kNikonCommandMappings[];
extern const int kNikonCommandMappingCount;

/* ─── 适配器接口 (函数表模式) ───────────────────────────────── */

/** 相机适配器虚函数表 */
typedef struct CameraAdapterVtable {
    /** 执行命令 */
    CameraResult (*execute)(void *ctx, CameraCommand *cmd);
    /** 获取连接状态 */
    ConnectionStatus (*get_status)(void *ctx);
    /** 注册事件回调 */
    void (*register_event)(void *ctx, EventType type,
                           event_handler_fn handler, void *user_data);
    /** 销毁适配器 */
    void (*destroy)(void *ctx);
} CameraAdapterVtable;

/** 适配器实例 */
typedef struct {
    const CameraAdapterVtable  *vtable;
    void                       *ctx;    /**< 适配器私有上下文 */
} CameraAdapter;

/* ─── 工厂函数 ───────────────────────────────────────────────── */

/**
 * 创建 USB PTP 适配器 (对应 NikonPTPAdapter)。
 * @param session 已初始化的 PTP 会话
 */
CameraAdapter *adapter_create_ptp(PtpSession *session);

/**
 * 创建 Wi-Fi HTTP 适配器 (对应 NikonWiFiAdapter)。
 * @param ip_addr 相机 IP
 * @param port    端口 (通常 15740)
 */
CameraAdapter *adapter_create_wifi(const char *ip_addr, uint16_t port);

/** 执行命令 (调用 vtable->execute)。 */
static inline CameraResult adapter_execute(CameraAdapter *a, CameraCommand *cmd) {
    return a->vtable->execute(a->ctx, cmd);
}

/** 销毁适配器。 */
static inline void adapter_destroy(CameraAdapter *a) {
    a->vtable->destroy(a->ctx);
}

#ifdef __cplusplus
}
#endif
#endif /* NIKON_CAMERA_ADAPTER_H */
