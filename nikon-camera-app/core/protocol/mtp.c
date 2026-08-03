/**
 * protocol/mtp.c — MTP 扩展 (文件传输对象管理)
 */
#include "protocol/ptp.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <strings.h>

/* ─── MTP 对象信息结构体 ────────────────────────────────────── */

/* 对象格式码 */
#define MTP_OBJ_FORMAT_JPEG     0x3801u
#define MTP_OBJ_FORMAT_NEF      0x3805u  /* Nikon RAW */
#define MTP_OBJ_FORMAT_NRW      0x3806u  /* Nikon RAW (小文件) */
#define MTP_OBJ_FORMAT_MP4      0xB982u
#define MTP_OBJ_FORMAT_MOV      0xB984u

typedef struct __attribute__((packed)) {
    uint32_t    storage_id;
    uint16_t    object_format;
    uint16_t    protection_status;
    uint32_t    object_compressed_size;
    uint16_t    thumb_format;
    uint32_t    thumb_compressed_size;
    uint32_t    thumb_offset;
    uint32_t    thumb_pix_width;
    uint32_t    thumb_pix_height;
    uint32_t    image_pix_width;
    uint32_t    image_pix_height;
    uint32_t    image_bit_depth;
    uint32_t    parent_object;
    uint16_t    association_type;
    uint32_t    association_desc;
    uint32_t    sequence_number;
    /* 以下字段为变长 UTF-16LE 字符串, 解析时按实际偏移读取 */
    /* char     filename[];     */
    /* char     capture_date[]; */
    /* char     modification_date[]; */
} MtpObjectInfoFixed;

/* ─── 获取对象列表 ───────────────────────────────────────────── */

/**
 * 获取指定存储卡上的文件 Handle 列表。
 *
 * @param session     PTP 会话
 * @param storage_id  存储卡 ID (0xFFFFFFFF=全部)
 * @param format      对象格式过滤 (0=全部; MTP_OBJ_FORMAT_JPEG 等)
 * @param handles     [out] Handle 数组 (调用者 free())
 * @param count       [out] 数量
 * @return 0=成功; < 0 错误
 */
int mtp_get_object_handles(PtpSession *session,
                            uint32_t storage_id, uint16_t format,
                            uint32_t **handles, int *count)
{
    if (!session || !handles || !count) return -1;
    *handles = NULL;
    *count   = 0;

    uint32_t params[5] = { storage_id, format, 0, 0, 0 };
    uint8_t  buf[4096];
    uint32_t out_len = 0;
    int rc = ptp_exec(session,
                      (uint16_t)PTP_OC_GetObjectHandles,
                      params, 3,
                      NULL, 0,
                      buf, sizeof(buf), &out_len);
    if (rc != (int)PTP_RC_OK || out_len < 4) return rc;

    uint32_t n = *(uint32_t *)buf;
    if (n == 0) return 0;
    if (n * 4 + 4 > out_len) n = (out_len - 4) / 4;

    *handles = (uint32_t *)malloc(n * sizeof(uint32_t));
    if (!*handles) return -1;

    memcpy(*handles, buf + 4, n * sizeof(uint32_t));
    *count = (int)n;
    return 0;
}

/**
 * 获取对象信息 (ObjectInfo)。
 * 将固定部分解析到 info 结构体。
 */
int mtp_get_object_info(PtpSession *session,
                         uint32_t handle,
                         MtpObjectInfoFixed *info)
{
    if (!session || !info) return -1;
    uint32_t params[5] = { handle, 0, 0, 0, 0 };
    uint8_t  buf[512];
    uint32_t out_len = 0;
    int rc = ptp_exec(session,
                      (uint16_t)PTP_OC_GetObjectInfo,
                      params, 1,
                      NULL, 0,
                      buf, sizeof(buf), &out_len);
    if (rc != (int)PTP_RC_OK || out_len < sizeof(MtpObjectInfoFixed)) return rc;
    memcpy(info, buf, sizeof(MtpObjectInfoFixed));
    return 0;
}

/**
 * 获取对象数据 (完整文件, 同步)。
 * 对大文件请使用 transfer_submit() 进行分块异步传输。
 *
 * @param data  [out] 数据缓冲 (调用者 free())
 * @param size  [out] 数据大小
 */
int mtp_get_object(PtpSession *session, uint32_t handle,
                   uint8_t **data, uint32_t *size)
{
    if (!session || !data || !size) return -1;
    *data = NULL;
    *size = 0;

    /* 先查对象信息获取文件大小 */
    MtpObjectInfoFixed info;
    int rc = mtp_get_object_info(session, handle, &info);
    if (rc != 0) return rc;

    uint32_t file_size = info.object_compressed_size;
    *data = (uint8_t *)malloc(file_size);
    if (!*data) return -1;

    uint32_t params[5] = { handle, 0, 0, 0, 0 };
    uint32_t out_len = 0;
    rc = ptp_exec(session,
                  (uint16_t)PTP_OC_GetObject,
                  params, 1,
                  NULL, 0,
                  *data, file_size, &out_len);
    if (rc != (int)PTP_RC_OK) {
        free(*data);
        *data = NULL;
        return rc;
    }
    *size = out_len;
    return 0;
}

/**
 * 获取缩略图。
 */
int mtp_get_thumbnail(PtpSession *session, uint32_t handle,
                      uint8_t **data, uint32_t *size)
{
    if (!session || !data || !size) return -1;
    *data = NULL;
    *size = 0;

    uint32_t params[5] = { handle, 0, 0, 0, 0 };
    uint8_t  buf[65536];   /* 64KB 缩略图上限 */
    uint32_t out_len = 0;
    int rc = ptp_exec(session,
                      (uint16_t)PTP_OC_GetThumb,
                      params, 1,
                      NULL, 0,
                      buf, sizeof(buf), &out_len);
    if (rc != (int)PTP_RC_OK || out_len == 0) return rc;

    *data = (uint8_t *)malloc(out_len);
    if (!*data) return -1;
    memcpy(*data, buf, out_len);
    *size = out_len;
    return 0;
}

