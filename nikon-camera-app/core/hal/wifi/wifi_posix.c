/**
 * hal/wifi/wifi_posix.c — POSIX Wi-Fi HAL 实现 (BSD Socket + mDNS)
 *
 * 适用: Linux / macOS / Android
 * mDNS 发现: 纯 UDP 多播, 零外部依赖
 */
#include "hal/wifi.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>

/* ─── 状态 ───────────────────────────────────────────────────── */

static int s_socket         = -1;
static int s_status         = WIFI_STATUS_DISCONNECTED;
static int s_rssi           = 0;

/* ─── mDNS 常量 ───────────────────────────────────────────────── */

#define MDNS_MULTICAST_ADDR  "224.0.0.251"
#define MDNS_PORT            5353
#define MDNS_SERVICE_TYPE    "_nikon-wtu._tcp.local"
#define MDNS_SCAN_TIMEOUT_MS 3000

/* ─── mDNS 查询构造 ──────────────────────────────────────────── */

static int _build_mdns_query(uint8_t *buf, int max_len) {
    /* DNS 头部: 12 bytes */
    if (max_len < 12) return -1;
    memset(buf, 0, 12);
    buf[0] = 0x00; buf[1] = 0x00;  /* Transaction ID */
    buf[2] = 0x00; buf[3] = 0x00;  /* Flags: standard query */
    buf[4] = 0x00; buf[5] = 0x01;  /* Questions: 1 */
    int pos = 12;

    /* 构造域名 "_nikon-wtu._tcp.local" */
    const char *name = MDNS_SERVICE_TYPE;
    while (*name && pos < max_len) {
        const char *dot = strchr(name, '.');
        if (!dot) dot = name + strlen(name);
        int seg_len = (int)(dot - name);
        if (seg_len > 63 || pos + 2 + seg_len > max_len) return -1;
        buf[pos++] = (uint8_t)seg_len;
        memcpy(buf + pos, name, (size_t)seg_len);
        pos += seg_len;
        name = (*dot == '.') ? dot + 1 : dot;
    }
    if (pos + 6 > max_len) return -1;
    buf[pos++] = 0x00;              /* null terminator */

    buf[pos++] = 0x00; buf[pos++] = 0x0C;  /* QTYPE = PTR */
    buf[pos++] = 0x00; buf[pos++] = 0x01;  /* QCLASS = IN */
    return pos;
}

/* ─── mDNS 响应解析 ──────────────────────────────────────────── */

static int _parse_mdns_response(const uint8_t *buf, int len,
                                WifiConnectionInfo *results, int max_count) {
    if (len < 12) return 0;
    int ancount = ((buf[6] << 8) | buf[7]);  /* Answer RRs */

    /* 跳过 12-byte 头 + question section */
    int pos = 12;
    while (pos < len && buf[pos] != 0) {
        int seg = buf[pos];
        pos += 1 + seg;
    }
    if (pos >= len) return 0;
    pos++;  /* null terminator */
    pos += 4; /* QTYPE + QCLASS */

    int found = 0;
    for (int a = 0; a < ancount && found < max_count && pos + 10 < len; a++) {
        /* 跳过 name (可能是压缩指针) */
        if ((buf[pos] & 0xC0) == 0xC0) {
            pos += 2;
        } else {
            while (pos < len && buf[pos] != 0) pos += 1 + buf[pos];
            pos++;
        }
        if (pos + 10 > len) break;

        uint16_t rtype  = (uint16_t)((buf[pos] << 8) | buf[pos + 1]);
        pos += 2;
        uint16_t rclass = (uint16_t)((buf[pos] << 8) | buf[pos + 1]);
        pos += 2;
        /* TTL */
        pos += 4;
        uint16_t rdlen  = (uint16_t)((buf[pos] << 8) | buf[pos + 1]);
        pos += 2;

        if (rtype == 0x0C && rclass == 0x0001 && rdlen > 0 && pos + rdlen <= len) {
            /* PTR record → instance name */
            int name_start = pos;
            /* 提取 instance 名称 (第一个 label) */
            int seg_len = buf[name_start];
            char name_buf[128] = {0};
            if (seg_len > 0 && seg_len < 63) {
                int nlen = (seg_len < (int)sizeof(name_buf) - 1) ? seg_len : (int)sizeof(name_buf) - 1;
                memcpy(name_buf, buf + name_start + 1, (size_t)nlen);
                snprintf(results[found].ip_address, sizeof(results[found].ip_address),
                         "mdns:%s.local", name_buf);
                results[found].port = NIKON_WIFI_DEFAULT_PORT;
                results[found].ssid[0] = '\0';
                found++;
            }
        }
        pos += rdlen;
    }
    return found;
}

/* ─── 接口 ───────────────────────────────────────────────────── */

int hal_wifi_init(void) {
    s_socket = -1;
    s_status = WIFI_STATUS_DISCONNECTED;
    return HAL_WIFI_OK;
}

