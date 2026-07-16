/**
 * tests/test_command_map.c — 命令映射表单元测试
 */
#include "adapter/camera_adapter.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

extern const CommandMapping *command_map_find(CameraCommandType type);

static int passed = 0, failed = 0;

#define CHECK(expr, msg) \
    do { \
        if (expr) { printf("  PASS: %s\n", msg); passed++; } \
        else      { printf("  FAIL: %s\n", msg); failed++; } \
    } while(0)

int main(void) {
    printf("=== test_command_map ===\n");

    const CommandMapping *m;

    m = command_map_find(CAM_CMD_CONNECT);
    CHECK(m != NULL,                       "find CAM_CMD_CONNECT");
    CHECK(m && m->ptp_opcode == (uint16_t)PTP_OC_OpenSession,
          "CAM_CMD_CONNECT maps to OpenSession");
    CHECK(m && m->nikon_private == 0,      "CONNECT is standard PTP");

    m = command_map_find(CAM_CMD_CAPTURE);
    CHECK(m != NULL,                       "find CAM_CMD_CAPTURE");
    CHECK(m && m->nikon_private == 1,      "CAPTURE is Nikon private");
    CHECK(m && m->ptp_opcode == (uint16_t)NIKON_OC_Capture,
          "CAPTURE opcode = 0x90C1");

    m = command_map_find(CAM_CMD_SET_PICTCTRL);
    CHECK(m != NULL,                       "find CAM_CMD_SET_PICTCTRL");
    CHECK(m && m->ptp_opcode == (uint16_t)NIKON_OC_SetPictCtrlData,
          "SET_PICTCTRL opcode = 0x90CD");

    m = command_map_find((CameraCommandType)999);
    CHECK(m == NULL,                       "unknown command returns NULL");

    printf("\n结果: %d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
