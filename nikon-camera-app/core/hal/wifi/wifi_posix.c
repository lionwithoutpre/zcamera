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
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>

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

/** DNS 名字解析 (支持压缩指针)。返回名字结束后的 offset; -1 出错 */
static int _dns_read_name(const uint8_t *buf, int len, int pos,
                          char *out, int out_cap) {
    int out_len = 0;
    int jump_pos = -1;
    int guard = 0;
    while (pos < len && guard++ < 128) {
        uint8_t seg = buf[pos];
        if ((seg & 0xC0) == 0xC0) {
            if (pos + 1 >= len) return -1;
            if (jump_pos < 0) jump_pos = pos + 2;
            pos = ((int)(seg & 0x3F) << 8) | (int)buf[pos + 1];
            continue;
        }
        pos++;
        if (seg == 0) break;
        if (seg > 63 || pos + seg > len) return -1;
        if (out_len > 0 && out_len < out_cap - 1) out[out_len++] = '.';
        for (int i = 0; i < seg && out_len < out_cap - 1; i++)
            out[out_len++] = (char)buf[pos + i];
        pos += seg;
    }
    if (out_len >= out_cap) out_len = out_cap - 1;
    out[out_len] = '\0';
    return (jump_pos >= 0) ? jump_pos : pos;
}

/** 从实例全名取实例短名 (第一个 '.' 之前)。 */
static void _instance_short_name(const char *full, char *out, int out_cap) {
    int i = 0;
    while (i < out_cap - 1 && full[i] != '\0' && full[i] != '.') {
        out[i] = full[i];
        i++;
    }
    out[i] = '\0';
}

/** SRV 记录: 实例全名 → 目标主机名 + 端口 */
typedef struct {
    char     inst[128];
    char     target[128];
    uint16_t port;
} MdnsSrv;

/** A 记录: 主机名 → IPv4 */
typedef struct {
    char host[128];
    char ip[16];
} MdnsAddr;

/**
 * 解析一个 mDNS 响应包: PTR(实例) → SRV(端口+目标) → A(真实 IPv4)。
 * 旧实现只读 PTR 并把地址填成 "mdns:NAME.local" (无法被 inet_pton/gethostbyname
 * 解析), 导致扫描出的设备永远连不上; 这里补全 SRV + A 记录解析。
 */
