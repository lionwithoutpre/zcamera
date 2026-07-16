/**
 * event/watcher.c — 事件监听器实现
 *
 * 通道 A: PTP Event Poll (50~200ms 自适应轮询)
 * 通道 B: inotify  (Linux DCIM 目录, MTP 挂载模式)
 * 通道 C: HTTP 轮询 (Wi-Fi 模式, 1000ms)
 */
#include "event/watcher.h"
#include "protocol/ptp.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <stdatomic.h>

/* ─── 事件去重 ───────────────────────────────────────────────── */

#define EVENT_DEDUP_WINDOW_MS  2000

typedef struct {
    uint32_t event_code;
    uint32_t object_handle;
    uint64_t timestamp_ms;
} DedupRecord;

#define EVENT_DEDUP_MAX  32

/* ─── 内部结构 ───────────────────────────────────────────────── */

struct EventWatcher {
    WatcherChannel  channel;
    PtpSession     *session;

    /* 事件处理器表 (按 EventType 索引) */
    event_handler_fn handlers[16];
    void            *handler_data[16];

    /* 线程控制 */
    pthread_t        thread;
    _Atomic int      running;

    /* 事件去重 */
    DedupRecord      dedup_table[EVENT_DEDUP_MAX];
    int              dedup_count;

    /* 轮询间隔 (通道 A) */
    int              poll_min_ms;
    int              poll_max_ms;
    int              current_poll_ms;   /* 自适应当前值 */

    /* Wi-Fi HTTP 端点 (通道 C) */
    char             wifi_host[64];
    uint16_t         wifi_port;
};

/* ─── 时间辅助 ───────────────────────────────────────────────── */

static uint64_t _now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

/* ─── 事件去重 ───────────────────────────────────────────────── */

static int _is_duplicate(EventWatcher *w, uint32_t event_code,
                          uint32_t object_handle)
{
    uint64_t now = _now_ms();
    for (int i = 0; i < w->dedup_count; i++) {
        DedupRecord *r = &w->dedup_table[i];
        if (r->event_code == event_code &&
            r->object_handle == object_handle &&
            (now - r->timestamp_ms) < EVENT_DEDUP_WINDOW_MS) {
            r->timestamp_ms = now;
            return 1;
        }
    }
    if (w->dedup_count < EVENT_DEDUP_MAX) {
        DedupRecord *r = &w->dedup_table[w->dedup_count++];
        r->event_code   = event_code;
        r->object_handle = object_handle;
        r->timestamp_ms  = now;
    } else {
        w->dedup_table[0].event_code   = event_code;
        w->dedup_table[0].object_handle = object_handle;
        w->dedup_table[0].timestamp_ms  = now;
    }
    return 0;
}

static void _cleanup_dedup(EventWatcher *w) {
    uint64_t now = _now_ms();
    int write = 0;
    for (int read = 0; read < w->dedup_count; read++) {
        if ((now - w->dedup_table[read].timestamp_ms) < EVENT_DEDUP_WINDOW_MS) {
            if (write != read)
                w->dedup_table[write] = w->dedup_table[read];
            write++;
        }
    }
    w->dedup_count = write;
}

/* ─── 事件分发 ───────────────────────────────────────────────── */

static void _dispatch_event(EventWatcher *w, EventType type,
                              CameraEvent *event)
{
    if ((int)type < 16 && w->handlers[type]) {
        w->handlers[type](event, w->handler_data[type]);
    }
}

/* ─── 通道 A: PTP 轮询线程 ──────────────────────────────────── */

