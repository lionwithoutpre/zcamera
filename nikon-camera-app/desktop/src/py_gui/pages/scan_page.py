"""
desktop/src/py_gui/pages/scan_page.py — 设备扫描与连接页面
"""
from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout,
    QPushButton, QListWidget, QListWidgetItem,
    QLabel, QComboBox, QProgressBar, QFrame
)
from PySide6.QtCore import Qt, Signal

from desktop_api import DesktopAPI, STATUS_CONNECTED, STATUS_ERROR, STATUS_DISCONNECTED

TRANSPORT_AUTO = 0
TRANSPORT_USB_ONLY = 1
TRANSPORT_WIFI_ONLY = 2


class ScanPage(QWidget):
    camera_selected = Signal(str)

    def __init__(self, api: DesktopAPI):
        super().__init__()
        self._api = api
        self._scanning = False
        self._device_ids = []
        self._setup_ui()
        self._connect_signals()

    def _setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(32, 24, 32, 24)
        layout.setSpacing(16)

        # 标题
        header = QLabel("设备扫描与连接")
        header.setStyleSheet("font-size: 22px; font-weight: 700; color: #F0F0F0;")
        subtitle = QLabel("扫描并连接您的尼康相机")
        subtitle.setStyleSheet("font-size: 13px; color: #808080; margin-bottom: 8px;")
        layout.addWidget(header)
        layout.addWidget(subtitle)

        # 传输方式
        transport_row = QHBoxLayout()
        transport_label = QLabel("传输方式")
        transport_label.setStyleSheet("font-size: 13px; color: #A0A0A0; min-width: 80px;")
        self.transport_combo = QComboBox()
        self.transport_combo.addItem("自动 (优先 USB)", TRANSPORT_AUTO)
        self.transport_combo.addItem("仅 USB", TRANSPORT_USB_ONLY)
        self.transport_combo.addItem("仅 Wi-Fi", TRANSPORT_WIFI_ONLY)
        transport_row.addWidget(transport_label)
        transport_row.addWidget(self.transport_combo, 1)
        transport_row.addStretch()
        layout.addLayout(transport_row)

        # 扫描区域
        scan_frame = QFrame()
        scan_frame.setObjectName("scanFrame")
        scan_frame.setStyleSheet(
            "#scanFrame {"
            "  background-color: #111111;"
            "  border: 1px solid rgba(255,255,255,0.06);"
            "  border-radius: 12px;"
            "  padding: 24px;"
            "}"
        )
        scan_layout = QVBoxLayout(scan_frame)
        scan_layout.setSpacing(12)

        self.scan_btn = QPushButton("开始扫描")
        self.scan_btn.setObjectName("primaryBtn")
        self.scan_btn.setFixedHeight(44)
        self.scan_btn.setCursor(Qt.PointingHandCursor)

        self.scan_progress = QProgressBar()
        self.scan_progress.setRange(0, 0)
        self.scan_progress.setFixedHeight(4)
        self.scan_progress.setTextVisible(False)
        self.scan_progress.hide()

        self.status_label = QLabel("点击「开始扫描」搜索可用的尼康相机")
        self.status_label.setStyleSheet("font-size: 12px; color: #616161;")
        self.status_label.setAlignment(Qt.AlignCenter)

        scan_layout.addWidget(self.scan_btn)
        scan_layout.addWidget(self.scan_progress)
        scan_layout.addWidget(self.status_label)
        layout.addWidget(scan_frame)

        # 设备列表
        list_label = QLabel("发现的设备")
        list_label.setStyleSheet("font-size: 14px; font-weight: 600; color: #B0B0B0; margin-top: 8px;")

        self.device_list = QListWidget()
        self.device_list.setMinimumHeight(200)

        empty_item = QListWidgetItem("暂无设备 — 请点击扫描")
        empty_item.setFlags(Qt.NoItemFlags)
        empty_item.setForeground(Qt.gray)
        empty_item.setTextAlignment(Qt.AlignCenter)
        self.device_list.addItem(empty_item)

        self.connect_btn = QPushButton("连接选中设备")
        self.connect_btn.setObjectName("primaryBtn")
        self.connect_btn.setFixedHeight(40)
        self.connect_btn.setEnabled(False)
        self.connect_btn.setCursor(Qt.PointingHandCursor)

        layout.addWidget(list_label)
        layout.addWidget(self.device_list, 1)
        layout.addWidget(self.connect_btn)
        layout.addStretch()

        # 事件
        self.scan_btn.clicked.connect(self._on_scan)
        self.connect_btn.clicked.connect(self._on_connect)
        self.device_list.itemDoubleClicked.connect(self._on_double_click)
        self.device_list.currentRowChanged.connect(
            lambda r: self.connect_btn.setEnabled(r >= 0 and r < len(self._device_ids))
        )

    def _connect_signals(self):
        self._api.on_scan_result = self._on_scan_result
        self._api.on_connection_changed = self._on_status

    def _on_scan(self):
        if self._scanning:
            return
        self._scanning = True
        self.device_list.clear()
        self._device_ids.clear()
        self.scan_btn.setEnabled(False)
        self.scan_btn.setText("扫描中...")
        self.scan_progress.show()
        self.status_label.setText("正在搜索尼康相机 (USB + Wi-Fi)...")
        self.status_label.setStyleSheet("font-size: 12px; color: #F5B800;")
        self._api.scan()

    def _on_scan_result(self, devices):
        self._scanning = False
        self.scan_progress.hide()
        self.scan_btn.setEnabled(True)
        self.scan_btn.setText("重新扫描")
        self.device_list.clear()
        self._device_ids.clear()

        if not devices:
            self.status_label.setText("未发现尼康相机。请确保相机已开启并设为 PTP 模式。")
            self.status_label.setStyleSheet("font-size: 12px; color: #808080;")
            item = QListWidgetItem("未发现设备")
            item.setFlags(Qt.NoItemFlags)
            item.setForeground(Qt.darkGray)
            item.setTextAlignment(Qt.AlignCenter)
            self.device_list.addItem(item)
            self.connect_btn.setEnabled(False)
            return

        self.status_label.setText(f"发现 {len(devices)} 台相机")
        self.status_label.setStyleSheet("font-size: 12px; color: #4CAF50;")

        for d in devices:
            self._device_ids.append(d.id)
            label = f"{d.model}  |  {'USB' if d.transport == 0 else 'Wi-Fi'}  |  {d.serial}  |  BAT {d.battery}%"
            item = QListWidgetItem(label)
            self.device_list.addItem(item)

        if self.device_list.count() > 0:
            self.device_list.setCurrentRow(0)
            self.connect_btn.setEnabled(True)

    def _on_connect(self):
        row = self.device_list.currentRow()
        if row < 0 or row >= len(self._device_ids):
            return
        cam_id = self._device_ids[row]
        self.status_label.setText(f"正在连接 {cam_id}...")
        self.status_label.setStyleSheet("font-size: 12px; color: #F5B800;")
        self.connect_btn.setEnabled(False)
        self.scan_btn.setEnabled(False)
        self.camera_selected.emit(cam_id)
        self._api.connect_to(cam_id)

    def _on_double_click(self, _item):
        self._on_connect()

    def _on_status(self, status: int):
        if status == STATUS_CONNECTED:
            self.status_label.setText("连接成功!")
            self.status_label.setStyleSheet("font-size: 12px; color: #4CAF50;")
        elif status == STATUS_ERROR:
            self.status_label.setText("连接失败，请重试。")
            self.status_label.setStyleSheet("font-size: 12px; color: #E03A3A;")
            self.connect_btn.setEnabled(True)
        elif status == STATUS_DISCONNECTED:
            self.scan_btn.setEnabled(True)