static int _parse_mdns_response(const uint8_t *buf, int len,
                                WifiConnectionInfo *results, int max_count) {
    if (len < 12) return 0;
    uint16_t qdcount = (uint16_t)((buf[4] << 8) | buf[5]);
    uint16_t ancount  = (uint16_t)((buf[6] << 8) | buf[7]);
    uint16_t nscount  = (uint16_t)((buf[8] << 8) | buf[9]);
    uint16_t arcount  = (uint16_t)((buf[10] << 8) | buf[11]);
    int total = (int)ancount + (int)nscount + (int)arcount;
    if (total <= 0) return 0;

    /* 跳过 question section */
    int pos = 12;
    for (int q = 0; q < qdcount; q++) {
        char tmp[128];
        pos = _dns_read_name(buf, len, pos, tmp, sizeof(tmp));
        if (pos < 0 || pos + 4 > len) return 0;
        pos += 4; /* QTYPE + QCLASS */
    }

    char     ptr_names[HAL_WIFI_MAX_DEVICES][128];
    int      ptr_count = 0;
    MdnsSrv  srvs[HAL_WIFI_MAX_DEVICES];
    int      srv_count = 0;
    MdnsAddr addrs[HAL_WIFI_MAX_DEVICES];
    int      addr_count = 0;

    for (int r = 0; r < total; r++) {
        char name[128];
        pos = _dns_read_name(buf, len, pos, name, sizeof(name));
        if (pos < 0 || pos + 10 > len) break;

        uint16_t rtype = (uint16_t)((buf[pos] << 8) | buf[pos + 1]); pos += 2;
        /* rclass */ pos += 2;
        /* ttl    */ pos += 4;
        uint16_t rdlen = (uint16_t)((buf[pos] << 8) | buf[pos + 1]); pos += 2;
        if (pos + rdlen > len) break;
        const uint8_t *rdata = buf + pos;
        pos += rdlen;

        switch (rtype) {
        case 0x0C: /* PTR: 服务类型 → 实例全名 */
            if (ptr_count < HAL_WIFI_MAX_DEVICES) {
                char target[128];
                int tpos = _dns_read_name(buf, len, (int)(rdata - buf),
                                          target, sizeof(target));
                if (tpos > 0) {
                    snprintf(ptr_names[ptr_count], sizeof(ptr_names[ptr_count]),
                             "%s", target);
                    ptr_count++;
                }
            }
            break;

        case 0x21: /* SRV: prio(2) weight(2) port(2) target(name) */
            if (rdlen >= 7 && srv_count < HAL_WIFI_MAX_DEVICES) {
                uint16_t port = (uint16_t)((rdata[4] << 8) | rdata[5]);
                char target[128];
                int tpos = _dns_read_name(buf, len, (int)(rdata - buf) + 6,
                                          target, sizeof(target));
                if (tpos > 0) {
                    snprintf(srvs[srv_count].inst, sizeof(srvs[srv_count].inst),
                             "%s", name);
                    snprintf(srvs[srv_count].target, sizeof(srvs[srv_count].target),
                             "%s", target);
                    srvs[srv_count].port = port;
                    srv_count++;
                }
            }
            break;

        case 0x01: /* A: IPv4 */
            if (rdlen == 4 && addr_count < HAL_WIFI_MAX_DEVICES) {
                snprintf(addrs[addr_count].ip, sizeof(addrs[addr_count].ip),
                         "%u.%u.%u.%u", rdata[0], rdata[1], rdata[2], rdata[3]);
                snprintf(addrs[addr_count].host, sizeof(addrs[addr_count].host),
                         "%s", name);
                addr_count++;
            }
            break;

        default:
            break;
        }
    }

    /* 组装: 实例名 → SRV 端口/目标 → A 地址 */
    int found = 0;
    for (int i = 0; i < ptr_count && found < max_count; i++) {
        const MdnsSrv *srv = NULL;
        for (int j = 0; j < srv_count; j++) {
            if (strcasecmp(srvs[j].inst, ptr_names[i]) == 0) {
                srv = &srvs[j];
                break;
            }
        }
        if (!srv) continue;

        char ip[16] = {0};
        for (int j = 0; j < addr_count; j++) {
            if (strcasecmp(addrs[j].host, srv->target) == 0) {
                snprintf(ip, sizeof(ip), "%s", addrs[j].ip);
                break;
            }
        }
        if (ip[0] == '\0') continue;

        snprintf(results[found].ip_address, sizeof(results[found].ip_address),
                 "%s", ip);
        results[found].port = srv->port ? srv->port : NIKON_WIFI_DEFAULT_PORT;
        _instance_short_name(ptr_names[i], results[found].ssid,
                             sizeof(results[found].ssid));
        found++;
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

    /* 接收超时设为 1s 一段, 总窗口内循环收集 (mDNS 常回复多个包) */
    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(mdns_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    int found = 0;
    uint8_t rbuf[2048];
    struct timespec t_start, t_now;
    clock_gettime(CLOCK_MONOTONIC, &t_start);

    while (found < max_count) {
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        ssize_t rlen = recvfrom(mdns_fd, rbuf, sizeof(rbuf), 0,
                                (struct sockaddr *)&from, &from_len);
        if (rlen > 0) {
            WifiConnectionInfo tmp[HAL_WIFI_MAX_DEVICES];
            int n = _parse_mdns_response(rbuf, (int)rlen, tmp, HAL_WIFI_MAX_DEVICES);
            for (int i = 0; i < n && found < max_count; i++) {
                int dup = 0;
                for (int j = 0; j < found; j++) {
                    if (strcmp(tmp[i].ip_address, results[j].ip_address) == 0) {
                        dup = 1;
                        break;
                    }
                }
                if (!dup) results[found++] = tmp[i];
            }
        }

        clock_gettime(CLOCK_MONOTONIC, &t_now);
        uint64_t elapsed_ms =
            (uint64_t)(t_now.tv_sec - t_start.tv_sec) * 1000 +
            (uint64_t)(t_now.tv_nsec - t_start.tv_nsec) / 1000000;
        if (elapsed_ms >= MDNS_SCAN_TIMEOUT_MS) break;
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
