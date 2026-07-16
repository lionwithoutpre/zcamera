/**
 * transfer/zerocopy.c — 平台特定零拷贝实现
 *
 * Linux   : splice() + sendfile()
 * macOS   : fcopyfile() / sendfile()
 * Windows : TransmitFile()
 */
#include "transfer/transfer.h"
#include <stdint.h>
#include <errno.h>

#if defined(__linux__)
#  include <sys/sendfile.h>
#  include <fcntl.h>
#  include <unistd.h>

int64_t transfer_zerocopy(int src_fd, int dest_fd, uint64_t offset, uint64_t length) {
    if (lseek(src_fd, (off_t)offset, SEEK_SET) < 0) return -errno;

    int64_t total = 0;
    while ((uint64_t)total < length) {
        size_t want = (size_t)(length - (uint64_t)total);
        if (want > (size_t)0x7ffff000) want = (size_t)0x7ffff000;
        ssize_t n = sendfile(dest_fd, src_fd, NULL, want);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -errno;
        }
        if (n == 0) break;
        total += n;
    }
    return total;
}

#elif defined(__APPLE__)
#  include <copyfile.h>
#  include <sys/types.h>
#  include <sys/socket.h>
#  include <unistd.h>

int64_t transfer_zerocopy(int src_fd, int dest_fd, uint64_t offset, uint64_t length) {
    /* macOS sendfile: int sendfile(int fd, int s, off_t offset, off_t *len, ...) */
    off_t len = (off_t)length;
    int rc = sendfile(src_fd, dest_fd, (off_t)offset, &len, NULL, 0);
    if (rc < 0 && errno != EAGAIN) return -errno;
    return (int64_t)len;
}

#elif defined(_WIN32)
#  include <windows.h>
#  include <winsock2.h>
#  include <mswsock.h>
#  include <io.h>

static bool _is_socket_handle(HANDLE h) {
    DWORD flags;
    return WSASend != NULL && GetFileType(h) == FILE_TYPE_PIPE;
}

int64_t transfer_zerocopy(int src_fd, int dest_fd, uint64_t offset, uint64_t length) {
    HANDLE h_src  = (HANDLE)_get_osfhandle(src_fd);
    HANDLE h_dest = (HANDLE)_get_osfhandle(dest_fd);
    if (h_src == INVALID_HANDLE_VALUE || h_dest == INVALID_HANDLE_VALUE)
        return -1;

    LARGE_INTEGER li;
    li.QuadPart = (LONGLONG)offset;
    if (!SetFilePointerEx(h_src, li, NULL, FILE_BEGIN)) return -1;

    if (_is_socket_handle(h_dest)) {
        SOCKET s = (SOCKET)h_dest;
        LPFN_TRANSMITFILE pTransmitFile = NULL;
        GUID guidTransmitFile = WSAID_TRANSMITFILE;
        DWORD dwBytes;

        SOCKET dummy = socket(AF_INET, SOCK_STREAM, 0);
        if (dummy != INVALID_SOCKET) {
            WSAIoctl(dummy, SIO_GET_EXTENSION_FUNCTION_POINTER,
                     &guidTransmitFile, sizeof(guidTransmitFile),
                     &pTransmitFile, sizeof(pTransmitFile),
                     &dwBytes, NULL, NULL);
            closesocket(dummy);
        }

        if (pTransmitFile) {
            uint64_t remaining = length;
            int64_t total = 0;
            while (remaining > 0) {
                DWORD chunk = (DWORD)(remaining > 0x80000000u
                                      ? 0x80000000u : remaining);
                OVERLAPPED ol = {0};
                ol.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
                if (!ol.hEvent) break;

                if (!pTransmitFile(s, h_src, chunk, 0, &ol, NULL, 0)) {
                    DWORD err = WSAGetLastError();
                    if (err != WSA_IO_PENDING) {
                        CloseHandle(ol.hEvent);
                        break;
                    }
                }
                DWORD transferred = 0;
                if (!GetOverlappedResult(s, &ol, &transferred, TRUE)) {
                    CloseHandle(ol.hEvent);
                    break;
                }
                CloseHandle(ol.hEvent);
                total += (int64_t)transferred;
                remaining -= transferred;
                if (transferred == 0) break;
            }
            return total;
        }
    }

    uint8_t buf[65536];
    int64_t total = 0;
    while ((uint64_t)total < length) {
        DWORD want = (DWORD)((length - (uint64_t)total) < sizeof(buf)
                             ? (length - (uint64_t)total)
                             : sizeof(buf));
        DWORD read_n = 0, write_n = 0;
        if (!ReadFile(h_src, buf, want, &read_n, NULL) || read_n == 0) break;
        if (!WriteFile(h_dest, buf, read_n, &write_n, NULL)) return -1;
        total += write_n;
    }
    return total;
}

#else
/* 通用 fallback */
#  include <unistd.h>

int64_t transfer_zerocopy(int src_fd, int dest_fd, uint64_t offset, uint64_t length) {
    if (lseek(src_fd, (off_t)offset, SEEK_SET) < 0) return -1;
    uint8_t buf[65536];
    int64_t total = 0;
    while ((uint64_t)total < length) {
        ssize_t want = (ssize_t)((length - (uint64_t)total) < sizeof(buf)
                                  ? (length - (uint64_t)total)
                                  : sizeof(buf));
        ssize_t n = read(src_fd, buf, (size_t)want);
        if (n <= 0) break;
        ssize_t w = write(dest_fd, buf, (size_t)n);
        if (w != n) return -1;
        total += w;
    }
    return total;
}
#endif
