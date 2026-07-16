/**
 * tests/test_ptp.c — PTP 会话管理单元测试 (使用 stub fd=-1)
 */
#include "protocol/ptp.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

static int passed = 0, failed = 0;

#define CHECK(expr, msg) \
    do { \
        if (expr) { printf("  PASS: %s\n", msg); passed++; } \
        else      { printf("  FAIL: %s\n", msg); failed++; } \
    } while(0)

int main(void) {
    printf("=== test_ptp ===\n");

    PtpSession session;

    /* 初始化 */
    int rc = ptp_session_init(&session, -1, PTP_TRANSPORT_USB, 0, 0);
    CHECK(rc == 0,                             "ptp_session_init ok");
    CHECK(session.state == SESSION_DISCONNECTED, "初始状态 DISCONNECTED");
    CHECK(session.transaction_id == 1,         "初始 transaction_id = 1");
    CHECK(session.heartbeat_interval_ms == 5000, "默认心跳间隔 5s");

    /* 事务 ID 自增 */
    uint32_t t1 = ptp_session_next_transaction(&session);
    uint32_t t2 = ptp_session_next_transaction(&session);
    CHECK(t1 == 1,      "第一个事务 ID = 1");
    CHECK(t2 == 2,      "第二个事务 ID = 2");
    CHECK(session.transaction_id == 3, "session.transaction_id 自增到 3");

    /* 空会话关闭不崩溃 */
    rc = ptp_session_close(&session);
    CHECK(session.state == SESSION_DISCONNECTED, "关闭后状态 DISCONNECTED");

    /* NULL 参数安全性 */
    rc = ptp_session_init(NULL, 0, PTP_TRANSPORT_USB, 0, 0);
    CHECK(rc == -1,     "NULL session 返回 -1");

    printf("\n结果: %d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
