/**
 * desktop/src/ui/pages/dashboard_page.cpp — 相机状态仪表盘
 */
#include "dashboard_page.h"
#include "../api/desktop_api.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFrame>
#include <QDebug>

DashboardPage::DashboardPage(DesktopAPI *api, QWidget *parent)
    : QWidget(parent), m_api(api), m_connected(false)
{
    setupUi();

    connect(m_api, &DesktopAPI::connected, this, &DashboardPage::onConnected);
    connect(m_api, &DesktopAPI::disconnected, this, &DashboardPage::onDisconnected);
    connect(m_api, &DesktopAPI::propertyValue, this, &DashboardPage::onPropertyValue);

    /* 初始禁用 */
    setEnabled(false);
}

void DashboardPage::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(32, 24, 32, 24);
    layout->setSpacing(20);

    /* 标题 */
    auto *header = new QLabel("相机仪表盘");
    header->setStyleSheet("font-size: 22px; font-weight: 700; color: #F0F0F0;");

    auto *subtitle = new QLabel("实时查看相机状态与快速拍摄");
    subtitle->setStyleSheet("font-size: 13px; color: #808080;");

    layout->addWidget(header);
    layout->addWidget(subtitle);

    /* ── 信息卡片行 ── */
    auto *cardRow = new QHBoxLayout;
    cardRow->setSpacing(16);

    auto makeCard = [](const QString &title, QLabel *&valueLabel, const QString &initial = "--") -> QFrame* {
        auto *card = new QFrame;
        card->setStyleSheet(
            "QFrame {"
            "  background-color: #111111;"
            "  border: 1px solid rgba(255,255,255,0.06);"
            "  border-radius: 10px;"
            "  padding: 16px;"
            "}"
        );
        auto *cl = new QVBoxLayout(card);
        cl->setSpacing(6);
        auto *tl = new QLabel(title);
        tl->setStyleSheet("font-size: 11px; color: #616161; font-weight: 600; text-transform: uppercase;");
        valueLabel = new QLabel(initial);
        valueLabel->setStyleSheet("font-size: 18px; font-weight: 700; color: #F5B800;");
        cl->addWidget(tl);
        cl->addWidget(valueLabel);
        return card;
    };

    cardRow->addWidget(makeCard("相机型号", m_modelLabel));
    cardRow->addWidget(makeCard("序列号", m_serialLabel));
    cardRow->addWidget(makeCard("快门计数", m_shutterLabel, "0"));
    cardRow->addWidget(makeCard("固件版本", m_firmwareLabel));

    layout->addLayout(cardRow);

    /* ── 电量 + 存储行 ── */
    auto *statusRow = new QHBoxLayout;
    statusRow->setSpacing(16);

    /* 电量卡片 */
    auto *batteryCard = new QFrame;
    batteryCard->setStyleSheet(
        "QFrame {"
        "  background-color: #111111;"
        "  border: 1px solid rgba(255,255,255,0.06);"
        "  border-radius: 10px;"
        "  padding: 16px;"
        "}"
    );
    auto *bcl = new QVBoxLayout(batteryCard);
    bcl->setSpacing(8);
    auto *batteryTitle = new QLabel("电量");
    batteryTitle->setStyleSheet("font-size: 13px; font-weight: 600; color: #A0A0A0;");

    m_batteryBar = new QProgressBar;
    m_batteryBar->setRange(0, 100);
    m_batteryBar->setValue(0);
    m_batteryBar->setFixedHeight(12);
    m_batteryBar->setStyleSheet(
        "QProgressBar { background-color: #1E1E1E; border-radius: 6px; border: none; }"
        "QProgressBar::chunk { background-color: #4CAF50; border-radius: 5px; }"
    );

    m_batteryLabel = new QLabel("--%");
    m_batteryLabel->setStyleSheet("font-size: 24px; font-weight: 700; color: #F0F0F0;");

    bcl->addWidget(batteryTitle);
    bcl->addWidget(m_batteryLabel);
    bcl->addWidget(m_batteryBar);

    /* 存储卡片 */
    auto *storageCard = new QFrame;
    storageCard->setStyleSheet(
        "QFrame {"
        "  background-color: #111111;"
        "  border: 1px solid rgba(255,255,255,0.06);"
        "  border-radius: 10px;"
        "  padding: 16px;"
        "}"
    );
    auto *scl = new QVBoxLayout(storageCard);
    scl->setSpacing(8);
    auto *storageTitle = new QLabel("存储空间");
    storageTitle->setStyleSheet("font-size: 13px; font-weight: 600; color: #A0A0A0;");

    m_storageBar = new QProgressBar;
    m_storageBar->setRange(0, 100);
    m_storageBar->setValue(0);
    m_storageBar->setFixedHeight(12);

    m_storageLabel = new QLabel("-- GB / -- GB");
    m_storageLabel->setStyleSheet("font-size: 24px; font-weight: 700; color: #F0F0F0;");

    scl->addWidget(storageTitle);
    scl->addWidget(m_storageLabel);
    scl->addWidget(m_storageBar);

    statusRow->addWidget(batteryCard, 1);
    statusRow->addWidget(storageCard, 1);

    layout->addLayout(statusRow);

    /* ── 操作按钮行 ── */
    auto *actionRow = new QHBoxLayout;
    actionRow->setSpacing(12);

    m_captureBtn = new QPushButton("拍摄");
    m_captureBtn->setObjectName("primaryBtn");
    m_captureBtn->setFixedHeight(48);
    m_captureBtn->setCursor(Qt::PointingHandCursor);

    m_afBtn = new QPushButton("自动对焦");
    m_afBtn->setFixedHeight(48);
    m_afBtn->setCursor(Qt::PointingHandCursor);

    m_refreshBtn = new QPushButton("刷新");
    m_refreshBtn->setFixedHeight(48);
    m_refreshBtn->setCursor(Qt::PointingHandCursor);

    actionRow->addWidget(m_captureBtn, 2);
    actionRow->addWidget(m_afBtn, 1);
    actionRow->addWidget(m_refreshBtn, 1);

    layout->addLayout(actionRow);

    /* ── v2:拍摄参数网格(6 格,轮询刷新)── */
    auto *paramTitle = new QLabel("当前拍摄参数");
    paramTitle->setStyleSheet("font-size: 12px; color: #616161; font-weight: 600; margin-top: 8px;");
    layout->addWidget(paramTitle);

    auto *paramGrid = new QGridLayout;
    paramGrid->setSpacing(8);

    auto makeParamCell = [](const QString &title, QLabel *&valueLabel) {
        auto *cell = new QFrame;
        cell->setStyleSheet(
            "QFrame { background-color: #111111; border: 1px solid rgba(255,255,255,0.06);"
            "  border-radius: 8px; padding: 10px; }");
        auto *cl = new QVBoxLayout(cell);
        cl->setSpacing(2);
        cl->setAlignment(Qt::AlignCenter);
        auto *tl = new QLabel(title);
        tl->setStyleSheet("font-size: 10px; color: #616161; font-weight: 600;");
        tl->setAlignment(Qt::AlignCenter);
        valueLabel = new QLabel("--");
        valueLabel->setStyleSheet("font-size: 15px; font-weight: 700; color: #F5B800;");
        valueLabel->setAlignment(Qt::AlignCenter);
        cl->addWidget(tl);
        cl->addWidget(valueLabel);
        return cell;
    };

    paramGrid->addWidget(makeParamCell("快门", m_paramShutter),  0, 0);
    paramGrid->addWidget(makeParamCell("光圈", m_paramAperture), 0, 1);
    paramGrid->addWidget(makeParamCell("ISO",  m_paramIso),      0, 2);
    paramGrid->addWidget(makeParamCell("EV",   m_paramEv),       1, 0);
    paramGrid->addWidget(makeParamCell("对焦", m_paramFocus),    1, 1);
    paramGrid->addWidget(makeParamCell("画质", m_paramQuality),  1, 2);

    layout->addLayout(paramGrid);
    layout->addStretch();

    /* v2:参数轮询定时器 */
    m_propTimer = new QTimer(this);
    m_propTimer->setInterval(2000);
    connect(m_propTimer, &QTimer::timeout, this, &DashboardPage::onRefreshInfo);

    /* 信号 */
    connect(m_captureBtn, &QPushButton::clicked, this, &DashboardPage::onCapture);
    connect(m_afBtn, &QPushButton::clicked, this, &DashboardPage::onAutoFocus);
    connect(m_refreshBtn, &QPushButton::clicked, this, &DashboardPage::onRefreshInfo);
}

