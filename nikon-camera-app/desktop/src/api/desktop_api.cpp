/**
 * desktop/src/api/desktop_api.cpp — DesktopAPI 实现
 *
 * 架构: CameraAPI 在专用工作线程运行, 所有 C 回调通过
 * QMetaObject::invokeMethod 转到 GUI 线程 emit Qt Signal。
 */
#include "desktop_api.h"
#include <QDebug>
#include <QFileInfo>
#include <cstring>

/* ─── 构造 / 析构 ────────────────────────────────────────────── */

DesktopAPI::DesktopAPI(int transport, QObject *parent)
    : QObject(parent), m_api(nullptr)
{
    /* CameraAPI 在工作线程中创建和使用 */
    m_api = camera_api_create(transport);
    if (!m_api) {
        qCritical("DesktopAPI: camera_api_create failed");
        return;
    }

    /* 注册事件回调 — 全部经 s_onEvent 中继 */
    camera_api_on_event(m_api, EVENT_CONNECTION_CHANGED, s_onEvent, this);
    camera_api_on_event(m_api, EVENT_NEW_FILE,          s_onEvent, this);
    camera_api_on_event(m_api, EVENT_TRANSFER_COMPLETE,  s_onEvent, this);
    camera_api_on_event(m_api, EVENT_TRANSFER_PROGRESS,  s_onEvent, this);
    camera_api_on_event(m_api, EVENT_ERROR,              s_onEvent, this);
    camera_api_on_event(m_api, EVENT_CAPTURE_COMPLETE,   s_onEvent, this);
    camera_api_on_event(m_api, EVENT_PROPERTY_CHANGED,   s_onEvent, this);

    /* 新文件边拍边传 */
    camera_api_on_new_file(m_api, s_onNewFile, this);

    /* 传输进度 (每块) */
    camera_api_on_transfer_progress(m_api, s_onProgress, this);

    /* 启动工作线程 */
    connect(&m_workerThread, &QThread::finished, this, [this]() {
        /* 线程结束时清理 CameraAPI */
        if (m_api) {
            camera_api_destroy(m_api);
            m_api = nullptr;
        }
    });
    m_workerThread.start();

    qDebug() << "DesktopAPI created, worker thread:" << &m_workerThread;
}

DesktopAPI::~DesktopAPI()
{
    shutdown();
}

void DesktopAPI::shutdown()
{
    if (m_shutdown.exchange(true)) return;

    qDebug() << "DesktopAPI shutting down...";

    /* 通知 worker 线程退出 */
    QMetaObject::invokeMethod(this, [this]() {
        m_workerThread.quit();
    }, Qt::QueuedConnection);

    m_workerThread.wait(3000);

    /* 兜底: 如果线程没能清理 */
    if (m_api) {
        camera_api_destroy(m_api);
        m_api = nullptr;
    }
}

/* ─── 任务调度 ───────────────────────────────────────────────── */

void DesktopAPI::runOnWorker(std::function<void()> task)
{
    if (m_shutdown) return;
    QMetaObject::invokeMethod(this, std::move(task), Qt::QueuedConnection);
}

/* ─── 连接管理 ───────────────────────────────────────────────── */

void DesktopAPI::scan()
{
    runOnWorker([this]() {
        QMutexLocker lock(&m_mutex);
        int count = 0;
        CameraInfo *cameras = camera_api_scan(m_api, &count);
        s_onScan(cameras, count, this);
    });
}

void DesktopAPI::connectTo(const QString &cameraId)
{
    runOnWorker([this, id = cameraId.toStdString()]() {
        QMutexLocker lock(&m_mutex);
        int ret = camera_api_connect(m_api, id.c_str());
        if (ret != CAM_OK) {
            QMetaObject::invokeMethod(this, [this, ret]() {
                emit connectionError(ret, QString::fromUtf8(camera_api_strerror(ret)));
            }, Qt::QueuedConnection);
        }
    });
}

void DesktopAPI::disconnect()
{
    runOnWorker([this]() {
        QMutexLocker lock(&m_mutex);
        camera_api_disconnect(m_api);
    });
}

/* ─── 拍摄 ───────────────────────────────────────────────────── */

