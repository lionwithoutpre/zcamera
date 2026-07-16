# 传输引擎规格书 — 零拷贝与分块传输

## 1. 零拷贝传输架构

```
传统路径 (3次拷贝):
  磁盘 ──► 内核缓冲区 ──► 用户空间缓冲区 ──► Socket/USB 缓冲区

零拷贝路径 (1次拷贝, 硬件 DMA):
  磁盘 ──► 内核缓冲区 ──► (直接 DMA) ──► USB 端点
                │
          splice() / sendfile()
```

### 1.1 Linux/Android 零拷贝 (优先)

```c
int transfer_splice(int pipe_fd, int usb_fd, size_t length) {
    loff_t offset = 0;
    size_t remaining = length;

    while (remaining > 0) {
        ssize_t n = splice(pipe_fd, NULL, usb_fd, NULL,
                          remaining > SPLICE_CHUNK ? SPLICE_CHUNK : remaining,
                          SPLICE_F_MORE | SPLICE_F_MOVE);
        if (n <= 0) {
            if (errno == EINTR) continue;
            return -errno;
        }
        remaining -= n;
    }
    return length - remaining;
}

#define SPLICE_CHUNK (512 * 1024)
```

### 1.2 macOS 零拷贝

```c
#include <copyfile.h>

int transfer_fcopyfile(int src_fd, int dst_fd, off_t length) {
    return fcopyfile(src_fd, dst_fd, NULL, COPYFILE_DATA);
}

int transfer_sendfile_macos(int src_fd, int dst_fd, off_t* offset, off_t length) {
    return sendfile(src_fd, dst_fd, *offset, &length, NULL, 0);
}
```

### 1.3 Windows 降级处理

```c
// Windows: TransmitFile 未实现, 降级为普通 read/write
// zerocopy.c 中 Windows 部分使用 ReadFile + WriteFile 循环
// 未来可优化为 TransmitFile 零拷贝
```

## 2. 分块传输与断点续传

### 2.1 分块策略

```c
typedef struct {
    uint32_t    block_index;
    uint64_t    block_offset;
    uint32_t    block_size;
    uint8_t     md5[16];
    int         status;           // 0=待传输, 1=传输中, 2=完成, -1=失败
    int         retry_count;
} TransferBlock;

typedef struct {
    char            object_path[512];
    uint64_t        total_size;
    uint32_t        block_count;
    TransferBlock*  blocks;
    uint32_t        concurrent_max;
    uint64_t        bytes_transferred;
    uint64_t        speed_bytes_per_sec;
    uint64_t        started_at;
    int             error_code;
} TransferJob;

TransferJob* transfer_job_create(const char* path, uint64_t size);
TransferJob* transfer_job_resume(const char* resume_path);
```

### 2.2 重试机制

```c
#define CHUNKED_MAX_RETRIES    3       // 最大重试次数
#define CHUNKED_RETRY_DELAY_MS 500     // 重试间隔 (毫秒)

// 传输失败时自动重试, 最多 CHUNKED_MAX_RETRIES 次
// 每次重试前等待 CHUNKED_RETRY_DELAY_MS
// 重试仍失败则标记块为 -1 (失败)
```

### 2.3 断点续传元数据

```c
typedef struct {
    char        source_identifier[128];
    char        dest_path[512];
    uint64_t    total_size;
    uint32_t    block_size;
    uint32_t    total_blocks;
    uint32_t    completed_blocks;
    uint64_t    completed_bytes;
    uint8_t     completed_bitmap[];
} ResumeMeta;

int transfer_save_checkpoint(TransferJob* job);
int transfer_load_checkpoint(TransferJob* job);
```

### 2.4 并发传输控制

```c
typedef struct {
    TransferJob*    job;
    _Atomic int     running;           // 原子标志, 线程安全
    _Atomic int     cancelled;         // 原子取消标志
    _Atomic int     initialized;       // 原子初始化标志
    int             thread_count;
    pthread_t*      threads;
    pthread_mutex_t lock;
    pthread_cond_t  cond;
    int             next_block_index;
} TransferEngine;

int transfer_engine_init(TransferEngine* engine, TransferJob* job, int concurrent);
int transfer_engine_run(TransferEngine* engine, transfer_progress_cb cb, void* user_data);
void transfer_engine_cancel(TransferEngine* engine);
```

## 3. 速度测量与自适应

```c
typedef struct {
    uint64_t    samples[60];
    int         sample_count;
    double      avg_speed;
    double      instant_speed;
    double      peak_speed;
} SpeedMeter;

uint32_t transfer_adaptive_chunk_size(SpeedMeter* meter) {
    double speed = meter->avg_speed;
    if (speed > 50 * 1024 * 1024) return 4 * 1024 * 1024;
    if (speed > 10 * 1024 * 1024) return 1 * 1024 * 1024;
    if (speed > 1 * 1024 * 1024)  return 256 * 1024;
    return 64 * 1024;
}
```

## 4. 传输回调

```c
typedef void (*transfer_progress_cb)(TransferContext* ctx, int percent, void* user_data);
typedef void (*transfer_complete_cb)(TransferContext* ctx, int error_code, void* user_data);
```
