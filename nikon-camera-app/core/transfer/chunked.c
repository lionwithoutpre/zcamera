/**
 * transfer/chunked.c — 分块传输与断点续传实现
 *
 * 架构:
 *   - 固定大小线程池 (worker_threads 参数控制并发度)
 *   - 任务队列 (FIFO + 条件变量)
 *   - 自适应块大小 (根据实时速度 64KB~4MB)
 *   - 断点续传 (检查 dest 文件已有大小)
 *   - PTP GetObject 分块读取 (NIKON_OC_GetPartialObject 或 PTP_OC_GetObject)
 */
#include "transfer/transfer.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdatomic.h>

/* ═══════════════════════════════════════════════════════════════
 *  任务与线程池
 * ══════════════════════════════════════════════════════════════ */

#define CHUNKED_MAX_RETRIES  3
#define CHUNKED_RETRY_DELAY_MS  500

typedef struct TransferJob {
    int                  job_id;
    TransferContext      ctx;
    PtpSession          *session;
    transfer_progress_cb on_progress;
    transfer_complete_cb on_complete;
    void                *user_data;
    _Atomic int          cancelled;
    pthread_t            tid;
    int                  active;           /* 1=占用中 */
} TransferJob;

/* ── 线程池 ──────────────────────────────────────────────────── */

static TransferJob   s_jobs[TRANSFER_MAX_JOBS];
static pthread_mutex_t s_pool_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  s_pool_cond = PTHREAD_COND_INITIALIZER;

static pthread_t      s_workers[16];        /* 最多 16 个工作线程 */
static int            s_worker_count   = 0;
static int            s_next_job_id    = 1;
static _Atomic int    s_initialized    = 0;
static _Atomic int    s_running        = 0;

/* ── 工作线程入口 ────────────────────────────────────────────── */

/* ── 前向声明 ────────────────────────────────────────────────── */

static uint64_t _now_ms(void);
static uint32_t _adaptive_chunk_size(uint64_t file_size, double speed_mbps);

static TransferJob *_pool_find_idle(void) {
    for (int i = 0; i < TRANSFER_MAX_JOBS; i++) {
        if (!s_jobs[i].active) return &s_jobs[i];
    }
    return NULL;
}

static TransferJob *_pool_dequeue(void) {
    for (int i = 0; i < TRANSFER_MAX_JOBS; i++) {
        if (s_jobs[i].active && s_jobs[i].job_id > 0 && s_jobs[i].tid == 0) {
            return &s_jobs[i];
        }
    }
    return NULL;
}

