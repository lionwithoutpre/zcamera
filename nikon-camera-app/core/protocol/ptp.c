/**
 * protocol/ptp.c — PTP/MTP 协议引擎实现
 *
 * 涵盖: 会话管理、数据包收发、事件轮询、属性读写。
 */
#include "protocol/ptp.h"
#include "hal/usb.h"
#include "hal/wifi.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/* ─── 内部工具 ───────────────────────────────────────────────── */

static uint64_t _now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

/* ─── 会话接口 ───────────────────────────────────────────────── */

int ptp_session_init(PtpSession *session, int fd,
                      PtpTransport transport, uint8_t ep_out, uint8_t ep_in) {
    if (!session) return -1;
    memset(session, 0, sizeof(*session));
    session->fd                   = fd;
    session->transport            = transport;
    session->ep_out               = ep_out;
    session->ep_in                = ep_in;
    session->state                = SESSION_DISCONNECTED;
    session->transaction_id       = 1;
    session->heartbeat_interval_ms = 5000;
    return 0;
}

int ptp_session_open(PtpSession *session) {
    if (!session || session->fd < 0) return -1;
    session->state = SESSION_CONNECTING;

    /* Step 1: OpenSession */
    uint32_t params[5] = { 0x0001, 0, 0, 0, 0 };   /* Session ID = 1 */
    uint32_t out_len = 0;
    int rc = ptp_exec(session,
                      (uint16_t)PTP_OC_OpenSession,
                      params, 1,
                      NULL, 0,
                      NULL, 0, &out_len);
    if (rc != PTP_RC_OK) {
        session->state = SESSION_ERROR;
        return rc;
    }
    session->session_id = params[0];

    /* Step 2: GetDeviceInfo (验证连接) */
    uint8_t  dev_info_buf[512];
    out_len = 0;
    rc = ptp_exec(session,
                  (uint16_t)PTP_OC_GetDeviceInfo,
                  NULL, 0,
                  NULL, 0,
                  dev_info_buf, sizeof(dev_info_buf), &out_len);
    if (rc != PTP_RC_OK) {
        session->state = SESSION_ERROR;
        return rc;
    }

    session->state              = SESSION_OPEN;
    session->last_heartbeat_ms  = _now_ms();
    return 0;
}

int ptp_session_heartbeat(PtpSession *session) {
    if (!session || session->state < SESSION_OPEN) return -1;

    /* 使用 GetDevicePropDesc 作为心跳探针 (不修改任何状态) */
    uint32_t params[5] = { 0xD00C, 0, 0, 0, 0 };   /* ShutterSpeed */
    uint8_t  buf[64];
    uint32_t out_len = 0;
    int rc = ptp_exec(session,
                      (uint16_t)PTP_OC_GetDevicePropDesc,
                      params, 1,
                      NULL, 0,
                      buf, sizeof(buf), &out_len);
    if (rc == PTP_RC_OK) {
        session->last_heartbeat_ms = _now_ms();
    }
    return rc;
}

int ptp_session_close(PtpSession *session) {
    if (!session || session->state == SESSION_DISCONNECTED) return 0;
    session->state = SESSION_CLOSING;

    uint32_t out_len = 0;
    ptp_exec(session,
             (uint16_t)PTP_OC_CloseSession,
             NULL, 0, NULL, 0, NULL, 0, &out_len);

    session->state      = SESSION_DISCONNECTED;
    session->session_id = 0;
    return 0;
}

uint32_t ptp_session_next_transaction(PtpSession *session) {
    return session->transaction_id++;
}

/* ─── 底层 PTP exec ──────────────────────────────────────────── */

/* ─── HAL 错误码 → PTP 响应码映射 ─────────────────────────── */

static uint16_t _map_usb_error(int hal_err) {
    if (hal_err >= 0) return PTP_RC_OK;
    switch (hal_err) {
    case HAL_USB_ERR_TIMEOUT:    return PTP_RC_DeviceBusy;
    case HAL_USB_ERR_IO:         return PTP_RC_GeneralError;
    case HAL_USB_ERR_NOT_FOUND:  return PTP_RC_GeneralError;
    default:                     return PTP_RC_GeneralError;
    }
}

static uint16_t _map_wifi_error(int hal_err) {
    if (hal_err >= 0) return PTP_RC_OK;
    switch (hal_err) {
    case HAL_WIFI_ERR_TIMEOUT:    return PTP_RC_DeviceBusy;
    case HAL_WIFI_ERR_IO:         return PTP_RC_GeneralError;
    case HAL_WIFI_ERR_DISCONNECTED: return PTP_RC_SessionNotOpen;
    default:                       return PTP_RC_GeneralError;
    }
}

