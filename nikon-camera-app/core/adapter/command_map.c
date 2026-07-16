/**
 * adapter/command_map.c — 尼康命令映射表
 *
 * 将 CameraCommandType 映射到对应的 PTP/尼康操作码。
 */
#include "adapter/camera_adapter.h"
#include <stddef.h>

const CommandMapping kNikonCommandMappings[] = {
    /* cmd_type                   ptp_opcode                    nikon_priv  needs_session  timeout_ms */

    /* 标准 PTP 命令 */
    { CAM_CMD_CONNECT,            PTP_OC_OpenSession,           0,          0,             5000  },
    { CAM_CMD_DISCONNECT,         PTP_OC_CloseSession,          0,          1,             2000  },
    { CAM_CMD_DEVICE_INFO,        PTP_OC_GetDeviceInfo,         0,          1,             3000  },
    { CAM_CMD_LIST_FILES,         PTP_OC_GetObjectHandles,      0,          1,             5000  },
    { CAM_CMD_GET_FILE,           PTP_OC_GetObject,             0,          1,             60000 },
    { CAM_CMD_GET_THUMBNAIL,      PTP_OC_GetThumb,              0,          1,             5000  },
    { CAM_CMD_DELETE_FILE,        PTP_OC_DeleteObject,          0,          1,             3000  },
    { CAM_CMD_GET_PROPERTY,       PTP_OC_GetDevicePropValue,    0,          1,             3000  },
    { CAM_CMD_SET_PROPERTY,       PTP_OC_SetDevicePropValue,    0,          1,             3000  },

    /* 尼康私有命令 */
    { CAM_CMD_CAPTURE,            NIKON_OC_Capture,             1,          1,             10000 },
    { CAM_CMD_CAPTURE_BURST,      NIKON_OC_Capture,             1,          1,             30000 },
    { CAM_CMD_START_LIVEVIEW,     NIKON_OC_StartLiveView,       1,          1,             5000  },
    { CAM_CMD_STOP_LIVEVIEW,      NIKON_OC_EndLiveView,         1,          1,             3000  },
    { CAM_CMD_GET_LIVEVIEW,       NIKON_OC_GetLiveViewImage,    1,          1,             3000  },
    { CAM_CMD_AUTOFOCUS,          NIKON_OC_AutoFocus,           1,          1,             5000  },
    { CAM_CMD_GET_PICTCTRL,       NIKON_OC_GetPictCtrlData,     1,          1,             3000  },
    { CAM_CMD_SET_PICTCTRL,       NIKON_OC_SetPictCtrlData,     1,          1,             3000  },
    { CAM_CMD_EVENT_POLL,         NIKON_OC_GetEvent,            1,          1,             3000  },
};

const int kNikonCommandMappingCount =
    (int)(sizeof(kNikonCommandMappings) / sizeof(kNikonCommandMappings[0]));

/**
 * 按命令类型查找映射条目。
 * @return 指向条目的指针; NULL 未找到
 */
const CommandMapping *command_map_find(CameraCommandType type) {
    for (int i = 0; i < kNikonCommandMappingCount; i++) {
        if (kNikonCommandMappings[i].cmd_type == type) {
            return &kNikonCommandMappings[i];
        }
    }
    return NULL;
}