static void *_ptp_poll_thread(void *arg) {
    EventWatcher *w = (EventWatcher *)arg;
    uint32_t events[32];

    while (atomic_load(&w->running)) {
        /* 自适应睡眠 */
        usleep((unsigned int)(w->current_poll_ms * 1000));
        if (!atomic_load(&w->running)) break;

        int count = ptp_get_events(w->session, events, 32);
        if (count <= 0) {
            /* 无事件 → 延长间隔 (最大 poll_max_ms) */
            if (w->current_poll_ms < w->poll_max_ms) {
                w->current_poll_ms += 10;
            }
            continue;
        }

        /* 有事件 → 缩短间隔至最小 */
        w->current_poll_ms = w->poll_min_ms;

        _cleanup_dedup(w);

        for (int i = 0; i < count; i++) {
            CameraEvent ev;
            memset(&ev, 0, sizeof(ev));

            switch (events[i]) {
            case NIKON_EVENT_ObjectAdded:
                if (_is_duplicate(w, events[i], 0)) break;
                ev.type = EVENT_NEW_FILE;
                /* TODO: 解析 object_handle from event param */
                _dispatch_event(w, EVENT_NEW_FILE, &ev);
                break;
            case NIKON_EVENT_CaptureComplete:
                if (_is_duplicate(w, events[i], 0)) break;
                ev.type = EVENT_CAPTURE_COMPLETE;
                _dispatch_event(w, EVENT_CAPTURE_COMPLETE, &ev);
                break;
            case NIKON_EVENT_ShutterSpeed:
            case NIKON_EVENT_Aperture:
            case NIKON_EVENT_ISO:
            case NIKON_EVENT_ExposureComp:
            case NIKON_EVENT_FocusMode:
                if (_is_duplicate(w, events[i], 0)) break;
                ev.type = EVENT_PROPERTY_CHANGED;
                ev.data.property_changed.prop_id = (uint16_t)events[i];
                _dispatch_event(w, EVENT_PROPERTY_CHANGED, &ev);
                break;
            default:
                break;
            }
        }
    }
    return NULL;
}

/* ─── 通道 B: inotify (Linux) ────────────────────────────────── */

#if defined(__linux__)
#  include <sys/inotify.h>
#  include <dirent.h>

static void *_inotify_thread(void *arg) {
    EventWatcher *w = (EventWatcher *)arg;
    int ifd = inotify_init1(IN_NONBLOCK);
    if (ifd < 0) return NULL;

    /* 监听 DCIM 目录 (MTP 挂载路径) */
    const char *dcim = getenv("NIKON_DCIM_PATH");
    if (!dcim) dcim = "/run/user/1000/gvfs/mtp:host/DCIM";
    inotify_add_watch(ifd, dcim, IN_CREATE | IN_CLOSE_WRITE);

    char buf[4096];
    while (atomic_load(&w->running)) {
        usleep(100000);  /* 100ms 轮询 inotify fd */
        ssize_t n = read(ifd, buf, sizeof(buf));
        if (n <= 0) continue;

        struct inotify_event *ie = (struct inotify_event *)buf;
        if (ie->mask & (IN_CREATE | IN_CLOSE_WRITE)) {
            CameraEvent ev;
            memset(&ev, 0, sizeof(ev));
            ev.type = EVENT_NEW_FILE;
            strncpy(ev.data.new_file.filename, ie->name,
                    sizeof(ev.data.new_file.filename) - 1);
            _dispatch_event(w, EVENT_NEW_FILE, &ev);
        }
    }
    close(ifd);
    return NULL;
}
#endif /* __linux__ */

/* ─── 通道 C: HTTP 轮询 ──────────────────────────────────────── */

/**
 * 简易 HTTP GET → 解析相机事件响应。
 * Nikon 无线模式相机运行一个轻量 HTTP 服务器,
 * 典型端点: GET /event 返回 JSON 事件数组。
 *
 * 使用持久连接 (Connection: keep-alive) 减少握手开销。
 * 返回 >= 0 事件数; < 0 错误码。
 */