/* ─── 操作 ───────────────────────────────────────────────────── */

void DashboardPage::onConnected()
{
    m_connected = true;
    setEnabled(true);
    onRefreshInfo();
    m_propTimer->start();  /* v2:启动 2s 轮询 */
}

void DashboardPage::onDisconnected()
{
    m_connected = false;
    setEnabled(false);
    m_propTimer->stop();  /* v2:停止轮询 */
    m_modelLabel->setText("--");
    m_serialLabel->setText("--");
    m_shutterLabel->setText("--");
    m_firmwareLabel->setText("--");
    m_batteryLabel->setText("--%");
    m_batteryBar->setValue(0);
    m_storageLabel->setText("-- GB / -- GB");
    m_storageBar->setValue(0);
    /* 清空参数 */
    m_paramShutter->setText("--");
    m_paramAperture->setText("--");
    m_paramIso->setText("--");
    m_paramEv->setText("--");
    m_paramFocus->setText("--");
    m_paramQuality->setText("--");
}

void DashboardPage::onCapture()
{
    if (!m_connected) return;
    m_captureBtn->setEnabled(false);
    m_captureBtn->setText("拍摄中...");
    m_api->capture();

    /* 1s 后恢复 */
    QTimer::singleShot(1500, this, [this]() {
        m_captureBtn->setEnabled(true);
        m_captureBtn->setText("拍摄");
    });
}