void DesktopAPI::capture()
{
    runOnWorker([this]() {
        QMutexLocker lock(&m_mutex);
        int ret = camera_api_capture(m_api);
        if (ret != CAM_OK) {
            QMetaObject::invokeMethod(this, [this, ret]() {
                emit captureError(ret, QString::fromUtf8(camera_api_strerror(ret)));
            }, Qt::QueuedConnection);
        }
    });
}

void DesktopAPI::burst(int count, int intervalMs)
{
    runOnWorker([this, count, intervalMs]() {
        QMutexLocker lock(&m_mutex);
        camera_api_capture_burst(m_api, count, intervalMs);
    });
}

void DesktopAPI::stopBurst()
{
    runOnWorker([this]() {
        QMutexLocker lock(&m_mutex);
        camera_api_stop_burst(m_api);
    });
}

void DesktopAPI::autoFocus()
{
    runOnWorker([this]() {
        QMutexLocker lock(&m_mutex);
        camera_api_autofocus(m_api);
    });
}

/* ─── 取景 ───────────────────────────────────────────────────── */

void DesktopAPI::startLiveView()
{
    runOnWorker([this]() {
        QMutexLocker lock(&m_mutex);
        int ret = camera_api_start_liveview(m_api);
        if (ret == CAM_OK) {
            QMetaObject::invokeMethod(this, [this]() {
                emit liveViewStarted();
            }, Qt::QueuedConnection);
        }
    });
}

void DesktopAPI::stopLiveView()
{
    runOnWorker([this]() {
        QMutexLocker lock(&m_mutex);
        camera_api_stop_liveview(m_api);
        QMetaObject::invokeMethod(this, [this]() {
            emit liveViewStopped();
        }, Qt::QueuedConnection);
    });
}

/* ─── 属性 ───────────────────────────────────────────────────── */

void DesktopAPI::setProperty(quint16 propId, quint32 value)
{
    runOnWorker([this, propId, value]() {
        QMutexLocker lock(&m_mutex);
        int ret = camera_api_set_property(m_api, propId, value);
        QMetaObject::invokeMethod(this, [this, propId, ret]() {
            emit propertySetResult(propId, ret);
        }, Qt::QueuedConnection);
    });
}

void DesktopAPI::getProperty(quint16 propId)
{
    runOnWorker([this, propId]() {
        QMutexLocker lock(&m_mutex);
        uint32_t val = 0;
        int ret = camera_api_get_property(m_api, propId, &val);
        if (ret == CAM_OK) {
            QMetaObject::invokeMethod(this, [this, propId, val]() {
                emit propertyValue(propId, val);
            }, Qt::QueuedConnection);
        } else {
            QMetaObject::invokeMethod(this, [this, ret]() {
                emit propertyError(ret, QString::fromUtf8(camera_api_strerror(ret)));
            }, Qt::QueuedConnection);
        }
    });
}

void DesktopAPI::setIso(int iso)        { setProperty(0xD010, (quint32)iso); }
void DesktopAPI::setShutter(int speedVal) { setProperty(0xD00C, (quint32)speedVal); }
void DesktopAPI::setAperture(int apertureVal) { setProperty(0xD00E, (quint32)apertureVal); }
void DesktopAPI::setWhiteBalance(int wbVal)   { setProperty(0xD00A, (quint32)wbVal); }

/* ─── Picture Control ─────────────────────────────────────────── */

void DesktopAPI::getPictureControl()
{
    runOnWorker([this]() {
        QMutexLocker lock(&m_mutex);
        PictureControl pc;
        int ret = camera_api_get_pictctrl(m_api, &pc);
        if (ret == CAM_OK) {
            CamPictureControl ctrl = convertPictureControl(&pc);
            QMetaObject::invokeMethod(this, [this, ctrl]() {
                emit pictureControlResult(ctrl);
            }, Qt::QueuedConnection);
        } else {
            QMetaObject::invokeMethod(this, [this, ret]() {
                emit propertyError(ret, QString::fromUtf8(camera_api_strerror(ret)));
            }, Qt::QueuedConnection);
        }
    });
}

