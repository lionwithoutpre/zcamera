/**
 * desktop/src/ui/pages/file_browser_page.cpp — 文件浏览与传输
 */
#include "file_browser_page.h"
#include "../api/desktop_api.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>
#include <QMenu>
#include <QAction>
#include <algorithm>
#include <QDebug>

FileBrowserPage::FileBrowserPage(DesktopAPI *api, QWidget *parent)
    : QWidget(parent), m_api(api), m_activeTransfers(0)
{
    setupUi();

    connect(m_api, &DesktopAPI::fileListResult, this, &FileBrowserPage::onFileListResult);
    connect(m_api, &DesktopAPI::thumbnailResult, this, &FileBrowserPage::onThumbnailResult);
    connect(m_api, &DesktopAPI::transferProgress, this, &FileBrowserPage::onTransferProgress);
    connect(m_api, &DesktopAPI::transferComplete, this, &FileBrowserPage::onTransferComplete);
    connect(m_api, &DesktopAPI::newFile, this, &FileBrowserPage::onNewFile);
    connect(m_api, &DesktopAPI::transferJobsUpdated, this, &FileBrowserPage::onTransferJobsUpdated);
}

void FileBrowserPage::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(32, 24, 32, 24);
    layout->setSpacing(12);

    /* 标题 */
    auto *header = new QLabel("文件管理");
    header->setStyleSheet("font-size: 22px; font-weight: 700; color: #F0F0F0;");

    layout->addWidget(header);

    /* ── 工具栏 ── */
    auto *toolbar = new QHBoxLayout;
    toolbar->setSpacing(8);

    auto *storageLabel = new QLabel("存储卡:");
    storageLabel->setStyleSheet("color: #A0A0A0; font-size: 12px;");

    m_storageCombo = new QComboBox;
    m_storageCombo->addItem("全部", 0);
    m_storageCombo->addItem("CF 卡", 0x00010001u);
    m_storageCombo->addItem("SD 卡", 0x00010002u);

    m_refreshBtn = new QPushButton("刷新");
    m_refreshBtn->setCursor(Qt::PointingHandCursor);

    m_downloadBtn = new QPushButton("下载选中");
    m_downloadBtn->setObjectName("primaryBtn");
    m_downloadBtn->setEnabled(false);
    m_downloadBtn->setCursor(Qt::PointingHandCursor);

    m_downloadAllBtn = new QPushButton("下载全部");
    m_downloadAllBtn->setCursor(Qt::PointingHandCursor);

    m_deleteBtn = new QPushButton("删除");
    m_deleteBtn->setObjectName("dangerBtn");
    m_deleteBtn->setEnabled(false);
    m_deleteBtn->setCursor(Qt::PointingHandCursor);

    toolbar->addWidget(storageLabel);
    toolbar->addWidget(m_storageCombo);
    toolbar->addSpacing(12);
    toolbar->addWidget(m_refreshBtn);
    toolbar->addStretch();
    toolbar->addWidget(m_downloadBtn);
    toolbar->addWidget(m_downloadAllBtn);
    toolbar->addWidget(m_deleteBtn);

    layout->addLayout(toolbar);

    /* ── 文件表格 + 预览 (QSplitter) ── */
    m_splitter = new QSplitter(Qt::Horizontal);
    m_splitter->setHandleWidth(1);

    /* 文件表格 */
    m_fileTree = new QTreeWidget;
    m_fileTree->setColumnCount(5);
    m_fileTree->setHeaderLabels({"文件名", "大小", "日期", "格式", "分辨率"});
    m_fileTree->setAlternatingRowColors(true);
    m_fileTree->setRootIsDecorated(false);
    m_fileTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_fileTree->header()->setStretchLastSection(false);
    m_fileTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_fileTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_fileTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_fileTree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_fileTree->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_fileTree->setStyleSheet(
        "QTreeWidget {"
        "  background-color: #111111;"
        "  border: 1px solid rgba(255,255,255,0.08);"
        "  border-radius: 8px;"
        "  padding: 4px;"
        "}"
    );

    /* 预览面板 */
    auto *previewPanel = new QWidget;
    previewPanel->setFixedWidth(260);
    previewPanel->setStyleSheet("background-color: #111111; border-radius: 8px;");

    auto *previewLayout = new QVBoxLayout(previewPanel);
    previewLayout->setContentsMargins(12, 16, 12, 16);
    previewLayout->setSpacing(8);

    auto *previewTitle = new QLabel("预览");
    previewTitle->setStyleSheet("font-size: 12px; font-weight: 600; color: #616161;");

    m_previewLabel = new QLabel;
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setMinimumHeight(160);
    m_previewLabel->setStyleSheet(
        "QLabel {"
        "  background-color: #0A0A0A;"
        "  border: 1px solid rgba(255,255,255,0.06);"
        "  border-radius: 6px;"
        "  color: #404040;"
        "  font-size: 14px;"
        "}"
    );
    m_previewLabel->setText("选择文件\n查看预览");

    m_fileInfoLabel = new QLabel;
    m_fileInfoLabel->setStyleSheet("font-size: 11px; color: #808080;");
    m_fileInfoLabel->setWordWrap(true);

    previewLayout->addWidget(previewTitle);
    previewLayout->addWidget(m_previewLabel);
    previewLayout->addWidget(m_fileInfoLabel);
    previewLayout->addStretch();

    m_splitter->addWidget(m_fileTree);
    m_splitter->addWidget(previewPanel);
    m_splitter->setStretchFactor(0, 3);
    m_splitter->setStretchFactor(1, 1);

    layout->addWidget(m_splitter, 1);

    /* ── v2:传输任务管理区(四分区 + 批量操作)── */
    auto *jobFrame = new QFrame;
    jobFrame->setStyleSheet("background-color: #111111; border-radius: 8px;");
    auto *jobLayout = new QVBoxLayout(jobFrame);
    jobLayout->setContentsMargins(12, 10, 12, 10);
    jobLayout->setSpacing(8);

    /* 工具栏 */
    auto *jobToolbar = new QHBoxLayout;
    auto *jobTitle = new QLabel("传输任务");
    jobTitle->setStyleSheet("font-size: 13px; font-weight: 700; color: #F0F0F0;");
    m_pauseAllBtn = new QPushButton("全部暂停");
    m_cancelAllBtn = new QPushButton("全部取消");
    m_cancelAllBtn->setObjectName("dangerBtn");
    m_pauseAllBtn->setCursor(Qt::PointingHandCursor);
    m_cancelAllBtn->setCursor(Qt::PointingHandCursor);
    jobToolbar->addWidget(jobTitle);
    jobToolbar->addStretch();
    jobToolbar->addWidget(m_pauseAllBtn);
    jobToolbar->addWidget(m_cancelAllBtn);
    jobLayout->addLayout(jobToolbar);

    /* 任务树(分组:传输中/失败/等待/完成) */
    m_jobTree = new QTreeWidget;
    m_jobTree->setColumnCount(4);
    m_jobTree->setHeaderLabels({"文件名", "进度", "速度", "状态"});
    m_jobTree->setRootIsDecorated(true);
    m_jobTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_jobTree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_jobTree->setStyleSheet(
        "QTreeWidget { background-color: #0E0E0E; border: 1px solid rgba(255,255,255,0.06); border-radius: 6px; }"
        "QTreeWidget::item { padding: 4px 8px; }"
        "QTreeWidget::item:selected { background-color: rgba(245,184,0,0.12); }");
    m_jobTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_jobTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_jobTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_jobTree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_jobTree->setMinimumHeight(140);
    jobLayout->addWidget(m_jobTree);

    /* 旧进度条保留为总览 */
    m_transferProgress = new QProgressBar;
    m_transferProgress->setRange(0, 100);
    m_transferProgress->setValue(0);
    m_transferProgress->setFixedHeight(6);
    m_transferProgress->setVisible(false);

    m_transferSpeed = new QLabel("0 MB/s");
    m_transferSpeed->setStyleSheet("color: #F5B800; font-size: 11px; font-weight: 600;");

    m_transferStatus = new QLabel("就绪");
    m_transferStatus->setStyleSheet("color: #808080; font-size: 11px;");

    auto *summaryRow = new QHBoxLayout;
    summaryRow->addWidget(m_transferProgress, 1);
    summaryRow->addWidget(m_transferSpeed);
    summaryRow->addWidget(m_transferStatus);
    jobLayout->addLayout(summaryRow);

    layout->addWidget(jobFrame);

    /* 信号 */
    connect(m_refreshBtn, &QPushButton::clicked, this, &FileBrowserPage::onRefresh);
    connect(m_downloadBtn, &QPushButton::clicked, this, &FileBrowserPage::onDownload);
    connect(m_downloadAllBtn, &QPushButton::clicked, this, &FileBrowserPage::onDownloadAll);
    connect(m_deleteBtn, &QPushButton::clicked, this, &FileBrowserPage::onDeleteFile);
    connect(m_fileTree, &QTreeWidget::itemSelectionChanged, this, &FileBrowserPage::onFileSelected);
    connect(m_storageCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &FileBrowserPage::onStorageChanged);
    connect(m_pauseAllBtn, &QPushButton::clicked, this, &FileBrowserPage::onPauseAll);
    connect(m_cancelAllBtn, &QPushButton::clicked, this, &FileBrowserPage::onCancelAll);
    connect(m_jobTree, &QTreeWidget::customContextMenuRequested,
            this, &FileBrowserPage::onJobContextMenu);
}

