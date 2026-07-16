"""
desktop/src/py_gui/pages/settings_page.py — 相机设置与 Picture Control
"""
from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QGridLayout, QFormLayout,
    QPushButton, QLabel, QComboBox, QSlider, QTabWidget, QFrame,
    QLineEdit, QSpinBox, QGroupBox
)
from PySide6.QtCore import Qt

from desktop_api import DesktopAPI, PictureControl


class SettingsPage(QWidget):
    def __init__(self, api: DesktopAPI):
        super().__init__()
        self._api = api
        self._updating = False
        self._setup_ui()
        self._connect_signals()
        self.setEnabled(False)

    def _setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(32, 24, 32, 24)
        layout.setSpacing(16)

        header = QLabel("相机设置")
        header.setStyleSheet("font-size: 22px; font-weight: 700; color: #F0F0F0;")
        layout.addWidget(header)

        tabs = QTabWidget()

        # 曝光参数 Tab
        exp_tab = QWidget()
        self._setup_exposure_tab(exp_tab)
        tabs.addTab(exp_tab, "曝光参数")

        # Picture Control Tab
        pc_tab = QWidget()
        self._setup_pc_tab(pc_tab)
        tabs.addTab(pc_tab, "Picture Control")

        # WiFi/FTP 连接 Tab
        conn_tab = QWidget()
        self._setup_connection_tab(conn_tab)
        tabs.addTab(conn_tab, "WiFi / FTP")

        layout.addWidget(tabs, 1)

    def _setup_exposure_tab(self, tab: QWidget):
        layout = QVBoxLayout(tab)
        layout.setContentsMargins(24, 20, 24, 20)
        layout.setSpacing(16)

        form = QFormLayout()
        form.setSpacing(16)
        form.setLabelAlignment(Qt.AlignRight | Qt.AlignVCenter)

        self.iso_combo = QComboBox()
        self.iso_combo.addItem("自动", 0)
        for iso in [64, 100, 200, 400, 800, 1600, 3200, 6400, 12800, 25600, 51200]:
            self.iso_combo.addItem(f"ISO {iso}", iso)
        iso_label = QLabel("ISO 感光度")
        iso_label.setStyleSheet("color: #A0A0A0; font-weight: 500;")
        form.addRow(iso_label, self.iso_combo)

        self.shutter_combo = QComboBox()
        self.shutter_combo.addItem("自动", 0)
        for s in ["30\"", "15\"", "8\"", "4\"", "2\"", "1\""]:
            self.shutter_combo.addItem(s, int(s.replace('"', '')) if '"' in s else int(s))
        for s in [2, 4, 8, 15, 30, 60, 125, 250, 500, 1000, 2000, 4000, 8000]:
            self.shutter_combo.addItem(f"1/{s}", s)
        shut_label = QLabel("快门速度")
        shut_label.setStyleSheet("color: #A0A0A0; font-weight: 500;")
        form.addRow(shut_label, self.shutter_combo)

        self.aperture_combo = QComboBox()
        self.aperture_combo.addItem("自动", 0)
        for f in [1.4, 2.0, 2.8, 4.0, 5.6, 8.0, 11.0, 16.0, 22.0]:
            self.aperture_combo.addItem(f"f/{f:.1f}", int(f * 10))
        ap_label = QLabel("光圈")
        ap_label.setStyleSheet("color: #A0A0A0; font-weight: 500;")
        form.addRow(ap_label, self.aperture_combo)

        self.wb_combo = QComboBox()
        for name, val in [("自动", 0), ("晴天", 1), ("阴天", 2), ("阴影", 3),
                           ("白炽灯", 4), ("荧光灯", 5), ("闪光灯", 6),
                           ("K值设定", 7), ("自然光自动", 8)]:
            self.wb_combo.addItem(name, val)
        wb_label = QLabel("白平衡")
        wb_label.setStyleSheet("color: #A0A0A0; font-weight: 500;")
        form.addRow(wb_label, self.wb_combo)

        layout.addLayout(form)

        self.exposure_info = QLabel("连接相机以调整参数")
        self.exposure_info.setStyleSheet(
            "color: #616161; font-size: 12px; padding: 12px;"
            "background-color: #1A1A1A; border-radius: 6px;"
        )
        layout.addWidget(self.exposure_info)
        layout.addStretch()

        self.iso_combo.currentIndexChanged.connect(lambda i: self._set_if_connected(self._api.set_iso, self.iso_combo.itemData(i)))
        self.shutter_combo.currentIndexChanged.connect(lambda i: self._set_if_connected(self._api.set_shutter, self.shutter_combo.itemData(i)))
        self.aperture_combo.currentIndexChanged.connect(lambda i: self._set_if_connected(self._api.set_aperture, self.aperture_combo.itemData(i)))
        self.wb_combo.currentIndexChanged.connect(lambda i: self._set_if_connected(self._api.set_white_balance, self.wb_combo.itemData(i)))

    def _set_if_connected(self, fn, val):
        if self._updating or not self._api.is_connected():
            return
        if val > 0:
            fn(val)

    def _setup_pc_tab(self, tab: QWidget):
        layout = QVBoxLayout(tab)
        layout.setContentsMargins(24, 20, 24, 20)
        layout.setSpacing(12)

        desc = QLabel("调整相机的 Picture Control 色彩参数。\n滑块范围对应尼康相机设置。")
        desc.setStyleSheet("color: #808080; font-size: 12px; margin-bottom: 8px;")
        desc.setWordWrap(True)
        layout.addWidget(desc)

        grid = QGridLayout()
        grid.setSpacing(10)

        slider_defs = [
            ("色相 Hue",       "hue",        -3, 3, 0),
            ("饱和度 Saturation", "saturation", -3, 3, 0),
            ("对比度 Contrast",  "contrast",   -3, 3, 0),
            ("清晰度 Clarity",   "clarity",    -3, 3, 0),
            ("锐化 Sharpening",  "sharpening",  0, 9, 3),
            ("亮度 Brightness",  "brightness", -1, 1, 0),
            ("WB A-B (琥珀→蓝)", "wb_ab",     -6, 6, 0),
            ("WB G-M (绿→品红)", "wb_gm",     -6, 6, 0),
        ]

        self._sliders = {}
        self._slider_labels = {}

        for i, (name, key, vmin, vmax, default) in enumerate(slider_defs):
            nl = QLabel(name)
            nl.setStyleSheet("color: #A0A0A0; font-size: 12px; font-weight: 500; min-width: 130px;")

            slider = QSlider(Qt.Horizontal)
            slider.setRange(vmin, vmax)
            slider.setValue(default)
            slider.setSingleStep(1)
            slider.setPageStep(1)
            slider.setTickPosition(QSlider.TicksBelow)
            slider.setTickInterval(1)

            vl = QLabel(str(default))
            vl.setStyleSheet("color: #F5B800; font-size: 14px; font-weight: 700; min-width: 30px;")
            vl.setAlignment(Qt.AlignCenter)

            grid.addWidget(nl, i, 0)
            grid.addWidget(slider, i, 1)
            grid.addWidget(vl, i, 2)

            self._sliders[key] = slider
            self._slider_labels[key] = vl

            slider.valueChanged.connect(lambda v, k=key: self._on_slider_changed(k, v))

        layout.addLayout(grid)

        # 色彩空间
        cs_row = QHBoxLayout()
        cs_label = QLabel("色彩空间")
        cs_label.setStyleSheet("color: #A0A0A0; font-size: 12px; font-weight: 500;")
        self.color_space_combo = QComboBox()
        self.color_space_combo.addItem("sRGB", 0)
        self.color_space_combo.addItem("Adobe RGB", 1)
        cs_row.addWidget(cs_label)
        cs_row.addWidget(self.color_space_combo, 1)
        cs_row.addStretch()
        layout.addLayout(cs_row)

        # 按钮
        btn_row = QHBoxLayout()
        btn_row.setSpacing(12)

        self.apply_btn = QPushButton("应用参数")
        self.apply_btn.setObjectName("primaryBtn")
        self.apply_btn.setFixedHeight(40)
        self.apply_btn.setCursor(Qt.PointingHandCursor)

        self.reset_btn = QPushButton("重置")
        self.reset_btn.setFixedHeight(40)
        self.reset_btn.setCursor(Qt.PointingHandCursor)

        self.pc_status = QLabel()
        self.pc_status.setStyleSheet("color: #808080; font-size: 12px;")

        btn_row.addWidget(self.apply_btn)
        btn_row.addWidget(self.reset_btn)
        btn_row.addWidget(self.pc_status, 1)
        layout.addLayout(btn_row)
        layout.addStretch()

        self.apply_btn.clicked.connect(self._on_apply_pc)
        self.reset_btn.clicked.connect(self._on_reset_pc)

    def _on_slider_changed(self, key: str, value: int):
        if key in self._slider_labels:
            label = self._slider_labels[key]
            label.setText(f"+{value}" if value >= 0 else str(value))

    def _connect_signals(self):
        orig_connected = self._api.on_connected
        orig_disconnected = self._api.on_disconnected

        def on_connected():
            if orig_connected:
                orig_connected()
            self._on_connected()

        def on_disconnected():
            if orig_disconnected:
                orig_disconnected()
            self._on_disconnected()

        self._api.on_connected = on_connected
        self._api.on_disconnected = on_disconnected
        self._api.on_picture_control = self._on_pc_result

    def _on_connected(self):
        self.setEnabled(True)
        self.exposure_info.setText("相机已连接 — 调整参数将立即生效")
        self.exposure_info.setStyleSheet(
            "color: #4CAF50; font-size: 12px; padding: 12px;"
            "background-color: rgba(76,175,80,0.1); border-radius: 6px;"
        )
        self._api.get_picture_control()

    def _on_disconnected(self):
        self.setEnabled(False)
        self.exposure_info.setText("连接相机以调整参数")
        self.exposure_info.setStyleSheet(
            "color: #616161; font-size: 12px; padding: 12px;"
            "background-color: #1A1A1A; border-radius: 6px;"
        )

    def _on_pc_result(self, ctrl: PictureControl):
        self._updating = True
        self._sliders["hue"].setValue(ctrl.hue)
        self._sliders["saturation"].setValue(ctrl.saturation)
        self._sliders["contrast"].setValue(ctrl.contrast)
        self._sliders["clarity"].setValue(ctrl.clarity)
        self._sliders["sharpening"].setValue(ctrl.sharpening)
        self._sliders["brightness"].setValue(ctrl.brightness)
        self._sliders["wb_ab"].setValue(ctrl.wb_ab)
        self._sliders["wb_gm"].setValue(ctrl.wb_gm)
        self.color_space_combo.setCurrentIndex(ctrl.color_space)
        self._updating = False
        self.pc_status.setText("✓ 参数已读取")

    def _on_apply_pc(self):
        ctrl = PictureControl(
            hue=self._sliders["hue"].value(),
            saturation=self._sliders["saturation"].value(),
            contrast=self._sliders["contrast"].value(),
            clarity=self._sliders["clarity"].value(),
            sharpening=self._sliders["sharpening"].value(),
            brightness=self._sliders["brightness"].value(),
            wb_ab=self._sliders["wb_ab"].value(),
            wb_gm=self._sliders["wb_gm"].value(),
            color_space=self.color_space_combo.currentData(),
        )
        self._api.set_picture_control(ctrl)
        self.pc_status.setText("✓ 参数已应用")
        self.pc_status.setStyleSheet("color: #4CAF50; font-size: 12px;")

    def _on_reset_pc(self):
        defaults = {"hue": 0, "saturation": 0, "contrast": 0, "clarity": 0,
                     "sharpening": 3, "brightness": 0, "wb_ab": 0, "wb_gm": 0}
        self._updating = True
        for key, val in defaults.items():
            self._sliders[key].setValue(val)
        self._updating = False
        if self._api.is_connected():
            self._on_apply_pc()

    def _setup_connection_tab(self, tab: QWidget):
        layout = QVBoxLayout(tab)
        layout.setContentsMargins(24, 20, 24, 20)
        layout.setSpacing(16)

        # WiFi 组
        wifi_group = QGroupBox("WiFi 连接")
        wifi_group.setStyleSheet(
            "QGroupBox { color: #A0A0A0; font-weight: 600; border: 1px solid #333; "
            "border-radius: 6px; margin-top: 12px; padding-top: 18px; }"
            "QGroupBox::title { subcontrol-origin: margin; left: 12px; }"
        )
        wifi_form = QFormLayout(wifi_group)
        wifi_form.setSpacing(12)

        self.wifi_ip_edit = QLineEdit("192.168.1.1")
        self.wifi_ip_edit.setPlaceholderText("相机 IP 地址")
        wifi_form.addRow("IP 地址:", self.wifi_ip_edit)

        self.wifi_port_spin = QSpinBox()
        self.wifi_port_spin.setRange(1, 65535)
        self.wifi_port_spin.setValue(15740)
        wifi_form.addRow("端口:", self.wifi_port_spin)

        self.wifi_connect_btn = QPushButton("连接 WiFi")
        self.wifi_connect_btn.setObjectName("primaryBtn")
        self.wifi_connect_btn.setFixedHeight(36)
        self.wifi_connect_btn.setCursor(Qt.PointingHandCursor)
        self.wifi_connect_btn.clicked.connect(self._on_wifi_connect)
        wifi_form.addRow(self.wifi_connect_btn)

        self.wifi_status = QLabel("未连接")
        self.wifi_status.setStyleSheet("color: #808080; font-size: 12px;")
        wifi_form.addRow("状态:", self.wifi_status)

        layout.addWidget(wifi_group)

        # FTP 组
        ftp_group = QGroupBox("FTP 传输配置")
        ftp_group.setStyleSheet(
            "QGroupBox { color: #A0A0A0; font-weight: 600; border: 1px solid #333; "
            "border-radius: 6px; margin-top: 12px; padding-top: 18px; }"
            "QGroupBox::title { subcontrol-origin: margin; left: 12px; }"
        )
        ftp_form = QFormLayout(ftp_group)
        ftp_form.setSpacing(12)

        self.ftp_host_edit = QLineEdit()
        self.ftp_host_edit.setPlaceholderText("FTP 服务器地址")
        ftp_form.addRow("主机:", self.ftp_host_edit)

        self.ftp_port_spin = QSpinBox()
        self.ftp_port_spin.setRange(1, 65535)
        self.ftp_port_spin.setValue(21)
        ftp_form.addRow("端口:", self.ftp_port_spin)

        self.ftp_user_edit = QLineEdit()
        self.ftp_user_edit.setPlaceholderText("用户名")
        ftp_form.addRow("用户名:", self.ftp_user_edit)

        self.ftp_pass_edit = QLineEdit()
        self.ftp_pass_edit.setPlaceholderText("密码")
        self.ftp_pass_edit.setEchoMode(QLineEdit.Password)
        ftp_form.addRow("密码:", self.ftp_pass_edit)

        self.ftp_passive_check = QLabel("被动模式 (PASV)")
        self.ftp_passive_check.setStyleSheet("color: #A0A0A0; font-size: 12px;")
        ftp_form.addRow(self.ftp_passive_check)

        self.ftp_save_btn = QPushButton("保存 FTP 配置")
        self.ftp_save_btn.setFixedHeight(36)
        self.ftp_save_btn.setCursor(Qt.PointingHandCursor)
        self.ftp_save_btn.clicked.connect(self._on_ftp_save)
        ftp_form.addRow(self.ftp_save_btn)

        self.ftp_status = QLabel("")
        self.ftp_status.setStyleSheet("color: #808080; font-size: 12px;")
        ftp_form.addRow("状态:", self.ftp_status)

        layout.addWidget(ftp_group)
        layout.addStretch()

    def _on_wifi_connect(self):
        ip = self.wifi_ip_edit.text().strip()
        port = self.wifi_port_spin.value()
        if not ip:
            self.wifi_status.setText("请输入 IP 地址")
            self.wifi_status.setStyleSheet("color: #F44336; font-size: 12px;")
            return
        self.wifi_status.setText("正在连接...")
        self.wifi_status.setStyleSheet("color: #F5B800; font-size: 12px;")
        self.wifi_connect_btn.setEnabled(False)

        orig_on_conn = self._api.on_connected
        orig_on_err = self._api.on_error

        def on_wifi_connected():
            if orig_on_conn:
                orig_on_conn()
            self.wifi_status.setText("已连接")
            self.wifi_status.setStyleSheet("color: #4CAF50; font-size: 12px;")
            self.wifi_connect_btn.setEnabled(True)

        def on_wifi_error(code, msg):
            if orig_on_err:
                orig_on_err(code, msg)
            self.wifi_status.setText(f"连接失败: {msg}")
            self.wifi_status.setStyleSheet("color: #F44336; font-size: 12px;")
            self.wifi_connect_btn.setEnabled(True)

        self._api.on_connected = on_wifi_connected
        self._api.on_error = on_wifi_error
        self._api.connect_wifi(ip, port)

    def _on_ftp_save(self):
        import json, os
        config = {
            "host": self.ftp_host_edit.text().strip(),
            "port": self.ftp_port_spin.value(),
            "user": self.ftp_user_edit.text().strip(),
            "pass_enc": self._ftp_pass_encoded(),
            "passive": True,
        }
        config_dir = os.path.join(os.path.expanduser("~"), ".nikon_camera")
        os.makedirs(config_dir, exist_ok=True)
        config_path = os.path.join(config_dir, "ftp_config.json")
        with open(config_path, "w") as f:
            json.dump(config, f, indent=2)
        os.chmod(config_path, 0o600)
        self.ftp_status.setText("配置已保存")
        self.ftp_status.setStyleSheet("color: #4CAF50; font-size: 12px;")

    def _ftp_pass_encoded(self):
        try:
            from desktop_api import DesktopAPI
            raw = self.ftp_pass_edit.text().encode("utf-8")
            key = b"N1k0nC4m"
            return bytes(b ^ key[i % len(key)] for i, b in enumerate(raw)).hex()
        except Exception:
            return ""