void DesktopAPI::setPictureControl(const CamPictureControl &ctrl)
{
    runOnWorker([this, ctrl]() {
        QMutexLocker lock(&m_mutex);
        PictureControl pc;
        pc.hue         = (int8_t)ctrl.hue;
        pc.saturation  = (int8_t)ctrl.saturation;
        pc.contrast    = (int8_t)ctrl.contrast;
        pc.clarity     = (int8_t)ctrl.clarity;
        pc.sharpening  = (int8_t)ctrl.sharpening;
        pc.brightness  = (int8_t)ctrl.brightness;
        pc.wb_ab       = (int8_t)ctrl.wbAb;
        pc.wb_gm       = (int8_t)ctrl.wbGm;
        pc.color_space = (uint8_t)ctrl.colorSpace;

        int ret = camera_api_set_pictctrl(m_api, &pc);
        QMetaObject::invokeMethod(this, [this, ret]() {
            emit pictureControlSetResult(ret);
        }, Qt::QueuedConnection);
    });
}

/* ─── 文件 ───────────────────────────────────────────────────── */

void DesktopAPI::listFiles(quint32 storageId)
{
    runOnWorker([this, storageId]() {
        QMutexLocker lock(&m_mutex);
        int count = 0;
        FileInfo *files = camera_api_list_files(m_api, storageId, &count);
        QList<CamFile> result;
        if (files) {
            result.reserve(count);
            for (int i = 0; i < count; i++) {
                result.append(convertFileInfo(&files[i]));
            }
            free(files);
        }
        QMetaObject::invokeMethod(this, [this, result]() {
            emit fileListResult(result);
        }, Qt::QueuedConnection);
    });
}

void DesktopAPI::getThumbnail(quint32 handle)
{
    runOnWorker([this, handle]() {
        QMutexLocker lock(&m_mutex);
        uint8_t *data = nullptr;
        uint32_t size = 0;
        int ret = camera_api_get_thumbnail(m_api, handle, &data, &size);
        if (ret == CAM_OK && data) {
            QByteArray jpeg(reinterpret_cast<const char *>(data), (int)size);
            free(data);
            QMetaObject::invokeMethod(this, [this, handle, jpeg]() {
                emit thumbnailResult(handle, jpeg);
            }, Qt::QueuedConnection);
        }
    });
}

void DesktopAPI::deleteFile(quint32 handle)
{
    runOnWorker([this, handle]() {
        QMutexLocker lock(&m_mutex);
        int ret = camera_api_delete_file(m_api, handle);
        if (ret == CAM_OK) {
            QMetaObject::invokeMethod(this, [this, handle]() {
                emit fileDeleted(handle);
            }, Qt::QueuedConnection);
        } else {
            QMetaObject::invokeMethod(this, [this, ret]() {
                emit fileError(ret, QString::fromUtf8(camera_api_strerror(ret)));
            }, Qt::QueuedConnection);
        }
    });
}

/* ─── 传输 ───────────────────────────────────────────────────── */

void DesktopAPI::startTransfer(quint32 handle, const QString &destPath)
{
    /* 本地登记任务 */
    CamTransferJob job;
    job.jobId      = m_nextJobId++;
    job.handle     = handle;
    job.filename   = QFileInfo(destPath).fileName();
    job.destPath   = destPath;
    job.status     = TS_ACTIVE;
    job.percent    = 0;
    job.speedMbps  = 0;
    job.note       = "传输中";
    upsertJob(job);

    runOnWorker([this, handle, path = destPath.toStdString(), localId = job.jobId]() {
        QMutexLocker lock(&m_mutex);
        int jobId = camera_api_start_transfer(m_api, handle, path.c_str());
        if (jobId < 0) {
            QMetaObject::invokeMethod(this, [this, localId, jobId]() {
                CamTransferJob j = m_transferJobs.value(localId);
                j.status = TS_FAILED;
                j.note   = QString("启动失败 (错误码: %1)").arg(jobId);
                upsertJob(j);
                emit transferError(jobId, QString::fromUtf8(camera_api_strerror(jobId)));
            }, Qt::QueuedConnection);
        }
    });
}

