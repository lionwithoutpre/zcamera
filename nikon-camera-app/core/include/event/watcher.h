/**
 * event/watcher.h — 事件监听器接口
 *
 * 三通道事件监听:
 *   通道 A: PTP Event Poll (USB 轮询, 50~200ms 自适应)
 *   通道 B: inotify DCIM 目录监听 (MTP 挂载模式, Linux)
 *   通道 C: HTTP 轮询 (Wi-Fi 模式, 1000ms)
 */
#ifndef NIKON_EVENT_WATCHER_H
#define NIKON_EVENT_WATCHER_H

#include <stdint.h>
#include "api/camera_api.h"
#include "protocol/ptp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ─── 轮询间隔 ───────────────────────────────────────────────── */

#define WATCHER_POLL_MIN_MS     50      /**< 最快轮询间隔 */
#define WATCHER_POLL_MAX_MS     200     /**< 最慢轮询间隔 (USB 自适应上限) */
#define WATCHER_POLL_WIFI_MS    1000    /**< Wi-Fi 轮询间隔 */

/* ─── 监听通道类型 ────────────────────────────────────────────── */

typedef enum {
    WATCHER_CHANNEL_PTP_POLL = 0,   /**< 通道 A: PTP Event Poll */
    WATCHER_CHANNEL_INOTIFY,        /**< 通道 B: inotify (Linux) */
    WATCHER_CHANNEL_HTTP_POLL,      /**< 通道 C: HTTP 轮询 */
} WatcherChannel;

/* ─── 监听器实例 ─────────────────────────────────────────────── */

typedef struct EventWatcher EventWatcher;

/* ─── 接口 ───────────────────────────────────────────────────── */

/**
 * 创建事件监听器。
 * @param session  PTP 会话 (通道 A/B 需要)
 * @param channel  使用的监听通道
 * @return 监听器实例; NULL 失败
 */
EventWatcher *watcher_create(PtpSession *session, WatcherChannel channel);

/**
 * 注册事件处理器。
 * @param type     事件类型 (EVENT_NEW_FILE / EVENT_CAPTURE_COMPLETE 等)
 * @param handler  回调函数
 * @param user_data
 */
int watcher_on_event(EventWatcher *watcher, EventType type,
                     event_handler_fn handler, void *user_data);

/** 启动监听 (创建后台轮询线程)。 */
int watcher_start(EventWatcher *watcher);

/** 停止监听。 */
void watcher_stop(EventWatcher *watcher);

/**
 * 设置 PTP 轮询间隔 (通道 A)。
 * 引擎会根据事件频率自适应调整, 此函数设置上下界。
 */
void watcher_set_poll_interval(EventWatcher *watcher, int min_ms, int max_ms);

/** 销毁监听器并释放资源。 */
void watcher_destroy(EventWatcher *watcher);

/**
 * 设置 Wi-Fi HTTP 轮询端点 (通道 C 必须)。
 * 必须在 watcher_start 之前调用。
 *
 * @param watcher  监听器实例
 * @param host     相机 IP 地址 (e.g. "192.168.1.1")
 * @param port     端口 (默认 80)
 */
void watcher_set_wifi_endpoint(EventWatcher *watcher,
                               const char *host, uint16_t port);

#ifdef __cplusplus
}
#endif
#endif /* NIKON_EVENT_WATCHER_H */
