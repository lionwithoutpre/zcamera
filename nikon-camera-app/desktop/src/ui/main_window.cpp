/**
 * desktop/src/ui/main_window.cpp — Qt 主窗口实现
 *
 * 布局:
 * ┌──────────┬──────────────────────────────┐
 * │          │                              │
 * │  导航栏   │      QStackedWidget          │
 * │  (150px) │                              │
 * │          │                              │
 * ├──────────┴──────────────────────────────┤
 * │           QStatusBar                     │
 * └──────────────────────────────────────────┘
 */
#include "main_window.h"
#include "api/desktop_api.h"
#include "pages/scan_page.h"
#include "pages/dashboard_page.h"
#include "pages/liveview_page.h"
#include "pages/file_browser_page.h"
#include "pages/settings_page.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSplitter>
#include <QFrame>
#include <QDateTime>
#include <QDebug>

MainWindow::MainWindow(DesktopAPI *api, QWidget *parent)
    : QMainWindow(parent)
    , m_api(api)
    , m_connected(false)
{
    setupUi();
    setupStyle();
    setupConnections();
}

MainWindow::~MainWindow() = default;

/* ─── UI 构建 ────────────────────────────────────────────────── */

void MainWindow::setupUi()
{
    auto *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    /* ── 可拖拽分割器 (导航栏 | 页面区) ── */
    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->setHandleWidth(2);
    splitter->setObjectName("mainSplitter");

    /* ── 左侧导航栏 ── */
    auto *navFrame = new QFrame;
    navFrame->setObjectName("navFrame");
    navFrame->setMinimumWidth(48);
    navFrame->setMaximumWidth(300);

    auto *navLayout = new QVBoxLayout(navFrame);
    navLayout->setContentsMargins(0, 0, 0, 0);
    navLayout->setSpacing(0);

    /* Logo 区域 */
    auto *logoWidget = new QWidget;
    logoWidget->setObjectName("logoWidget");
    logoWidget->setFixedHeight(64);
    auto *logoLayout = new QHBoxLayout(logoWidget);
    logoLayout->setContentsMargins(16, 0, 16, 0);

    auto *logoMark = new QLabel("N");
    logoMark->setObjectName("logoMark");
    logoMark->setFixedSize(32, 32);
    logoMark->setAlignment(Qt::AlignCenter);

    auto *logoText = new QLabel("Nikon Connect");
    logoText->setObjectName("logoText");

    logoLayout->addWidget(logoMark);
    logoLayout->addWidget(logoText);
    logoLayout->addStretch();

    /* 导航列表 */
    m_navList = new QListWidget;
    m_navList->setObjectName("navList");
    m_navList->setIconSize(QSize(20, 20));
    m_navList->setSpacing(2);
    m_navList->setFrameShape(QFrame::NoFrame);

    /* 导航项 */
    const QStringList items = {"扫描连接", "相机状态", "实时取景", "文件管理", "相机设置"};

    for (const auto &text : items) {
        auto *listItem = new QListWidgetItem(text);
        listItem->setSizeHint(QSize(0, 44));
        m_navList->addItem(listItem);
    }
    m_navList->setCurrentRow(0);

    navLayout->addWidget(logoWidget);
    navLayout->addWidget(m_navList);
    navLayout->addStretch();

    /* 版本号 */
    auto *versionLabel = new QLabel("v2.0.0");
    versionLabel->setObjectName("versionLabel");
    versionLabel->setAlignment(Qt::AlignCenter);
    versionLabel->setFixedHeight(32);
    navLayout->addWidget(versionLabel);

    /* ── 右侧页面容器 ── */
    m_pages = new QStackedWidget;
    m_pages->setObjectName("pageContainer");

    m_scanPage       = new ScanPage(m_api);
    m_dashboardPage  = new DashboardPage(m_api);
    m_liveViewPage   = new LiveViewPage(m_api);
    m_fileBrowserPage = new FileBrowserPage(m_api);
    m_settingsPage   = new SettingsPage(m_api);

    m_pages->addWidget(m_scanPage);       // index 0
    m_pages->addWidget(m_dashboardPage);  // index 1
    m_pages->addWidget(m_liveViewPage);   // index 2
    m_pages->addWidget(m_fileBrowserPage); // index 3
    m_pages->addWidget(m_settingsPage);   // index 4

    splitter->addWidget(navFrame);
    splitter->addWidget(m_pages);
    splitter->setSizes({160, 740});
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);

    auto *outerLayout = new QVBoxLayout(centralWidget);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);
    outerLayout->addWidget(splitter);

    /* ── 状态栏 ── */
    m_statusIcon = new QLabel("[ ]");
    m_statusIcon->setObjectName("statusIcon");
    m_statusIcon->setFixedWidth(24);
    m_statusIcon->setAlignment(Qt::AlignCenter);

    m_statusText = new QLabel("未连接");
    m_statusText->setObjectName("statusText");

    m_cameraLabel = new QLabel;
    m_cameraLabel->setObjectName("cameraLabel");

    m_batteryLabel = new QLabel;
    m_batteryLabel->setObjectName("batteryLabel");

    statusBar()->addPermanentWidget(m_statusIcon);
    statusBar()->addPermanentWidget(m_statusText, 1);
    statusBar()->addPermanentWidget(m_cameraLabel);
    statusBar()->addPermanentWidget(m_batteryLabel);
    statusBar()->setObjectName("statusBar");
}

