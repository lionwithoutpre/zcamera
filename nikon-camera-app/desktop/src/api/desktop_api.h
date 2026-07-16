/**
 * desktop/src/api/desktop_api.h — C++ Qt 友好的 CameraAPI 封装
 *
 * 将 C 的 camera_api.h 事件回调转换为 Qt Signal，
 * 所有 CameraAPI 调用在专用工作线程执行，GUI 线程永远不会阻塞。
 */
#ifndef NIKON_DESKTOP_API_H
#define NIKON_DESKTOP_API_H

#include <QObject>
#include <QThread>
#include <QString>
#include <QList>
#include <QByteArray>
#include <QMutex>
#include <QHash>
#include <functional>
#include <atomic>

extern "C" {
#include "api/camera_api.h"
}

/* ─── C++ 友好的数据结构 ─────────────────────────────────────── */

struct CamDevice {
    QString  id;
    QString  model;
    QString  serial;
    int      transport;      // 0=USB, 1=WiFi
    int      battery;
    quint64  storageFree;
    quint64  storageTotal;
    quint32  shutterCount;
};

struct CamFile {
    quint32  handle;
    QString  filename;
    quint64  size;
    QString  datetime;
    bool     isRaw;
    bool     isJpeg;
    int      width;
    int      height;
    quint32  storageId;
};

struct CamTransferProgress {
    int      jobId;
    quint32  handle;
    QString  filename;
    quint64  totalSize;
    quint64  transferred;
    int      percent;
    double   speedMbps;
    quint64  elapsedMs;
    quint64  remainingMs;
    int      status;
    int      errorCode;
};

struct CamPictureControl {
    int  hue;
    int  saturation;
    int  contrast;
    int  clarity;
    int  sharpening;
    int  brightness;
    int  wbAb;
    int  wbGm;
    int  colorSpace;
};

/* ─── 传输任务(v2,本地维护的任务视图)─────────────────────── */
enum CamTransferStatus {
    TS_WAITING = 0,
    TS_ACTIVE  = 1,
    TS_PAUSED  = 2,
    TS_DONE    = 3,
    TS_FAILED  = -1,
    TS_CANCELLED = -2,
};

struct CamTransferJob {
    int      jobId;
    quint32  handle;
    QString  filename;
    QString  destPath;
    quint64  totalSize;
    quint64  transferred;
    int      percent;
    double   speedMbps;
    QString  note;
    int      status;  // CamTransferStatus
};

/* ─── DesktopAPI 类 ──────────────────────────────────────────── */

class DesktopAPI : public QObject {
    Q_OBJECT

public:
    explicit DesktopAPI(int transport = TRANSPORT_AUTO, QObject *parent = nullptr);
    ~DesktopAPI() override;

    /* 生命周期 */
    Q_INVOKABLE void scan();
    Q_INVOKABLE void connectTo(const QString &cameraId);
    Q_INVOKABLE void disconnect();
    Q_INVOKABLE void shutdown();

    /* 拍摄 */
    Q_INVOKABLE void capture();
    Q_INVOKABLE void burst(int count, int intervalMs);
    Q_INVOKABLE void stopBurst();
    Q_INVOKABLE void autoFocus();

    /* 取景 */
    Q_INVOKABLE void startLiveView();
    Q_INVOKABLE void stopLiveView();

    /* 属性 */
    Q_INVOKABLE void setProperty(quint16 propId, quint32 value);
    Q_INVOKABLE void getProperty(quint16 propId);
    Q_INVOKABLE void setIso(int iso);
    Q_INVOKABLE void setShutter(int speedVal);
    Q_INVOKABLE void setAperture(int apertureVal);
    Q_INVOKABLE void setWhiteBalance(int wbVal);

    /* Picture Control */
    Q_INVOKABLE void getPictureControl();
    Q_INVOKABLE void setPictureControl(const CamPictureControl &ctrl);

    /* 文件 */
    Q_INVOKABLE void listFiles(quint32 storageId = 0);
    Q_INVOKABLE void getThumbnail(quint32 handle);
    Q_INVOKABLE void deleteFile(quint32 handle);

    /* 传输 */
    Q_INVOKABLE void startTransfer(quint32 handle, const QString &destPath);
    Q_INVOKABLE void startBatchTransfer(const QList<quint32> &handles, const QString &destDir);
    Q_INVOKABLE void cancelTransfer(int jobId);
    /* v2:传输任务管理 */
    Q_INVOKABLE void pauseAllTransfers();
    Q_INVOKABLE void cancelAllTransfers();
    Q_INVOKABLE void resumeTransfer(int jobId);
    Q_INVOKABLE void retryTransfer(int jobId);
    QList<CamTransferJob> transferJobs() const;

    /* 查询 */
    bool isConnected() const;

signals:
    /* 扫描 */
    void scanResult(QList<CamDevice> devices);
    void scanError(int code, QString message);

    /* 连接 */
    void connectionStatusChanged(int status);  // ConnectionStatus enum
    void connected();
    void disconnected();
    void connectionError(int code, QString message);

    /* 拍摄 */
    void captureCompleted();
    void captureError(int code, QString message);

    /* 取景 */
    void liveViewStarted();
    void liveViewStopped();
    void liveViewFrame(QByteArray jpegData);

    /* 属性 */
    void propertyValue(quint16 propId, quint32 value);
    void propertySetResult(quint16 propId, int result);
    void propertyError(int code, QString message);

    /* Picture Control */
    void pictureControlResult(CamPictureControl ctrl);
    void pictureControlSetResult(int result);

    /* 文件 */
    void fileListResult(QList<CamFile> files);
    void thumbnailResult(quint32 handle, QByteArray jpegData);
    void fileDeleted(quint32 handle);
    void fileError(int code, QString message);

    /* 传输 */
    void transferProgress(int jobId, CamTransferProgress progress);
    void transferComplete(int jobId, int errorCode);
    void newFile(quint32 handle, QString filename);
    void transferError(int code, QString message);
    /* v2:任务列表变更(增/改/删) */
    void transferJobsUpdated(QList<CamTransferJob> jobs);

    /* 通用 */
    void errorOccurred(int code, QString message);

private:
    void runOnWorker(std::function<void()> task);

    CameraAPI *m_api;
    QThread    m_workerThread;

    /* 线程安全 */
    QMutex     m_mutex;
    std::atomic<bool> m_shutdown{false};

    /* v2:本地传输任务表(jobId → CamTransferJob),GUI 线程访问 */
    mutable QMutex  m_jobsMutex;
    QHash<int, CamTransferJob> m_transferJobs;
    int  m_nextJobId = 1;

    void upsertJob(const CamTransferJob &job);
    void emitJobsChanged();

    /* 静态回调 → Qt Signal 桥接 */
    static void s_onScan(CameraInfo *cameras, int count, void *user);
    static void s_onStatus(ConnectionStatus status, void *user);
    static void s_onEvent(CameraEvent *event, void *user);
    static void s_onProgress(int jobId, TransferProgress *p, void *user);
    static void s_onNewFile(uint32_t handle, const char *filename, void *user);

    /* 辅助 */
    CamDevice  convertInfo(const CameraInfo *info);
    CamFile   convertFileInfo(const FileInfo *info);
    CamTransferProgress convertProgress(int jobId, const TransferProgress *p);
    CamPictureControl convertPictureControl(const PictureControl *pc);
};

#endif // NIKON_DESKTOP_API_H
