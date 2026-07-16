/**
 * desktop/src/ui/pages/settings_page.cpp — 相机设置与 Picture Control
 */
#include "settings_page.h"
#include "../api/desktop_api.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QSplitter>
#include <QInputDialog>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

/* v2:预设数据 */
struct CamPreset {
    QString name;
    QString desc;
    CamPictureControl pc;
    bool active;
};

static QList<CamPreset> g_presets;

SettingsPage::SettingsPage(DesktopAPI *api, QWidget *parent)
    : QWidget(parent), m_api(api), m_settings(new QSettings("Nikon", "NikonConnect", this)),
      m_activePreset(-1), m_connected(false), m_updatingUi(false)
{
    setupUi();
    loadPresets();
    refreshPresetList();

    connect(m_api, &DesktopAPI::connected, this, &SettingsPage::onConnected);
    connect(m_api, &DesktopAPI::disconnected, this, &SettingsPage::onDisconnected);
    connect(m_api, &DesktopAPI::propertyValue, this, &SettingsPage::onPropertyValue);
    connect(m_api, &DesktopAPI::pictureControlResult, this, &SettingsPage::onPictureControlResult);
}

void SettingsPage::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(32, 24, 32, 24);
    layout->setSpacing(16);

    auto *header = new QLabel("相机设置");
    header->setStyleSheet("font-size: 22px; font-weight: 700; color: #F0F0F0;");

    layout->addWidget(header);

    /* ── TabWidget ── */
    m_tabs = new QTabWidget;

    auto *exposureTab = new QWidget;
    setupExposureTab(exposureTab);
    m_tabs->addTab(exposureTab, "曝光参数");

    auto *pcTab = new QWidget;
    setupPictureControlTab(pcTab);
    m_tabs->addTab(pcTab, "Picture Control");

    /* v2:新增预设管理 Tab */
    auto *presetTab = new QWidget;
    setupPresetTab(presetTab);
    m_tabs->addTab(presetTab, "预设管理");

    /* v2:新增应用设置 Tab */
    auto *appTab = new QWidget;
    setupAppSettingsTab(appTab);
    m_tabs->addTab(appTab, "应用设置");

    layout->addWidget(m_tabs, 1);

    /* 曝光/PC/预设 Tab 依赖连接;应用设置 Tab 始终可用 */
    m_tabs->setTabEnabled(0, false);
    m_tabs->setTabEnabled(1, false);
    m_tabs->setTabEnabled(2, false);
    /* index 3 (应用设置) 保持 enabled */
}

/* ─── 曝光参数 Tab ───────────────────────────────────────────── */

