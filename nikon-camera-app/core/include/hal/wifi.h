/**
 * hal/wifi.h — Wi-Fi 硬件抽象层接口
 *
 * 尼康 SnapBridge Wi-Fi 连接管理。
 * 平台实现:
 *   Android : hal/wifi/wifi_android.c (android.net.wifi)
 *   POSIX   : hal/wifi/wifi_posix.c   (BSD socket)
 *   Windows : hal/wifi/wifi_win.c     (WinHTTP / Winsock2)
 */
#ifndef NIKON_HAL_WIFI_H
#define NIKON_HAL_WIFI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ─── 常量 ───────────────────────────────────────────────────── */

#define NIKON_WIFI_DEFAULT_PORT     15740   /**< 尼康 PTP/IP 服务端口 */
#define NIKON_WIFI_SSID_PREFIX      "Nikon" /**< SnapBridge SSID 前缀 */
#define HAL_WIFI_MAX_DEVICES        16

/* 安全类型 */
#define WIFI_SEC_OPEN   0
#define WIFI_SEC_WPA2   1

/* 连接状态 */
#define WIFI_STATUS_DISCONNECTED    0
#define WIFI_STATUS_CONNECTING      1
#define WIFI_STATUS_CONNECTED       2

/* 错误码 */
#define HAL_WIFI_OK                 0
#define HAL_WIFI_ERR_NOT_FOUND      (-1)
#define HAL_WIFI_ERR_AUTH           (-2)
#define HAL_WIFI_ERR_TIMEOUT        (-3)
#define HAL_WIFI_ERR_IO             (-4)
#define HAL_WIFI_ERR_DISCONNECTED   (-5)

/* ─── 数据结构 ───────────────────────────────────────────────── */

/** Wi-Fi 相机连接信息 */
typedef struct {
    char        ssid[64];       /**< SSID (SnapBridge 热点名) */
    char        password[64];   /**< 密码 (混淆存储, 使用时由 _wifi_decode_pw 解码) */
    char        ip_address[16]; /**< 相机 IP 地址 (IPv4) */
    uint16_t    port;           /**< PTP/IP 端口 (默认 15740) */
    uint8_t     security_type;  /**< WIFI_SEC_* */
    int         rssi;           /**< 信号强度 dBm */
} WifiConnectionInfo;

void hal_wifi_encode_pw(const char *plain, char *obfuscated, int buf_len);
void hal_wifi_decode_pw(const char *obfuscated, char *plain, int buf_len);

/* ─── 接口 ───────────────────────────────────────────────────── */

/** 初始化 Wi-Fi 子系统。 */
int hal_wifi_init(void);

/**
 * 扫描尼康相机 Wi-Fi 热点。
 *
 * @param filter_prefix SSID 前缀过滤 (NULL=全部)
 * @param results       输出缓冲区
 * @param max_count     最大结果数
 * @return >= 0 找到的热点数量; < 0 错误码
 */
int hal_wifi_scan(const char *filter_prefix,
                  WifiConnectionInfo *results, int max_count);

/**
 * 建立 TCP 连接到相机 PTP/IP 服务。
 * 成功后调用 hal_wifi_send/recv 进行通信。
 *
 * @return HAL_WIFI_OK; < 0 错误码
 */
int hal_wifi_connect(WifiConnectionInfo *info);

/** 断开 TCP 连接并释放 socket。 */
void hal_wifi_disconnect(void);

/** 获取当前连接状态 (WIFI_STATUS_*)。 */
int hal_wifi_get_status(void);

/**
 * 发送数据 (阻塞直到全部发出或超时)。
 *
 * @return 实际发送字节数; < 0 错误码
 */
int hal_wifi_send(const uint8_t *data, int length, int timeout_ms);

/**
 * 接收数据。
 *
 * @return 实际接收字节数; 0=连接关闭; < 0 错误码
 */
int hal_wifi_recv(uint8_t *buf, int max_length, int timeout_ms);

/** 获取当前连接的信号强度 (dBm)。 */
int hal_wifi_get_rssi(void);

/** 释放 Wi-Fi 子系统。 */
void hal_wifi_shutdown(void);

#ifdef __cplusplus
}
#endif
#endif /* NIKON_HAL_WIFI_H */
