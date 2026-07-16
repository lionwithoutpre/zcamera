/**
 * desktop/src/ui/pages/scan_page.cpp — 设备扫描与连接页面
 */
#include "scan_page.h"
#include "../api/desktop_api.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QDebug>
#include <QTimer>

ScanPage::ScanPage(DesktopAPI *api, QWidget *parent)
    : QWidget(parent), m_api(api), m_scanning(false)
{
    setupUi();

    connect(m_api, &DesktopAPI::scanResult, this, &ScanPage::onScanResult);
    connect(m_api, &DesktopAPI::connectionStatusChanged,
            this, &ScanPage::onConnectionStatusChanged);
}

void ScanPage::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(32, 24, 32, 24);
    layout->setSpacing(16);

    /* 标题 */
    auto *header = new QLabel("设备扫描与连接");
    header->setObjectName("pageHeader");
    header->setStyleSheet("font-size: 22px; font-weight: 700; color: #F0F0F0;");

    auto *subtitle = new QLabel("扫描并连接您的尼康相机");
    subtitle->setStyleSheet("font-size: 13px; color: #808080; margin-bottom: 8px;");

    layout->addWidget(header);
    layout->addWidget(subtitle);

    /* ── 传输方式选择 ── */
    auto *transportRow = new QHBoxLayout;

    auto *transportLabel = new QLabel("传输方式");
    transportLabel->setStyleSheet("font-size: 13px; color: #A0A0A0; min-width: 80px;");

    m_transportCombo = new QComboBox;
    m_transportCombo->addItem("自动 (优先 USB)", TRANSPORT_AUTO);
    m_transportCombo->addItem("仅 USB", TRANSPORT_USB_ONLY);
    m_transportCombo->addItem("仅 Wi-Fi", TRANSPORT_WIFI_ONLY);

    transportRow->addWidget(transportLabel);
    transportRow->addWidget(m_transportCombo, 1);
    transportRow->addStretch();

    layout->addLayout(transportRow);

    /* ── 扫描区域 ── */
    auto *scanFrame = new QFrame;
    scanFrame->setObjectName("scanFrame");
    scanFrame->setStyleSheet(
        "#scanFrame {"
        "  background-color: #111111;"
        "  border: 1px solid rgba(255,255,255,0.06);"
        "  border-radius: 12px;"
        "  padding: 24px;"
        "}"
    );

    auto *scanLayout = new QVBoxLayout(scanFrame);
    scanLayout->setSpacing(12);

    /* 扫描按钮 */
    m_scanBtn = new QPushButton("开始扫描");
    m_scanBtn->setObjectName("primaryBtn");
    m_scanBtn->setFixedHeight(44);
    m_scanBtn->setCursor(Qt::PointingHandCursor);

    m_scanProgress = new QProgressBar;
    m_scanProgress->setRange(0, 0);  // 不确定模式
    m_scanProgress->setFixedHeight(4);
    m_scanProgress->setTextVisible(false);
    m_scanProgress->hide();

    m_statusLabel = new QLabel("点击「开始扫描」搜索可用的尼康相机");
    m_statusLabel->setStyleSheet("font-size: 12px; color: #616161;");
    m_statusLabel->setAlignment(Qt::AlignCenter);

    scanLayout->addWidget(m_scanBtn);
    scanLayout->addWidget(m_scanProgress);
    scanLayout->addWidget(m_statusLabel);

    layout->addWidget(scanFrame);

    /* ── 设备列表 ── */
    auto *listLabel = new QLabel("发现的设备");
    listLabel->setStyleSheet("font-size: 14px; font-weight: 600; color: #B0B0B0; margin-top: 8px;");

    m_deviceList = new QListWidget;
    m_deviceList->setMinimumHeight(200);
    m_deviceList->setStyleSheet(
        "QListWidget {"
        "  background-color: #111111;"
        "  border: 1px solid rgba(255,255,255,0.08);"
        "  border-radius: 10px;"
        "  padding: 4px;"
        "}"
        "QListWidget::item {"
        "  padding: 14px 16px;"
        "  border-radius: 8px;"
        "  margin: 2px 0;"
        "}"
        "QListWidget::item:selected {"
        "  background-color: rgba(245,184,0,0.1);"
        "  border: 1px solid rgba(245,184,0,0.25);"
        "}"
    );

    /* 空状态占位 */
    auto *emptyItem = new QListWidgetItem("暂无设备 — 请点击扫描");
    emptyItem->setFlags(emptyItem->flags() & ~Qt::ItemIsSelectable);
    emptyItem->setForeground(QColor("#404040"));
    emptyItem->setTextAlignment(Qt::AlignCenter);
    m_deviceList->addItem(emptyItem);

    /* 连接按钮 */
    m_connectBtn = new QPushButton("连接选中设备");
    m_connectBtn->setObjectName("primaryBtn");
    m_connectBtn->setFixedHeight(40);
    m_connectBtn->setEnabled(false);
    m_connectBtn->setCursor(Qt::PointingHandCursor);

    layout->addWidget(listLabel);
    layout->addWidget(m_deviceList, 1);
    layout->addWidget(m_connectBtn);
    layout->addStretch();

    /* 信号 */
    connect(m_scanBtn, &QPushButton::clicked, this, &ScanPage::onScanClicked);
    connect(m_connectBtn, &QPushButton::clicked, this, &ScanPage::onConnectClicked);
    connect(m_deviceList, &QListWidget::itemDoubleClicked,
            this, &ScanPage::onDeviceDoubleClicked);
    connect(m_deviceList, &QListWidget::currentRowChanged, this, [this](int row) {
        m_connectBtn->setEnabled(row >= 0 && row < m_deviceIds.size());
    });
}

