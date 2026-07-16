/**
 * desktop/src/ui/pages/liveview_page.h — 实时取景页面
 */
#ifndef NIKON_LIVEVIEW_PAGE_H
#define NIKON_LIVEVIEW_PAGE_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>

class DesktopAPI;

class LiveViewPage : public QWidget {
    Q_OBJECT

public:
    explicit LiveViewPage(DesktopAPI *api, QWidget *parent = nullptr);
    ~LiveViewPage() override;

public slots:
    void onLiveViewStarted();
    void onLiveViewStopped();
    void onLiveViewFrame(QByteArray jpegData);

private slots:
    void onToggleLiveView();
    void onCapture();

private:
    void setupUi();

    DesktopAPI  *m_api;
    QLabel      *m_viewLabel;      // 显示 JPEG 帧
    QPushButton *m_liveViewBtn;
    QPushButton *m_captureBtn;
    QLabel      *m_statusLabel;

    QPixmap      m_currentFrame;
    bool         m_liveActive;
};

#endif // NIKON_LIVEVIEW_PAGE_H