static int _http_get_events(EventWatcher *w, CameraEvent *out_ev, int max_ev) {
    if (!w->wifi_host[0]) return 0;

    /* 创建 TCP socket */
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    /* 设置超时 2s */
    struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(w->wifi_port);
    addr.sin_addr.s_addr = inet_addr(w->wifi_host);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        return -2;
    }

    /* 构造 HTTP GET 请求 (keep-alive) */
    char req[512];
    int req_len = snprintf(req, sizeof(req),
        "GET /event HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Accept: application/json\r\n"
        "Connection: keep-alive\r\n"
        "\r\n",
        w->wifi_host);

    if (send(sock, req, (size_t)req_len, 0) < 0) {
        close(sock);
        return -3;
    }

    /* 读取响应 (最多 8KB) */
    char resp[8192];
    memset(resp, 0, sizeof(resp));
    int total = 0;
    while (total < (int)sizeof(resp) - 1) {
        int n = (int)recv(sock, resp + total, sizeof(resp) - 1 - (size_t)total, 0);
        if (n <= 0) break;
        total += n;
    }
    close(sock);

    if (total <= 0) return -4;

    /* 跳过 HTTP 头 → 找 \r\n\r\n */
    char *body = strstr(resp, "\r\n\r\n");
    if (!body) return 0;
    body += 4;

    int events_found = 0;

    /* ── JSON 事件解析 (简易状态机) ────────────────────────── */
    /* 格式: [{"type":"new_file","handle":12345678,"name":"DSC_0001.NEF"}, ...] */
    const char *p = body;
    while (*p && events_found < max_ev) {
        /* 找 "type" */
        const char *t = strstr(p, "\"type\"");
        if (!t) break;
        t = strchr(t, ':');
        if (!t) break;
        t++; while (*t == ' ' || *t == '"') t++;

        /* 提取事件类型 */
        char etype[32] = {0};
        int ei = 0;
        while (*t && *t != '"' && *t != ',' && ei < 31) etype[ei++] = *t++;

        /* 找 "handle" */
        uint32_t handle = 0;
        const char *h = strstr(p, "\"handle\"");
        if (h) {
            h = strchr(h, ':');
            if (h) {
                h++; while (*h == ' ' || *h == '"') h++;
                handle = (uint32_t)strtoul(h, NULL, 10);
            }
        }

        /* 找 "name" */
        char fname[256] = {0};
        const char *n = strstr(p, "\"name\"");
        if (n) {
            n = strchr(n, ':');
            if (n) {
                n++; while (*n == ' ' || *n == '"') n++;
                int fi = 0;
                while (*n && *n != '"' && fi < 255) fname[fi++] = *n++;
            }
        }

        /* 分发事件 */
        memset(&out_ev[events_found], 0, sizeof(CameraEvent));
        if (strcmp(etype, "new_file") == 0) {
            out_ev[events_found].type = EVENT_NEW_FILE;
            out_ev[events_found].data.new_file.object_handle = handle;
            if (fname[0]) strncpy(out_ev[events_found].data.new_file.filename,
                                   fname,
                                   sizeof(out_ev[events_found].data.new_file.filename) - 1);
            events_found++;
        } else if (strcmp(etype, "capture_complete") == 0) {
            out_ev[events_found].type = EVENT_CAPTURE_COMPLETE;
            events_found++;
        }

        /* 跳到下一个对象 (找 } 然后 { 或 ]) */
        const char *close_brace = strchr(p, '}');
        p = close_brace ? close_brace + 1 : p + 1;
    }

    return events_found;
}

