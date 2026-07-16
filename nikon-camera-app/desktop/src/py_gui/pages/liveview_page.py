"""
desktop/src/py_gui/pages/liveview_page.py — 实时取景页面
"""
from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout,
    QPushButton, QLabel, QFrame
)
from PySide6.QtCore import Qt
from PySide6.QtGui import QPixmap

from desktop_api import DesktopAPI


class LiveViewPage(QWidget):
    def __init__(self, api: DesktopAPI):
        super().__init__()
        self._api = api
        self._live_active = False
        self._setup_ui()
        self._connect_signals()

    def _setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(32, 24, 32, 24)
        layout.setSpacing(16)

        # 标题
        title_row = QHBoxLayout()
        header = QLabel("实时取景")
        header.setStyleSheet("font-size: 22px; font-weight: 700; color: #F0F0F0;")
        self.status_label = QLabel("就绪")
        self.status_label.setStyleSheet(
            "font-size: 12px; color: #616161; padding: 4px 12px;"
            "background-color: #1A1A1A; border-radius: 10px;"
        )
        title_row.addWidget(header)
        title_row.addStretch()
        title_row.addWidget(self.status_label)
        layout.addLayout(title_row)

        # 取景画面
        view_frame = QFrame()
        view_frame.setStyleSheet(
            "QFrame {"
            "  background-color: #000000;"
            "  border: 2px solid rgba(255,255,255,0.08);"
            "  border-radius: 12px;"
            "}"
        )
        view_frame.setMinimumHeight(300)
        view_layout = QVBoxLayout(view_frame)
        view_layout.setContentsMargins(0, 0, 0, 0)

        self.view_label = QLabel("实时取景未激活")
        self.view_label.setAlignment(Qt.AlignCenter)
        self.view_label.setMinimumSize(640, 360)
        self.view_label.setStyleSheet(
            "QLabel { color: #404040; font-size: 48px; background-color: #0A0A0A; border-radius: 10px; }"
        )
        view_layout.addWidget(self.view_label)
        layout.addWidget(view_frame, 1)

        # 按钮
        ctrl_row = QHBoxLayout()
        ctrl_row.setSpacing(12)

        self.live_btn = QPushButton("启动实时取景")
        self.live_btn.setObjectName("primaryBtn")
        self.live_btn.setFixedHeight(44)
        self.live_btn.setCursor(Qt.PointingHandCursor)

        self.capture_btn = QPushButton("拍摄")
        self.capture_btn.setFixedHeight(44)
        self.capture_btn.setCursor(Qt.PointingHandCursor)
        self.capture_btn.setEnabled(False)

        ctrl_row.addWidget(self.live_btn, 2)
        ctrl_row.addWidget(self.capture_btn, 1)
        ctrl_row.addStretch()
        layout.addLayout(ctrl_row)

        self.live_btn.clicked.connect(self._on_toggle)
        self.capture_btn.clicked.connect(lambda: self._api.capture())

    def _connect_signals(self):
        self._api.on_live_view_started = self._on_started
        self._api.on_live_view_stopped = self._on_stopped
        self._api.on_live_view_frame = self._on_frame

    def _on_toggle(self):
        if self._live_active:
            self.live_btn.setEnabled(False)
            self.live_btn.setText("停止中...")
            self._api.stop_live_view()
        else:
            self.live_btn.setEnabled(False)
            self.live_btn.setText("启动中...")
            self.view_label.setText("⏳\n正在连接实时取景...")
            self.view_label.setStyleSheet(
                "QLabel { color: #F5B800; font-size: 24px; background-color: #0A0A0A; border-radius: 10px; }"
            )
            self._api.start_live_view()

    def _on_started(self):
        self._live_active = True
        self.live_btn.setEnabled(True)
        self.live_btn.setText("停止实时取景")
        self.capture_btn.setEnabled(True)
        self.status_label.setText("取景中")
        self.status_label.setStyleSheet(
            "font-size: 12px; color: #4CAF50; padding: 4px 12px;"
            "background-color: rgba(76,175,80,0.12); border-radius: 10px;"
        )

    def _on_stopped(self):
        self._live_active = False
        self.live_btn.setEnabled(True)
        self.live_btn.setText("启动实时取景")
        self.capture_btn.setEnabled(False)
        self.status_label.setText("已停止")
        self.status_label.setStyleSheet(
            "font-size: 12px; color: #616161; padding: 4px 12px;"
            "background-color: #1A1A1A; border-radius: 10px;"
        )
        self.view_label.setText("实时取景未激活")
        self.view_label.setStyleSheet(
            "QLabel { color: #404040; font-size: 48px; background-color: #0A0A0A; border-radius: 10px; }"
        )
        self.view_label.setPixmap(QPixmap())

    def _on_frame(self, jpeg_data: bytes):
        if not self._live_active:
            return
        pixmap = QPixmap()
        if pixmap.loadFromData(jpeg_data):
            scaled = pixmap.scaled(
                self.view_label.size(),
                Qt.KeepAspectRatio,
                Qt.FastTransformation
            )
            self.view_label.setPixmap(scaled)
