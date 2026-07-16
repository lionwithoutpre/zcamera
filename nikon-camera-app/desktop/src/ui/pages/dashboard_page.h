/**
 * desktop/src/ui/pages/dashboard_page.h — 相机状态仪表盘
 */
#ifndef NIKON_DASHBOARD_PAGE_H
#define NIKON_DASHBOARD_PAGE_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QTimer>
#include <QGridLayout>

class DesktopAPI;

class DashboardPage : public QWidget {
    Q_OBJECT

public:
    explicit DashboardPage(DesktopAPI *api, QWidget *parent = nullptr);

public slots:
    void onConnected();
    void onDisconnected();
    void onPropertyValue(quint16 propId, quint32 value);

private slots:
    void onCapture();
    void onAutoFocus();
    void onRefreshInfo();

private:
    void setupUi();
    void updateCameraInfo(const struct CamDevice &info);

    DesktopAPI  *m_api;

    /* 状态卡片 */
    QLabel      *m_modelLabel;
    QLabel      *m_serialLabel;
    QLabel      *m_shutterLabel;
    QLabel      *m_firmwareLabel;

    /* 电量 */
    QProgressBar *m_batteryBar;
    QLabel       *m_batteryLabel;

    /* 存储 */
    QProgressBar *m_storageBar;
    QLabel       *m_storageLabel;

    /* 按钮 */
    QPushButton  *m_captureBtn;
    QPushButton  *m_afBtn;
    QPushButton  *m_refreshBtn;

    /* v2:参数轮询 + 参数网格 */
    QTimer       *m_propTimer;
    QLabel       *m_paramShutter;
    QLabel       *m_paramAperture;
    QLabel       *m_paramIso;
    QLabel       *m_paramEv;
    QLabel       *m_paramFocus;
    QLabel       *m_paramQuality;

    bool          m_connected;
};

#endif // NIKON_DASHBOARD_PAGE_H