void SettingsPage::setupExposureTab(QWidget *tab)
{
    auto *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(16);

    auto *form = new QFormLayout;
    form->setSpacing(16);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    /* ISO */
    m_isoCombo = new QComboBox;
    m_isoCombo->addItem("自动", 0);
    for (int iso : {64, 100, 200, 400, 800, 1600, 3200, 6400, 12800, 25600, 51200}) {
        m_isoCombo->addItem(QString("ISO %1").arg(iso), iso);
    }
    auto *isoLabel = new QLabel("ISO 感光度");
    isoLabel->setStyleSheet("color: #A0A0A0; font-weight: 500;");
    form->addRow(isoLabel, m_isoCombo);

    /* 快门 */
    m_shutterCombo = new QComboBox;
    m_shutterCombo->addItem("自动", 0);
    m_shutterCombo->addItem("30\"", 30);
    m_shutterCombo->addItem("15\"", 15);
    m_shutterCombo->addItem("8\"", 8);
    m_shutterCombo->addItem("4\"", 4);
    m_shutterCombo->addItem("2\"", 2);
    m_shutterCombo->addItem("1\"", 1);
    for (int s : {2, 4, 8, 15, 30, 60, 125, 250, 500, 1000, 2000, 4000, 8000}) {
        m_shutterCombo->addItem(QString("1/%1").arg(s), s);
    }
    auto *shutterLabel = new QLabel("快门速度");
    shutterLabel->setStyleSheet("color: #A0A0A0; font-weight: 500;");
    form->addRow(shutterLabel, m_shutterCombo);

    /* 光圈 */
    m_apertureCombo = new QComboBox;
    m_apertureCombo->addItem("自动", 0);
    for (double f : {1.4, 2.0, 2.8, 4.0, 5.6, 8.0, 11.0, 16.0, 22.0}) {
        m_apertureCombo->addItem(QString("f/%1").arg(f, 0, 'f', 1), (int)(f * 10));
    }
    auto *apertureLabel = new QLabel("光圈");
    apertureLabel->setStyleSheet("color: #A0A0A0; font-weight: 500;");
    form->addRow(apertureLabel, m_apertureCombo);

    /* 白平衡 */
    m_wbCombo = new QComboBox;
    m_wbCombo->addItem("自动", 0);
    m_wbCombo->addItem("晴天", 1);
    m_wbCombo->addItem("阴天", 2);
    m_wbCombo->addItem("阴影", 3);
    m_wbCombo->addItem("白炽灯", 4);
    m_wbCombo->addItem("荧光灯", 5);
    m_wbCombo->addItem("闪光灯", 6);
    m_wbCombo->addItem("K值设定", 7);
    m_wbCombo->addItem("自然光自动", 8);
    auto *wbLabel = new QLabel("白平衡");
    wbLabel->setStyleSheet("color: #A0A0A0; font-weight: 500;");
    form->addRow(wbLabel, m_wbCombo);

    layout->addLayout(form);

    /* 状态提示 */
    m_exposureInfo = new QLabel("连接相机以调整参数");
    m_exposureInfo->setStyleSheet("color: #616161; font-size: 12px; padding: 12px;"
                                   "background-color: #1A1A1A; border-radius: 6px;");
    layout->addWidget(m_exposureInfo);
    layout->addStretch();

    /* 连接信号 */
    connect(m_isoCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsPage::onIsoChanged);
    connect(m_shutterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsPage::onShutterChanged);
    connect(m_apertureCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsPage::onApertureChanged);
    connect(m_wbCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsPage::onWbChanged);
}

/* ─── Picture Control Tab ────────────────────────────────────── */