int hal_wifi_scan(const char *filter_prefix,
                  WifiConnectionInfo *results, int max_count) {
    /* mDNS 发现 Nikon WTU 服务 */
    (void)filter_prefix;

    if (!results || max_count <= 0) return 0;

    int mdns_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (mdns_fd < 0) return 0;

    /* 允许端口复用 */
    int reuse = 1;
    setsockopt(mdns_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#ifdef SO_REUSEPORT
    setsockopt(mdns_fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
#endif

    /* 绑定本地端口 5353 */
    struct sockaddr_in local;
    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_port   = htons(MDNS_PORT);
    local.sin_addr.s_addr = INADDR_ANY;
    if (bind(mdns_fd, (struct sockaddr *)&local, sizeof(local)) < 0) {
        /* 端口可能被占用 (系统 mDNSResponder), 尝试不绑定 */
        close(mdns_fd);
        mdns_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (mdns_fd < 0) return 0;
    }

    /* 构造多播目标 */
    struct sockaddr_in mcast;
    memset(&mcast, 0, sizeof(mcast));
    mcast.sin_family = AF_INET;
    mcast.sin_port   = htons(MDNS_PORT);
    inet_pton(AF_INET, MDNS_MULTICAST_ADDR, &mcast.sin_addr);

    /* 加入多播组 */
    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = mcast.sin_addr.s_addr;
    mreq.imr_interface.s_addr = INADDR_ANY;
    setsockopt(mdns_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));

    /* 设置多播 TTL = 1 (局域网) */
    uint8_t ttl = 1;
    setsockopt(mdns_fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));

    /* 构造查询包 */
    uint8_t query[512];
    int qlen = _build_mdns_query(query, (int)sizeof(query));
    if (qlen <= 0) { close(mdns_fd); return 0; }

    /* 发送查询 */
    sendto(mdns_fd, query, (size_t)qlen, 0,
           (struct sockaddr *)&mcast, sizeof(mcast));

    /* 设置接收超时 */
    struct timeval tv = { .tv_sec = MDNS_SCAN_TIMEOUT_MS / 1000,
                          .tv_usec = (MDNS_SCAN_TIMEOUT_MS % 1000) * 1000 };
    setsockopt(mdns_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    /* 收集响应 */
    int found = 0;
    uint8_t rbuf[1500];
    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);
    ssize_t rlen = recvfrom(mdns_fd, rbuf, sizeof(rbuf), 0,
                            (struct sockaddr *)&from, &from_len);
    if (rlen > 0) {
        found = _parse_mdns_response(rbuf, (int)rlen, results, max_count);
    }

    close(mdns_fd);
    return found;
}

int hal_wifi_connect(WifiConnectionInfo *info) {
    if (!info) return HAL_WIFI_ERR_NOT_FOUND;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(info->port ? info->port : NIKON_WIFI_DEFAULT_PORT);

    if (inet_pton(AF_INET, info->ip_address, &addr.sin_addr) <= 0) {
        /* 尝试 DNS 解析 */
        struct hostent *h = gethostbyname(info->ip_address);
        if (!h) return HAL_WIFI_ERR_NOT_FOUND;
        memcpy(&addr.sin_addr, h->h_addr_list[0], (size_t)h->h_length);
    }

    s_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (s_socket < 0) return HAL_WIFI_ERR_IO;

    /* 设置超时 5s */
    struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
    setsockopt(s_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s_socket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    s_status = WIFI_STATUS_CONNECTING;
    if (connect(s_socket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(s_socket);
        s_socket = -1;
        s_status = WIFI_STATUS_DISCONNECTED;
        return (errno == ETIMEDOUT) ? HAL_WIFI_ERR_TIMEOUT : HAL_WIFI_ERR_IO;
    }

    s_status = WIFI_STATUS_CONNECTED;
    return HAL_WIFI_OK;
}

void hal_wifi_disconnect(void) {
    if (s_socket >= 0) {
        shutdown(s_socket, SHUT_RDWR);
        close(s_socket);
        s_socket = -1;
    }
    s_status = WIFI_STATUS_DISCONNECTED;
}

int hal_wifi_get_status(void) {
    return s_status;
}

int hal_wifi_send(const uint8_t *data, int length, int timeout_ms) {
    if (s_socket < 0 || !data || length <= 0) return HAL_WIFI_ERR_IO;

    struct timeval tv;
    tv.tv_sec  =  timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(s_socket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    int total = 0;
    while (total < length) {
        ssize_t n = send(s_socket, data + total, (size_t)(length - total), 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return (errno == ETIMEDOUT) ? HAL_WIFI_ERR_TIMEOUT : HAL_WIFI_ERR_IO;
        }
        total += (int)n;
    }
    return total;
}

int hal_wifi_recv(uint8_t *buf, int max_length, int timeout_ms) {
    if (s_socket < 0 || !buf || max_length <= 0) return HAL_WIFI_ERR_IO;

    struct timeval tv;
    tv.tv_sec  =  timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(s_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ssize_t n = recv(s_socket, buf, (size_t)max_length, 0);
    if (n < 0) {
        if (errno == ETIMEDOUT || errno == EAGAIN) return HAL_WIFI_ERR_TIMEOUT;
        return HAL_WIFI_ERR_IO;
    }
    return (int)n;
}

int hal_wifi_get_rssi(void) {
    return s_rssi;
}

void hal_wifi_shutdown(void) {
    hal_wifi_disconnect();
}