/* ─── USB 路径: 发包 / 收包 ────────────────────────────────── */

static int _usb_send(int fd, uint8_t ep, const uint8_t *buf, int len, int timeout) {
    int sent = hal_usb_bulk_transfer(fd, ep, (uint8_t *)buf, len, timeout);
    return (sent < 0) ? sent : 0;
}

static int _usb_recv(int fd, uint8_t ep, uint8_t *buf, int max_len, int timeout) {
    return hal_usb_bulk_transfer(fd, ep, buf, max_len, timeout);
}

/* ℹ──────────────────────────────────────────────────────────────
 *  ptp_exec() — PTP 事务执行 (支持 USB / WiFi 双通道)
 *
 *  PTP 事务流:
 *   1. 发送命令包 (Command PDU)
 *   2. 可选数据阶段:
 *        data_buf != NULL → Data OUT → 发送数据包
 *        out_buf  != NULL → Data IN  → 接收数据包
 *   3. 接收响应包 (Response PDU)
 * ───────────────────────────────────────────────────────────── */
int ptp_exec(PtpSession *session,
             uint16_t opcode,
             const uint32_t *params, int param_count,
             const uint8_t *data_buf, uint32_t data_len,
             uint8_t *out_buf, uint32_t out_max, uint32_t *out_len)
{
    if (!session) return -(int)PTP_RC_GeneralError;
    if (out_len) *out_len = 0;

    int     fd  = session->fd;
    int     is_usb = (session->transport == PTP_TRANSPORT_USB);
    int     is_wifi = (session->transport == PTP_TRANSPORT_WIFI);

    if (!is_usb && !is_wifi) return -(int)PTP_RC_GeneralError;

    uint32_t tid = ptp_session_next_transaction(session);

    /* ── 1) 构造命令包 ── */
    PtpCommandPacket cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.header.packet_type    = PTP_PKT_TYPE_COMMAND;
    cmd.header.code           = opcode;
    cmd.header.transaction_id = tid;
    cmd.operation_code        = opcode;
    cmd.session_id            = (uint16_t)session->session_id;

    int n = param_count < 5 ? param_count : 5;
    for (int i = 0; i < n; i++) cmd.params[i] = params ? params[i] : 0;

    /* header(12) + operation_code(2) + session_id(2) + params(n*4) */
    cmd.header.length = (uint32_t)(sizeof(PtpContainerHeader) + 2 + 2 + n * 4);

    /* ── 2) 发送命令包 ── */
    int rc;
    if (is_usb) {
        rc = _usb_send(fd, session->ep_out,
                       (const uint8_t *)&cmd, (int)cmd.header.length, 3000);
        if (rc < 0) return -(int)_map_usb_error(rc);
    } else {
        rc = hal_wifi_send((const uint8_t *)&cmd, (int)cmd.header.length, 3000);
        if (rc < 0) return -(int)_map_wifi_error(rc);
    }

    /* ── 3) 数据阶段: 发送 (data_buf 非空) ── */
    if (data_buf && data_len > 0) {
        PtpDataPacketHeader data_hdr;
        memset(&data_hdr, 0, sizeof(data_hdr));
        data_hdr.header.length         = (uint32_t)(sizeof(PtpDataPacketHeader) + data_len);
        data_hdr.header.packet_type     = PTP_PKT_TYPE_DATA;
        data_hdr.header.code            = opcode;
        data_hdr.header.transaction_id  = tid;

        if (is_usb) {
            /* 头部 */
            rc = _usb_send(fd, session->ep_out,
                           (const uint8_t *)&data_hdr, (int)sizeof(data_hdr), 3000);
            if (rc < 0) return -(int)_map_usb_error(rc);
            /* 数据 */
            rc = _usb_send(fd, session->ep_out,
                           (const uint8_t *)data_buf, (int)data_len, 10000);
            if (rc < 0) return -(int)_map_usb_error(rc);
        } else {
            /* WiFi: header + payload 合并一次发送 (≤64KB) 或分两次 */
            uint32_t total = (uint32_t)sizeof(PtpDataPacketHeader) + data_len;
            #define WIFI_COPY_BUF  65536u
            if (total <= WIFI_COPY_BUF) {
                uint8_t *sbuf = (uint8_t *)malloc(total);
                if (!sbuf) return -(int)PTP_RC_GeneralError;
                memcpy(sbuf, &data_hdr, sizeof(data_hdr));
                memcpy(sbuf + sizeof(data_hdr), data_buf, data_len);
                rc = hal_wifi_send(sbuf, (int)total, 10000);
                free(sbuf);
                if (rc < 0) return -(int)_map_wifi_error(rc);
            } else {
                rc = hal_wifi_send((const uint8_t *)&data_hdr, (int)sizeof(data_hdr), 3000);
                if (rc >= 0)
                    rc = hal_wifi_send((const uint8_t *)data_buf, (int)data_len, 10000);
                if (rc < 0) return -(int)_map_wifi_error(rc);
            }
        }
    }

    /* ── 4) 数据阶段: 接收 (out_buf 非空) ── */
    if (out_buf && out_max > 0) {
        PtpDataPacketHeader data_hdr;
        int data_rx = 0;

        if (is_usb) {
            data_rx = _usb_recv(fd, session->ep_in,
                                (uint8_t *)&data_hdr, (int)sizeof(data_hdr), 5000);
            if (data_rx < 0) return -(int)_map_usb_error(data_rx);
        } else {
            data_rx = hal_wifi_recv((uint8_t *)&data_hdr, (int)sizeof(data_hdr), 5000);
            if (data_rx < 0) return -(int)_map_wifi_error(data_rx);
        }

        /* 解析数据长度 */
        uint32_t data_sz = data_hdr.header.length - (uint32_t)sizeof(PtpDataPacketHeader);
        if (data_sz > out_max) data_sz = out_max;

        if (is_usb) {
            data_rx = _usb_recv(fd, session->ep_in,
                                out_buf, (int)data_sz, 10000);
            if (data_rx < 0) return -(int)_map_usb_error(data_rx);
        } else {
            data_rx = hal_wifi_recv(out_buf, (int)data_sz, 10000);
            if (data_rx < 0) return -(int)_map_wifi_error(data_rx);
        }
        if (out_len) *out_len = (uint32_t)data_rx;
    }

    /* ── 5) 接收响应包 ── */
    PtpResponsePacket resp;
    memset(&resp, 0, sizeof(resp));
    int resp_rx;

    if (is_usb) {
        resp_rx = _usb_recv(fd, session->ep_in,
                            (uint8_t *)&resp, (int)sizeof(resp), 3000);
        if (resp_rx < 0) return -(int)_map_usb_error(resp_rx);
    } else {
        resp_rx = hal_wifi_recv((uint8_t *)&resp, (int)sizeof(resp), 3000);
        if (resp_rx < 0) return -(int)_map_wifi_error(resp_rx);
    }

    /* 检查响应包 */
    if (resp_rx < (int)sizeof(PtpContainerHeader) + 2) {
        return -(int)PTP_RC_GeneralError;
    }

    return (int)(uint32_t)resp.response_code;
}