/* ─── 操作 ───────────────────────────────────────────────────── */

void FileBrowserPage::onRefresh()
{
    m_fileTree->clear();
    m_files.clear();
    m_previewLabel->setText("加载中...");
    quint32 storageId = m_storageCombo->currentData().toUInt();
    m_api->listFiles(storageId);
}

void FileBrowserPage::onStorageChanged(int) { onRefresh(); }

void FileBrowserPage::onFileSelected()
{
    auto items = m_fileTree->selectedItems();
    m_downloadBtn->setEnabled(!items.isEmpty());
    m_deleteBtn->setEnabled(!items.isEmpty());

    if (items.isEmpty()) {
        m_previewLabel->setText("选择文件\n查看预览");
        m_fileInfoLabel->setText("");
        return;
    }

    auto *item = items.first();
    int idx = item->data(0, Qt::UserRole).toInt();
    if (idx < 0 || idx >= m_files.size()) return;

    const auto &f = m_files[idx];
    m_fileInfoLabel->setText(QString(
        "文件名: %1\n大小: %2 MB\n日期: %3\n格式: %4\n分辨率: %5×%6")
        .arg(f.filename)
        .arg((double)f.size / 1e6, 0, 'f', 1)
        .arg(f.datetime)
        .arg(f.isRaw ? "RAW" : (f.isJpeg ? "JPEG" : "未知"))
        .arg(f.width).arg(f.height));

    m_previewLabel->setText("加载预览...");
    refreshThumbnail(f.handle);
}