static void *_http_poll_thread(void *arg) {
    EventWatcher *w = (EventWatcher *)arg;
    CameraEvent events[16];
    int consecutive_errors = 0;

    while (atomic_load(&w->running)) {
        usleep((unsigned int)(WATCHER_POLL_WIFI_MS * 1000));
        if (!atomic_load(&w->running)) break;

        int count = _http_get_events(w, events, 16);
        if (count < 0) {
            consecutive_errors++;
            if (consecutive_errors >= 3) {
                CameraEvent err_ev;
                memset(&err_ev, 0, sizeof(err_ev));
                err_ev.type = EVENT_ERROR;
                err_ev.data.error.error_code = CAM_ERR_WIFI;
                snprintf(err_ev.data.error.message,
                         sizeof(err_ev.data.error.message),
                         "WiFi event poll failed (%d consecutive errors)",
                         consecutive_errors);
                _dispatch_event(w, EVENT_ERROR, &err_ev);
            }
            continue;
        }
        consecutive_errors = 0;

        _cleanup_dedup(w);

        for (int i = 0; i < count; i++) {
            EventType t = events[i].type;
            uint32_t ec = 0;
            uint32_t oh = 0;
            if (t == EVENT_NEW_FILE) {
                ec = NIKON_EVENT_ObjectAdded;
                oh = events[i].data.new_file.object_handle;
            } else if (t == EVENT_CAPTURE_COMPLETE) {
                ec = NIKON_EVENT_CaptureComplete;
            }
            if (ec && _is_duplicate(w, ec, oh)) continue;

            if ((int)t < 16) {
                _dispatch_event(w, t, &events[i]);
            }
        }
    }
    return NULL;
}

/* ─── 公开接口 ───────────────────────────────────────────────── */

EventWatcher *watcher_create(PtpSession *session, WatcherChannel channel) {
    EventWatcher *w = (EventWatcher *)calloc(1, sizeof(EventWatcher));
    if (!w) return NULL;
    w->channel          = channel;
    w->session          = session;
    w->poll_min_ms      = WATCHER_POLL_MIN_MS;
    w->poll_max_ms      = WATCHER_POLL_MAX_MS;
    w->current_poll_ms  = WATCHER_POLL_MIN_MS;
    atomic_store(&w->running, 0);
    return w;
}

int watcher_on_event(EventWatcher *watcher, EventType type,
                     event_handler_fn handler, void *user_data)
{
    if (!watcher || (int)type >= 16) return -1;
    watcher->handlers[type]     = handler;
    watcher->handler_data[type] = user_data;
    return 0;
}

int watcher_start(EventWatcher *watcher) {
    if (!watcher || atomic_load(&watcher->running)) return -1;
    atomic_store(&watcher->running, 1);

    void *(*thread_fn)(void *) = NULL;
    switch (watcher->channel) {
    case WATCHER_CHANNEL_PTP_POLL:
        thread_fn = _ptp_poll_thread;
        break;
#if defined(__linux__)
    case WATCHER_CHANNEL_INOTIFY:
        thread_fn = _inotify_thread;
        break;
#endif
    case WATCHER_CHANNEL_HTTP_POLL:
        thread_fn = _http_poll_thread;
        break;
    default:
        atomic_store(&watcher->running, 0);
        return -1;
    }

    return pthread_create(&watcher->thread, NULL, thread_fn, watcher);
}

void watcher_stop(EventWatcher *watcher) {
    if (!watcher || !atomic_load(&watcher->running)) return;
    atomic_store(&watcher->running, 0);
    pthread_join(watcher->thread, NULL);
}

void watcher_set_poll_interval(EventWatcher *watcher, int min_ms, int max_ms) {
    if (!watcher) return;
    watcher->poll_min_ms = min_ms;
    watcher->poll_max_ms = max_ms;
    if (watcher->current_poll_ms < min_ms) watcher->current_poll_ms = min_ms;
    if (watcher->current_poll_ms > max_ms) watcher->current_poll_ms = max_ms;
}

void watcher_set_wifi_endpoint(EventWatcher *watcher,
                               const char *host, uint16_t port) {
    if (!watcher || !host) return;
    strncpy(watcher->wifi_host, host, sizeof(watcher->wifi_host) - 1);
    watcher->wifi_port = port;
}

void watcher_destroy(EventWatcher *watcher) {
    if (!watcher) return;
    watcher_stop(watcher);
    free(watcher);
}
