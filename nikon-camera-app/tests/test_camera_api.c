/**
 * tests/test_camera_api.c — CameraAPI 业务层单元测试
 *
 * 测试实例创建/销毁、错误码、strerror、连接状态。
 */
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
    printf("=== test_camera_api ===\n");

    /* 创建实例 — AUTO 模式 */
    CameraAPI *api = camera_api_create(TRANSPORT_AUTO);
    CHECK(api != NULL, "camera_api_create(AUTO) ok");

    /* 初始状态应为 DISCONNECTED */
    ConnectionStatus st = camera_api_get_status(api);
    CHECK(st == STATUS_DISCONNECTED, "initial status DISCONNECTED");

    /* 未连接时操作应返回 NOT_CONNECTED */
    int rc = camera_api_capture(api);
    CHECK(rc == CAM_ERR_NOT_CONNECTED, "capture w/o connect → NOT_CONNECTED");

    rc = camera_api_start_liveview(api);
    CHECK(rc == CAM_ERR_NOT_CONNECTED, "liveview w/o connect → NOT_CONNECTED");

    rc = camera_api_set_property(api, 0xD010, 800);
    CHECK(rc == CAM_ERR_NOT_CONNECTED, "set_property w/o connect → NOT_CONNECTED");

    uint32_t val = 0;
    rc = camera_api_get_property(api, 0xD010, &val);
    CHECK(rc == CAM_ERR_NOT_CONNECTED, "get_property w/o connect → NOT_CONNECTED");

    /* 错误码转字符串 */
    const char *s = camera_api_strerror(CAM_OK);
    CHECK(s != NULL && s[0] != '\0', "strerror(CAM_OK) non-empty");

    s = camera_api_strerror(CAM_ERR_NOT_CONNECTED);
    CHECK(s != NULL && s[0] != '\0', "strerror(NOT_CONNECTED) non-empty");

    s = camera_api_strerror(CAM_ERR_TIMEOUT);
    CHECK(s != NULL && s[0] != '\0', "strerror(TIMEOUT) non-empty");

    s = camera_api_strerror(-9999);
    CHECK(s != NULL, "strerror(unknown) non-NULL");

    /* USB_ONLY 模式 */
    CameraAPI *api_usb = camera_api_create(TRANSPORT_USB_ONLY);
    CHECK(api_usb != NULL, "camera_api_create(USB_ONLY) ok");
    camera_api_destroy(api_usb);

    /* WIFI_ONLY 模式 */
    CameraAPI *api_wifi = camera_api_create(TRANSPORT_WIFI_ONLY);
    CHECK(api_wifi != NULL, "camera_api_create(WIFI_ONLY) ok");
    camera_api_destroy(api_wifi);

    /* 错误码范围验证 */
    CHECK(CAM_OK == 0, "CAM_OK = 0");
    CHECK(CAM_ERR_NOT_CONNECTED < 0, "error codes are negative");
    CHECK(CAM_ERR_TRANSFER_FAILED < 0, "TRANSFER_FAILED is negative");
    CHECK(CAM_ERR_USB < 0, "USB error is negative");
    CHECK(CAM_ERR_WIFI < 0, "WiFi error is negative");
    CHECK(CAM_ERR_PROTOCOL < 0, "PROTOCOL error is negative");

    /* 传输常量验证 */
    CHECK(NIKON_STORAGE_CF == 0x00010001u, "STORAGE_CF = 0x00010001");
    CHECK(NIKON_STORAGE_SD == 0x00010002u, "STORAGE_SD = 0x00010002");

    /* NULL API 安全性 */
    camera_api_disconnect(NULL);
    camera_api_destroy(NULL);
    CHECK(1, "NULL API calls no crash");

    /* 销毁 */
    camera_api_destroy(api);
    CHECK(1, "camera_api_destroy ok");

    printf("\n结果: %d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