void FileBrowserPage::refreshThumbnail(quint32 handle)
{
    m_api->getThumbnail(handle);
}

void FileBrowserPage::onDownload()
{
    auto items = m_fileTree->selectedItems();
    if (items.isEmpty()) return;

    QString destDir = QFileDialog::getExistingDirectory(this, "选择保存目录",
                                                         QDir::homePath() + "/Pictures");
    if (destDir.isEmpty()) return;

    for (auto *item : items) {
        int idx = item->data(0, Qt::UserRole).toInt();
        if (idx < 0 || idx >= m_files.size()) continue;
        const auto &f = m_files[idx];
        QString destPath = destDir + "/" + f.filename;
        m_api->startTransfer(f.handle, destPath);
        m_activeTransfers++;
    }

    m_transferStatus->setText(QString("传输 %1 个文件...").arg(items.size()));
    m_transferStatus->setStyleSheet("color: #F5B800; font-size: 12px;");
}

void FileBrowserPage::onDownloadAll()
{
    if (m_files.isEmpty()) return;

    QString destDir = QFileDialog::getExistingDirectory(this, "选择保存目录",
                                                         QDir::homePath() + "/Pictures");
    if (destDir.isEmpty()) return;

    QList<quint32> handles;
    for (const auto &f : m_files) {
        handles.append(f.handle);
    }

    m_api->startBatchTransfer(handles, destDir);
    m_activeTransfers = m_files.size();
    m_transferStatus->setText(QString("批量传输 %1 个文件...").arg(m_files.size()));
    m_transferStatus->setStyleSheet("color: #F5B800; font-size: 12px;");
}

