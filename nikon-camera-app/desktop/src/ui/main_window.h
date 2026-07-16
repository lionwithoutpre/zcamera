/**
 * desktop/src/ui/main_window.h — Qt 主窗口
 *
 * 左侧导航栏 + 右侧 QStackedWidget 页面容器
 * Nikon 黑黄配色, 通过 QSS 样式表实现
 */
#ifndef NIKON_MAIN_WINDOW_H
#define NIKON_MAIN_WINDOW_H

#include <QMainWindow>
#include <QStackedWidget>
#include <QListWidget>
#include <QLabel>
#include <QStatusBar>
#include <QTimer>

class DesktopAPI;
class ScanPage;
class DashboardPage;
class LiveViewPage;
class FileBrowserPage;
class SettingsPage;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(DesktopAPI *api, QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onNavChanged(int row);
    void onConnectionStatusChanged(int status);
    void onConnected();
    void onDisconnected();

private:
    void setupUi();
    void setupStyle();
    void setupConnections();

    DesktopAPI      *m_api;

    /* 导航 */
    QListWidget     *m_navList;
    QStackedWidget  *m_pages;

    /* 页面 */
    ScanPage        *m_scanPage;
    DashboardPage   *m_dashboardPage;
    LiveViewPage    *m_liveViewPage;
    FileBrowserPage *m_fileBrowserPage;
    SettingsPage    *m_settingsPage;

    /* 状态栏 */
    QLabel          *m_statusIcon;
    QLabel          *m_statusText;
    QLabel          *m_cameraLabel;
    QLabel          *m_batteryLabel;
    QTimer          *m_clockTimer;

    /* 状态 */
    bool             m_connected;
    QString          m_currentCamera;
};

#endif // NIKON_MAIN_WINDOW_H