static void *_worker_thread(void *arg) {
    (void)arg;
    while (atomic_load(&s_running)) {
        TransferJob *job = NULL;
        pthread_mutex_lock(&s_pool_lock);
        /* 等待任务 */
        while (atomic_load(&s_running) && !(job = _pool_dequeue())) {
            pthread_cond_wait(&s_pool_cond, &s_pool_lock);
        }
        if (!atomic_load(&s_running)) { pthread_mutex_unlock(&s_pool_lock); break; }
        job->tid = pthread_self();  /* 标记为已被此线程认领 */
        pthread_mutex_unlock(&s_pool_lock);

        /* ── 执行传输 ──────────────────────────────────────── */
        TransferContext *ctx = &job->ctx;
        ctx->timestamp_start_ms = _now_ms();

        /* 断点续传: 检查目标文件已有大小 */
        struct stat st;
        if (stat(ctx->dest_path, &st) == 0 && (uint64_t)st.st_size < ctx->file_size) {
            ctx->offset      = (uint64_t)st.st_size;
            ctx->transferred = (uint64_t)st.st_size;
        }

        /* 打开目标文件 */
        int flags = O_WRONLY | O_CREAT;
        flags |= (ctx->offset > 0) ? O_APPEND : O_TRUNC;
        int dest_fd = open(ctx->dest_path, flags, 0644);
        if (dest_fd < 0) {
            if (job->on_complete)
                job->on_complete(ctx, CAM_ERR_TRANSFER_FAILED, job->user_data);
            goto job_cleanup;
        }

        double  total_speed = 0.0;
        int     speed_samples = 0;

        while (!atomic_load(&job->cancelled) && ctx->transferred < ctx->file_size) {
            uint64_t remaining = ctx->file_size - ctx->transferred;

            /* 自适应块大小 (基于实时速度) */
            double avg_speed = (speed_samples > 0)
                ? total_speed / (double)speed_samples : 0.0;
            uint32_t chunk = _adaptive_chunk_size(remaining, avg_speed);
            if ((uint64_t)chunk > remaining) chunk = (uint32_t)remaining;

            /* ── PTP GetObject 分块读取 (含重试) ──────────── */
            uint64_t t_chunk_start = _now_ms();
            uint32_t params[5] = {
                ctx->object_handle,
                (uint32_t)(ctx->offset & 0xFFFFFFFF),
                chunk
            };
            uint8_t  *chunk_buf = (uint8_t *)malloc(chunk);
            if (!chunk_buf) {
                close(dest_fd);
                if (job->on_complete)
                    job->on_complete(ctx, CAM_ERR_OUT_OF_MEMORY, job->user_data);
                goto job_cleanup;
            }

            uint32_t recv_len = 0;
            int ptp_rc = -1;
            int retry_count;
            for (retry_count = 0; retry_count < CHUNKED_MAX_RETRIES; retry_count++) {
                if (atomic_load(&job->cancelled)) break;
                recv_len = 0;
                ptp_rc = ptp_exec(job->session,
                                  (uint16_t)PTP_OC_GetObject,
                                  params, 3,
                                  NULL, 0,
                                  chunk_buf, chunk, &recv_len);
                if (ptp_rc == (int)PTP_RC_OK && recv_len > 0) break;
                if (retry_count < CHUNKED_MAX_RETRIES - 1)
                    usleep(CHUNKED_RETRY_DELAY_MS * 1000);
            }

            uint64_t t_chunk_end = _now_ms();
            uint64_t t_elapsed   = (t_chunk_end > t_chunk_start)
                                        ? t_chunk_end - t_chunk_start : 1;

            if (ptp_rc != (int)PTP_RC_OK || recv_len == 0) {
                free(chunk_buf);
                close(dest_fd);
                if (job->on_complete)
                    job->on_complete(ctx, CAM_ERR_TRANSFER_FAILED, job->user_data);
                goto job_cleanup;
            }

            /* 写入目标文件 */
            ssize_t written = write(dest_fd, chunk_buf, recv_len);
            free(chunk_buf);

            if (written < 0 || (uint32_t)written != recv_len) {
                close(dest_fd);
                if (job->on_complete)
                    job->on_complete(ctx, CAM_ERR_TRANSFER_FAILED, job->user_data);
                goto job_cleanup;
            }

            /* 更新进度 */
            ctx->transferred += recv_len;
            ctx->offset       = ctx->transferred;
            ctx->chunk_index++;

            /* 计算实时速度 */
            double chunk_mbps = (double)recv_len / (1024.0 * 1024.0)
                                / ((double)t_elapsed / 1000.0);
            total_speed += chunk_mbps;
            speed_samples++;

            /* 通知进度 */
            int pct = (ctx->file_size > 0)
                          ? (int)(ctx->transferred * 100 / ctx->file_size)
                          : 0;
            if (job->on_progress) job->on_progress(ctx, pct, job->user_data);
        }

        close(dest_fd);

        int err = atomic_load(&job->cancelled) ? CAM_ERR_TRANSFER_FAILED : 0;
        if (job->on_complete) job->on_complete(ctx, err, job->user_data);

    job_cleanup:
        pthread_mutex_lock(&s_pool_lock);
        job->active = 0;
        job->job_id = 0;
        job->tid    = 0;
        pthread_mutex_unlock(&s_pool_lock);
    }
    return NULL;
}

/* ═══════════════════════════════════════════════════════════════
 *  自适应块大小
 * ══════════════════════════════════════════════════════════════ */

static uint64_t _now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static uint32_t _adaptive_chunk_size(uint64_t file_size, double speed_mbps) {
    /* 小文件不分块 */
    if (file_size <= TRANSFER_SMALL_FILE_THRESH)
        return (uint32_t)file_size;

    uint32_t size;
    if (speed_mbps > 20.0)
        size = TRANSFER_CHUNK_MAX;
    else if (speed_mbps > 5.0)
        size = TRANSFER_CHUNK_DEFAULT * 2;
    else
        size = TRANSFER_CHUNK_DEFAULT;

    if (size > TRANSFER_CHUNK_MAX) size = TRANSFER_CHUNK_MAX;
    if (size < TRANSFER_CHUNK_MIN) size = TRANSFER_CHUNK_MIN;
    return size;
}

/* ═══════════════════════════════════════════════════════════════
 *  公开接口
 * ══════════════════════════════════════════════════════════════ */

int transfer_engine_init(int worker_threads) {
    if (atomic_load(&s_initialized)) return 0;

    memset(s_jobs, 0, sizeof(s_jobs));

    if (worker_threads <= 0) worker_threads = 2;
    if (worker_threads > 16) worker_threads = 16;

    atomic_store(&s_running, 1);
    for (int i = 0; i < worker_threads; i++) {
        if (pthread_create(&s_workers[i], NULL, _worker_thread, NULL) == 0) {
            s_worker_count++;
        }
    }

    int ok = (s_worker_count > 0) ? 1 : 0;
    atomic_store(&s_initialized, ok);
    return ok ? 0 : -1;
}

