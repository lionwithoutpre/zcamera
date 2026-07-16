"""
desktop/src/py_gui/pages/dashboard_page.py — 相机状态仪表盘
"""
from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout,
    QPushButton, QLabel, QProgressBar, QFrame
)
from PySide6.QtCore import Qt, QTimer

from desktop_api import DesktopAPI


class DashboardPage(QWidget):
    def __init__(self, api: DesktopAPI):
        super().__init__()
        self._api = api
        self._setup_ui()
        self._connect_signals()
        self.setEnabled(False)

    def _setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(32, 24, 32, 24)
        layout.setSpacing(20)

        header = QLabel("相机仪表盘")
        header.setStyleSheet("font-size: 22px; font-weight: 700; color: #F0F0F0;")
        subtitle = QLabel("实时查看相机状态与快速拍摄")
        subtitle.setStyleSheet("font-size: 13px; color: #808080;")
        layout.addWidget(header)
        layout.addWidget(subtitle)

        # ── 信息卡片 ──
        card_row = QHBoxLayout()
        card_row.setSpacing(16)

        def make_card(title: str):
            card = QFrame()
            card.setStyleSheet(
                "QFrame {"
                "  background-color: #111111;"
                "  border: 1px solid rgba(255,255,255,0.06);"
                "  border-radius: 10px;"
                "  padding: 16px;"
                "}"
            )
            cl = QVBoxLayout(card)
            cl.setSpacing(6)
            tl = QLabel(title)
            tl.setStyleSheet("font-size: 11px; color: #616161; font-weight: 600;")
            val = QLabel("--")
            val.setStyleSheet("font-size: 18px; font-weight: 700; color: #F5B800;")
            cl.addWidget(tl)
            cl.addWidget(val)
            return card, val

        c1, self.model_label = make_card("相机型号")
        c2, self.serial_label = make_card("序列号")
        c3, self.shutter_label = make_card("快门计数")
        c4, self.firmware_label = make_card("固件版本")

        card_row.addWidget(c1)
        card_row.addWidget(c2)
        card_row.addWidget(c3)
        card_row.addWidget(c4)
        layout.addLayout(card_row)

        # ── 电量 + 存储 ──
        status_row = QHBoxLayout()
        status_row.setSpacing(16)

        # 电量
        bat_card = QFrame()
        bat_card.setStyleSheet(
            "QFrame { background-color: #111111; border: 1px solid rgba(255,255,255,0.06); border-radius: 10px; padding: 16px; }"
        )
        bcl = QVBoxLayout(bat_card)
        bcl.setSpacing(8)
        bat_title = QLabel("电量")
        bat_title.setStyleSheet("font-size: 13px; font-weight: 600; color: #A0A0A0;")
        self.battery_label = QLabel("--%")
        self.battery_label.setStyleSheet("font-size: 24px; font-weight: 700; color: #F0F0F0;")
        self.battery_bar = QProgressBar()
        self.battery_bar.setRange(0, 100)
        self.battery_bar.setFixedHeight(12)
        self.battery_bar.setStyleSheet(
            "QProgressBar { background-color: #1E1E1E; border-radius: 6px; border: none; }"
            "QProgressBar::chunk { background-color: #4CAF50; border-radius: 5px; }"
        )
        bcl.addWidget(bat_title)
        bcl.addWidget(self.battery_label)
        bcl.addWidget(self.battery_bar)

        # 存储
        sto_card = QFrame()
        sto_card.setStyleSheet(
            "QFrame { background-color: #111111; border: 1px solid rgba(255,255,255,0.06); border-radius: 10px; padding: 16px; }"
        )
        scl = QVBoxLayout(sto_card)
        scl.setSpacing(8)
        sto_title = QLabel("存储空间")
        sto_title.setStyleSheet("font-size: 13px; font-weight: 600; color: #A0A0A0;")
        self.storage_label = QLabel("-- GB / -- GB")
        self.storage_label.setStyleSheet("font-size: 24px; font-weight: 700; color: #F0F0F0;")
        self.storage_bar = QProgressBar()
        self.storage_bar.setRange(0, 100)
        self.storage_bar.setFixedHeight(12)
        scl.addWidget(sto_title)
        scl.addWidget(self.storage_label)
        scl.addWidget(self.storage_bar)

        status_row.addWidget(bat_card, 1)
        status_row.addWidget(sto_card, 1)
        layout.addLayout(status_row)

        # ── 按钮 ──
        action_row = QHBoxLayout()
        action_row.setSpacing(12)

        self.capture_btn = QPushButton("拍摄")
        self.capture_btn.setObjectName("primaryBtn")
        self.capture_btn.setFixedHeight(48)
        self.capture_btn.setCursor(Qt.PointingHandCursor)

        self.af_btn = QPushButton("自动对焦")
        self.af_btn.setFixedHeight(48)
        self.af_btn.setCursor(Qt.PointingHandCursor)

        self.refresh_btn = QPushButton("刷新")
        self.refresh_btn.setFixedHeight(48)
        self.refresh_btn.setCursor(Qt.PointingHandCursor)

        action_row.addWidget(self.capture_btn, 2)
        action_row.addWidget(self.af_btn, 1)
        action_row.addWidget(self.refresh_btn, 1)
        layout.addLayout(action_row)
        self.exposure_info = QLabel("连接相机以调整参数")
        self.exposure_info.setStyleSheet(
            "color: #616161; font-size: 12px; padding: 12px;"
            "background-color: #1A1A1A; border-radius: 6px;"
        )
        layout.addWidget(self.exposure_info)
        layout.addStretch()

        self.capture_btn.clicked.connect(self._on_capture)
        self.af_btn.clicked.connect(lambda: self._api.auto_focus())
        self.refresh_btn.clicked.connect(self._on_refresh)

    def _connect_signals(self):
        self._api.on_connected = self._on_connected
        self._api.on_disconnected = self._on_disconnected
        self._api.on_property_value = self._on_property

    def _on_connected(self):
        self.setEnabled(True)
        # 模拟填充数据
        self.model_label.setText("NIKON Z 8")
        self.serial_label.setText("3001234")
        self.shutter_label.setText("12,450")
        self.firmware_label.setText("C 2.00")
        self.battery_label.setText("85%")
        self.battery_bar.setValue(85)
        self.storage_label.setText("78.0 / 128.0 GB")
        self.storage_bar.setValue(61)

    def _on_disconnected(self):
        self.setEnabled(False)
        for lbl in [self.model_label, self.serial_label, self.shutter_label, self.firmware_label]:
            lbl.setText("--")
        self.battery_label.setText("--%")
        self.battery_bar.setValue(0)
        self.storage_label.setText("-- GB / -- GB")
        self.storage_bar.setValue(0)

    def _on_capture(self):
        self.capture_btn.setEnabled(False)
        self.capture_btn.setText("拍摄中...")
        self._api.capture()
        QTimer.singleShot(1500, lambda: (
            self.capture_btn.setEnabled(True),
            self.capture_btn.setText("拍摄")
        ))

    def _on_refresh(self):
        self._api.get_property(0xD010)
        self._api.get_property(0xD00C)
        self._api.get_property(0xD00E)
        self._api.get_property(0xD00A)

    def _on_property(self, prop_id: int, value: int):
        prop_map = {
            0xD010: ("ISO", self._fmt_iso),
            0xD00C: ("快门", self._fmt_shutter),
            0xD00E: ("光圈", self._fmt_aperture),
            0xD00A: ("白平衡", self._fmt_wb),
        }
        info = prop_map.get(prop_id)
        if info:
            name, fmt_fn = info
            self.exposure_info.setText(f"{name}: {fmt_fn(value)}")

    @staticmethod
    def _fmt_iso(v):
        return f"ISO {v}" if v else "Auto"

    @staticmethod
    def _fmt_shutter(v):
        if v == 0: return "Auto"
        if v < 100: return f"{v}\""
        return f"1/{v}"

    @staticmethod
    def _fmt_aperture(v):
        if v == 0: return "Auto"
        return f"f/{v / 10:.1f}"

    @staticmethod
    def _fmt_wb(v):
        names = {0: "Auto", 1: "晴天", 2: "阴天", 3: "阴影",
                 4: "白炽灯", 5: "荧光灯", 6: "闪光灯", 7: "K值", 8: "自然光Auto"}
        return names.get(v, str(v))
