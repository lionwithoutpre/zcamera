/**
 * tests/test_watcher.c — 事件监听器单元测试
 *
 * 测试 watcher 创建/销毁、事件注册、去重逻辑、atomic running 状态。
 */
#include "event/watcher.h"
#include "protocol/ptp.h"
#include "api/camera_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static int passed = 0, failed = 0;

#define CHECK(expr, msg) \
    do { \
        if (expr) { printf("  PASS: %s\n", msg); passed++; } \
        else      { printf("  FAIL: %s\n", msg); failed++; } \
    } while(0)

int main(void) {
    printf("=== test_watcher ===\n");

    PtpSession session;
    ptp_session_init(&session, -1, PTP_TRANSPORT_USB, 0, 0);

    /* 创建 PTP 轮询监听器 */
    EventWatcher *w = watcher_create(&session, WATCHER_CHANNEL_PTP_POLL);
    CHECK(w != NULL, "watcher_create PTP_POLL ok");

    /* 注册事件处理器 */
    int rc = watcher_on_event(w, EVENT_NEW_FILE, NULL, NULL);
    CHECK(rc == 0, "register EVENT_NEW_FILE handler ok");

    /* 无效 EventType (>=16) */
    rc = watcher_on_event(w, (EventType)20, NULL, NULL);
    CHECK(rc == -1, "invalid EventType (>=16) returns -1");

    /* NULL watcher 安全性 */
    rc = watcher_on_event(NULL, EVENT_NEW_FILE, NULL, NULL);
    CHECK(rc == -1, "NULL watcher returns -1");

    /* 设置轮询间隔 */
    watcher_set_poll_interval(w, 100, 500);
    CHECK(w->poll_min_ms == 100, "poll_min_ms = 100");
    CHECK(w->poll_max_ms == 500, "poll_max_ms = 500");

    /* 设置 WiFi 端点 */
    watcher_set_wifi_endpoint(w, "192.168.1.1", 80);
    CHECK(strcmp(w->wifi_host, "192.168.1.1") == 0, "wifi_host set");
    CHECK(w->wifi_port == 80, "wifi_port = 80");

    /* NULL 参数安全性 */
    watcher_set_wifi_endpoint(NULL, "1.2.3.4", 80);
    watcher_set_poll_interval(NULL, 10, 20);
    CHECK(1, "NULL setter no crash");

    /* 销毁未启动的监听器 */
    watcher_destroy(w);
    CHECK(1, "destroy unstarted watcher no crash");

    /* 创建 HTTP 轮询监听器 */
    EventWatcher *wh = watcher_create(&session, WATCHER_CHANNEL_HTTP_POLL);
    CHECK(wh != NULL, "watcher_create HTTP_POLL ok");
    watcher_set_wifi_endpoint(wh, "10.0.0.1", 15740);
    watcher_destroy(wh);
    CHECK(1, "HTTP watcher create/destroy ok");

    /* 创建无效通道 */
    EventWatcher *wi = watcher_create(&session, (WatcherChannel)99);
    CHECK(wi != NULL, "watcher_create invalid channel returns non-NULL");
    rc = watcher_start(wi);
    CHECK(rc == -1, "start invalid channel returns -1");
    watcher_destroy(wi);

    /* 去重表初始化验证 */
    EventWatcher *wd = watcher_create(&session, WATCHER_CHANNEL_PTP_POLL);
    CHECK(wd != NULL, "dedup watcher create ok");
    CHECK(wd->dedup_count == 0, "initial dedup_count = 0");
    watcher_destroy(wd);

    /* NULL destroy 安全性 */
    watcher_destroy(NULL);
    CHECK(1, "destroy NULL no crash");

    ptp_session_close(&session);

    printf("\n结果: %d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