void SettingsPage::setupPictureControlTab(QWidget *tab)
{
    auto *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(12);

    auto *desc = new QLabel("调整相机的 Picture Control 色彩参数。\n"
                            "滑块范围对应尼康相机 -3 到 +3 的设置。");
    desc->setStyleSheet("color: #808080; font-size: 12px; margin-bottom: 8px;");
    desc->setWordWrap(true);
    layout->addWidget(desc);

    /* ── 滑块网格 ── */
    auto *grid = new QGridLayout;
    grid->setSpacing(10);

    struct SliderDef {
        QString name;
        QSlider **slider;
        QLabel **label;
        int min, max, val, step;
    };

    QList<SliderDef> defs = {
        {"色相 (Hue)",        &m_sliderHue,        &m_labelHue,        -3, 3, 0, 1},
        {"饱和度 (Saturation)", &m_sliderSaturation, &m_labelSaturation, -3, 3, 0, 1},
        {"对比度 (Contrast)",  &m_sliderContrast,   &m_labelContrast,   -3, 3, 0, 1},
        {"清晰度 (Clarity)",   &m_sliderClarity,    &m_labelClarity,    -3, 3, 0, 1},
        {"锐化 (Sharpening)",  &m_sliderSharpening, &m_labelSharpening,  0, 9, 3, 1},
        {"亮度 (Brightness)",  &m_sliderBrightness, &m_labelBrightness, -1, 1, 0, 1},
        {"WB A-B (琥珀→蓝)",  &m_sliderWbAb,       &m_labelWbAb,       -6, 6, 0, 1},
        {"WB G-M (绿→品红)",  &m_sliderWbGm,       &m_labelWbGm,       -6, 6, 0, 1},
    };

    for (int i = 0; i < defs.size(); i++) {
        const auto &d = defs[i];

        auto *nameLabel = new QLabel(d.name);
        nameLabel->setStyleSheet("color: #A0A0A0; font-size: 12px; font-weight: 500; min-width: 130px;");

        *d.slider = new QSlider(Qt::Horizontal);
        (*d.slider)->setRange(d.min, d.max);
        (*d.slider)->setValue(d.val);
        (*d.slider)->setSingleStep(d.step);
        (*d.slider)->setPageStep(1);
        (*d.slider)->setTickPosition(QSlider::TicksBelow);
        (*d.slider)->setTickInterval(1);

        *d.label = new QLabel(QString::number(d.val));
        (*d.label)->setStyleSheet("color: #F5B800; font-size: 14px; font-weight: 700; min-width: 30px;");
        (*d.label)->setAlignment(Qt::AlignCenter);

        grid->addWidget(nameLabel, i, 0);
        grid->addWidget(*d.slider, i, 1);
        grid->addWidget(*d.label, i, 2);

        connect(*d.slider, &QSlider::valueChanged, this, &SettingsPage::onPictureControlSliderChanged);
    }

    layout->addLayout(grid);

    /* 色彩空间 */
    auto *csRow = new QHBoxLayout;
    auto *csLabel = new QLabel("色彩空间");
    csLabel->setStyleSheet("color: #A0A0A0; font-size: 12px; font-weight: 500;");
    m_colorSpaceCombo = new QComboBox;
    m_colorSpaceCombo->addItem("sRGB", 0);
    m_colorSpaceCombo->addItem("Adobe RGB", 1);
    csRow->addWidget(csLabel);
    csRow->addWidget(m_colorSpaceCombo, 1);
    csRow->addStretch();
    connect(m_colorSpaceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsPage::onColorSpaceChanged);

    layout->addLayout(csRow);

    /* 按钮行 */
    auto *btnRow = new QHBoxLayout;
    btnRow->setSpacing(12);

    m_applyBtn = new QPushButton("应用参数");
    m_applyBtn->setObjectName("primaryBtn");
    m_applyBtn->setFixedHeight(40);
    m_applyBtn->setCursor(Qt::PointingHandCursor);

    m_resetBtn = new QPushButton("重置");
    m_resetBtn->setFixedHeight(40);
    m_resetBtn->setCursor(Qt::PointingHandCursor);

    m_pcStatusLabel = new QLabel;
    m_pcStatusLabel->setStyleSheet("color: #808080; font-size: 12px;");

    btnRow->addWidget(m_applyBtn);
    btnRow->addWidget(m_resetBtn);
    btnRow->addWidget(m_pcStatusLabel, 1);

    layout->addLayout(btnRow);
    layout->addStretch();

    connect(m_applyBtn, &QPushButton::clicked, this, &SettingsPage::onPictureControlApply);
    connect(m_resetBtn, &QPushButton::clicked, this, &SettingsPage::onPictureControlReset);
}

/* ─── 连接状态 ───────────────────────────────────────────────── */

void SettingsPage::onConnected()
{
    m_connected = true;
    m_tabs->setTabEnabled(0, true);  /* 曝光 */
    m_tabs->setTabEnabled(1, true);  /* PC */
    m_tabs->setTabEnabled(2, true);  /* 预设 */
    m_exposureInfo->setText("相机已连接 — 调整参数将立即生效");
    m_exposureInfo->setStyleSheet("color: #4CAF50; font-size: 12px; padding: 12px;"
                                   "background-color: rgba(76,175,80,0.1); border-radius: 6px;");

    /* 读取当前 Picture Control */
    refreshPictureControl();
}

void SettingsPage::onDisconnected()
{
    m_connected = false;
    m_tabs->setTabEnabled(0, false);
    m_tabs->setTabEnabled(1, false);
    m_tabs->setTabEnabled(2, false);
    m_exposureInfo->setText("连接相机以调整参数");
    m_exposureInfo->setStyleSheet("color: #616161; font-size: 12px; padding: 12px;"
                                   "background-color: #1A1A1A; border-radius: 6px;");
}

void SettingsPage::onPropertyValue(quint16 propId, quint32 value)
{
    Q_UNUSED(propId);
    Q_UNUSED(value);
}

/* ─── 曝光参数 ───────────────────────────────────────────────── */

void SettingsPage::onIsoChanged(int index)
{
    if (!m_connected || m_updatingUi) return;
    int iso = m_isoCombo->itemData(index).toInt();
    if (iso > 0) m_api->setIso(iso);
}

