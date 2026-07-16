/**
 * desktop/src/ui/pages/liveview_page.cpp — 实时取景页面
 */
#include "liveview_page.h"
#include "../api/desktop_api.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QDebug>

LiveViewPage::LiveViewPage(DesktopAPI *api, QWidget *parent)
    : QWidget(parent), m_api(api), m_liveActive(false)
{
    setupUi();

    connect(m_api, &DesktopAPI::liveViewStarted, this, &LiveViewPage::onLiveViewStarted);
    connect(m_api, &DesktopAPI::liveViewStopped, this, &LiveViewPage::onLiveViewStopped);
    connect(m_api, &DesktopAPI::liveViewFrame, this, &LiveViewPage::onLiveViewFrame);
}

LiveViewPage::~LiveViewPage()
{
    if (m_liveActive) {
        m_api->stopLiveView();
    }
}

void LiveViewPage::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(32, 24, 32, 24);
    layout->setSpacing(16);

    /* 标题行 */
    auto *titleRow = new QHBoxLayout;
    auto *header = new QLabel("实时取景");
    header->setStyleSheet("font-size: 22px; font-weight: 700; color: #F0F0F0;");

    m_statusLabel = new QLabel("就绪");
    m_statusLabel->setStyleSheet("font-size: 12px; color: #616161; padding: 4px 12px;"
                                  "background-color: #1A1A1A; border-radius: 10px;");

    titleRow->addWidget(header);
    titleRow->addStretch();
    titleRow->addWidget(m_statusLabel);

    layout->addLayout(titleRow);

    /* ── 取景画面 ── */
    auto *viewFrame = new QFrame;
    viewFrame->setStyleSheet(
        "QFrame {"
        "  background-color: #000000;"
        "  border: 2px solid rgba(255,255,255,0.08);"
        "  border-radius: 12px;"
        "}"
    );
    viewFrame->setMinimumHeight(300);

    auto *viewLayout = new QVBoxLayout(viewFrame);
    viewLayout->setContentsMargins(0, 0, 0, 0);

    m_viewLabel = new QLabel;
    m_viewLabel->setAlignment(Qt::AlignCenter);
    m_viewLabel->setMinimumSize(640, 360);
    m_viewLabel->setStyleSheet(
        "QLabel {"
        "  color: #404040;"
        "  font-size: 48px;"
        "  background-color: #0A0A0A;"
        "  border-radius: 10px;"
        "}"
    );
    m_viewLabel->setText("实时取景未激活");

    viewLayout->addWidget(m_viewLabel);

    layout->addWidget(viewFrame, 1);

    /* ── 控制按钮 ── */
    auto *ctrlRow = new QHBoxLayout;
    ctrlRow->setSpacing(12);

    m_liveViewBtn = new QPushButton("启动实时取景");
    m_liveViewBtn->setObjectName("primaryBtn");
    m_liveViewBtn->setFixedHeight(44);
    m_liveViewBtn->setCursor(Qt::PointingHandCursor);

    m_captureBtn = new QPushButton("拍摄");
    m_captureBtn->setFixedHeight(44);
    m_captureBtn->setCursor(Qt::PointingHandCursor);
    m_captureBtn->setEnabled(false);

    ctrlRow->addWidget(m_liveViewBtn, 2);
    ctrlRow->addWidget(m_captureBtn, 1);
    ctrlRow->addStretch();

    layout->addLayout(ctrlRow);

    /* 信号 */
    connect(m_liveViewBtn, &QPushButton::clicked, this, &LiveViewPage::onToggleLiveView);
    connect(m_captureBtn, &QPushButton::clicked, this, &LiveViewPage::onCapture);
}

/* ─── 取景控制 ───────────────────────────────────────────────── */

void LiveViewPage::onToggleLiveView()
{
    if (m_liveActive) {
        m_liveViewBtn->setEnabled(false);
        m_liveViewBtn->setText("停止中...");
        m_api->stopLiveView();
    } else {
        m_liveViewBtn->setEnabled(false);
        m_liveViewBtn->setText("启动中...");
        m_viewLabel->setText("正在连接实时取景...");
        m_viewLabel->setStyleSheet(
            "QLabel { color: #F5B800; font-size: 24px; background-color: #0A0A0A; border-radius: 10px; }");
        m_api->startLiveView();
    }
}

void LiveViewPage::onLiveViewStarted()
{
    m_liveActive = true;
    m_liveViewBtn->setEnabled(true);
    m_liveViewBtn->setText("停止实时取景");
    m_captureBtn->setEnabled(true);
    m_statusLabel->setText("取景中");
    m_statusLabel->setStyleSheet("font-size: 12px; color: #4CAF50; padding: 4px 12px;"
                                  "background-color: rgba(76,175,80,0.12); border-radius: 10px;");
}

void LiveViewPage::onLiveViewStopped()
{
    m_liveActive = false;
    m_liveViewBtn->setEnabled(true);
    m_liveViewBtn->setText("启动实时取景");
    m_captureBtn->setEnabled(false);
    m_statusLabel->setText("已停止");
    m_statusLabel->setStyleSheet("font-size: 12px; color: #616161; padding: 4px 12px;"
                                  "background-color: #1A1A1A; border-radius: 10px;");
    m_viewLabel->setText("实时取景未激活");
    m_viewLabel->setStyleSheet(
        "QLabel { color: #404040; font-size: 48px; background-color: #0A0A0A; border-radius: 10px; }");
}

void LiveViewPage::onLiveViewFrame(QByteArray jpegData)
{
    if (!m_liveActive) return;

    QPixmap pixmap;
    if (pixmap.loadFromData(jpegData)) {
        m_currentFrame = pixmap.scaled(m_viewLabel->size(),
                                       Qt::KeepAspectRatio,
                                       Qt::FastTransformation);
        m_viewLabel->setPixmap(m_currentFrame);
    }
}

void LiveViewPage::onCapture()
{
    if (!m_liveActive) return;
    m_api->capture();
}