void DesktopAPI::startBatchTransfer(const QList<quint32> &handles, const QString &destDir)
{
    runOnWorker([this, handles, path = destDir.toStdString()]() {
        QMutexLocker lock(&m_mutex);
        QVector<uint32_t> h;
        h.reserve(handles.size());
        for (auto v : handles) h.append(v);
        int jobId = camera_api_start_batch_transfer(m_api, h.data(), h.size(), path.c_str());
        if (jobId < 0) {
            QMetaObject::invokeMethod(this, [this, jobId]() {
                emit transferError(jobId, QString::fromUtf8(camera_api_strerror(jobId)));
            }, Qt::QueuedConnection);
        }
    });
}

void DesktopAPI::cancelTransfer(int jobId)
{
    runOnWorker([this, jobId]() {
        QMutexLocker lock(&m_mutex);
        camera_api_cancel_transfer(m_api, jobId);
    });
    /* 更新本地任务状态 */
    QMutexLocker jl(&m_jobsMutex);
    for (auto it = m_transferJobs.begin(); it != m_transferJobs.end(); ++it) {
        if (it.value().status == TS_ACTIVE || it.value().status == TS_WAITING) {
            it.value().status = TS_CANCELLED;
            it.value().note   = "已取消";
        }
    }
    emitJobsChanged();
}

/* ─── v2:批量任务管理 ─────────────────────────────────────────── */

void DesktopAPI::pauseAllTransfers()
{
    /* native 无 pause 语义,cancel + 标记 PAUSED(native 层记录 offset) */
    QList<int> activeIds;
    {
        QMutexLocker jl(&m_jobsMutex);
        for (auto it = m_transferJobs.begin(); it != m_transferJobs.end(); ++it) {
            if (it.value().status == TS_ACTIVE) {
                activeIds.append(it.key());
                it.value().status = TS_PAUSED;
                it.value().note   = "已暂停";
            }
        }
    }
    for (int id : activeIds) {
        runOnWorker([this, id]() {
            QMutexLocker lock(&m_mutex);
            camera_api_cancel_transfer(m_api, id);
        });
    }
    emitJobsChanged();
}

void DesktopAPI::cancelAllTransfers()
{
    QList<int> cancellable;
    {
        QMutexLocker jl(&m_jobsMutex);
        for (auto it = m_transferJobs.begin(); it != m_transferJobs.end(); ++it) {
            int s = it.value().status;
            if (s == TS_ACTIVE || s == TS_PAUSED || s == TS_WAITING) {
                cancellable.append(it.key());
                it.value().status = TS_CANCELLED;
                it.value().note   = "已取消";
            }
        }
    }
    for (int id : cancellable) {
        runOnWorker([this, id]() {
            QMutexLocker lock(&m_mutex);
            camera_api_cancel_transfer(m_api, id);
        });
    }
    emitJobsChanged();
}

void DesktopAPI::resumeTransfer(int jobId)
{
    CamTransferJob job;
    {
        QMutexLocker jl(&m_jobsMutex);
        job = m_transferJobs.value(jobId);
    }
    if (job.jobId == 0) return;
    job.status  = TS_ACTIVE;
    job.note    = "断点续传中";
    upsertJob(job);

    /* native 支持 offset 续传:重新对同一 handle 发起 startTransfer */
    runOnWorker([this, handle = job.handle, path = job.destPath.toStdString(), localId = jobId]() {
        QMutexLocker lock(&m_mutex);
        int rc = camera_api_start_transfer(m_api, handle, path.c_str());
        if (rc < 0) {
            QMetaObject::invokeMethod(this, [this, localId, rc]() {
                QMutexLocker jl(&m_jobsMutex);
                auto &j = m_transferJobs[localId];
                j.status = TS_FAILED;
                j.note   = QString("续传失败 (错误码: %1)").arg(rc);
                emitJobsChanged();
            }, Qt::QueuedConnection);
        }
    });
}

void DesktopAPI::retryTransfer(int jobId)
{
    CamTransferJob job;
    {
        QMutexLocker jl(&m_jobsMutex);
        job = m_transferJobs.value(jobId);
    }
    if (job.jobId == 0) return;
    job.status  = TS_ACTIVE;
    job.percent = 0;
    job.note    = "重新传输中";
    upsertJob(job);

    runOnWorker([this, handle = job.handle, path = job.destPath.toStdString(), localId = jobId]() {
        QMutexLocker lock(&m_mutex);
        int rc = camera_api_start_transfer(m_api, handle, path.c_str());
        if (rc < 0) {
            QMetaObject::invokeMethod(this, [this, localId, rc]() {
                QMutexLocker jl(&m_jobsMutex);
                auto &j = m_transferJobs[localId];
                j.status = TS_FAILED;
                j.note   = QString("重传失败 (错误码: %1)").arg(rc);
                emitJobsChanged();
            }, Qt::QueuedConnection);
        }
    });
}

