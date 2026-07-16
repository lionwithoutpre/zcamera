/**
 * tests/test_wifi_adapter.c — WiFi 适配器单元测试
 *
 * 测试 adapter_create_wifi、vtable 绑定、NULL 安全性。
 */
#include "adapter/camera_adapter.h"
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
    printf("=== test_wifi_adapter ===\n");

    /* 创建 WiFi 适配器 */
    CameraAdapter *a = adapter_create_wifi("192.168.1.1", 15740);
    CHECK(a != NULL, "adapter_create_wifi ok");
    CHECK(a->vtable != NULL, "vtable non-NULL");
    CHECK(a->ctx != NULL, "ctx non-NULL");

    /* 初始状态应为 DISCONNECTED */
    ConnectionStatus st = a->vtable->get_status(a->ctx);
    CHECK(st == STATUS_DISCONNECTED, "initial status DISCONNECTED");

    /* NULL ctx 的 get_status */
    ConnectionStatus st2 = a->vtable->get_status(NULL);
    CHECK(st2 == STATUS_DISCONNECTED, "NULL ctx returns DISCONNECTED");

    /* 未连接时执行需要会话的命令应返回 NOT_CONNECTED */
    CameraCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = CAM_CMD_CAPTURE;
    CameraResult res = a->vtable->execute(a->ctx, &cmd);
    CHECK(res.result_code == CAM_ERR_NOT_CONNECTED, "CAPTURE w/o session → NOT_CONNECTED");

    /* 不支持的命令类型 */
    cmd.type = (CameraCommandType)999;
    res = a->vtable->execute(a->ctx, &cmd);
    CHECK(res.result_code == CAM_ERR_NOT_SUPPORTED, "unknown cmd → NOT_SUPPORTED");

    /* NULL 参数安全性 */
    res = a->vtable->execute(NULL, &cmd);
    CHECK(res.result_code == CAM_ERR_INVALID_PARAM, "NULL ctx → INVALID_PARAM");
    res = a->vtable->execute(a->ctx, NULL);
    CHECK(res.result_code == CAM_ERR_INVALID_PARAM, "NULL cmd → INVALID_PARAM");

    /* 注入 session 后 CONNECT 命令 (WiFi HAL 未初始化, 预期失败) */
    PtpSession session;
    ptp_session_init(&session, -1, PTP_TRANSPORT_WIFI, 0, 0);
    adapter_wifi_set_session(a, &session);
    cmd.type = CAM_CMD_CONNECT;
    res = a->vtable->execute(a->ctx, &cmd);
    CHECK(res.result_code == CAM_ERR_WIFI, "CONNECT without WiFi HAL → WIFI error");

    /* adapter_wifi_set_session NULL 安全性 */
    adapter_wifi_set_session(NULL, &session);
    adapter_wifi_set_session(a, NULL);
    CHECK(1, "set_session NULL args no crash");

    /* 默认端口测试 */
    CameraAdapter *a2 = adapter_create_wifi("10.0.0.1", 0);
    CHECK(a2 != NULL, "create_wifi port=0 ok (uses default)");
    adapter_destroy(a2);

    /* NULL ip_addr */
    CameraAdapter *a3 = adapter_create_wifi(NULL, 15740);
    CHECK(a3 == NULL, "NULL ip_addr returns NULL");

    /* 销毁 */
    adapter_destroy(a);
    CHECK(1, "adapter_destroy ok");

    ptp_session_close(&session);

    printf("\n结果: %d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
