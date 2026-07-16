/**
 * desktop/src/ui/pages/settings_page.h — 相机设置与 Picture Control
 */
#ifndef NIKON_SETTINGS_PAGE_H
#define NIKON_SETTINGS_PAGE_H

#include <QWidget>
#include <QTabWidget>
#include <QComboBox>
#include <QSlider>
#include <QSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QCheckBox>
#include <QSettings>

class DesktopAPI;

class SettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit SettingsPage(DesktopAPI *api, QWidget *parent = nullptr);

public slots:
    void onConnected();
    void onDisconnected();
    void onPropertyValue(quint16 propId, quint32 value);
    void onPictureControlResult(struct CamPictureControl ctrl);

private slots:
    /* 曝光参数 */
    void onIsoChanged(int index);
    void onShutterChanged(int index);
    void onApertureChanged(int index);
    void onWbChanged(int index);

    /* Picture Control */
    void onPictureControlSliderChanged();
    void onPictureControlReset();
    void onPictureControlApply();
    void onColorSpaceChanged(int index);

    /* v2:预设管理 */
    void onPresetSelected();
    void onPresetActivate();
    void onPresetApply();
    void onPresetSave();
    void onPresetDelete();

    /* v2:应用设置 */
    void onAppSettingChanged();

private:
    void setupUi();
    void setupExposureTab(QWidget *tab);
    void setupPictureControlTab(QWidget *tab);
    void setupPresetTab(QWidget *tab);
    void setupAppSettingsTab(QWidget *tab);
    void refreshPictureControl();
    void applyPictureControlStyle();
    void loadPresets();
    void savePresets();
    void refreshPresetList();

    DesktopAPI *m_api;
    QSettings  *m_settings;
    QTabWidget *m_tabs;

    /* 曝光 Tab */
    QComboBox  *m_isoCombo;
    QComboBox  *m_shutterCombo;
    QComboBox  *m_apertureCombo;
    QComboBox  *m_wbCombo;
    QLabel     *m_exposureInfo;

    /* Picture Control Tab */
    QSlider    *m_sliderHue;
    QSlider    *m_sliderSaturation;
    QSlider    *m_sliderContrast;
    QSlider    *m_sliderClarity;
    QSlider    *m_sliderSharpening;
    QSlider    *m_sliderBrightness;
    QSlider    *m_sliderWbAb;
    QSlider    *m_sliderWbGm;
    QComboBox  *m_colorSpaceCombo;
    QPushButton *m_applyBtn;
    QPushButton *m_resetBtn;
    QLabel     *m_pcStatusLabel;

    /* 滑块值标签 */
    QLabel     *m_labelHue;
    QLabel     *m_labelSaturation;
    QLabel     *m_labelContrast;
    QLabel     *m_labelClarity;
    QLabel     *m_labelSharpening;
    QLabel     *m_labelBrightness;
    QLabel     *m_labelWbAb;
    QLabel     *m_labelWbGm;

    /* v2:预设 Tab */
    QListWidget *m_presetList;
    QPushButton *m_presetActivateBtn;
    QPushButton *m_presetApplyBtn;
    QPushButton *m_presetSaveBtn;
    QPushButton *m_presetDeleteBtn;
    int          m_activePreset;

    /* v2:应用设置 Tab */
    QCheckBox  *m_chkAutoTransfer;
    QSpinBox   *m_concurrencySpin;
    QCheckBox  *m_chkJpeg;
    QCheckBox  *m_chkNef;
    QCheckBox  *m_chkMov;
    QCheckBox  *m_chkResume;
    QCheckBox  *m_chkFtps;
    QCheckBox  *m_chkFtpAuto;
    QCheckBox  *m_chkPreferUsb;
    QCheckBox  *m_chkNotifyComplete;
    QCheckBox  *m_chkNotifyFail;

    bool        m_connected;
    bool        m_updatingUi;  // 防止循环更新
};

#endif // NIKON_SETTINGS_PAGE_H
