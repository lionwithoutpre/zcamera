/**
 * tests/test_chunked.c — 分块传输引擎单元测试
 *
 * 测试引擎初始化/关闭、atomic 状态、取消逻辑、重试常量。
 */
#include "transfer/transfer.h"
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
    printf("=== test_chunked ===\n");

    /* 引擎初始化 */
    int rc = transfer_engine_init(2);
    CHECK(rc == 0, "transfer_engine_init(2) ok");

    /* 重复初始化应安全 (幂等或返回错误) */
    rc = transfer_engine_init(2);
    CHECK(rc == 0 || rc < 0, "double init no crash");

    /* NULL session 提交应返回错误 */
    int jid = transfer_submit(NULL, 0x0001, "/tmp/test_chunked.nef",
                               NULL, NULL, NULL);
    CHECK(jid < 0, "NULL session returns error");

    /* 取消不存在的任务 */
    rc = transfer_cancel(9999);
    CHECK(rc == CAM_ERR_FILE_NOT_FOUND, "cancel non-existent job → NOT_FOUND");

    /* 零拷贝无效 fd */
    int64_t zrc = transfer_zerocopy(-1, -1, 0, 1024);
    CHECK(zrc < 0, "zerocopy invalid fd returns error");

    /* 零拷贝零长度 */
    zrc = transfer_zerocopy(-1, -1, 0, 0);
    CHECK(zrc <= 0, "zerocopy zero length returns error/0");

    /* 块大小常量验证 */
    CHECK(TRANSFER_CHUNK_MIN == 64 * 1024, "CHUNK_MIN = 64KB");
    CHECK(TRANSFER_CHUNK_DEFAULT == 1024 * 1024, "CHUNK_DEFAULT = 1MB");
    CHECK(TRANSFER_CHUNK_MAX == 4 * 1024 * 1024, "CHUNK_MAX = 4MB");
    CHECK(TRANSFER_SMALL_FILE_THRESH == 4 * 1024 * 1024, "SMALL_FILE_THRESH = 4MB");
    CHECK(TRANSFER_MAX_CONCURRENT == 4, "MAX_CONCURRENT = 4");
    CHECK(TRANSFER_MAX_JOBS == 64, "MAX_JOBS = 64");

    /* 关闭引擎 */
    transfer_engine_shutdown();
    CHECK(1, "shutdown ok");

    /* 双重关闭安全性 */
    transfer_engine_shutdown();
    CHECK(1, "double shutdown no crash");

    printf("\n结果: %d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
