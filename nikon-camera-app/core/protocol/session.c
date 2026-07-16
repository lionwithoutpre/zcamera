/**
 * protocol/session.c — PTP 会话管理与心跳保活
 */
#include "protocol/ptp.h"
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <pthread.h>
#include <unistd.h>

/* ─── 重连回调类型 ───────────────────────────────────────────── */

typedef void (*session_reconnect_cb)(PtpSession *session, void *user_data);

/* ─── 心跳线程 ───────────────────────────────────────────────── */

typedef struct {
    PtpSession          *session;
    _Atomic int          stop;
    session_reconnect_cb on_reconnect;
    void                *reconnect_user_data;
    int                  reconnect_delay_ms;
} HeartbeatCtx;

static void *_heartbeat_thread(void *arg) {
    HeartbeatCtx *ctx = (HeartbeatCtx *)arg;
    int consecutive_failures = 0;

    while (!atomic_load(&ctx->stop)) {
        usleep((unsigned int)(ctx->session->heartbeat_interval_ms * 1000));
        if (atomic_load(&ctx->stop)) break;
        if (ctx->session->state >= SESSION_OPEN) {
            int rc = ptp_session_heartbeat(ctx->session);
            if (rc != (int)PTP_RC_OK) {
                consecutive_failures++;
                ctx->session->state = SESSION_ERROR;

                if (consecutive_failures >= 3 && ctx->on_reconnect) {
                    ctx->on_reconnect(ctx->session, ctx->reconnect_user_data);
                }
            } else {
                consecutive_failures = 0;
            }
        }
    }
    return NULL;
}

/* ─── 带心跳保活的会话管理器 ────────────────────────────────── */

typedef struct {
    PtpSession      session;
    pthread_t       heartbeat_tid;
    HeartbeatCtx    hb_ctx;
} ManagedSession;

/**
 * 创建托管会话 (含心跳线程)。
 * @param fd        传输层 fd (USB or socket)
 * @param transport 传输类型 (PTP_TRANSPORT_USB / PTP_TRANSPORT_WIFI)
 * @param ep_out    USB Bulk OUT 端点 (WiFi 传 0)
 * @param ep_in     USB Bulk IN 端点 (WiFi 传 0)
 */
ManagedSession *managed_session_create(int fd, PtpTransport transport,
                                        uint8_t ep_out, uint8_t ep_in) {
    ManagedSession *ms = (ManagedSession *)calloc(1, sizeof(ManagedSession));
    if (!ms) return NULL;

    ptp_session_init(&ms->session, fd, transport, ep_out, ep_in);
    atomic_store(&ms->hb_ctx.stop, 0);
    ms->hb_ctx.session             = &ms->session;
    ms->hb_ctx.on_reconnect        = NULL;
    ms->hb_ctx.reconnect_user_data = NULL;
    ms->hb_ctx.reconnect_delay_ms  = 3000;
    return ms;
}

/** 设置心跳失败重连回调 (需在 open 前调用)。 */
void managed_session_set_reconnect_cb(ManagedSession *ms,
                                       session_reconnect_cb cb,
                                       void *user_data) {
    if (!ms) return;
    ms->hb_ctx.on_reconnect        = cb;
    ms->hb_ctx.reconnect_user_data = user_data;
}

/** 建立 PTP 会话并启动心跳线程。 */
int managed_session_open(ManagedSession *ms) {
    if (!ms) return -1;
    int rc = ptp_session_open(&ms->session);
    if (rc != 0) return rc;

    pthread_create(&ms->heartbeat_tid, NULL, _heartbeat_thread, &ms->hb_ctx);
    return 0;
}

/** 关闭会话并停止心跳线程。 */
void managed_session_close(ManagedSession *ms) {
    if (!ms) return;
    atomic_store(&ms->hb_ctx.stop, 1);
    pthread_join(ms->heartbeat_tid, NULL);
    ptp_session_close(&ms->session);
}

void managed_session_destroy(ManagedSession *ms) {
    if (!ms) return;
    managed_session_close(ms);
    free(ms);
}
