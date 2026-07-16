/**
 * transfer/transfer.h — 文件传输引擎接口
 *
 * 支持零拷贝 + 分块并发传输 + 断点续传。
 * 传输引擎在独立工作线程池中运行。
 */
#ifndef NIKON_TRANSFER_H
#define NIKON_TRANSFER_H

#include <stdint.h>
#include "api/camera_api.h"
#include "protocol/ptp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ─── 块大小策略 (SPEC_TRANSFER §3) ─────────────────────────── */

#define TRANSFER_CHUNK_MIN          (64   * 1024)           /**< 64 KB */
#define TRANSFER_CHUNK_DEFAULT      (1024 * 1024)           /**< 1 MB */
#define TRANSFER_CHUNK_MAX          (4    * 1024 * 1024)    /**< 4 MB */
#define TRANSFER_SMALL_FILE_THRESH  (4    * 1024 * 1024)    /**< < 4MB 不分块 */
#define TRANSFER_MAX_CONCURRENT     4                       /**< 最大并发块数 */
#define TRANSFER_MAX_JOBS           64                      /**< 最大任务数 */

/* ─── 任务状态 ───────────────────────────────────────────────── */

#define TRANSFER_STATUS_PENDING     0
#define TRANSFER_STATUS_RUNNING     1
#define TRANSFER_STATUS_DONE        2
#define TRANSFER_STATUS_FAILED     (-1)
#define TRANSFER_STATUS_CANCELLED  (-2)

/* ─── 传输上下文 ─────────────────────────────────────────────── */

typedef struct {
    uint32_t    object_handle;
    uint64_t    file_size;
    uint64_t    offset;             /**< 断点续传起始偏移 */
    uint32_t    chunk_size;         /**< 当前块大小 (自适应调整) */
    uint32_t    chunk_index;
    uint64_t    transferred;
    uint64_t    timestamp_start_ms;
    uint8_t     md5[16];            /**< 当前块 MD5 */
    char        dest_path[512];
    int         job_id;
} TransferContext;

/** 进度回调 */
typedef void (*transfer_progress_cb)(TransferContext *ctx, int percent, void *user_data);
/** 完成回调 (error_code=0 表示成功) */
typedef void (*transfer_complete_cb)(TransferContext *ctx, int error_code, void *user_data);

/* ─── 传输引擎接口 ───────────────────────────────────────────── */

/** 初始化传输引擎 (创建线程池)。 */
int transfer_engine_init(int worker_threads);

/** 释放传输引擎。 */
void transfer_engine_shutdown(void);

/**
 * 提交传输任务 (异步)。
 * 支持断点续传: 若 dest_path 已存在且大小 > 0 则从断点继续。
 *
 * @param session      PTP 会话
 * @param handle       PTP object handle
 * @param dest_path    本地目标路径
 * @param on_progress  进度回调 (可 NULL)
 * @param on_complete  完成回调 (可 NULL)
 * @param user_data    回调用户数据
 * @return >= 0 job_id; < 0 错误码
 */
int transfer_submit(PtpSession *session,
                    uint32_t handle, const char *dest_path,
                    transfer_progress_cb on_progress,
                    transfer_complete_cb on_complete,
                    void *user_data);

/** 取消任务 (若已完成则无效)。 */
int transfer_cancel(int job_id);

/** 查询任务当前进度快照。 */
int transfer_get_progress(int job_id, TransferProgress *out);

/** 等待所有任务完成 (阻塞)。 */
void transfer_wait_all(void);

/* ─── 零拷贝接口 ─────────────────────────────────────────────── */

/**
 * 零拷贝写文件 (platform-specific)。
 * Linux   : splice() + sendfile()
 * macOS   : fcopyfile()
 * Windows : TransmitFile()
 *
 * @param src_fd   源文件描述符 (已就绪)
 * @param dest_fd  目标文件描述符 (已打开写)
 * @param offset   源起始偏移
 * @param length   拷贝字节数
 * @return 实际拷贝字节数; < 0 错误码
 */
int64_t transfer_zerocopy(int src_fd, int dest_fd, uint64_t offset, uint64_t length);

#ifdef __cplusplus
}
#endif
#endif /* NIKON_TRANSFER_H */