QList<CamTransferJob> DesktopAPI::transferJobs() const
{
    QMutexLocker jl(&m_jobsMutex);
    return m_transferJobs.values();
}

void DesktopAPI::upsertJob(const CamTransferJob &job)
{
    {
        QMutexLocker jl(&m_jobsMutex);
        m_transferJobs[job.jobId] = job;
    }
    emitJobsChanged();
}

void DesktopAPI::emitJobsChanged()
{
    QList<CamTransferJob> snapshot;
    {
        QMutexLocker jl(&m_jobsMutex);
        snapshot = m_transferJobs.values();
    }
    emit transferJobsUpdated(snapshot);
}

bool DesktopAPI::isConnected() const
{
    /* 通过非阻塞方式检查 — 暂不实现跨线程状态查询 */
    return false;
}

/* ─── C 回调 → Qt Signal 桥接 ────────────────────────────────── */

void DesktopAPI::s_onScan(CameraInfo *cameras, int count, void *user)
{
    auto *self = static_cast<DesktopAPI *>(user);
    QList<CamDevice> devices;
    if (cameras && count > 0) {
        devices.reserve(count);
        for (int i = 0; i < count; i++) {
            devices.append(self->convertInfo(&cameras[i]));
        }
        free(cameras);
    }
    QMetaObject::invokeMethod(self, [self, devices]() {
        emit self->scanResult(devices);
    }, Qt::QueuedConnection);
}

void DesktopAPI::s_onStatus(ConnectionStatus status, void *user)
{
    auto *self = static_cast<DesktopAPI *>(user);
    QMetaObject::invokeMethod(self, [self, status]() {
        emit self->connectionStatusChanged((int)status);
        if (status == STATUS_CONNECTED) emit self->connected();
        if (status == STATUS_DISCONNECTED) emit self->disconnected();
    }, Qt::QueuedConnection);
}

void DesktopAPI::s_onEvent(CameraEvent *event, void *user)
{
    auto *self = static_cast<DesktopAPI *>(user);
    if (!event) return;

    switch (event->type) {
    case EVENT_CONNECTION_CHANGED:
        s_onStatus(event->data.connection_status, user);
        break;

    case EVENT_CAPTURE_COMPLETE:
        QMetaObject::invokeMethod(self, [self]() {
            emit self->captureCompleted();
        }, Qt::QueuedConnection);
        break;

    case EVENT_NEW_FILE: {
        uint32_t h  = event->data.new_file.object_handle;
        QString  fn = QString::fromUtf8(event->data.new_file.filename);
        QMetaObject::invokeMethod(self, [self, h, fn]() {
            emit self->newFile(h, fn);
        }, Qt::QueuedConnection);
        break;
    }

    case EVENT_TRANSFER_COMPLETE: {
        int jobId = event->data.transfer_complete.job_id;
        int err   = event->data.transfer_complete.error_code;
        /* 更新本地任务表 */
        {
            QMutexLocker jl(&self->m_jobsMutex);
            auto it = self->m_transferJobs.find(jobId);
            if (it != self->m_transferJobs.end()) {
                if (err == 0) { it.value().status = TS_DONE; it.value().note = "完成"; it.value().percent = 100; }
                else { it.value().status = TS_FAILED; it.value().note = QString("失败 (错误码: %1)").arg(err); }
            }
        }
        QMetaObject::invokeMethod(self, [self, jobId, err]() {
            emit self->transferComplete(jobId, err);
            self->emitJobsChanged();
        }, Qt::QueuedConnection);
        break;
    }

    case EVENT_ERROR: {
        int code = event->data.error.error_code;
        QString msg = QString::fromUtf8(event->data.error.message);
        QMetaObject::invokeMethod(self, [self, code, msg]() {
            emit self->errorOccurred(code, msg);
        }, Qt::QueuedConnection);
        break;
    }

    case EVENT_PROPERTY_CHANGED: {
        uint16_t pid = event->data.property_changed.prop_id;
        uint32_t val = event->data.property_changed.value;
        QMetaObject::invokeMethod(self, [self, pid, val]() {
            emit self->propertyValue(pid, val);
        }, Qt::QueuedConnection);
        break;
    }

    default:
        break;
    }
}

