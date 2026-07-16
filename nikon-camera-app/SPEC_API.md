# 业务层 API 规格书 — CameraAPI 统一接口

## 1. CameraAPI 总览

```c
typedef struct CameraAPI CameraAPI;

CameraAPI* camera_api_create(int transport);
void camera_api_destroy(CameraAPI* api);

// ---- 连接管理 ----
CameraInfo* camera_api_scan(CameraAPI* api, int* count);
int camera_api_connect(CameraAPI* api, const char* camera_id);
void camera_api_disconnect(CameraAPI* api);
ConnectionStatus camera_api_get_status(CameraAPI* api);
void camera_api_on_status_change(CameraAPI* api,
    void (*cb)(ConnectionStatus status, void*), void* user_data);

// ---- 拍摄控制 ----
int camera_api_capture(CameraAPI* api);
int camera_api_capture_burst(CameraAPI* api, int count, int interval_ms);
int camera_api_stop_burst(CameraAPI* api);

// ---- 实时取景 ----
int camera_api_start_liveview(CameraAPI* api);
int camera_api_stop_liveview(CameraAPI* api);
int camera_api_get_liveview_frame(CameraAPI* api,
    void (*cb)(const uint8_t* jpeg_data, int size, void*), void* user_data);

// ---- 相机参数 ----
int camera_api_set_property(CameraAPI* api, uint16_t prop_id, uint32_t value);
int camera_api_get_property(CameraAPI* api, uint16_t prop_id, uint32_t* value);
int camera_api_get_pictctrl(CameraAPI* api, uint16_t ctrl_id, void* data, uint32_t* size);
int camera_api_set_pictctrl(CameraAPI* api, uint16_t ctrl_id, const void* data, uint32_t size);

static inline int camera_api_set_iso(CameraAPI* api, int iso) {
    return camera_api_set_property(api, NIKON_PROP_ISO, iso);
}
static inline int camera_api_set_shutter(CameraAPI* api, int speed) {
    return camera_api_set_property(api, NIKON_PROP_ShutterSpeed, speed);
}
static inline int camera_api_set_aperture(CameraAPI* api, int aperture) {
    return camera_api_set_property(api, NIKON_PROP_Aperture, aperture);
}

// ---- 文件传输 ----
FileInfo* camera_api_list_files(CameraAPI* api, int* count);
int camera_api_start_transfer(CameraAPI* api, uint32_t object_handle,
                              const char* dest_path);
int camera_api_start_batch_transfer(CameraAPI* api,
    uint32_t* handles, int count, const char* dest_dir);
int camera_api_cancel_transfer(CameraAPI* api, int job_id);
int camera_api_get_progress(CameraAPI* api, int job_id, TransferProgress* progress);
void camera_api_on_transfer_progress(CameraAPI* api,
    void (*cb)(int job_id, TransferProgress* progress, void*), void* user_data);
void camera_api_on_new_file(CameraAPI* api,
    void (*cb)(uint32_t object_handle, const char* filename, void*), void* user_data);

// ---- FTP / 自动化 ----
int camera_api_set_ftp_config(CameraAPI* api, FtpConfig* config);
int camera_api_export_to_ftp(CameraAPI* api, const char* local_path);
```

## 2. 数据结构

```c
typedef struct {
    char        id[64];
    char        model[64];
    char        serial[32];
    int         transport;
    int         battery_level;
    uint64_t    storage_free;
    uint64_t    storage_total;
} CameraInfo;

typedef enum {
    STATUS_DISCONNECTED = 0,
    STATUS_SCANNING,
    STATUS_CONNECTING,
    STATUS_CONNECTED,
    STATUS_TRANSFERRING,
    STATUS_ERROR,
} ConnectionStatus;

typedef struct {
    uint32_t    object_handle;
    char        filename[256];
    uint64_t    size;
    char        datetime[32];
    int         is_raw;
    int         is_jpeg;
    int         width;
    int         height;
} FileInfo;

typedef struct {
    int             job_id;
    uint32_t        object_handle;
    char            filename[256];
    uint64_t        total_size;
    uint64_t        transferred;
    int             percent;
    double          speed_mbps;
    uint64_t        elapsed_ms;
    uint64_t        remaining_ms;
    int             status;
    int             error_code;
} TransferProgress;

typedef struct {
    char        host[128];
    uint16_t    port;
    char        username[64];
    char        password[64];
    char        remote_path[256];
    int         use_tls;
    int         auto_upload;
    int         keep_original;
} FtpConfig;
```

## 3. 异步事件通道

```c
typedef enum {
    EVENT_NEW_FILE,               // 新文件产生
    EVENT_CAPTURE_COMPLETE,       // 拍摄完成
    EVENT_PROPERTY_CHANGED,       // 属性变更
    EVENT_DEVICE_CHANGED,         // 设备变更 (连接/断开)
    EVENT_ERROR,                  // 错误事件
} EventType;

typedef void (*event_handler_fn)(EventType type, const void* data, void* user_data);

int camera_api_on_event(CameraAPI* api, EventType type,
                        event_handler_fn handler, void* user_data);
void camera_api_remove_handler(CameraAPI* api, EventType type);
```

## 4. 错误码

```c
#define CAM_OK                      0
#define CAM_ERR_NOT_CONNECTED      -100
#define CAM_ERR_ALREADY_CONNECTED  -101
#define CAM_ERR_TIMEOUT            -102
#define CAM_ERR_NOT_SUPPORTED      -103
#define CAM_ERR_BUSY               -104
#define CAM_ERR_INVALID_PARAM      -105
#define CAM_ERR_TRANSFER_FAILED    -200
#define CAM_ERR_FILE_NOT_FOUND     -201
#define CAM_ERR_OUT_OF_MEMORY      -300
#define CAM_ERR_PERMISSION_DENIED  -301
#define CAM_ERR_USB                -400
#define CAM_ERR_WIFI               -401
#define CAM_ERR_BLUETOOTH          -402
#define CAM_ERR_PROTOCOL           -500
```

## 5. 线程安全保证

```c
// CameraAPI 实现是线程安全的:
// - 内部使用读写锁分离 (读: 状态查询, 写: 发送命令)
// - 回调在独立的事件线程中执行
// - 传输引擎在工作线程池中运行
// - UI 线程可通过回调安全获取数据
// - _Atomic 标志用于 running/cancelled/initialized

// 使用模式:
//   1. 主线程: create / scan / connect
//   2. 任意线程: set_property / capture (命令队列)
//   3. 回调线程: 接收事件/进度
//   4. 主线程: disconnect / destroy
```