/* ─── 扫描 ───────────────────────────────────────────────────── */

void ScanPage::onScanClicked()
{
    if (m_scanning) return;

    m_scanning = true;
    m_deviceList->clear();
    m_deviceIds.clear();

    m_scanBtn->setEnabled(false);
    m_scanBtn->setText("扫描中...");
    m_scanProgress->show();
    m_statusLabel->setText("正在搜索尼康相机 (USB + Wi-Fi)...");
    m_statusLabel->setStyleSheet("font-size: 12px; color: #F5B800;");

    m_api->scan();
}

void ScanPage::onScanResult(QList<CamDevice> devices)
{
    m_scanning = false;
    m_scanProgress->hide();
    m_scanBtn->setEnabled(true);
    m_scanBtn->setText("重新扫描");

    m_deviceList->clear();
    m_deviceIds.clear();

    if (devices.isEmpty()) {
        m_statusLabel->setText("未发现尼康相机。请确保相机已开启并设为 PTP 模式。");
        m_statusLabel->setStyleSheet("font-size: 12px; color: #808080;");

        auto *emptyItem = new QListWidgetItem("未发现设备");
        emptyItem->setFlags(Qt::NoItemFlags);
        emptyItem->setForeground(QColor("#404040"));
        emptyItem->setTextAlignment(Qt::AlignCenter);
        m_deviceList->addItem(emptyItem);
        m_connectBtn->setEnabled(false);
        return;
    }

    m_statusLabel->setText(QString("发现 %1 台相机").arg(devices.size()));
    m_statusLabel->setStyleSheet("font-size: 12px; color: #4CAF50;");

    for (const auto &d : devices) {
        m_deviceIds.append(d.id);

        QString label = QString("%1  |  %2  |  %3  |  BAT %4%")
                            .arg(d.model)
                            .arg(d.transport == 0 ? "USB" : "Wi-Fi")
                            .arg(d.serial)
                            .arg(d.battery);

        auto *item = new QListWidgetItem(label);
        item->setToolTip(QString(
            "型号: %1\n序列号: %2\n接口: %3\n电量: %4%%\n快门: %5\n存储: %6 / %7 GB")
            .arg(d.model)
            .arg(d.serial)
            .arg(d.transport == 0 ? "USB" : "Wi-Fi")
            .arg(d.battery)
            .arg(d.shutterCount)
            .arg((double)(d.storageTotal - d.storageFree) / 1e9, 0, 'f', 1)
            .arg((double)d.storageTotal / 1e9, 0, 'f', 1));

        m_deviceList->addItem(item);
    }

    /* 自动选中第一台 */
    if (m_deviceList->count() > 0) {
        m_deviceList->setCurrentRow(0);
        m_connectBtn->setEnabled(true);
    }
}

/* ─── 连接 ───────────────────────────────────────────────────── */

void ScanPage::onConnectClicked()
{
    int row = m_deviceList->currentRow();
    if (row < 0 || row >= m_deviceIds.size()) return;

    QString id = m_deviceIds[row];
    m_statusLabel->setText(QString("正在连接 %1...").arg(id));
    m_statusLabel->setStyleSheet("font-size: 12px; color: #F5B800;");
    m_connectBtn->setEnabled(false);
    m_scanBtn->setEnabled(false);

    emit cameraSelected(id);
    m_api->connectTo(id);
}

void ScanPage::onDeviceDoubleClicked(QListWidgetItem *item)
{
    Q_UNUSED(item);
    onConnectClicked();
}

void ScanPage::onConnectionStatusChanged(int status)
{
    if (status == STATUS_CONNECTED) {
        m_statusLabel->setText("连接成功!");
        m_statusLabel->setStyleSheet("font-size: 12px; color: #4CAF50;");
    } else if (status == STATUS_ERROR) {
        m_statusLabel->setText("连接失败，请重试。");
        m_statusLabel->setStyleSheet("font-size: 12px; color: #E03A3A;");
        m_connectBtn->setEnabled(true);
    } else if (status == STATUS_DISCONNECTED) {
        m_scanBtn->setEnabled(true);
    }
}