void SettingsPage::onShutterChanged(int index)
{
    if (!m_connected || m_updatingUi) return;
    int speed = m_shutterCombo->itemData(index).toInt();
    if (speed > 0) m_api->setShutter(speed);
}

void SettingsPage::onApertureChanged(int index)
{
    if (!m_connected || m_updatingUi) return;
    int aperture = m_apertureCombo->itemData(index).toInt();
    if (aperture > 0) m_api->setAperture(aperture);
}

void SettingsPage::onWbChanged(int index)
{
    if (!m_connected || m_updatingUi) return;
    int wb = m_wbCombo->itemData(index).toInt();
    if (wb >= 0) m_api->setWhiteBalance(wb);
}

/* ─── Picture Control ─────────────────────────────────────────── */

void SettingsPage::onPictureControlSliderChanged()
{
    if (m_updatingUi) return;

    QSlider *s = qobject_cast<QSlider *>(sender());
    if (!s) return;

    /* 更新数值标签 */
    QLabel *label = nullptr;
    if (s == m_sliderHue)        label = m_labelHue;
    else if (s == m_sliderSaturation) label = m_labelSaturation;
    else if (s == m_sliderContrast)   label = m_labelContrast;
    else if (s == m_sliderClarity)    label = m_labelClarity;
    else if (s == m_sliderSharpening) label = m_labelSharpening;
    else if (s == m_sliderBrightness) label = m_labelBrightness;
    else if (s == m_sliderWbAb)       label = m_labelWbAb;
    else if (s == m_sliderWbGm)       label = m_labelWbGm;

    if (label) {
        int v = s->value();
        label->setText(v >= 0 ? QString("+%1").arg(v) : QString::number(v));
    }
}

void SettingsPage::onPictureControlReset()
{
    m_updatingUi = true;

    m_sliderHue->setValue(0);
    m_sliderSaturation->setValue(0);
    m_sliderContrast->setValue(0);
    m_sliderClarity->setValue(0);
    m_sliderSharpening->setValue(3);
    m_sliderBrightness->setValue(0);
    m_sliderWbAb->setValue(0);
    m_sliderWbGm->setValue(0);

    m_labelHue->setText("0");
    m_labelSaturation->setText("0");
    m_labelContrast->setText("0");
    m_labelClarity->setText("0");
    m_labelSharpening->setText("3");
    m_labelBrightness->setText("0");
    m_labelWbAb->setText("0");
    m_labelWbGm->setText("0");

    m_updatingUi = false;

    if (m_connected) onPictureControlApply();
}

void SettingsPage::onPictureControlApply()
{
    if (!m_connected) return;

    CamPictureControl ctrl;
    ctrl.hue         = m_sliderHue->value();
    ctrl.saturation  = m_sliderSaturation->value();
    ctrl.contrast    = m_sliderContrast->value();
    ctrl.clarity     = m_sliderClarity->value();
    ctrl.sharpening  = m_sliderSharpening->value();
    ctrl.brightness  = m_sliderBrightness->value();
    ctrl.wbAb        = m_sliderWbAb->value();
    ctrl.wbGm        = m_sliderWbGm->value();
    ctrl.colorSpace  = m_colorSpaceCombo->currentData().toInt();

    m_api->setPictureControl(ctrl);

    m_pcStatusLabel->setText("参数已应用 ✓");
    m_pcStatusLabel->setStyleSheet("color: #4CAF50; font-size: 12px;");
}

void SettingsPage::onPictureControlResult(CamPictureControl ctrl)
{
    m_updatingUi = true;

    m_sliderHue->setValue(ctrl.hue);
    m_sliderSaturation->setValue(ctrl.saturation);
    m_sliderContrast->setValue(ctrl.contrast);
    m_sliderClarity->setValue(ctrl.clarity);
    m_sliderSharpening->setValue(ctrl.sharpening);
    m_sliderBrightness->setValue(ctrl.brightness);
    m_sliderWbAb->setValue(ctrl.wbAb);
    m_sliderWbGm->setValue(ctrl.wbGm);
    m_colorSpaceCombo->setCurrentIndex(ctrl.colorSpace);

    applyPictureControlStyle();
    m_updatingUi = false;
}

