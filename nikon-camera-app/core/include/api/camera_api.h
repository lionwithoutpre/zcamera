/**
 * api/camera_api.h — CameraAPI 业务层统一接口
 *
 * 对上层 (Android ViewModel / Desktop CLI) 暴露的全部功能入口。
 * 线程安全: 所有公开函数均可从任意线程调用。
 * 回调执行: 在独立事件线程中执行, 不得在回调内调用阻塞 API。
 */
#ifndef NIKON_CAMERA_API_H
#define NIKON_CAMERA_API_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ─── 前置声明 ───────────────────────────────────────────────── */

typedef struct CameraAPI CameraAPI;

/* ─── 传输方式 ───────────────────────────────────────────────── */

#define TRANSPORT_AUTO      0   /**< 优先 USB, 降级 Wi-Fi */
#define TRANSPORT_USB_ONLY  1
#define TRANSPORT_WIFI_ONLY 2

/* ─── 存储卡 ─────────────────────────────────────────────────── */

#define NIKON_STORAGE_CF    0x00010001u     /**< CompactFlash */
#define NIKON_STORAGE_SD    0x00010002u     /**< SD Card */

/* ─── 错误码 ─────────────────────────────────────────────────── */

#define CAM_OK                      0
#define CAM_ERR_NOT_CONNECTED      (-100)
#define CAM_ERR_ALREADY_CONNECTED  (-101)
#define CAM_ERR_TIMEOUT            (-102)
#define CAM_ERR_NOT_SUPPORTED      (-103)
#define CAM_ERR_BUSY               (-104)
#define CAM_ERR_TRANSFER_FAILED    (-200)
#define CAM_ERR_FILE_NOT_FOUND     (-201)
#define CAM_ERR_OUT_OF_MEMORY      (-300)
#define CAM_ERR_PERMISSION_DENIED  (-301)
#define CAM_ERR_USB                (-400)
#define CAM_ERR_WIFI               (-401)
#define CAM_ERR_BLUETOOTH          (-402)
#define CAM_ERR_PROTOCOL           (-500)
#define CAM_ERR_INVALID_PARAM      (-600)

/* ─── 连接状态 ───────────────────────────────────────────────── */

typedef enum {
    STATUS_DISCONNECTED = 0,
    STATUS_SCANNING,
    STATUS_CONNECTING,
    STATUS_CONNECTED,
    STATUS_TRANSFERRING,
    STATUS_ERROR,
} ConnectionStatus;

/* ─── 数据结构 ───────────────────────────────────────────────── */

/** 相机基本信息 */
typedef struct {
    char        id[64];             /**< 唯一标识 (序列号+接口类型) */
    char        model[64];          /**< 型号, e.g. "NIKON Z 8" */
    char        serial[32];         /**< 序列号 */
    int         transport;          /**< 0=USB 1=Wi-Fi */
    int         battery_level;      /**< 剩余电量 0-100 */
    uint64_t    storage_free;       /**< 存储卡剩余空间 (bytes) */
    uint64_t    storage_total;      /**< 存储卡总空间 (bytes) */
    uint32_t    shutter_count;      /**< 快门计数 */
} CameraInfo;

/** 文件信息 */
typedef struct {
    uint32_t    object_handle;      /**< PTP Object Handle */
    char        filename[256];      /**< 文件名 */
    uint64_t    size;               /**< 文件大小 (bytes) */
    char        datetime[32];       /**< 拍摄时间 ISO8601 */
    bool        is_raw;             /**< NEF/NRW 格式 */
    bool        is_jpeg;            /**< JPEG 格式 */
    int         width;              /**< 图像宽度 (像素) */
    int         height;             /**< 图像高度 (像素) */
    uint32_t    storage_id;         /**< 所在存储卡 */
} FileInfo;

/** 传输进度快照 */
typedef struct {
    int         job_id;
    uint32_t    object_handle;
    char        filename[256];
    uint64_t    total_size;
    uint64_t    transferred;
    int         percent;            /**< 0-100 */
    double      speed_mbps;         /**< 实时速度 MB/s */
    uint64_t    elapsed_ms;
    uint64_t    remaining_ms;
    int         status;             /**< 0=等待 1=传输中 2=完成 -1=失败 */
    int         error_code;
} TransferProgress;

/** FTP 服务器配置 */
typedef struct {
    char        host[128];
    uint16_t    port;               /**< 默认 21 */
    char        username[64];
    char        password[64];
    char        remote_path[256];
    bool        use_tls;            /**< FTPS */
    bool        auto_upload;        /**< 传输完成后自动上传 */
    bool        keep_original;      /**< 保留本地副本 */
} FtpConfig;

/** Picture Control 色彩参数 (PTP 0x90CC/0x90CD) */
typedef struct {
    int8_t  hue;            /**< 色相      -3 ~ +3 */
    int8_t  saturation;     /**< 饱和度    -3 ~ +3 */
    int8_t  contrast;       /**< 对比度    -3 ~ +3 */
    int8_t  clarity;        /**< 清晰度    -3 ~ +3 */
    int8_t  sharpening;     /**< 锐化      0 ~ 9 */
    int8_t  brightness;     /**< 亮度      -1 ~ +1 */
    int8_t  wb_ab;          /**< WB A-B 轴 -6 ~ +6 (琥珀→蓝) */
    int8_t  wb_gm;          /**< WB G-M 轴 -6 ~ +6 (绿→品红) */
    uint8_t color_space;    /**< 0=sRGB 1=AdobeRGB */
} PictureControl;