void FileBrowserPage::onDeleteFile()
{
    auto items = m_fileTree->selectedItems();
    if (items.isEmpty()) return;

    auto reply = QMessageBox::question(this, "确认删除",
        QString("确定要删除选中的 %1 个文件吗？\n此操作不可撤销。").arg(items.size()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (reply != QMessageBox::Yes) return;

    for (auto *item : items) {
        int idx = item->data(0, Qt::UserRole).toInt();
        if (idx < 0 || idx >= m_files.size()) continue;
        m_api->deleteFile(m_files[idx].handle);
    }

    /* 延迟刷新 */
    QTimer::singleShot(500, this, &FileBrowserPage::onRefresh);
}

/* ─── 结果处理 ───────────────────────────────────────────────── */

void FileBrowserPage::onFileListResult(QList<CamFile> files)
{
    m_files = files;
    m_fileTree->clear();

    if (files.isEmpty()) {
        m_previewLabel->setText("存储卡为空");
        m_downloadAllBtn->setEnabled(false);
        return;
    }

    m_downloadAllBtn->setEnabled(true);

    for (int i = 0; i < files.size(); i++) {
        const auto &f = files[i];

        auto *item = new QTreeWidgetItem;
        item->setText(0, f.filename);
        item->setText(1, QString("%1 MB").arg((double)f.size / 1e6, 0, 'f', 1));
        item->setText(2, f.datetime);
        item->setText(3, f.isRaw ? "RAW" : (f.isJpeg ? "JPEG" : "?"));
        item->setText(4, QString("%1×%2").arg(f.width).arg(f.height));
        item->setData(0, Qt::UserRole, i);

        /* RAW 文件高亮 */
        if (f.isRaw) {
            item->setForeground(3, QColor("#F59300"));  // 橙色标记 RAW
        }

        m_fileTree->addTopLevelItem(item);
    }

    m_previewLabel->setText(QString("共 %1 个文件\n选择文件查看预览").arg(files.size()));
}

void FileBrowserPage::onThumbnailResult(quint32 handle, QByteArray jpegData)
{
    Q_UNUSED(handle);
    QPixmap pixmap;
    if (pixmap.loadFromData(jpegData)) {
        m_previewLabel->setPixmap(pixmap.scaled(m_previewLabel->size(),
                                                 Qt::KeepAspectRatio,
                                                 Qt::SmoothTransformation));
    }
}

void FileBrowserPage::onTransferProgress(int jobId, CamTransferProgress progress)
{
    Q_UNUSED(jobId);
    m_transferProgress->setValue(progress.percent);
    m_transferSpeed->setText(QString("%1 MB/s").arg(progress.speedMbps, 0, 'f', 1));
    m_transferStatus->setText(QString("%1: %2%").arg(progress.filename).arg(progress.percent));
}

void FileBrowserPage::onTransferComplete(int jobId, int errorCode)
{
    Q_UNUSED(jobId);
    m_activeTransfers--;
    if (m_activeTransfers <= 0) {
        m_activeTransfers = 0;
        m_transferStatus->setText(errorCode == 0 ? "传输完成" : "传输出错");
        m_transferStatus->setStyleSheet(errorCode == 0
            ? "color: #4CAF50; font-size: 12px;"
            : "color: #E03A3A; font-size: 12px;");
        m_transferProgress->setValue(errorCode == 0 ? 100 : 0);
    }
}

void FileBrowserPage::onNewFile(quint32 handle, QString filename)
{
    Q_UNUSED(handle);
    m_transferStatus->setText(QString("新文件: %1").arg(filename));
    m_transferStatus->setStyleSheet("color: #4CAF50; font-size: 12px;");
}

/* ─── v2:传输任务四分区管理 ─────────────────────────────────── */

void FileBrowserPage::onTransferJobsUpdated(QList<CamTransferJob> jobs)
{
    rebuildJobTree(jobs);

    /* 总览:活跃任务数 + 平均速度 */
    int active = 0;
    double totalSpeed = 0;
    for (const auto &j : jobs) {
        if (j.status == TS_ACTIVE) {
            active++;
            totalSpeed += j.speedMbps;
        }
    }
    if (active > 0) {
        m_transferProgress->setVisible(true);
        m_transferSpeed->setText(QString("%1 MB/s").arg(totalSpeed, 0, 'f', 1));
        m_transferStatus->setText(QString("%1 个任务传输中").arg(active));
        m_transferStatus->setStyleSheet("color: #F5B800; font-size: 11px;");
    } else {
        m_transferProgress->setVisible(false);
        bool hasFailed = std::any_of(jobs.begin(), jobs.end(),
            [](const CamTransferJob &j){ return j.status == TS_FAILED; });
        if (hasFailed) {
            m_transferStatus->setText("有失败任务,右键可重试");
            m_transferStatus->setStyleSheet("color: #E03A3A; font-size: 11px;");
        } else {
            m_transferStatus->setText("就绪");
            m_transferStatus->setStyleSheet("color: #808080; font-size: 11px;");
        }
    }
    m_activeTransfers = active;
}

void FileBrowserPage::rebuildJobTree(const QList<CamTransferJob> &jobs)
{
    m_jobTree->clear();
    m_jobItems.clear();

    /* 四个分组根节点 */
    auto *grpActive  = new QTreeWidgetItem({"传输中", "", "", ""});
    auto *grpFailed  = new QTreeWidgetItem({"传输失败", "", "", ""});
    auto *grpWaiting = new QTreeWidgetItem({"等待中", "", "", ""});
    auto *grpDone    = new QTreeWidgetItem({"已完成", "", "", ""});
    grpActive->setForeground(0, QColor("#F5B800"));
    grpFailed->setForeground(0, QColor("#E03A3A"));
    grpWaiting->setForeground(0, QColor("#808080"));
    grpDone->setForeground(0, QColor("#4CAF50"));
    QFont bold; bold.setBold(true);
    grpActive->setFont(0, bold); grpFailed->setFont(0, bold);
    grpWaiting->setFont(0, bold); grpDone->setFont(0, bold);

    m_jobTree->addTopLevelItem(grpActive);
    m_jobTree->addTopLevelItem(grpFailed);
    m_jobTree->addTopLevelItem(grpWaiting);
    m_jobTree->addTopLevelItem(grpDone);

    int cntA = 0, cntF = 0, cntW = 0, cntD = 0;
    for (const auto &j : jobs) {
        auto *item = new QTreeWidgetItem;
        item->setText(0, j.filename);
        item->setText(1, QString("%1%").arg(j.percent));
        item->setText(2, QString("%1 MB/s").arg(j.speedMbps, 0, 'f', 1));
        item->setText(3, j.note);
        item->setData(0, Qt::UserRole, j.jobId);

        switch (j.status) {
        case TS_ACTIVE:
            item->setForeground(3, QColor("#F5B800"));
            grpActive->addChild(item); cntA++; break;
        case TS_FAILED:
            item->setForeground(3, QColor("#E03A3A"));
            grpFailed->addChild(item); cntF++; break;
        case TS_WAITING:
            grpWaiting->addChild(item); cntW++; break;
        case TS_PAUSED:
            item->setForeground(3, QColor("#F59300"));
            grpActive->addChild(item); cntA++; break;  /* 暂停归到传输中分组显示 */
        case TS_DONE:
            item->setForeground(3, QColor("#4CAF50"));
            grpDone->addChild(item); cntD++; break;
        case TS_CANCELLED:
            grpDone->addChild(item); cntD++; break;
        }
        m_jobItems[j.jobId] = item;
    }
    grpActive->setText(0, QString("传输中 (%1)").arg(cntA));
    grpFailed->setText(0, QString("传输失败 (%1)").arg(cntF));
    grpWaiting->setText(0, QString("等待中 (%1)").arg(cntW));
    grpDone->setText(0, QString("已完成 (%1)").arg(cntD));
    m_jobTree->expandAll();
}

void FileBrowserPage::onPauseAll()
{
    m_api->pauseAllTransfers();
}

void FileBrowserPage::onCancelAll()
{
    auto reply = QMessageBox::question(this, "确认取消",
        "确定要取消所有传输任务吗?", QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply == QMessageBox::Yes) m_api->cancelAllTransfers();
}

void FileBrowserPage::onJobContextMenu(const QPoint &pos)
{
    auto *item = m_jobTree->itemAt(pos);
    if (!item) return;
    /* 只对叶子节点(任务)弹菜单,分组根节点不弹 */
    if (item->parent() == nullptr) return;

    int jobId = item->data(0, Qt::UserRole).toInt();
    if (jobId == 0) return;

    /* 查找任务状态 */
    CamTransferJob job;
    for (const auto &j : m_api->transferJobs()) {
        if (j.jobId == jobId) { job = j; break; }
    }
    if (job.jobId == 0) return;

    auto *menu = new QMenu(this);
    menu->setStyleSheet(
        "QMenu { background-color: #1E1E1E; color: #F0F0F0; border: 1px solid rgba(255,255,255,0.1); }"
        "QMenu::item:selected { background-color: rgba(245,184,0,0.2); }");

    if (job.status == TS_FAILED) {
        auto *resumeAct = menu->addAction("断点续传");
        auto *retryAct  = menu->addAction("重新传输");
        resumeAct->setForeground(QColor("#F5B800"));
        retryAct->setForeground(QColor("#F5B800"));
        connect(resumeAct, &QAction::triggered, this, [this, jobId]() { m_api->resumeTransfer(jobId); });
        connect(retryAct,  &QAction::triggered, this, [this, jobId]() { m_api->retryTransfer(jobId); });
    } else if (job.status == TS_ACTIVE || job.status == TS_PAUSED) {
        auto *cancelAct = menu->addAction("取消此任务");
        cancelAct->setForeground(QColor("#E03A3A"));
        connect(cancelAct, &QAction::triggered, this, [this, jobId]() { m_api->cancelTransfer(jobId); });
    } else if (job.status == TS_DONE) {
        auto *clearAct = menu->addAction("从列表清除");
        connect(clearAct, &QAction::triggered, this, [this, item]() {
            m_jobTree->takeTopLevelItem(m_jobTree->indexOfTopLevelItem(item->parent()));
        });
    }

    menu->exec(m_jobTree->viewport()->mapToGlobal(pos));
}