void SettingsPage::refreshPictureControl()
{
    if (!m_connected) return;
    m_api->getPictureControl();
}

void SettingsPage::applyPictureControlStyle()
{
    /* 更新所有数值标签 */
    m_labelHue->setText(QString::number(m_sliderHue->value()));
    m_labelSaturation->setText(QString::number(m_sliderSaturation->value()));
    m_labelContrast->setText(QString::number(m_sliderContrast->value()));
    m_labelClarity->setText(QString::number(m_sliderClarity->value()));
    m_labelSharpening->setText(QString::number(m_sliderSharpening->value()));
    m_labelBrightness->setText(QString::number(m_sliderBrightness->value()));
    m_labelWbAb->setText(QString::number(m_sliderWbAb->value()));
    m_labelWbGm->setText(QString::number(m_sliderWbGm->value()));
}

void SettingsPage::onColorSpaceChanged(int index)
{
    Q_UNUSED(index);
    if (!m_connected || m_updatingUi) return;
    /* 色彩空间修改通过 onPictureControlApply 一起发送 */
}

/* ═══════════════════════════════════════════════════════════════
 *  v2:预设管理 Tab
 * ══════════════════════════════════════════════════════════════ */

void SettingsPage::setupPresetTab(QWidget *tab)
{
    auto *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(12);

    auto *desc = new QLabel("保存多组 Picture Control 参数,一键切换风格。\n"
                            "「激活」=加载到滑块预览;「应用到相机」=实际下发。");
    desc->setStyleSheet("color: #808080; font-size: 12px;");
    desc->setWordWrap(true);
    layout->addWidget(desc);

    auto *split = new QSplitter(Qt::Horizontal);

    /* 左:预设列表 */
    auto *leftFrame = new QFrame;
    leftFrame->setStyleSheet("background-color: #111111; border-radius: 8px;");
    auto *leftLayout = new QVBoxLayout(leftFrame);
    leftLayout->setContentsMargins(8, 8, 8, 8);
    auto *leftTitle = new QLabel("预设列表");
    leftTitle->setStyleSheet("color: #616161; font-size: 11px; font-weight: 600;");
    leftLayout->addWidget(leftTitle);

    m_presetList = new QListWidget;
    m_presetList->setStyleSheet(
        "QListWidget { background-color: #0E0E0E; border: none; }"
        "QListWidget::item { padding: 10px 12px; border-radius: 6px; }"
        "QListWidget::item:selected { background-color: rgba(245,184,0,0.12); color: #F5B800; }");
    leftLayout->addWidget(m_presetList);

    auto *btnRow = new QHBoxLayout;
    m_presetSaveBtn = new QPushButton("存为预设");
    m_presetDeleteBtn = new QPushButton("删除");
    m_presetDeleteBtn->setObjectName("dangerBtn");
    btnRow->addWidget(m_presetSaveBtn);
    btnRow->addWidget(m_presetDeleteBtn);
    leftLayout->addLayout(btnRow);

    split->addWidget(leftFrame);

    /* 右:操作区 */
    auto *rightFrame = new QFrame;
    rightFrame->setStyleSheet("background-color: #111111; border-radius: 8px;");
    auto *rightLayout = new QVBoxLayout(rightFrame);
    rightLayout->setContentsMargins(16, 16, 16, 16);
    auto *rightTitle = new QLabel("预设操作");
    rightTitle->setStyleSheet("color: #616161; font-size: 11px; font-weight: 600;");
    rightLayout->addWidget(rightTitle);

    auto *info = new QLabel("选择左侧预设后:\n"
                            "  · 「激活此预设」:载入滑块(不下发相机)\n"
                            "  · 「应用到相机」:载入并下发 PTP 0x90CD");
    info->setStyleSheet("color: #A0A0A0; font-size: 12px;");
    info->setWordWrap(true);
    rightLayout->addWidget(info);

    rightLayout->addStretch();

    m_presetActivateBtn = new QPushButton("激活此预设");
    m_presetActivateBtn->setObjectName("primaryBtn");
    m_presetActivateBtn->setFixedHeight(40);
    m_presetApplyBtn = new QPushButton("应用到相机");
    m_presetApplyBtn->setFixedHeight(40);
    rightLayout->addWidget(m_presetActivateBtn);
    rightLayout->addWidget(m_presetApplyBtn);

    split->addWidget(rightFrame);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 1);

    layout->addWidget(split, 1);

    connect(m_presetList, &QListWidget::currentRowChanged, this, &SettingsPage::onPresetSelected);
    connect(m_presetActivateBtn, &QPushButton::clicked, this, &SettingsPage::onPresetActivate);
    connect(m_presetApplyBtn, &QPushButton::clicked, this, &SettingsPage::onPresetApply);
    connect(m_presetSaveBtn, &QPushButton::clicked, this, &SettingsPage::onPresetSave);
    connect(m_presetDeleteBtn, &QPushButton::clicked, this, &SettingsPage::onPresetDelete);
}