void transfer_engine_shutdown(void) {
    if (!atomic_load(&s_initialized)) return;

    atomic_store(&s_running, 0);
    pthread_cond_broadcast(&s_pool_cond);

    for (int i = 0; i < s_worker_count; i++) {
        if (s_workers[i]) pthread_join(s_workers[i], NULL);
    }

    atomic_store(&s_initialized, 0);
    s_worker_count = 0;
}

int transfer_submit(PtpSession *session,
                    uint32_t handle, const char *dest_path,
                    transfer_progress_cb on_progress,
                    transfer_complete_cb on_complete,
                    void *user_data)
{
    if (!atomic_load(&s_initialized) || !session || !dest_path) return CAM_ERR_INVALID_PARAM;

    /* 获取文件大小 (通过 GetObjectInfo) */
    uint32_t info_params[5] = { handle, 0, 0, 0, 0 };
    uint8_t  info_buf[512];
    uint32_t info_len = 0;
    int rc = ptp_exec(session,
                      (uint16_t)PTP_OC_GetObjectInfo,
                      info_params, 1,
                      NULL, 0,
                      info_buf, sizeof(info_buf), &info_len);
    if (rc != (int)PTP_RC_OK || info_len < sizeof(uint32_t) * 3) {
        return CAM_ERR_FILE_NOT_FOUND;
    }

    /* MtpObjectInfoFixed packed struct:
     * offset 0: storage_id (4)
     * offset 4: object_format (2)
     * offset 6: protection_status (2)
     * offset 8: object_compressed_size (4) */
    uint64_t file_size = (uint64_t)*(uint32_t *)(info_buf + 8);

    pthread_mutex_lock(&s_pool_lock);
    TransferJob *slot = _pool_find_idle();
    if (!slot) {
        pthread_mutex_unlock(&s_pool_lock);
        return CAM_ERR_BUSY;
    }

    memset(slot, 0, sizeof(*slot));
    slot->job_id                  = s_next_job_id++;
    slot->session                 = session;
    slot->ctx.object_handle       = handle;
    slot->ctx.job_id              = slot->job_id;
    slot->ctx.file_size           = file_size;
    slot->ctx.chunk_size          = TRANSFER_CHUNK_DEFAULT;
    slot->on_progress             = on_progress;
    slot->on_complete             = on_complete;
    slot->user_data               = user_data;
    slot->active                  = 1;
    strncpy(slot->ctx.dest_path, dest_path, sizeof(slot->ctx.dest_path) - 1);

    int jid = slot->job_id;

    /* 唤醒一个空闲工作线程 */
    pthread_cond_signal(&s_pool_cond);
    pthread_mutex_unlock(&s_pool_lock);
    return jid;
}

int transfer_cancel(int job_id) {
    pthread_mutex_lock(&s_pool_lock);
    for (int i = 0; i < TRANSFER_MAX_JOBS; i++) {
        if (s_jobs[i].job_id == job_id && s_jobs[i].active) {
            atomic_store(&s_jobs[i].cancelled, 1);
            pthread_mutex_unlock(&s_pool_lock);
            return 0;
        }
    }
    pthread_mutex_unlock(&s_pool_lock);
    return CAM_ERR_FILE_NOT_FOUND;
}

int transfer_get_progress(int job_id, TransferProgress *out) {
    if (!out) return CAM_ERR_INVALID_PARAM;
    pthread_mutex_lock(&s_pool_lock);
    for (int i = 0; i < TRANSFER_MAX_JOBS; i++) {
        if (s_jobs[i].job_id == job_id && s_jobs[i].active) {
            TransferContext *ctx = &s_jobs[i].ctx;
            out->job_id        = job_id;
            out->object_handle = ctx->object_handle;
            out->total_size    = ctx->file_size;
            out->transferred   = ctx->transferred;
            out->percent       = ctx->file_size
                ? (int)(ctx->transferred * 100 / ctx->file_size)
                : 0;
            out->status        = atomic_load(&s_jobs[i].cancelled)
                ? TRANSFER_STATUS_CANCELLED : TRANSFER_STATUS_RUNNING;
            pthread_mutex_unlock(&s_pool_lock);
            return 0;
        }
    }
    pthread_mutex_unlock(&s_pool_lock);
    return CAM_ERR_FILE_NOT_FOUND;
}

void transfer_wait_all(void) {
    /* 等待所有 active job 被清理 */
    for (;;) {
        int any_active = 0;
        pthread_mutex_lock(&s_pool_lock);
        for (int i = 0; i < TRANSFER_MAX_JOBS; i++) {
            if (s_jobs[i].active) { any_active = 1; break; }
        }
        pthread_mutex_unlock(&s_pool_lock);
        if (!any_active) break;
        usleep(50000);  /* 50ms */
    }
}