/* ─── 事件系统 ───────────────────────────────────────────────── */

typedef enum {
    EVENT_CONNECTION_CHANGED,
    EVENT_NEW_FILE,
    EVENT_TRANSFER_COMPLETE,
    EVENT_TRANSFER_PROGRESS,
    EVENT_ERROR,
    EVENT_CAPTURE_COMPLETE,
    EVENT_PROPERTY_CHANGED,
    EVENT_STORAGE_CHANGED,
} EventType;

typedef struct {
    EventType type;
    union {
        ConnectionStatus connection_status;
        struct { uint32_t object_handle; char filename[256]; } new_file;
        struct { int job_id; int error_code; }                 transfer_complete;
        struct { int job_id; TransferProgress progress; }      transfer_progress;
        struct { int error_code; char message[128]; }          error;
        struct { uint16_t prop_id; uint32_t value; }           property_changed;
    } data;
} CameraEvent;

typedef void (*event_handler_fn)(CameraEvent *event, void *user_data);

/* ─── 生命周期 ───────────────────────────────────────────────── */

/**
 * 创建 CameraAPI 实例。
 * @param transport TRANSPORT_AUTO / TRANSPORT_USB_ONLY / TRANSPORT_WIFI_ONLY
 * @return 实例指针; NULL 表示内存不足
 */
CameraAPI *camera_api_create(int transport);

/** 断开连接并销毁实例, 释放所有资源。 */
void camera_api_destroy(CameraAPI *api);

/* ─── 连接管理 ───────────────────────────────────────────────── */

/** 扫描可用相机 (阻塞, 约 3s)。返回的数组由调用者 free()。 */
CameraInfo *camera_api_scan(CameraAPI *api, int *count);

/** 连接指定相机 (camera_id 来自 CameraInfo.id)。 */
int camera_api_connect(CameraAPI *api, const char *camera_id);

/** 主动断开连接。 */
void camera_api_disconnect(CameraAPI *api);

/**
 * 使用预打开的 USB fd 连接相机 (Android 专用)。
 *
 * Android 平台 USB 权限请求和 UsbDeviceConnection 打开由 Java 层处理,
 * 此函数将已获取的 fd 直接注入 native 层。
 *
 * @param api      CameraAPI 实例
 * @param fd       已打开的 USB 设备文件描述符
 * @param serial   相机序列号 (用于填充 current_camera)
 * @param ep_out   USB Bulk OUT 端点 (默认 0x02)
 * @param ep_in    USB Bulk IN 端点 (默认 0x81)
 * @return CAM_OK; < 0 错误码
 */
int camera_api_connect_usb_fd(CameraAPI *api, int fd, const char *serial,
                               uint8_t ep_out, uint8_t ep_in);

/** 获取当前连接状态。 */
ConnectionStatus camera_api_get_status(CameraAPI *api);

/** 注册连接状态变化回调 (覆盖之前注册的)。 */
void camera_api_on_status_change(CameraAPI *api,
    void (*cb)(ConnectionStatus status, void *), void *user_data);

/** 通过 Wi-Fi 连接相机 (IP + 端口)。 */
int camera_api_connect_wifi(CameraAPI *api, const char *ip_addr, uint16_t port);

/** 发送本地文件到相机 (MTP SendObject)。 */
int camera_api_send_file(CameraAPI *api, const char *local_path,
                         uint32_t storage_id, const char *remote_name);

/* ─── 拍摄控制 ───────────────────────────────────────────────── */

/** 单张拍摄 (NIKON_OC_Capture 0x90C1)。 */
int camera_api_capture(CameraAPI *api);

/**
 * 连拍。
 * @param count       张数 (0=不限直到 stop)
 * @param interval_ms 帧间隔 (0=相机默认)
 */
int camera_api_capture_burst(CameraAPI *api, int count, int interval_ms);

/** 停止连拍。 */
int camera_api_stop_burst(CameraAPI *api);

/** 触发自动对焦 (NIKON_OC_AutoFocus 0x90C0)。 */
int camera_api_autofocus(CameraAPI *api);

/* ─── 实时取景 ───────────────────────────────────────────────── */

/** 开始实时取景 (NIKON_OC_StartLiveView 0x9201)。 */
int camera_api_start_liveview(CameraAPI *api);

/** 停止实时取景 (NIKON_OC_EndLiveView 0x9202)。 */
int camera_api_stop_liveview(CameraAPI *api);

/**
 * 注册实时取景帧回调 (NIKON_OC_GetLiveViewImage 0x9203)。
 * 每帧 JPEG 数据通过 cb 传出, 约 30fps。
 * @param cb         回调, jpeg_data 的生命周期仅在回调内有效
 * @param user_data  用户数据
 */