void DesktopAPI::s_onProgress(int jobId, TransferProgress *p, void *user)
{
    if (!p) return;
    auto *self = static_cast<DesktopAPI *>(user);
    auto cp = self->convertProgress(jobId, p);

    /* 更新本地任务表 */
    {
        QMutexLocker jl(&self->m_jobsMutex);
        auto it = self->m_transferJobs.find(jobId);
        if (it != self->m_transferJobs.end()) {
            it.value().percent     = cp.percent;
            it.value().speedMbps   = cp.speedMbps;
            it.value().transferred = cp.transferred;
            it.value().totalSize   = cp.totalSize;
            if (cp.status == 2) { it.value().status = TS_DONE;   it.value().note = "完成"; }
            else if (cp.status < 0) { it.value().status = TS_FAILED; it.value().note = "传输失败"; }
            else { it.value().status = TS_ACTIVE; it.value().note = "传输中"; }
        }
    }

    QMetaObject::invokeMethod(self, [self, jobId, cp]() {
        emit self->transferProgress(jobId, cp);
        self->emitJobsChanged();
    }, Qt::QueuedConnection);
}

void DesktopAPI::s_onNewFile(uint32_t handle, const char *filename, void *user)
{
    auto *self = static_cast<DesktopAPI *>(user);
    QString fn = QString::fromUtf8(filename);
    QMetaObject::invokeMethod(self, [self, handle, fn]() {
        emit self->newFile(handle, fn);
    }, Qt::QueuedConnection);
}

/* ─── 结构转换 ───────────────────────────────────────────────── */

CamDevice DesktopAPI::convertInfo(const CameraInfo *info)
{
    CamDevice d;
    d.id           = QString::fromUtf8(info->id);
    d.model        = QString::fromUtf8(info->model);
    d.serial       = QString::fromUtf8(info->serial);
    d.transport    = info->transport;
    d.battery      = info->battery_level;
    d.storageFree  = info->storage_free;
    d.storageTotal = info->storage_total;
    d.shutterCount = info->shutter_count;
    return d;
}

CamFile DesktopAPI::convertFileInfo(const FileInfo *info)
{
    CamFile f;
    f.handle    = info->object_handle;
    f.filename  = QString::fromUtf8(info->filename);
    f.size      = info->size;
    f.datetime  = QString::fromUtf8(info->datetime);
    f.isRaw     = info->is_raw;
    f.isJpeg    = info->is_jpeg;
    f.width     = info->width;
    f.height    = info->height;
    f.storageId = info->storage_id;
    return f;
}

CamTransferProgress DesktopAPI::convertProgress(int jobId, const TransferProgress *p)
{
    CamTransferProgress cp;
    cp.jobId       = jobId;
    cp.handle      = p->object_handle;
    cp.filename    = QString::fromUtf8(p->filename);
    cp.totalSize   = p->total_size;
    cp.transferred = p->transferred;
    cp.percent     = p->percent;
    cp.speedMbps   = p->speed_mbps;
    cp.elapsedMs   = p->elapsed_ms;
    cp.remainingMs = p->remaining_ms;
    cp.status      = p->status;
    cp.errorCode   = p->error_code;
    return cp;
}

CamPictureControl DesktopAPI::convertPictureControl(const PictureControl *pc)
{
    CamPictureControl c;
    c.hue         = pc->hue;
    c.saturation  = pc->saturation;
    c.contrast    = pc->contrast;
    c.clarity     = pc->clarity;
    c.sharpening  = pc->sharpening;
    c.brightness  = pc->brightness;
    c.wbAb        = pc->wb_ab;
    c.wbGm        = pc->wb_gm;
    c.colorSpace  = pc->color_space;
    return c;
}
