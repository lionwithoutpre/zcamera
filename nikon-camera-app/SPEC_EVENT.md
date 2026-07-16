# 事件驱动引擎规格书 — 毫秒级"边拍边传"

## 1. 整体架构

```
┌─────────────────────────────────┐
│         拍摄事件源               │
│  ┌───────────────────────────┐  │
│  │ 通道A: PTP_POLL           │  │  ← USB 连接 (首选, 50~200ms 自适应)
│  │ 通道B: INOTIFY (DCIM)     │  │  ← USB MTP 模式 (备选, Linux)
│  │ 通道C: HTTP_POLL          │  │  ← Wi-Fi 连接 (备用, 1000ms)
│  └────────────┬──────────────┘  │
└───────────────┼─────────────────┘
                ▼
┌─────────────────────────────────┐
│   EventWatcher (统一监听器)      │
│  • 事件去重 (2s 窗口, 32条记录)  │
│  • HTTP 错误连续≥3次→EVENT_ERROR│
│  • _Atomic running 线程安全     │
│  • keep-alive 持久连接          │
│  • 回调分发到业务层              │
└─────────────────────────────────┘
```

## 2. EventWatcher 接口

```c
// 监听通道类型
typedef enum {
    WATCHER_CHANNEL_PTP_POLL = 0,   // PTP 事件轮询
    WATCHER_CHANNEL_INOTIFY,        // inotify DCIM 监听
    WATCHER_CHANNEL_HTTP_POLL,      // HTTP 轮询 (Wi-Fi)
} WatcherChannel;

// 事件回调类型 (与 CameraAPI 对齐)
typedef void (*event_handler_fn)(EventType type, const void* data, void* user_data);

// EventWatcher 接口
typedef struct {
    int  (*start)(WatcherChannel channel, event_handler_fn cb, void* user_data);
    void (*stop)(void);
    int  (*is_running)(void);
} EventWatcher;
```

## 3. PTP 事件轮询 (通道 A)

```c
// PTP 事件轮询: USB 连接首选通道
// 自适应间隔: 有事件 50ms, 空闲 200ms

static void* ptp_poll_thread(void* arg) {
    EventWatcherCtx* ctx = (EventWatcherCtx*)arg;

    while (atomic_load(&ctx->running)) {
        PtpEvent events[16];
        int count = ptp_get_events(ctx->session, events, 16);

        for (int i = 0; i < count; i++) {
            events[i].timestamp = get_current_time_ms();

            switch (events[i].event_code) {
                case NIKON_EVENT_ObjectAdded:
                    if (!_is_duplicate(&ctx->dedup, events[i].param1, events[i].timestamp)) {
                        ctx->callback(EVENT_NEW_FILE, &events[i], ctx->user_data);
                    }
                    break;
                case NIKON_EVENT_CaptureComplete:
                    ctx->callback(EVENT_CAPTURE_COMPLETE, &events[i], ctx->user_data);
                    break;
                default:
                    ctx->callback(EVENT_PROPERTY_CHANGED, &events[i], ctx->user_data);
                    break;
            }
        }

        uint64_t interval = (count > 0) ? 50 : 200;
        usleep(interval * 1000);
    }
    return NULL;
}
```

## 4. DCIM 目录监听 (通道 B)

```c
// inotify DCIM 监听: Linux MTP 模式备选通道
// DCIM 路径: 优先 NIKON_DCIM_PATH 环境变量, 否则 /media/*/DCIM

static void* inotify_thread(void* arg) {
    EventWatcherCtx* ctx = (EventWatcherCtx*)arg;
    const char* dcim_path = getenv("NIKON_DCIM_PATH");
    if (!dcim_path) dcim_path = "/media";

    int inotify_fd = inotify_init1(IN_NONBLOCK);
    int watch_fd = inotify_add_watch(inotify_fd, dcim_path,
                                      IN_CREATE | IN_MOVED_TO | IN_CLOSE_WRITE);

    char buf[4096];
    while (atomic_load(&ctx->running)) {
        int len = read(inotify_fd, buf, sizeof(buf));
        if (len < 0) {
            if (errno == EAGAIN) { usleep(50000); continue; }
            break;
        }
        // 解析 inotify_event, 过滤图片文件, 回调通知
    }
    close(watch_fd);
    close(inotify_fd);
    return NULL;
}
```

## 5. Wi-Fi HTTP 轮询 (通道 C)

```c
// HTTP 轮询: Wi-Fi 模式备用通道
// 使用 HTTP/1.1 + Connection: keep-alive 持久连接
// 错误处理: 连续≥3次错误派发 EVENT_ERROR

static void* http_poll_thread(void* arg) {
    EventWatcherCtx* ctx = (EventWatcherCtx*)arg;
    int consecutive_errors = 0;

    while (atomic_load(&ctx->running)) {
        int ret = _http_get_events(ctx, ctx->base_url);

        if (ret < 0) {
            consecutive_errors++;
            if (consecutive_errors >= 3) {
                ctx->callback(EVENT_ERROR, &(int){ret}, ctx->user_data);
            }
        } else {
            consecutive_errors = 0;
        }

        usleep(ctx->poll_interval_ms * 1000);
    }
    return NULL;
}

// HTTP 错误码
// -1: socket 创建失败
// -2: connect 失败
// -3: send 失败
// -4: recv 失败
```

## 6. 事件去重

```c
// DedupRecord — 去重记录
typedef struct {
    uint32_t    object_handle;
    uint64_t    timestamp;
} DedupRecord;

#define DEDUP_TABLE_SIZE  32
#define DEDUP_WINDOW_MS   2000   // 2秒去重窗口

// 去重表 (固定 32 条记录环形表)
typedef struct {
    DedupRecord records[DEDUP_TABLE_SIZE];
    int         count;
} DedupTable;

// 检查是否重复 (2秒窗口内相同 object_handle 视为重复)
int _is_duplicate(DedupTable* table, uint32_t object_handle, uint64_t now);

// 清理过期记录 (超过去重窗口的条目)
void _cleanup_dedup(DedupTable* table, uint64_t now);
```

## 7. 事件优先级

```c
typedef enum {
    EVENT_PRIORITY_CRITICAL = 0,   // 错误、断连
    EVENT_PRIORITY_HIGH,           // ObjectAdded、CaptureComplete
    EVENT_PRIORITY_NORMAL,         // 属性变更
    EVENT_PRIORITY_LOW,            // 设备信息更新
} EventPriority;
```
