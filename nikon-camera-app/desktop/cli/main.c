/**
 * desktop/cli/main.c — Nikon Camera Connect CLI
 *
 * 使用: nikon-cli [command] [options]
 *
 * Commands:
 *   scan              扫描相机
 *   connect <id>      连接相机
 *   capture           拍摄
 *   liveview          实时取景 (帧数据到 stdout)
 *   list [storage]    列出文件 (CF/SD)
 *   transfer <handle> <dest>   传输单文件
 *   transfer-all <dest>        传输全部文件
 *   prop get <id>     读取属性
 *   prop set <id> <val>  写入属性
 *   preset get        读取 Picture Control
 *   preset set <json> 写入 Picture Control
 *   disconnect        断开连接
 */
#include "api/camera_api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ─── 辅助 ───────────────────────────────────────────────────── */

static void _print_usage(const char *prog) {
    fprintf(stderr,
        "Nikon Camera Connect CLI v1.0\n"
        "Usage: %s <command> [args]\n\n"
        "Commands:\n"
        "  scan                      扫描相机\n"
        "  connect <camera_id>       连接相机\n"
        "  capture                   拍摄单张\n"
        "  burst <count> [interval]  连拍\n"
        "  list [CF|SD]              列出文件\n"
        "  transfer <handle> <dest>  传输文件\n"
        "  transfer-all <dest>       传输全部\n"
        "  prop get <prop_id_hex>    读取属性\n"
        "  prop set <prop_id_hex> <val_hex>\n"
        "  preset get                读取色彩控制\n"
        "  preset set <json_file>    写入色彩控制\n"
        "  disconnect                断开连接\n",
        prog);
}

static void _on_progress(int job_id, TransferProgress *p, void *ud) {
    (void)ud;
    printf("\r  [job %d] %s  %d%%  %.1f MB/s  ",
           job_id, p->filename, p->percent, p->speed_mbps);
    fflush(stdout);
    if (p->percent >= 100) printf("\n");
}

static void _on_new_file(uint32_t handle, const char *filename, void *ud) {
    (void)ud;
    printf("[边拍边传] 新文件: %s (handle=0x%X)\n", filename, handle);
}