/* ─── 样式 ───────────────────────────────────────────────────── */

void MainWindow::setupStyle()
{
    setStyleSheet(R"(
        /* 全局 */
        QMainWindow {
            background-color: #0E0E0E;
        }

        /* 导航栏 */
        #navFrame {
            background-color: #111111;
            border-right: 1px solid rgba(255,255,255,0.06);
        }

        #logoWidget {
            background-color: #0A0A0A;
            border-bottom: 1px solid rgba(245,184,0,0.15);
        }

        #logoMark {
            background-color: #F5B800;
            color: #000000;
            font-size: 18px;
            font-weight: 900;
            border-radius: 6px;
        }

        #logoText {
            color: #F0F0F0;
            font-size: 13px;
            font-weight: 700;
        }

        #versionLabel {
            color: #404040;
            font-size: 10px;
        }

        /* 导航列表 */
        #navList {
            background: transparent;
            color: #808080;
            font-size: 13px;
            padding: 8px 4px;
        }

        #navList::item {
            border-radius: 8px;
            padding: 6px 12px;
            margin: 1px 2px;
        }

        #navList::item:selected {
            background-color: rgba(245,184,0,0.12);
            color: #F5B800;
        }

        #navList::item:hover:!selected {
            background-color: rgba(255,255,255,0.04);
            color: #B0B0B0;
        }

        /* 页面容器 */
        #pageContainer {
            background-color: #0E0E0E;
        }

        /* 状态栏 */
        QStatusBar {
            background-color: #0A0A0A;
            border-top: 1px solid rgba(255,255,255,0.06);
            color: #808080;
            font-size: 11px;
            padding: 2px 12px;
            min-height: 28px;
        }

        #statusIcon {
            color: #E03A3A;
            font-size: 10px;
        }

        #statusIcon[connected="true"] {
            color: #4CAF50;
        }

        #statusText {
            color: #A0A0A0;
        }

        #cameraLabel, #batteryLabel {
            color: #808080;
            padding: 0 8px;
        }

        /* 通用组件 */
        QPushButton {
            background-color: #1E1E1E;
            color: #F0F0F0;
            border: 1px solid rgba(255,255,255,0.08);
            border-radius: 6px;
            padding: 8px 20px;
            font-size: 13px;
            font-weight: 500;
        }

        QPushButton:hover {
            background-color: #2A2A2A;
            border-color: rgba(245,184,0,0.3);
        }

        QPushButton:pressed {
            background-color: #1A1A1A;
        }

        QPushButton#primaryBtn {
            background-color: #F5B800;
            color: #000000;
            font-weight: 700;
            border: none;
        }

        QPushButton#primaryBtn:hover {
            background-color: #E6A800;
        }

        QPushButton#primaryBtn:pressed {
            background-color: #CC9600;
        }

        QPushButton#dangerBtn {
            background-color: rgba(224,58,58,0.15);
            color: #E03A3A;
            border: 1px solid rgba(224,58,58,0.3);
        }

        QPushButton#dangerBtn:hover {
            background-color: rgba(224,58,58,0.25);
        }

        QPushButton:disabled {
            background-color: #161616;
            color: #404040;
            border-color: rgba(255,255,255,0.04);
        }

        QListWidget {
            background-color: #111111;
            border: 1px solid rgba(255,255,255,0.06);
            border-radius: 8px;
            color: #F0F0F0;
            font-size: 13px;
            padding: 4px;
        }

        QListWidget::item {
            border-radius: 6px;
            padding: 10px 14px;
            margin: 2px 0;
        }

        QListWidget::item:selected {
            background-color: rgba(245,184,0,0.1);
            color: #F5B800;
        }

        QListWidget::item:hover:!selected {
            background-color: rgba(255,255,255,0.04);
        }

        QTreeWidget {
            background-color: #111111;
            border: 1px solid rgba(255,255,255,0.06);
            border-radius: 8px;
            color: #F0F0F0;
            font-size: 13px;
            alternate-background-color: #141414;
        }

        QTreeWidget::item {
            padding: 4px 8px;
        }

        QTreeWidget::item:selected {
            background-color: rgba(245,184,0,0.12);
            color: #F5B800;
        }

        QHeaderView::section {
            background-color: #161616;
            color: #808080;
            border: none;
            border-bottom: 1px solid rgba(255,255,255,0.06);
            padding: 6px 12px;
            font-size: 11px;
            font-weight: 600;
            text-transform: uppercase;
        }

        QProgressBar {
            background-color: #1E1E1E;
            border: 1px solid rgba(255,255,255,0.06);
            border-radius: 4px;
            height: 6px;
            text-align: center;
            font-size: 10px;
            color: transparent;
        }

        QProgressBar::chunk {
            background-color: #F5B800;
            border-radius: 3px;
        }

        QComboBox {
            background-color: #1E1E1E;
            color: #F0F0F0;
            border: 1px solid rgba(255,255,255,0.08);
            border-radius: 6px;
            padding: 6px 12px;
            font-size: 13px;
            min-width: 120px;
        }

        QComboBox:hover {
            border-color: rgba(245,184,0,0.3);
        }

        QComboBox QAbstractItemView {
            background-color: #1E1E1E;
            color: #F0F0F0;
            selection-background-color: rgba(245,184,0,0.12);
            selection-color: #F5B800;
            border: 1px solid rgba(255,255,255,0.08);
            border-radius: 4px;
        }

        QSlider::groove:horizontal {
            background: #1E1E1E;
            height: 4px;
            border-radius: 2px;
        }

        QSlider::handle:horizontal {
            background: #F5B800;
            width: 14px;
            height: 14px;
            margin: -5px 0;
            border-radius: 7px;
        }

        QSlider::handle:horizontal:hover {
            background: #E6A800;
        }

        QTabWidget::pane {
            background-color: #111111;
            border: 1px solid rgba(255,255,255,0.06);
            border-radius: 8px;
            top: -1px;
        }

        QTabBar::tab {
            background-color: #161616;
            color: #808080;
            padding: 8px 20px;
            border: 1px solid rgba(255,255,255,0.04);
            border-bottom: none;
            border-top-left-radius: 6px;
            border-top-right-radius: 6px;
            margin-right: 2px;
        }

        QTabBar::tab:selected {
            background-color: #111111;
            color: #F5B800;
        }

        QTabBar::tab:hover:!selected {
            color: #B0B0B0;
        }

        QLabel {
            color: #F0F0F0;
        }

        QScrollBar:vertical {
            background: #0E0E0E;
            width: 8px;
            border-radius: 4px;
        }

        QScrollBar::handle:vertical {
            background: #2A2A2A;
            border-radius: 4px;
            min-height: 30px;
        }

        QScrollBar::handle:vertical:hover {
            background: #3A3A3A;
        }

        QScrollBar::add-line:vertical,
        QScrollBar::sub-line:vertical {
            height: 0;
        }

        /* ── 分割器 ── */
        #mainSplitter::handle {
            background-color: rgba(255,255,255,0.05);
        }
        #mainSplitter::handle:hover {
            background-color: rgba(245,184,0,0.30);
        }
        #mainSplitter::handle:pressed {
            background-color: rgba(245,184,0,0.50);
        }
    )");
}

