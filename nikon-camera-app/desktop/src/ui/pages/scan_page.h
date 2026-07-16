/**
 * desktop/src/ui/pages/scan_page.h — 设备扫描与连接页面
 */
#ifndef NIKON_SCAN_PAGE_H
#define NIKON_SCAN_PAGE_H

#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QProgressBar>

class DesktopAPI;

class ScanPage : public QWidget {
    Q_OBJECT

public:
    explicit ScanPage(DesktopAPI *api, QWidget *parent = nullptr);

signals:
    void cameraSelected(const QString &cameraId);

public slots:
    void onScanResult(QList<struct CamDevice> devices);
    void onConnectionStatusChanged(int status);

private slots:
    void onScanClicked();
    void onConnectClicked();
    void onDeviceDoubleClicked(QListWidgetItem *item);

private:
    void setupUi();

    DesktopAPI  *m_api;
    QPushButton *m_scanBtn;
    QPushButton *m_connectBtn;
    QListWidget *m_deviceList;
    QLabel      *m_statusLabel;
    QProgressBar*m_scanProgress;
    QComboBox   *m_transportCombo;  // Auto / USB / WiFi

    QStringList  m_deviceIds;
    bool         m_scanning;
};

#endif // NIKON_SCAN_PAGE_H
