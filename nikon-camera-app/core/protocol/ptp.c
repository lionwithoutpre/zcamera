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
#include <arpa/inet.h>   /* htonl / ntohl (PTP/IP 封装) */

#ifdef PLATFORM_ANDROID
#include <poll.h>
#include <unistd.h>
#include <errno.h>
#endif

/* ─── 内部工具 ───────────────────────────────────────────────── */

static uint64_t _now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

/* 前向声明: PTP/IP 握手 (Wi-Fi 专用), 定义见下文 */
static int _ptpip_handshake(void);

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
    if (!session) return -1;
    /* Wi-Fi 会话 fd 为 -1 (socket 由 hal_wifi_* 全局管理), 仅 USB 校验 fd */
    if (session->transport == PTP_TRANSPORT_USB && session->fd < 0) return -1;
    session->state = SESSION_CONNECTING;

    /* Wi-Fi: 先做 PTP/IP 握手 (InitCommandRequest / InitEventRequest) */
    if (session->transport == PTP_TRANSPORT_WIFI) {
        int ip_rc = _ptpip_handshake();
        if (ip_rc < 0) {
            session->state = SESSION_ERROR;
            return ip_rc;
        }
    }

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

/* ─── Android: usbfs fd 直接 read/write 做 bulk 传输 ────────────
 *
 * Android 上 core 使用 usb_stub.c (hal_usb_bulk_transfer 恒为 UNSUPPORTED),
 * 但 Java 层通过 UsbDeviceConnection.getFileDescriptor() 注入的 usbfs fd
 * 可以直接用 read()/write() 完成 bulk IN/OUT (内核按已声明的接口路由端点,
 * 单次 read/write 上限 16KB, 需循环)。用 poll() 实现超时。
 * ──────────────────────────────────────────────────────────── */

#ifdef PLATFORM_ANDROID

static int _android_usb_bulk_read(int fd, uint8_t *buf, int len, int timeout_ms) {
    int got = 0;
    while (got < len) {
        struct pollfd pfd;
        pfd.fd      = fd;
        pfd.events  = POLLIN;
        pfd.revents = 0;
        int pr = poll(&pfd, 1, timeout_ms);
        if (pr < 0) return -HAL_USB_ERR_IO;
        if (pr == 0) return (got > 0) ? got : -HAL_USB_ERR_TIMEOUT;

        ssize_t n;
        do {
            n = read(fd, buf + got, (size_t)(len - got));
        } while (n < 0 && errno == EINTR);
        if (n < 0) return (got > 0) ? got : -HAL_USB_ERR_IO;
        if (n == 0) return (got > 0) ? got : -HAL_USB_ERR_IO;
        got += (int)n;
    }
    return got;
}

static int _android_usb_bulk_write(int fd, const uint8_t *buf, int len, int timeout_ms) {
    int sent = 0;
    while (sent < len) {
        struct pollfd pfd;
        pfd.fd      = fd;
        pfd.events  = POLLOUT;
        pfd.revents = 0;
        int pr = poll(&pfd, 1, timeout_ms);
        if (pr < 0) return -HAL_USB_ERR_IO;
        if (pr == 0) return (sent > 0) ? sent : -HAL_USB_ERR_TIMEOUT;

        ssize_t n = write(fd, buf + sent, (size_t)(len - sent));
        if (n < 0) {
            if (errno == EINTR) continue;
            return (sent > 0) ? sent : -HAL_USB_ERR_IO;
        }
        sent += (int)n;
    }
    return sent;
}

#endif /* PLATFORM_ANDROID */

/* ═══════════════════════════════════════════════════════════════
 *  PTP/IP (CIPA DC-005) 封装层 — 尼康 Wi-Fi (端口 15740)
 *
 * 尼康 Wi-Fi 传输的不是裸 PTP 容器, 而是带 4 字节包头 + 包类型 + 事务号
 * 的 PTP/IP 包; 连接前需先做 InitCommandRequest / InitEventRequest 握手。
 * 包结构: length(4) + type(4) + transaction(4) + payload
 * ══════════════════════════════════════════════════════════════ */