/* ─── 信号连接 ───────────────────────────────────────────────── */

void MainWindow::setupConnections()
{
    /* 导航切换 */
    connect(m_navList, &QListWidget::currentRowChanged,
            this, &MainWindow::onNavChanged);

    /* API 事件 */
    connect(m_api, &DesktopAPI::connectionStatusChanged,
            this, &MainWindow::onConnectionStatusChanged);
    connect(m_api, &DesktopAPI::connected,
            this, &MainWindow::onConnected);
    connect(m_api, &DesktopAPI::disconnected,
            this, &MainWindow::onDisconnected);

    /* 扫描页 → Dashboard 跳转 */
    connect(m_scanPage, &ScanPage::cameraSelected, this, [this](const QString &id) {
        m_currentCamera = id;
        m_navList->setCurrentRow(1);  // 跳转到仪表盘
    });
}

/* ─── 导航 ───────────────────────────────────────────────────── */

void MainWindow::onNavChanged(int row)
{
    m_pages->setCurrentIndex(row);
}

/* ─── 连接状态 ───────────────────────────────────────────────── */

void MainWindow::onConnectionStatusChanged(int status)
{
    switch (status) {
    case STATUS_DISCONNECTED:
        m_statusText->setText("未连接");
        m_statusIcon->setProperty("connected", false);
        m_statusIcon->style()->unpolish(m_statusIcon);
        m_statusIcon->style()->polish(m_statusIcon);
        m_connected = false;
        break;
    case STATUS_SCANNING:
        m_statusText->setText("扫描中...");
        break;
    case STATUS_CONNECTING:
        m_statusText->setText("连接中...");
        break;
    case STATUS_CONNECTED:
        m_statusText->setText("已连接");
        m_statusIcon->setProperty("connected", true);
        m_statusIcon->style()->unpolish(m_statusIcon);
        m_statusIcon->style()->polish(m_statusIcon);
        m_connected = true;
        break;
    case STATUS_TRANSFERRING:
        m_statusText->setText("传输中...");
        break;
    case STATUS_ERROR:
        m_statusText->setText("错误");
        m_connected = false;
        break;
    }
}

void MainWindow::onConnected()
{
    m_cameraLabel->setText(m_currentCamera);
    m_batteryLabel->setText("BAT --%");
}

void MainWindow::onDisconnected()
{
    m_cameraLabel->setText("");
    m_batteryLabel->setText("");
    m_navList->setCurrentRow(0);  // 回到扫描页
}
