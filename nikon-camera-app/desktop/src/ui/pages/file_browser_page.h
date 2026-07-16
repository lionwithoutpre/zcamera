/**
 * desktop/src/ui/pages/file_browser_page.h — 文件浏览与传输页面
 */
#ifndef NIKON_FILE_BROWSER_PAGE_H
#define NIKON_FILE_BROWSER_PAGE_H

#include <QWidget>
#include <QTreeWidget>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QComboBox>
#include <QSplitter>

class DesktopAPI;

class FileBrowserPage : public QWidget {
    Q_OBJECT

public:
    explicit FileBrowserPage(DesktopAPI *api, QWidget *parent = nullptr);

public slots:
    void onFileListResult(QList<struct CamFile> files);
    void onThumbnailResult(quint32 handle, QByteArray jpegData);
    void onTransferProgress(int jobId, struct CamTransferProgress progress);
    void onTransferComplete(int jobId, int errorCode);
    void onNewFile(quint32 handle, QString filename);
    void onTransferJobsUpdated(QList<struct CamTransferJob> jobs);

private slots:
    void onRefresh();
    void onFileSelected();
    void onDownload();
    void onDownloadAll();
    void onDeleteFile();
    void onStorageChanged(int index);
    void onPauseAll();
    void onCancelAll();
    void onJobContextMenu(const QPoint &pos);

private:
    void setupUi();
    void refreshThumbnail(quint32 handle);
    void rebuildJobTree(const QList<struct CamTransferJob> &jobs);

    DesktopAPI   *m_api;
    QTreeWidget  *m_fileTree;
    QLabel       *m_previewLabel;
    QLabel       *m_fileInfoLabel;
    QPushButton  *m_refreshBtn;
    QPushButton  *m_downloadBtn;
    QPushButton  *m_downloadAllBtn;
    QPushButton  *m_deleteBtn;
    QComboBox    *m_storageCombo;
    QProgressBar *m_transferProgress;
    QLabel       *m_transferSpeed;
    QLabel       *m_transferStatus;
    QSplitter    *m_splitter;

    /* v2:传输任务树 + 批量操作 */
    QTreeWidget  *m_jobTree;
    QPushButton  *m_pauseAllBtn;
    QPushButton  *m_cancelAllBtn;
    QHash<int, QTreeWidgetItem*> m_jobItems;

    QList<struct CamFile> m_files;
    int                   m_activeTransfers;
};

#endif // NIKON_FILE_BROWSER_PAGE_H
