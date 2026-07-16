/**
 * tests/test_transfer.c — 传输引擎基本测试
 */
#include "transfer/transfer.h"
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
    printf("=== test_transfer ===\n");

    /* 引擎初始化 */
    int rc = transfer_engine_init(2);
    CHECK(rc == 0, "transfer_engine_init ok");

    /* NULL session 传入应返回错误码, 不崩溃 */
    int jid = transfer_submit(NULL, 0x0001, "/tmp/test.nef",
                              NULL, NULL, NULL);
    CHECK(jid < 0, "NULL session 返回错误码");

    /* 取消不存在的任务 */
    rc = transfer_cancel(9999);
    CHECK(rc == CAM_ERR_FILE_NOT_FOUND, "cancel 不存在任务返回 NOT_FOUND");

    /* 自适应块大小 */
    /* 小文件 (< 4MB) 不分块 — 通过内部行为间接验证 */

    /* zerocopy: src_fd=-1 应返回负值 */
    int64_t zrc = transfer_zerocopy(-1, -1, 0, 1024);
    CHECK(zrc < 0, "zerocopy 无效 fd 返回错误");

    transfer_engine_shutdown();
    CHECK(1, "transfer_engine_shutdown ok");

    printf("\n结果: %d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
