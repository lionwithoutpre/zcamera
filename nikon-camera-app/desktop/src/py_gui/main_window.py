"""
desktop/src/py_gui/main_window.py — Qt 主窗口

左侧导航栏 + QStackedWidget 页面容器，Nikon 黑黄配色 QSS。
"""
from PySide6.QtWidgets import (
    QMainWindow, QWidget, QHBoxLayout, QVBoxLayout,
    QListWidget, QListWidgetItem, QStackedWidget,
    QLabel, QStatusBar, QFrame, QSplitter, QSizePolicy
)
from PySide6.QtCore import Qt, QSize
from PySide6.QtGui import QFont

from desktop_api import DesktopAPI, STATUS_CONNECTED, STATUS_DISCONNECTED

from pages.scan_page import ScanPage
from pages.dashboard_page import DashboardPage
from pages.liveview_page import LiveViewPage
from pages.file_browser_page import FileBrowserPage
from pages.settings_page import SettingsPage


STYLESHEET = """
/* ── 全局 ── */
QMainWindow {
    background-color: #0E0E0E;
}

/* ── 导航栏 ── */
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

/* ── 导航列表 ── */
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

/* ── 页面容器 ── */
#pageContainer {
    background-color: #0E0E0E;
}

/* ── 状态栏 ── */
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

#statusText {
    color: #A0A0A0;
}

#cameraLabel, #batteryLabel {
    color: #808080;
    padding: 0 8px;
}

/* ── 通用组件 ── */
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
"""


class MainWindow(QMainWindow):
    def __init__(self, api: DesktopAPI):
        super().__init__()
        self._api = api
        self._current_camera = ""
        self._setup_ui()
        self._apply_style()
        self._connect_signals()

    def _setup_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        # ── 可拖拽分割器 (导航栏 | 页面区) ──
        splitter = QSplitter(Qt.Horizontal)
        splitter.setHandleWidth(2)
        splitter.setObjectName("mainSplitter")

        # ── 左侧导航栏 ──
        nav_frame = QFrame()
        nav_frame.setObjectName("navFrame")
        nav_frame.setMinimumWidth(48)
        nav_frame.setMaximumWidth(300)

        nav_layout = QVBoxLayout(nav_frame)
        nav_layout.setContentsMargins(0, 0, 0, 0)
        nav_layout.setSpacing(0)

        # Logo
        logo_widget = QWidget()
        logo_widget.setObjectName("logoWidget")
        logo_widget.setFixedHeight(64)
        logo_layout = QHBoxLayout(logo_widget)
        logo_layout.setContentsMargins(16, 0, 16, 0)

        logo_mark = QLabel("N")
        logo_mark.setObjectName("logoMark")
        logo_mark.setFixedSize(32, 32)
        logo_mark.setAlignment(Qt.AlignCenter)

        logo_text = QLabel("Nikon Connect")
        logo_text.setObjectName("logoText")

        logo_layout.addWidget(logo_mark)
        logo_layout.addWidget(logo_text)
        logo_layout.addStretch()

        # 导航列表
        self.nav_list = QListWidget()
        self.nav_list.setObjectName("navList")
        self.nav_list.setIconSize(QSize(20, 20))
        self.nav_list.setSpacing(2)
        self.nav_list.setFrameShape(QFrame.NoFrame)

        nav_items = [
            ("扫描连接", "scan"),
            ("相机状态", "dashboard"),
            ("实时取景", "liveview"),
            ("文件管理", "files"),
            ("相机设置", "settings"),
        ]
        for text, _ in nav_items:
            item = QListWidgetItem(text)
            item.setSizeHint(QSize(0, 44))
            self.nav_list.addItem(item)

        self.nav_list.setCurrentRow(0)

        nav_layout.addWidget(logo_widget)
        nav_layout.addWidget(self.nav_list)
        nav_layout.addStretch()

        version_label = QLabel("v1.0.0")
        version_label.setObjectName("versionLabel")
        version_label.setAlignment(Qt.AlignCenter)
        version_label.setFixedHeight(32)
        nav_layout.addWidget(version_label)

        # ── 右侧页面容器 ──
        self.pages = QStackedWidget()
        self.pages.setObjectName("pageContainer")

        self.scan_page = ScanPage(self._api)
        self.dashboard_page = DashboardPage(self._api)
        self.liveview_page = LiveViewPage(self._api)
        self.file_browser_page = FileBrowserPage(self._api)
        self.settings_page = SettingsPage(self._api)

        self.pages.addWidget(self.scan_page)
        self.pages.addWidget(self.dashboard_page)
        self.pages.addWidget(self.liveview_page)
        self.pages.addWidget(self.file_browser_page)
        self.pages.addWidget(self.settings_page)

        splitter.addWidget(nav_frame)
        splitter.addWidget(self.pages)
        splitter.setSizes([160, 740])
        splitter.setStretchFactor(0, 0)
        splitter.setStretchFactor(1, 1)

        main_layout = QVBoxLayout(central)
        main_layout.setContentsMargins(0, 0, 0, 0)
        main_layout.setSpacing(0)
        main_layout.addWidget(splitter)

        # ── 状态栏 ──
        self.status_icon = QLabel("[ ]")
        self.status_icon.setObjectName("statusIcon")
        self.status_icon.setFixedWidth(24)
        self.status_icon.setAlignment(Qt.AlignCenter)

        self.status_text = QLabel("未连接")
        self.status_text.setObjectName("statusText")

        self.camera_label = QLabel()
        self.camera_label.setObjectName("cameraLabel")

        self.battery_label = QLabel()
        self.battery_label.setObjectName("batteryLabel")

        status_bar = self.statusBar()
        status_bar.setObjectName("statusBar")
        status_bar.addPermanentWidget(self.status_icon)
        status_bar.addPermanentWidget(self.status_text, 1)
        status_bar.addPermanentWidget(self.camera_label)
        status_bar.addPermanentWidget(self.battery_label)

    def _apply_style(self):
        self.setStyleSheet(STYLESHEET)

    def _connect_signals(self):
        self.nav_list.currentRowChanged.connect(self._on_nav_changed)

        # 扫描页 → 跳转
        self.scan_page.camera_selected.connect(self._on_camera_selected)

        # API 回调
        api = self._api
        api.on_connection_changed = self._on_status_changed
        api.on_connected = self._on_connected
        api.on_disconnected = self._on_disconnected

    def _on_nav_changed(self, row: int):
        self.pages.setCurrentIndex(row)

    def _on_camera_selected(self, camera_id: str):
        self._current_camera = camera_id
        self.nav_list.setCurrentRow(1)

    def _on_status_changed(self, status: int):
        status_map = {
            STATUS_DISCONNECTED: ("未连接", "#E03A3A"),
            1: ("扫描中...", "#F5B800"),
            2: ("连接中...", "#F5B800"),
            STATUS_CONNECTED: ("已连接", "#4CAF50"),
            4: ("传输中...", "#F5B800"),
            5: ("错误", "#E03A3A"),
        }
        text, color = status_map.get(status, ("未知", "#808080"))
        self.status_text.setText(text)
        self.status_icon.setStyleSheet(f"color: {color}; font-size: 10px;")
        self.status_icon.repaint()

    def _on_connected(self):
        self.camera_label.setText(self._current_camera.split("_")[1] if "_" in self._current_camera else self._current_camera)
        self.battery_label.setText("BAT --%")

    def _on_disconnected(self):
        self.camera_label.setText("")
        self.battery_label.setText("")
        self.nav_list.setCurrentRow(0)