void DashboardPage::onAutoFocus()
{
    if (!m_connected) return;
    m_api->autoFocus();
}

void DashboardPage::onRefreshInfo()
{
    if (!m_connected) return;
    /* v2:轮询 6 个常用属性(与 Android 6 格网格对齐) */
    m_api->getProperty(0xD00C);  // Shutter
    m_api->getProperty(0xD00E);  // Aperture
    m_api->getProperty(0xD010);  // ISO
    m_api->getProperty(0xD012);  // ExposureComp
    m_api->getProperty(0xD014);  // FocusMode
    m_api->getProperty(0xD01A);  // ImageQuality
}

void DashboardPage::onPropertyValue(quint16 propId, quint32 value)
{
    /* v2:把属性值显示到参数网格 */
    switch (propId) {
    case 0xD00C: {  /* Shutter */
        if (value == 0) m_paramShutter->setText("Bulb");
        else if (value > 0) m_paramShutter->setText(QString("1/%1s").arg(value));
        else m_paramShutter->setText(QString("%1s").arg(value));
        break;
    }
    case 0xD00E:  /* Aperture */
        m_paramAperture->setText(QString("f/%1").arg(value / 10.0, 0, 'f', 1));
        break;
    case 0xD010:  /* ISO */
        m_paramIso->setText(QString("ISO %1").arg(value));
        break;
    case 0xD012:  /* EV */
        m_paramEv->setText(QString("%1EV").arg(value / 10.0, 0, 'f', 1));
        break;
    case 0xD014:  /* FocusMode */
        switch ((int)value) {
        case 0: m_paramFocus->setText("MF"); break;
        case 1: m_paramFocus->setText("AF-S"); break;
        case 2: m_paramFocus->setText("AF-C"); break;
        case 3: m_paramFocus->setText("AF-F"); break;
        default: m_paramFocus->setText("AF"); break;
        }
        break;
    case 0xD01A:  /* ImageQuality */
        switch ((int)value) {
        case 0: m_paramQuality->setText("RAW"); break;
        case 1: m_paramQuality->setText("JPEG"); break;
        case 2: m_paramQuality->setText("RAW+JPEG"); break;
        case 3: m_paramQuality->setText("TIFF"); break;
        default: m_paramQuality->setText("RAW"); break;
        }
        break;
    }
}

void DashboardPage::updateCameraInfo(const CamDevice &info)
{
    m_modelLabel->setText(info.model);
    m_serialLabel->setText(info.serial);
    m_shutterLabel->setText(QString::number(info.shutterCount));
    m_firmwareLabel->setText("--");

    /* 电量 */
    m_batteryLabel->setText(QString("%1%").arg(info.battery));
    m_batteryBar->setValue(info.battery);
    if (info.battery < 20)
        m_batteryBar->setStyleSheet(
            "QProgressBar { background-color: #1E1E1E; border-radius: 6px; border: none; }"
            "QProgressBar::chunk { background-color: #E03A3A; border-radius: 5px; }");

    /* 存储 */
    double usedGB  = (double)(info.storageTotal - info.storageFree) / 1e9;
    double totalGB = (double)info.storageTotal / 1e9;
    m_storageLabel->setText(QString("%1 / %2 GB").arg(usedGB, 0, 'f', 1).arg(totalGB, 0, 'f', 1));
    if (info.storageTotal > 0)
        m_storageBar->setValue((int)((info.storageTotal - info.storageFree) * 100 / info.storageTotal));
}