#define PTPIP_INIT_COMMAND_REQUEST   1u
#define PTPIP_INIT_COMMAND_ACK       2u
#define PTPIP_INIT_EVENT_REQUEST     3u
#define PTPIP_INIT_EVENT_ACK         4u
#define PTPIP_INIT_FAIL              5u
#define PTPIP_COMMAND_REQUEST        6u
#define PTPIP_COMMAND_ACK            7u
#define PTPIP_EVENT                  8u
#define PTPIP_START_DATA_PACKET      9u
#define PTPIP_DATA_PACKET            10u
#define PTPIP_CANCEL_TRANSACTION     11u
#define PTPIP_END_DATA_PACKET        12u

#define PTPIP_HDR_SIZE        12u                 /* length+type+transaction */
#define PTPIP_MAX_DATA_CHUNK  (64u * 1024u)

/* 单连接模式 (项目既有限制), 事务号与 GUID 用全局即可 */
static uint32_t s_ptpip_transaction = 0;

static const uint8_t s_ptpip_guid[16] = {
    0x37, 0x11, 0x0C, 0x1F, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/** 发送一个 PTP/IP 包。 */
static int _ptpip_send_packet(uint32_t type, uint32_t transaction,
                              const uint8_t *payload, uint32_t payload_len) {
    uint8_t hdr[PTPIP_HDR_SIZE];
    uint32_t n_len  = htonl(PTPIP_HDR_SIZE + payload_len);
    uint32_t n_type = htonl(type);
    uint32_t n_tid  = htonl(transaction);
    memcpy(hdr,      &n_len,  4);
    memcpy(hdr + 4,  &n_type, 4);
    memcpy(hdr + 8,  &n_tid,  4);
    if (hal_wifi_send(hdr, (int)sizeof(hdr), 3000) < 0) return -HAL_WIFI_ERR_IO;
    if (payload_len > 0 && payload) {
        if (hal_wifi_send(payload, (int)payload_len, 10000) < 0)
            return -HAL_WIFI_ERR_IO;
    }
    return 0;
}

/** 读取精确长度 (TCP 可能分片)。 */
static int _ptpip_read_exact(uint8_t *buf, int len, int timeout_ms) {
    int got = 0;
    while (got < len) {
        int n = hal_wifi_recv(buf + got, len - got, timeout_ms);
        if (n < 0) return n;
        if (n == 0) return -HAL_WIFI_ERR_DISCONNECTED;
        got += n;
    }
    return got;
}

/**
 * 接收一个 PTP/IP 包。
 * @return payload 字节数 (>=0); < 0 HAL 错误码
 */
static int _ptpip_recv_packet(uint32_t *type, uint32_t *transaction,
                              uint8_t *payload, int max_payload) {
    uint8_t hdr[PTPIP_HDR_SIZE];
    if (_ptpip_read_exact(hdr, (int)sizeof(hdr), 5000) < 0) return -HAL_WIFI_ERR_IO;

    uint32_t n_len, n_type, n_tid;
    memcpy(&n_len,  hdr,     4);
    memcpy(&n_type, hdr + 4, 4);
    memcpy(&n_tid,  hdr + 8, 4);
    uint32_t length = ntohl(n_len);
    if (length < PTPIP_HDR_SIZE ||
        length > (uint32_t)(PTPIP_HDR_SIZE + max_payload)) {
        return -HAL_WIFI_ERR_IO;
    }
    *type        = ntohl(n_type);
    *transaction = ntohl(n_tid);
    uint32_t payload_len = length - PTPIP_HDR_SIZE;
    if (payload_len > 0) {
        if (_ptpip_read_exact(payload, (int)payload_len, 10000) < 0)
            return -HAL_WIFI_ERR_IO;
    }
    return (int)payload_len;
}

/** PTP/IP 连接握手: InitCommandRequest → InitCommandAck → InitEventRequest → InitEventAck */
static int _ptpip_handshake(void) {
    uint8_t buf[1024];
    uint32_t type, tid;
    int n;

    /* 1) InitCommandRequest (GUID) */
    if (_ptpip_send_packet(PTPIP_INIT_COMMAND_REQUEST, 0,
                           s_ptpip_guid, sizeof(s_ptpip_guid)) < 0)
        return -HAL_WIFI_ERR_IO;

    /* 2) InitCommandAck (GUID + 机型名) / InitFail */
    n = _ptpip_recv_packet(&type, &tid, buf, (int)sizeof(buf));
    if (n < 0) return n;
    if (type == PTPIP_INIT_FAIL) return -HAL_WIFI_ERR_AUTH;
    if (type != PTPIP_INIT_COMMAND_ACK) return -HAL_WIFI_ERR_IO;

    /* 3) InitEventRequest (connection number + GUID) */
    uint8_t ev_payload[20];
    uint32_t conn_no = htonl(1);
    memcpy(ev_payload,      &conn_no, 4);
    memcpy(ev_payload + 4,  s_ptpip_guid, 16);
    if (_ptpip_send_packet(PTPIP_INIT_EVENT_REQUEST, 0,
                           ev_payload, sizeof(ev_payload)) < 0)
        return -HAL_WIFI_ERR_IO;

    /* 4) InitEventAck */
    n = _ptpip_recv_packet(&type, &tid, buf, (int)sizeof(buf));
    if (n < 0) return n;
    if (type != PTPIP_INIT_EVENT_ACK) return -HAL_WIFI_ERR_IO;
    return 0;
}

/** 发送 PTP 命令 (CommandRequest 封装)。 */
static int _ptpip_send_command(const uint8_t *cmd, uint32_t cmd_len) {
    s_ptpip_transaction++;
    return _ptpip_send_packet(PTPIP_COMMAND_REQUEST, s_ptpip_transaction,
                              cmd, cmd_len);
}

/** 接收 PTP 响应 (CommandAck 封装, 跳过推送的 Event 包)。 */
static int _ptpip_recv_response(uint8_t *resp, int max_len) {
    uint8_t tmp[1024];
    uint32_t type, tid;
    for (int i = 0; i < 64; i++) {
        int n = _ptpip_recv_packet(&type, &tid, tmp, (int)sizeof(tmp));
        if (n < 0) return n;
        if (type == PTPIP_EVENT) continue;          /* 跳过事件包 */
        if (type == PTPIP_COMMAND_ACK) {
            if (n > max_len) n = max_len;
            if (n > 0) memcpy(resp, tmp, (size_t)n);
            return n;
        }
        return -HAL_WIFI_ERR_IO;                    /* 未知包类型 */
    }
    return -HAL_WIFI_ERR_IO;
}

/** 发送 PTP 数据阶段 (StartDataPacket + DataPacket* + EndDataPacket)。 */
static int _ptpip_send_data(const uint8_t *data_header, uint32_t header_len,
                            const uint8_t *data, uint32_t data_len) {
    uint32_t tid = s_ptpip_transaction;             /* 与命令同事务号 */
    if (_ptpip_send_packet(PTPIP_START_DATA_PACKET, tid,
                           data_header, header_len) < 0)
        return -HAL_WIFI_ERR_IO;

    uint32_t sent = 0;
    while (sent < data_len) {
        uint32_t chunk = data_len - sent;
        if (chunk > PTPIP_MAX_DATA_CHUNK) chunk = PTPIP_MAX_DATA_CHUNK;
        if (_ptpip_send_packet(PTPIP_DATA_PACKET, tid,
                               data + sent, chunk) < 0)
            return -HAL_WIFI_ERR_IO;
        sent += chunk;
    }

    /* EndDataPacket: payload = 最后一个 DataPacket 的字节数 */
    uint32_t last_sz = htonl(((data_len - 1) % PTPIP_MAX_DATA_CHUNK) + 1);
    if (_ptpip_send_packet(PTPIP_END_DATA_PACKET, tid,
                           (const uint8_t *)&last_sz, 4) < 0)
        return -HAL_WIFI_ERR_IO;
    return 0;
}

/**
 * 接收 PTP 数据阶段 (StartDataPacket → DataPacket* → EndDataPacket)。
 * 返回 0; 数据写入 out_buf (最多 out_max), 实际长度写入 *out_len。
 */
static int _ptpip_recv_data(uint8_t *out_buf, uint32_t out_max, uint32_t *out_len) {
    uint8_t tmp[PTPIP_MAX_DATA_CHUNK + 32];
    uint32_t type, tid;
    int n;

    n = _ptpip_recv_packet(&type, &tid, tmp, (int)sizeof(tmp));
    if (n < 0) return n;

    if (type != PTPIP_START_DATA_PACKET) {
        return -HAL_WIFI_ERR_IO;    /* 无数据阶段或协议错乱 */
    }
    if (n < (int)sizeof(PtpDataPacketHeader)) return -HAL_WIFI_ERR_IO;

    PtpDataPacketHeader *dh = (PtpDataPacketHeader *)tmp;
    uint32_t total = dh->header.length - (uint32_t)sizeof(PtpDataPacketHeader);

    uint32_t got = 0;
    int ended = 0;
    while (!ended && got < total + 1) {
        n = _ptpip_recv_packet(&type, &tid, tmp, (int)sizeof(tmp));
        if (n < 0) return n;
        if (type == PTPIP_DATA_PACKET) {
            uint32_t chunk = (uint32_t)n;
            uint32_t store = chunk;
            if (got + store > out_max) store = out_max - got;
            if (store > 0 && got < out_max) {
                memcpy(out_buf + got, tmp, store);
            }
            got += chunk;
        } else if (type == PTPIP_END_DATA_PACKET) {
            ended = 1;
        } else if (type == PTPIP_EVENT) {
            continue;
        } else {
            return -HAL_WIFI_ERR_IO;
        }
    }
    if (out_len) *out_len = (got > out_max) ? out_max : got;
    return 0;
}

/* ─── USB 路径: 发包 / 收包 ────────────────────────────────── */

static int _usb_send(int fd, uint8_t ep, const uint8_t *buf, int len, int timeout) {
#ifdef PLATFORM_ANDROID
    (void)ep;
    int w = _android_usb_bulk_write(fd, buf, len, timeout);
    return (w < 0) ? w : 0;
#else
    int sent = hal_usb_bulk_transfer(fd, ep, (uint8_t *)buf, len, timeout);
    return (sent < 0) ? sent : 0;
#endif
}

static int _usb_recv(int fd, uint8_t ep, uint8_t *buf, int max_len, int timeout) {
#ifdef PLATFORM_ANDROID
    (void)ep;
    return _android_usb_bulk_read(fd, buf, max_len, timeout);
#else
    return hal_usb_bulk_transfer(fd, ep, buf, max_len, timeout);
#endif
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
        /* Wi-Fi: PTP/IP CommandRequest 封装 */
        rc = _ptpip_send_command((const uint8_t *)&cmd, cmd.header.length);
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
            /* Wi-Fi: PTP/IP StartDataPacket + DataPacket* + EndDataPacket */
            rc = _ptpip_send_data((const uint8_t *)&data_hdr,
                                  (uint32_t)sizeof(data_hdr),
                                  data_buf, data_len);
            if (rc < 0) return -(int)_map_wifi_error(rc);
        }
    }

    /* ── 4) 数据阶段: 接收 (out_buf 非空) ── */
    if (out_buf && out_max > 0) {
        if (is_usb) {
            PtpDataPacketHeader data_hdr;
            int data_rx = _usb_recv(fd, session->ep_in,
                                    (uint8_t *)&data_hdr, (int)sizeof(data_hdr), 5000);
            if (data_rx < 0) return -(int)_map_usb_error(data_rx);

            /* 解析数据长度 */
            uint32_t data_sz = data_hdr.header.length - (uint32_t)sizeof(PtpDataPacketHeader);
            if (data_sz > out_max) data_sz = out_max;

            data_rx = _usb_recv(fd, session->ep_in,
                                out_buf, (int)data_sz, 10000);
            if (data_rx < 0) return -(int)_map_usb_error(data_rx);
            if (out_len) *out_len = (uint32_t)data_rx;
        } else {
            /* Wi-Fi: PTP/IP StartDataPacket → DataPacket* → EndDataPacket */
            rc = _ptpip_recv_data(out_buf, out_max, out_len);
            if (rc < 0) return -(int)_map_wifi_error(rc);
        }
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
        /* Wi-Fi: PTP/IP CommandAck 封装 (自动跳过 Event 包) */
        resp_rx = _ptpip_recv_response((uint8_t *)&resp, (int)sizeof(resp));
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