int camera_api_get_liveview_frame(CameraAPI *api,
    void (*cb)(const uint8_t *jpeg_data, int size, void *), void *user_data);

/* ─── 相机参数 ───────────────────────────────────────────────── */

/** 设置设备属性 (PTP SetDevicePropValue)。 */
int camera_api_set_property(CameraAPI *api, uint16_t prop_id, uint32_t value);

/** 读取设备属性 (PTP GetDevicePropValue)。 */
int camera_api_get_property(CameraAPI *api, uint16_t prop_id, uint32_t *value);

/**
 * 批量读取设备属性 (一次调用取多个, 减少上层跨语言边界次数,
 * 并让调用方拿到一组近似同一时刻的参数快照)。
 * @param prop_ids  属性码数组, 长度 count
 * @param values    出参数组, 长度 count; 单项失败时保留调用前的原值
 * @param count     属性个数 (<=0 或指针为空返回 CAM_ERR_INVALID_PARAM)
 * @return 失败项个数, 0 表示全部成功
 */
int camera_api_get_properties(CameraAPI *api, const uint16_t *prop_ids,
                              uint32_t *values, int count);

/* 便捷内联方法 */
static inline int camera_api_set_iso(CameraAPI *api, int iso) {
    return camera_api_set_property(api, 0xD010, (uint32_t)iso);
}
static inline int camera_api_set_shutter(CameraAPI *api, int speed_val) {
    return camera_api_set_property(api, 0xD00C, (uint32_t)speed_val);
}
static inline int camera_api_set_aperture(CameraAPI *api, int aperture_val) {
    return camera_api_set_property(api, 0xD00E, (uint32_t)aperture_val);
}
static inline int camera_api_set_wb(CameraAPI *api, int wb_val) {
    return camera_api_set_property(api, 0xD00A, (uint32_t)wb_val);
}

/* ─── Picture Control (预设 / 色彩偏移) ────────────────────── */

/** 读取当前 Picture Control 参数 (PTP 0x90CC)。 */
int camera_api_get_pictctrl(CameraAPI *api, PictureControl *ctrl);

/** 写入 Picture Control 参数 (PTP 0x90CD)。 */
int camera_api_set_pictctrl(CameraAPI *api, const PictureControl *ctrl);

/* ─── 文件操作 ───────────────────────────────────────────────── */

/**
 * 获取文件列表 (PTP GetObjectHandles)。
 * 返回的数组由调用者 free()。
 * @param storage_id  0=全部存储卡; NIKON_STORAGE_CF / NIKON_STORAGE_SD
 */
FileInfo *camera_api_list_files(CameraAPI *api, uint32_t storage_id, int *count);

/** 获取缩略图 JPEG 数据 (调用者 free() data)。 */
int camera_api_get_thumbnail(CameraAPI *api, uint32_t object_handle,
                             uint8_t **data, uint32_t *size);

/** 删除相机内文件。 */
int camera_api_delete_file(CameraAPI *api, uint32_t object_handle);

/* ─── 文件传输 ───────────────────────────────────────────────── */

/**
 * 异步传输单个文件。
 * @return >= 0 job_id; < 0 错误码
 */
int camera_api_start_transfer(CameraAPI *api,
                              uint32_t object_handle, const char *dest_path);

/**
 * 批量异步传输。
 * @return >= 0 batch_job_id; < 0 错误码
 */
int camera_api_start_batch_transfer(CameraAPI *api,
                                    const uint32_t *handles, int count,
                                    const char *dest_dir);

/** 取消传输任务。 */
int camera_api_cancel_transfer(CameraAPI *api, int job_id);

/** 查询传输进度快照。 */
int camera_api_get_progress(CameraAPI *api, int job_id, TransferProgress *out);

/** 注册传输进度回调 (每块完成时触发)。 */
void camera_api_on_transfer_progress(CameraAPI *api,
    void (*cb)(int job_id, TransferProgress *progress, void *), void *user_data);

/**
 * 注册"边拍边传"新文件回调。
 * 当相机产生新文件时触发, 可在此回调内调用 camera_api_start_transfer()。
 */
void camera_api_on_new_file(CameraAPI *api,
    void (*cb)(uint32_t object_handle, const char *filename, void *),
    void *user_data);

/* ─── FTP 自动化 ─────────────────────────────────────────────── */

/** 配置 FTP 服务器 (不立即连接)。 */
int camera_api_set_ftp_config(CameraAPI *api, const FtpConfig *config);

/** 将本地文件导出到 FTP (异步)。 */
int camera_api_export_to_ftp(CameraAPI *api, const char *local_path);

/* ─── 通用事件监听 ───────────────────────────────────────────── */

/** 注册指定事件类型的监听器 (覆盖同类型的已有监听器)。 */
int camera_api_on_event(CameraAPI *api, EventType type,
                        event_handler_fn handler, void *user_data);

/** 移除指定类型的监听器。 */
void camera_api_remove_handler(CameraAPI *api, EventType type);

/** 将错误码转换为可读字符串。 */
const char *camera_api_strerror(int error_code);

#ifdef __cplusplus
}
#endif
#endif /* NIKON_CAMERA_API_H */
