/**
 * hal/wifi/wifi_pw_obfuscate.c — WiFi 密码简单混淆
 *
 * 非加密, 仅防止明文存储。使用 XOR + 位移混淆。
 * 安全性依赖系统权限 (文件不可全局可读)。
 */
#include "hal/wifi.h"
#include <string.h>

static const uint8_t kXorKey[] = {0x5A, 0xC3, 0x7E, 0x91, 0x2D, 0xF4, 0x08, 0xB6};
#define KEY_LEN 8

void hal_wifi_encode_pw(const char *plain, char *obfuscated, int buf_len) {
    if (!plain || !obfuscated || buf_len <= 0) return;
    int plen = (int)strlen(plain);
    int max = buf_len - 1;
    if (plen > max) plen = max;

    for (int i = 0; i < plen; i++) {
        uint8_t b = (uint8_t)plain[i];
        b ^= kXorKey[i % KEY_LEN];
        b = (uint8_t)((b << 3) | (b >> 5));
        obfuscated[i] = (char)b;
    }
    obfuscated[plen] = '\0';
}

void hal_wifi_decode_pw(const char *obfuscated, char *plain, int buf_len) {
    if (!obfuscated || !plain || buf_len <= 0) return;
    int olen = (int)strlen(obfuscated);
    int max = buf_len - 1;
    if (olen > max) olen = max;

    for (int i = 0; i < olen; i++) {
        uint8_t b = (uint8_t)obfuscated[i];
        b = (uint8_t)((b >> 3) | (b << 5));
        b ^= kXorKey[i % KEY_LEN];
        plain[i] = (char)b;
    }
    plain[olen] = '\0';
}
