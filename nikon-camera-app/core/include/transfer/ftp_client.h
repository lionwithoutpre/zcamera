/**
 * transfer/ftp_client.h — FTP 上传客户端接口
 */
#ifndef NIKON_TRANSFER_FTP_CLIENT_H
#define NIKON_TRANSFER_FTP_CLIENT_H

#include "api/camera_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 上传单个本地文件到 FTP 服务器 (阻塞, 同步)。
 *
 * @param cfg        服务器配置 (host/port/username/password/remote_path/use_tls)
 * @param local_path 本地文件绝对路径
 * @return CAM_OK; 或 CAM_ERR_* 错误码
 */
int ftp_client_upload(const FtpConfig *cfg, const char *local_path);

#ifdef __cplusplus
}
#endif
#endif /* NIKON_TRANSFER_FTP_CLIENT_H */