/* ─── 主程序 ─────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    if (argc < 2) {
        _print_usage(argv[0]);
        return 1;
    }

    /* 创建 API (自动选择传输方式) */
    CameraAPI *api = camera_api_create(TRANSPORT_AUTO);
    if (!api) {
        fprintf(stderr, "camera_api_create 失败\n");
        return 1;
    }

    int ret = 0;
    const char *cmd = argv[1];

    /* ── scan ── */
    if (strcmp(cmd, "scan") == 0) {
        int count = 0;
        CameraInfo *cameras = camera_api_scan(api, &count);
        if (!cameras || count == 0) {
            printf("未发现相机\n");
        } else {
            printf("发现 %d 台相机:\n", count);
            for (int i = 0; i < count; i++) {
                printf("  [%d] %s  SN=%s  Transport=%s  Battery=%d%%\n",
                       i + 1,
                       cameras[i].model,
                       cameras[i].serial,
                       cameras[i].transport == 0 ? "USB" : "Wi-Fi",
                       cameras[i].battery_level);
            }
            free(cameras);
        }
    }

    /* ── connect ── */
    else if (strcmp(cmd, "connect") == 0 && argc >= 3) {
        printf("连接相机: %s\n", argv[2]);
        ret = camera_api_connect(api, argv[2]);
        if (ret != CAM_OK)
            fprintf(stderr, "连接失败: %s\n", camera_api_strerror(ret));
        else
            printf("连接成功\n");
    }

    /* ── capture ── */
    else if (strcmp(cmd, "capture") == 0) {
        ret = camera_api_capture(api);
        if (ret != CAM_OK)
            fprintf(stderr, "拍摄失败: %s\n", camera_api_strerror(ret));
        else
            printf("拍摄完成\n");
    }

    /* ── burst ── */
    else if (strcmp(cmd, "burst") == 0 && argc >= 3) {
        int count    = atoi(argv[2]);
        int interval = (argc >= 4) ? atoi(argv[3]) : 0;
        ret = camera_api_capture_burst(api, count, interval);
        printf("连拍 %d 张 (间隔 %dms)\n", count, interval);
    }

    /* ── list ── */
    else if (strcmp(cmd, "list") == 0) {
        uint32_t storage_id = 0;
        if (argc >= 3) {
            if (strcmp(argv[2], "CF") == 0) storage_id = NIKON_STORAGE_CF;
            else if (strcmp(argv[2], "SD") == 0) storage_id = NIKON_STORAGE_SD;
        }
        int count = 0;
        FileInfo *files = camera_api_list_files(api, storage_id, &count);
        if (!files) {
            fprintf(stderr, "列表获取失败\n");
        } else {
            printf("共 %d 个文件:\n", count);
            for (int i = 0; i < count; i++) {
                printf("  0x%08X  %-30s  %7.1f MB  %s  %s\n",
                       files[i].object_handle,
                       files[i].filename,
                       (double)files[i].size / (1024 * 1024),
                       files[i].datetime,
                       files[i].is_raw ? "RAW" : "JPG");
            }
            free(files);
        }
    }

    /* ── transfer ── */
    else if (strcmp(cmd, "transfer") == 0 && argc >= 4) {
        uint32_t handle = (uint32_t)strtoul(argv[2], NULL, 0);
        camera_api_on_transfer_progress(api, _on_progress, NULL);
        int job = camera_api_start_transfer(api, handle, argv[3]);
        if (job < 0)
            fprintf(stderr, "传输失败: %s\n", camera_api_strerror(job));
        else
            printf("传输任务 #%d 已提交\n", job);
    }

    /* ── transfer-all ── */
    else if (strcmp(cmd, "transfer-all") == 0 && argc >= 3) {
        camera_api_on_new_file(api, _on_new_file, NULL);
        int count = 0;
        FileInfo *files = camera_api_list_files(api, 0, &count);
        if (files && count > 0) {
            uint32_t *handles = (uint32_t *)malloc((size_t)count * sizeof(uint32_t));
            for (int i = 0; i < count; i++) handles[i] = files[i].object_handle;
            int job = camera_api_start_batch_transfer(api, handles, count, argv[2]);
            printf("批量传输 #%d (%d 文件) → %s\n", job, count, argv[2]);
            free(handles);
            free(files);
        }
    }

    /* ── prop get ── */
    else if (strcmp(cmd, "prop") == 0 && argc >= 4 && strcmp(argv[2], "get") == 0) {
        uint16_t prop_id = (uint16_t)strtoul(argv[3], NULL, 16);
        uint32_t val = 0;
        ret = camera_api_get_property(api, prop_id, &val);
        if (ret == CAM_OK)
            printf("0x%04X = 0x%08X (%u)\n", prop_id, val, val);
        else
            fprintf(stderr, "读取失败: %s\n", camera_api_strerror(ret));
    }

    /* ── prop set ── */
    else if (strcmp(cmd, "prop") == 0 && argc >= 5 && strcmp(argv[2], "set") == 0) {
        uint16_t prop_id = (uint16_t)strtoul(argv[3], NULL, 16);
        uint32_t val     = (uint32_t)strtoul(argv[4], NULL, 0);
        ret = camera_api_set_property(api, prop_id, val);
        printf("0x%04X ← 0x%08X: %s\n", prop_id, val,
               ret == CAM_OK ? "OK" : camera_api_strerror(ret));
    }

    /* ── preset get ── */
    else if (strcmp(cmd, "preset") == 0 && argc >= 3 && strcmp(argv[2], "get") == 0) {
        PictureControl pc;
        ret = camera_api_get_pictctrl(api, &pc);
        if (ret == CAM_OK) {
            printf("Picture Control:\n"
                   "  Hue        : %+d\n"
                   "  Saturation : %+d\n"
                   "  Contrast   : %+d\n"
                   "  Clarity    : %+d\n"
                   "  Sharpening : %d\n"
                   "  Brightness : %+d\n"
                   "  WB A-B     : %+d\n"
                   "  WB G-M     : %+d\n"
                   "  ColorSpace : %s\n",
                   pc.hue, pc.saturation, pc.contrast,
                   pc.clarity, pc.sharpening, pc.brightness,
                   pc.wb_ab, pc.wb_gm,
                   pc.color_space == 0 ? "sRGB" : "AdobeRGB");
        } else {
            fprintf(stderr, "读取失败: %s\n", camera_api_strerror(ret));
        }
    }

    /* ── disconnect ── */
    else if (strcmp(cmd, "disconnect") == 0) {
        camera_api_disconnect(api);
        printf("已断开\n");
    }

    else {
        _print_usage(argv[0]);
        ret = 1;
    }

    camera_api_destroy(api);
    return ret;
}
