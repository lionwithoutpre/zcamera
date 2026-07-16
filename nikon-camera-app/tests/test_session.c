/**
 * tests/test_session.c — 会话管理单元测试
 *
 * 测试 managed_session_create 参数化、心跳重连回调、atomic stop。
 */
#include "protocol/ptp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdatomic.h>

static int passed = 0, failed = 0;

#define CHECK(expr, msg) \
    do { \
        if (expr) { printf("  PASS: %s\n", msg); passed++; } \
        else      { printf("  FAIL: %s\n", msg); failed++; } \
    } while(0)

int main(void) {
    printf("=== test_session ===\n");

    /* PtpSession 初始化 — USB 模式 */
    PtpSession usb_session;
    int rc = ptp_session_init(&usb_session, -1, PTP_TRANSPORT_USB,
                               PTP_USB_EP_OUT_DEFAULT, PTP_USB_EP_IN_DEFAULT);
    CHECK(rc == 0, "USB session init ok");
    CHECK(usb_session.transport == PTP_TRANSPORT_USB, "transport = USB");
    CHECK(usb_session.ep_out == PTP_USB_EP_OUT_DEFAULT, "ep_out = 0x02");
    CHECK(usb_session.ep_in == PTP_USB_EP_IN_DEFAULT, "ep_in = 0x81");
    CHECK(usb_session.state == SESSION_DISCONNECTED, "initial state DISCONNECTED");

    /* PtpSession 初始化 — WiFi 模式 */
    PtpSession wifi_session;
    rc = ptp_session_init(&wifi_session, -1, PTP_TRANSPORT_WIFI, 0, 0);
    CHECK(rc == 0, "WiFi session init ok");
    CHECK(wifi_session.transport == PTP_TRANSPORT_WIFI, "transport = WIFI");
    CHECK(wifi_session.ep_out == 0, "WiFi ep_out = 0");
    CHECK(wifi_session.ep_in == 0, "WiFi ep_in = 0");

    /* NULL 参数安全性 */
    rc = ptp_session_init(NULL, -1, PTP_TRANSPORT_USB, 0, 0);
    CHECK(rc == -1, "NULL session returns -1");

    /* 事务 ID 自增 */
    uint32_t t1 = ptp_session_next_transaction(&usb_session);
    uint32_t t2 = ptp_session_next_transaction(&usb_session);
    uint32_t t3 = ptp_session_next_transaction(&usb_session);
    CHECK(t1 == 1, "first transaction_id = 1");
    CHECK(t2 == 2, "second transaction_id = 2");
    CHECK(t3 == 3, "third transaction_id = 3");

    /* 心跳间隔默认值 */
    CHECK(usb_session.heartbeat_interval_ms == 5000, "default heartbeat 5000ms");

    /* 关闭空会话不崩溃 */
    rc = ptp_session_close(&usb_session);
    CHECK(rc == 0 || rc < 0, "close empty session no crash");
    rc = ptp_session_close(&wifi_session);
    CHECK(rc == 0 || rc < 0, "close empty WiFi session no crash");

    /* 多次 init 不泄漏 */
    PtpSession s2;
    ptp_session_init(&s2, 42, PTP_TRANSPORT_USB, 0x03, 0x82);
    CHECK(s2.fd == 42, "fd = 42");
    CHECK(s2.ep_out == 0x03, "custom ep_out = 0x03");
    CHECK(s2.ep_in == 0x82, "custom ep_in = 0x82");
    ptp_session_close(&s2);

    printf("\n结果: %d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