/**
 * 删除相机内对象。
 */
int mtp_delete_object(PtpSession *session, uint32_t handle) {
    if (!session) return -1;
    uint32_t params[5] = { handle, 0, 0, 0, 0 };
    uint32_t out_len = 0;
    return ptp_exec(session,
                    (uint16_t)PTP_OC_DeleteObject,
                    params, 1,
                    NULL, 0,
                    NULL, 0, &out_len);
}

/**
 * 发送本地文件到相机 (PTP SendObject)。
 *
 * 完整流程:
 *   1. SendObjectInfo — 告知相机即将接收的文件元数据
 *   2. SendObject     — 传输文件数据
 *
 * @param session      PTP 会话 (已打开)
 * @param local_path   本地文件路径
 * @param storage_id   目标存储卡 ID (0=主卡)
 * @param parent_obj   父目录 Object Handle (0=根目录)
 * @param remote_name  相机端文件名 (NULL 则使用 local_path 基名)
 * @return 0=成功; < 0 错误
 */
int mtp_send_file(PtpSession *session,
                  const char *local_path,
                  uint32_t storage_id,
                  uint32_t parent_obj,
                  const char *remote_name)
{
    if (!session || !local_path) return -1;

    FILE *fp = fopen(local_path, "rb");
    if (!fp) return -2;

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (file_size <= 0) { fclose(fp); return -3; }

    const char *basename = remote_name ? remote_name : local_path;
    const char *slash = strrchr(basename, '/');
    if (slash) basename = slash + 1;
    const char *bslash = strrchr(basename, '\\');
    if (bslash) basename = bslash + 1;

    uint16_t obj_format = 0x3000u;
    size_t namelen = strlen(basename);
    if (namelen >= 4) {
        const char *ext = basename + namelen - 4;
        if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0)
            obj_format = MTP_OBJ_FORMAT_JPEG;
        else if (strcasecmp(ext, ".nef") == 0)
            obj_format = MTP_OBJ_FORMAT_NEF;
        else if (strcasecmp(ext, ".nrw") == 0)
            obj_format = MTP_OBJ_FORMAT_NRW;
        else if (strcasecmp(ext, ".mp4") == 0)
            obj_format = MTP_OBJ_FORMAT_MP4;
        else if (strcasecmp(ext, ".mov") == 0)
            obj_format = MTP_OBJ_FORMAT_MOV;
    }

    uint8_t info_buf[1024];
    uint32_t info_off = 0;

    uint32_t sid = storage_id ? storage_id : 0x00010001u;
    memcpy(info_buf + info_off, &sid, 4); info_off += 4;
    memcpy(info_buf + info_off, &obj_format, 2); info_off += 2;
    uint16_t protection = 0;
    memcpy(info_buf + info_off, &protection, 2); info_off += 2;
    uint32_t fsize32 = (uint32_t)file_size;
    memcpy(info_buf + info_off, &fsize32, 4); info_off += 4;
    uint16_t thumb_fmt = 0;
    memcpy(info_buf + info_off, &thumb_fmt, 2); info_off += 2;
    uint32_t zero32 = 0;
    for (int i = 0; i < 6; i++) {
        memcpy(info_buf + info_off, &zero32, 4); info_off += 4;
    }
    memcpy(info_buf + info_off, &parent_obj, 4); info_off += 4;
    uint16_t assoc_type = 0;
    memcpy(info_buf + info_off, &assoc_type, 2); info_off += 2;
    uint32_t assoc_desc = 0;
    memcpy(info_buf + info_off, &assoc_desc, 4); info_off += 4;
    uint32_t seq_num = 0;
    memcpy(info_buf + info_off, &seq_num, 4); info_off += 4;

    uint8_t name_utf16[256];
    int name_u16_len = 0;
    for (size_t i = 0; i < strlen(basename) && name_u16_len < (int)sizeof(name_utf16) - 4; i++) {
        name_utf16[name_u16_len++] = (uint8_t)basename[i];
        name_utf16[name_u16_len++] = 0;
    }
    name_utf16[name_u16_len++] = 0;
    name_utf16[name_u16_len++] = 0;
    memcpy(info_buf + info_off, name_utf16, (size_t)name_u16_len);
    info_off += (uint32_t)name_u16_len;

    uint32_t soi_params[5] = { storage_id ? storage_id : 0x00010001u, parent_obj, 0, 0, 0 };
    uint32_t out_len = 0;
    int rc = ptp_exec(session,
                      (uint16_t)PTP_OC_SendObjectInfo,
                      soi_params, 2,
                      info_buf, info_off,
                      NULL, 0, &out_len);
    if (rc != (int)PTP_RC_OK) {
        fclose(fp);
        return rc;
    }

    uint8_t *file_data = (uint8_t *)malloc((size_t)file_size);
    if (!file_data) { fclose(fp); return -4; }
    size_t read_n = fread(file_data, 1, (size_t)file_size, fp);
    fclose(fp);
    if ((long)read_n != file_size) { free(file_data); return -5; }

    uint32_t send_out = 0;
    rc = ptp_exec(session,
                  (uint16_t)PTP_OC_SendObject,
                  NULL, 0,
                  file_data, (uint32_t)file_size,
                  NULL, 0, &send_out);
    free(file_data);
    return rc == (int)PTP_RC_OK ? 0 : rc;
}