/* ─── 事件轮询 ───────────────────────────────────────────────── */

int ptp_get_events(PtpSession *session, uint32_t *events, int max_events) {
    if (!session || !events || max_events <= 0) return -1;

    uint8_t  buf[256];
    uint32_t out_len = 0;
    int rc = ptp_exec(session,
                      (uint16_t)NIKON_OC_GetEvent,
                      NULL, 0,
                      NULL, 0,
                      buf, sizeof(buf), &out_len);
    if (rc != PTP_RC_OK || out_len < 2) return 0;

    /* 解析尼康事件包: [uint16_t count] [uint16_t event_code] ... */
    uint16_t count = *(uint16_t *)buf;
    if (count > (uint16_t)max_events) count = (uint16_t)max_events;
    for (int i = 0; i < count; i++) {
        events[i] = ((uint16_t *)buf)[1 + i];
    }
    return (int)count;
}

/* ─── 属性读写 ───────────────────────────────────────────────── */

int ptp_get_device_prop(PtpSession *session, uint16_t prop_id, uint32_t *value) {
    if (!session || !value) return -1;
    uint32_t params[5] = { prop_id, 0, 0, 0, 0 };
    uint8_t  buf[64];
    uint32_t out_len = 0;
    int rc = ptp_exec(session,
                      (uint16_t)PTP_OC_GetDevicePropValue,
                      params, 1,
                      NULL, 0,
                      buf, sizeof(buf), &out_len);
    if (rc == PTP_RC_OK && out_len >= 4) {
        *value = *(uint32_t *)buf;
    }
    return rc;
}

int ptp_set_device_prop(PtpSession *session, uint16_t prop_id, uint32_t value) {
    if (!session) return -1;
    uint32_t params[5] = { prop_id, 0, 0, 0, 0 };
    uint8_t  data[4];
    *(uint32_t *)data = value;
    uint32_t out_len = 0;
    return ptp_exec(session,
                    (uint16_t)PTP_OC_SetDevicePropValue,
                    params, 1,
                    data, sizeof(data),
                    NULL, 0, &out_len);
}
