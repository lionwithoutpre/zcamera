/**
 * transfer/ftp_client.c — 轻量 FTP 上传客户端 (POSIX socket, 无第三方依赖)
 *
 * 实现标准 FTP 主动登录 + PASV 被动模式 + STOR 上传:
 *   220 → USER → 331/230 → (PASS) → 230 → TYPE I → (CWD) → PASV → STOR → 226 → QUIT
 *
 * 平台:
 *   - Android (bionic) / Linux / macOS : 完整实现
 *   - Windows : 未实现 (返回 CAM_ERR_NOT_SUPPORTED, 与既有 FTP 占位一致)
 *
 * 安全说明: FTPS (use_tls) 需要 TLS 库 (OpenSSL 等), 本工程未链接 TLS 库,
 * 因此 use_tls=true 时降级为明文 FTP 并在日志中提示 (不因此直接失败)。
 */
#include "transfer/ftp_client.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
/* Windows 侧保持占位, 避免引入 winsock 依赖破坏构建 */
int ftp_client_upload(const FtpConfig *cfg, const char *local_path) {
    (void)cfg; (void)local_path;
    return CAM_ERR_NOT_SUPPORTED;
}
#else

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

/* ─── 内部工具 ────────────────────────────────────────────────── */

/** 建立到 host:port 的 TCP 连接, 失败返回 -1。 */
static int _tcp_connect(const char *host, uint16_t port) {
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", (unsigned)(port ? port : 21));

    if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res) return -1;

    int fd = -1;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

/** 读取一行 (以 '\n' 结尾), 去掉尾部 "\r\n", 返回行长; 失败返回 -1。 */
static int _read_line(int fd, char *buf, int cap) {
    if (cap <= 0) return -1;
    int n = 0;
    while (n < cap - 1) {
        char c;
        ssize_t r = recv(fd, &c, 1, 0);
        if (r <= 0) return -1;
        buf[n++] = c;
        if (c == '\n') break;
    }
    buf[n] = '\0';
    /* 去掉尾部 "\r\n" */
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = '\0';
    return n;
}

/** 发送单行命令并追加 CRLF。 */
static int _send_cmd(int fd, const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= sizeof(buf)) n = (int)sizeof(buf) - 1;
    buf[n++] = '\r';
    buf[n++] = '\n';
    ssize_t sent = 0;
    while (sent < n) {
        ssize_t r = send(fd, buf + sent, (size_t)(n - sent), 0);
        if (r <= 0) return -1;
        sent += r;
    }
    return (int)sent;
}

/** 解析 "227 Entering Passive Mode (h1,h2,h3,h4,p1,p2)" 并建立数据连接。 */
static int _open_passive_data(int ctrl_fd, char *reply_buf, int reply_cap) {
    char line[256];
    _send_cmd(ctrl_fd, "PASV");
    if (_read_line(ctrl_fd, line, sizeof(line)) < 0) return -1;

    char *l = strchr(line, '(');
    char *r = strchr(line, ')');
    if (!l || !r || r <= l) return -1;
    *r = '\0';

    int h1 = 0, h2 = 0, h3 = 0, h4 = 0, p1 = 0, p2 = 0;
    if (sscanf(l + 1, "%d,%d,%d,%d,%d,%d", &h1, &h2, &h3, &h4, &p1, &p2) != 6) {
        return -1;
    }

    char ip[64];
    snprintf(ip, sizeof(ip), "%d.%d.%d.%d", h1, h2, h3, h4);
    int port = p1 * 256 + p2;
    (void)reply_buf; (void)reply_cap;
    return _tcp_connect(ip, (uint16_t)port);
}

/** 从本地路径提取文件名 (最后一个 '/' 之后)。 */
static const char *_basename(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

/* ─── 上传实现 ────────────────────────────────────────────────── */

int ftp_client_upload(const FtpConfig *cfg, const char *local_path) {
    if (!cfg || !local_path) return CAM_ERR_INVALID_PARAM;
    if (cfg->host[0] == '\0') return CAM_ERR_INVALID_PARAM;

    if (cfg->use_tls) {
        fprintf(stderr, "[FTP] use_tls=true 但未链接 TLS 库, 降级为明文 FTP\n");
    }

    FILE *fp = fopen(local_path, "rb");
    if (!fp) return CAM_ERR_FILE_NOT_FOUND;

    char line[256];
    int rc = CAM_ERR_WIFI; /* 网络类错误默认值 */

    int ctrl = _tcp_connect(cfg->host, cfg->port);
    if (ctrl < 0) goto out_file;

    /* 220 欢迎 */
    if (_read_line(ctrl, line, sizeof(line)) < 0) goto out_ctrl;
    if (line[0] != '2') { rc = CAM_ERR_WIFI; goto out_ctrl; }

    /* USER */
    const char *user = cfg->username[0] ? cfg->username : "anonymous";
    _send_cmd(ctrl, "USER %s", user);
    if (_read_line(ctrl, line, sizeof(line)) < 0) goto out_ctrl;

    /* PASS (仅当 331 要求密码时) */
    if (line[0] == '3') {
        _send_cmd(ctrl, "PASS %s", cfg->password);
        if (_read_line(ctrl, line, sizeof(line)) < 0) goto out_ctrl;
    }
    if (line[0] != '2') { rc = CAM_ERR_PERMISSION_DENIED; goto out_ctrl; }

    /* TYPE I (二进制) */
    _send_cmd(ctrl, "TYPE I");
    if (_read_line(ctrl, line, sizeof(line)) < 0) goto out_ctrl;

    /* CWD 远端目录 (失败不阻断, 可能目录不存在或无需切换) */
    if (cfg->remote_path[0] != '\0') {
        _send_cmd(ctrl, "CWD %s", cfg->remote_path);
        _read_line(ctrl, line, sizeof(line));
    }

    /* PASV + 数据连接 */
    int data = _open_passive_data(ctrl, line, sizeof(line));
    if (data < 0) goto out_ctrl;

    /* STOR */
    _send_cmd(ctrl, "STOR %s", _basename(local_path));
    if (_read_line(ctrl, line, sizeof(line)) < 0) { close(data); goto out_ctrl; }

    /* 数据阶段: 流式发送文件 */
    char iobuf[64 * 1024];
    size_t nread;
    int send_ok = 1;
    while ((nread = fread(iobuf, 1, sizeof(iobuf), fp)) > 0) {
        size_t off = 0;
        while (off < nread) {
            ssize_t w = send(data, iobuf + off, nread - off, 0);
            if (w <= 0) { send_ok = 0; break; }
            off += (size_t)w;
        }
        if (!send_ok) break;
    }
    close(data);

    /* 226 完成 */
    if (_read_line(ctrl, line, sizeof(line)) < 0) goto out_ctrl;
    rc = send_ok ? CAM_OK : CAM_ERR_TRANSFER_FAILED;

    _send_cmd(ctrl, "QUIT");
out_ctrl:
    close(ctrl);
out_file:
    fclose(fp);
    return rc;
}

#endif /* !_WIN32 */