void SettingsPage::loadPresets()
{
    /* 默认 3 个内置预设 */
    if (g_presets.isEmpty()) {
        CamPreset landscape;
        landscape.name = "风景 A";
        landscape.desc = "高饱和高清晰";
        landscape.pc = {0, 2, 1, 2, 4, 0, -2, 0, 0};
        landscape.active = false;
        g_presets.append(landscape);

        CamPreset portrait;
        portrait.name = "人像柔光";
        portrait.desc = "低对比柔肤";
        portrait.pc = {-1, 0, -1, -1, 2, 1, 2, 1, 0};
        portrait.active = false;
        g_presets.append(portrait);

        CamPreset night;
        night.name = "夜景高感";
        night.desc = "降噪高锐化";
        night.pc = {0, -1, 0, 1, 5, 0, 0, 0, 1};
        night.active = false;
        g_presets.append(night);
    }
    /* TODO:从 QSettings 读用户自定义预设 */
}

void SettingsPage::savePresets()
{
    /* 持久化到 QSettings(JSON 序列化) */
    QJsonArray arr;
    for (const auto &p : g_presets) {
        QJsonObject o;
        o["name"] = p.name;
        o["hue"]  = p.pc.hue;
        o["sat"]  = p.pc.saturation;
        o["con"]  = p.pc.contrast;
        o["cla"]  = p.pc.clarity;
        o["sha"]  = p.pc.sharpening;
        o["bri"]  = p.pc.brightness;
        o["ab"]   = p.pc.wbAb;
        o["gm"]   = p.pc.wbGm;
        o["cs"]   = p.pc.colorSpace;
        arr.append(o);
    }
    m_settings->setValue("presets", QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

void SettingsPage::refreshPresetList()
{
    m_presetList->clear();
    for (int i = 0; i < g_presets.size(); i++) {
        const auto &p = g_presets[i];
        QString label = p.name;
        if (p.active) label += "  [已激活]";
        label += QString("\n%1").arg(p.desc);
        auto *item = new QListWidgetItem(label);
        if (p.active) {
            item->setForeground(QColor("#F5B800"));
        }
        m_presetList->addItem(item);
    }
}

void SettingsPage::onPresetSelected()
{
    int row = m_presetList->currentRow();
    m_presetActivateBtn->setEnabled(row >= 0);
    m_presetApplyBtn->setEnabled(row >= 0 && m_connected);
}

void SettingsPage::onPresetActivate()
{
    int row = m_presetList->currentRow();
    if (row < 0 || row >= g_presets.size()) return;

    m_updatingUi = true;
    const auto &pc = g_presets[row].pc;
    m_sliderHue->setValue(pc.hue);
    m_sliderSaturation->setValue(pc.saturation);
    m_sliderContrast->setValue(pc.contrast);
    m_sliderClarity->setValue(pc.clarity);
    m_sliderSharpening->setValue(pc.sharpening);
    m_sliderBrightness->setValue(pc.brightness);
    m_sliderWbAb->setValue(pc.wbAb);
    m_sliderWbGm->setValue(pc.wbGm);
    m_colorSpaceCombo->setCurrentIndex(pc.colorSpace);
    applyPictureControlStyle();
    m_updatingUi = false;

    /* 切换激活标记 */
    for (int i = 0; i < g_presets.size(); i++) g_presets[i].active = (i == row);
    m_activePreset = row;
    refreshPresetList();
    m_pcStatusLabel->setText(QString("已激活预设: %1 ✓").arg(g_presets[row].name));
    m_pcStatusLabel->setStyleSheet("color: #4CAF50; font-size: 12px;");
}

void SettingsPage::onPresetApply()
{
    if (!m_connected) return;
    onPresetActivate();  /* 先激活 */
    onPictureControlApply();  /* 再下发 */
}

void SettingsPage::onPresetSave()
{
    bool ok;
    QString name = QInputDialog::getText(this, "保存预设", "预设名称:", QLineEdit::Normal, "", &ok);
    if (!ok || name.isEmpty()) return;

    CamPreset p;
    p.name = name;
    p.desc = "自定义";
    p.pc.hue         = m_sliderHue->value();
    p.pc.saturation  = m_sliderSaturation->value();
    p.pc.contrast    = m_sliderContrast->value();
    p.pc.clarity     = m_sliderClarity->value();
    p.pc.sharpening  = m_sliderSharpening->value();
    p.pc.brightness  = m_sliderBrightness->value();
    p.pc.wbAb        = m_sliderWbAb->value();
    p.pc.wbGm        = m_sliderWbGm->value();
    p.pc.colorSpace  = m_colorSpaceCombo->currentIndex();
    p.active = false;
    g_presets.append(p);
    savePresets();
    refreshPresetList();
}

void SettingsPage::onPresetDelete()
{
    int row = m_presetList->currentRow();
    if (row < 0 || row >= g_presets.size()) return;
    auto reply = QMessageBox::question(this, "删除预设",
        QString("确定删除预设「%1」?").arg(g_presets[row].name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        g_presets.removeAt(row);
        savePresets();
        refreshPresetList();
    }
}

/* ═══════════════════════════════════════════════════════════════
 *  v2:应用设置 Tab
 * ══════════════════════════════════════════════════════════════ */

void SettingsPage::setupAppSettingsTab(QWidget *tab)
{
    auto *layout = new QVBoxLayout(tab);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(16);

    /* 传输设置组 */
    auto *transferGroup = new QGroupBox("传输设置");
    transferGroup->setStyleSheet(
        "QGroupBox { color: #F5B800; font-size: 12px; font-weight: 700; border: 1px solid rgba(255,255,255,0.08);"
        "  border-radius: 8px; margin-top: 12px; padding-top: 12px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px; }");
    auto *tForm = new QFormLayout(transferGroup);
    tForm->setSpacing(10);

    m_chkAutoTransfer = new QCheckBox("边拍边传(新文件自动传输)");
    m_chkResume = new QCheckBox("断点续传(中断后自动恢复)");
    m_concurrencySpin = new QSpinBox;
    m_concurrencySpin->setRange(1, 8);
    m_concurrencySpin->setValue(3);
    tForm->addRow(m_chkAutoTransfer);
    tForm->addRow(m_chkResume);
    tForm->addRow("并发任务数:", m_concurrencySpin);
    layout->addWidget(transferGroup);

    /* 格式过滤组 */
    auto *fmtGroup = new QGroupBox("文件格式过滤");
    fmtGroup->setStyleSheet(transferGroup->styleSheet());
    auto *fForm = new QVBoxLayout(fmtGroup);
    m_chkJpeg = new QCheckBox("JPEG");
    m_chkJpeg->setChecked(true);
    m_chkNef = new QCheckBox("NEF (RAW)");
    m_chkNef->setChecked(true);
    m_chkMov = new QCheckBox("MOV (视频)");
    fForm->addWidget(m_chkJpeg);
    fForm->addWidget(m_chkNef);
    fForm->addWidget(m_chkMov);
    layout->addWidget(fmtGroup);

    /* 连接偏好组 */
    auto *connGroup = new QGroupBox("连接偏好");
    connGroup->setStyleSheet(transferGroup->styleSheet());
    auto *cForm = new QVBoxLayout(connGroup);
    m_chkPreferUsb = new QCheckBox("优先 USB 连接(USB 可用时自动切换)");
    m_chkPreferUsb->setChecked(true);
    cForm->addWidget(m_chkPreferUsb);
    layout->addWidget(connGroup);

    /* FTP 设置组 */
    auto *ftpGroup = new QGroupBox("FTP 设置");
    ftpGroup->setStyleSheet(transferGroup->styleSheet());
    auto *ftpForm = new QVBoxLayout(ftpGroup);
    m_chkFtps = new QCheckBox("FTPS 加密(TLS)");
    m_chkFtps->setChecked(true);
    m_chkFtpAuto = new QCheckBox("传输完成后自动上传至 FTP");
    ftpForm->addWidget(m_chkFtps);
    ftpForm->addWidget(m_chkFtpAuto);
    layout->addWidget(ftpGroup);

    /* 通知设置组 */
    auto *notifGroup = new QGroupBox("通知设置");
    notifGroup->setStyleSheet(transferGroup->styleSheet());
    auto *nForm = new QVBoxLayout(notifGroup);
    m_chkNotifyComplete = new QCheckBox("传输完成通知");
    m_chkNotifyComplete->setChecked(true);
    m_chkNotifyFail = new QCheckBox("传输失败通知");
    m_chkNotifyFail->setChecked(true);
    nForm->addWidget(m_chkNotifyComplete);
    nForm->addWidget(m_chkNotifyFail);
    layout->addWidget(notifGroup);

    layout->addStretch();

    /* 从 QSettings 读回 */
    m_chkAutoTransfer->setChecked(m_settings->value("app/autoTransfer", true).toBool());
    m_chkResume->setChecked(m_settings->value("app/resumeTransfer", true).toBool());
    m_concurrencySpin->setValue(m_settings->value("app/concurrentJobs", 3).toInt());
    m_chkJpeg->setChecked(m_settings->value("app/formatJpg", true).toBool());
    m_chkNef->setChecked(m_settings->value("app/formatNef", true).toBool());
    m_chkMov->setChecked(m_settings->value("app/formatMov", false).toBool());
    m_chkPreferUsb->setChecked(m_settings->value("app/preferUsb", true).toBool());
    m_chkFtps->setChecked(m_settings->value("app/ftps", true).toBool());
    m_chkFtpAuto->setChecked(m_settings->value("app/ftpAuto", false).toBool());
    m_chkNotifyComplete->setChecked(m_settings->value("app/notifyComplete", true).toBool());
    m_chkNotifyFail->setChecked(m_settings->value("app/notifyFail", true).toBool());

    /* 全部 checkbox/spinbox 变更 → 持久化 */
    auto persist = [this]() { onAppSettingChanged(); };
    for (auto *cb : {m_chkAutoTransfer, m_chkResume, m_chkJpeg, m_chkNef, m_chkMov,
                     m_chkPreferUsb, m_chkFtps, m_chkFtpAuto,
                     m_chkNotifyComplete, m_chkNotifyFail}) {
        connect(cb, &QCheckBox::toggled, this, persist);
    }
    connect(m_concurrencySpin, QOverload<int>::of(&QSpinBox::valueChanged), this, persist);
}

void SettingsPage::onAppSettingChanged()
{
    m_settings->setValue("app/autoTransfer", m_chkAutoTransfer->isChecked());
    m_settings->setValue("app/resumeTransfer", m_chkResume->isChecked());
    m_settings->setValue("app/concurrentJobs", m_concurrencySpin->value());
    m_settings->setValue("app/formatJpg", m_chkJpeg->isChecked());
    m_settings->setValue("app/formatNef", m_chkNef->isChecked());
    m_settings->setValue("app/formatMov", m_chkMov->isChecked());
    m_settings->setValue("app/preferUsb", m_chkPreferUsb->isChecked());
    m_settings->setValue("app/ftps", m_chkFtps->isChecked());
    m_settings->setValue("app/ftpAuto", m_chkFtpAuto->isChecked());
    m_settings->setValue("app/notifyComplete", m_chkNotifyComplete->isChecked());
    m_settings->setValue("app/notifyFail", m_chkNotifyFail->isChecked());
}
