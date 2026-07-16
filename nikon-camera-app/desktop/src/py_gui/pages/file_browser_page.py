"""
desktop/src/py_gui/pages/file_browser_page.py — 文件浏览与传输
"""
import os
from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout,
    QPushButton, QLabel, QComboBox, QProgressBar,
    QTreeWidget, QTreeWidgetItem, QSplitter,
    QFileDialog, QMessageBox, QHeaderView
)
from PySide6.QtCore import Qt, QTimer
from PySide6.QtGui import QPixmap

from desktop_api import DesktopAPI, NIKON_STORAGE_CF, NIKON_STORAGE_SD


class FileBrowserPage(QWidget):
    def __init__(self, api: DesktopAPI):
        super().__init__()
        self._api = api
        self._files = []
        self._active_transfers = 0
        self._setup_ui()
        self._connect_signals()

    def _setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(32, 24, 32, 24)
        layout.setSpacing(12)

        header = QLabel("文件管理")
        header.setStyleSheet("font-size: 22px; font-weight: 700; color: #F0F0F0;")
        layout.addWidget(header)

        # 工具栏
        toolbar = QHBoxLayout()
        toolbar.setSpacing(8)

        storage_label = QLabel("存储卡:")
        storage_label.setStyleSheet("color: #A0A0A0; font-size: 12px;")
        self.storage_combo = QComboBox()
        self.storage_combo.addItem("全部", 0)
        self.storage_combo.addItem("CF 卡", NIKON_STORAGE_CF)
        self.storage_combo.addItem("SD 卡", NIKON_STORAGE_SD)

        self.refresh_btn = QPushButton("刷新")
        self.refresh_btn.setCursor(Qt.PointingHandCursor)

        self.download_btn = QPushButton("下载选中")
        self.download_btn.setObjectName("primaryBtn")
        self.download_btn.setEnabled(False)
        self.download_btn.setCursor(Qt.PointingHandCursor)

        self.download_all_btn = QPushButton("下载全部")
        self.download_all_btn.setCursor(Qt.PointingHandCursor)

        self.delete_btn = QPushButton("删除")
        self.delete_btn.setObjectName("dangerBtn")
        self.delete_btn.setEnabled(False)
        self.delete_btn.setCursor(Qt.PointingHandCursor)

        self.upload_btn = QPushButton("上传到相机")
        self.upload_btn.setCursor(Qt.PointingHandCursor)

        toolbar.addWidget(storage_label)
        toolbar.addWidget(self.storage_combo)
        toolbar.addSpacing(12)
        toolbar.addWidget(self.refresh_btn)
        toolbar.addStretch()
        toolbar.addWidget(self.upload_btn)
        toolbar.addWidget(self.download_btn)
        toolbar.addWidget(self.download_all_btn)
        toolbar.addWidget(self.delete_btn)
        layout.addLayout(toolbar)

        # 文件表格 + 预览
        self.splitter = QSplitter(Qt.Horizontal)
        self.splitter.setHandleWidth(1)

        self.file_tree = QTreeWidget()
        self.file_tree.setColumnCount(5)
        self.file_tree.setHeaderLabels(["文件名", "大小", "日期", "格式", "分辨率"])
        self.file_tree.setAlternatingRowColors(True)
        self.file_tree.setRootIsDecorated(False)
        self.file_tree.setSelectionMode(QTreeWidget.ExtendedSelection)
        self.file_tree.header().setSectionResizeMode(0, QHeaderView.Stretch)
        self.file_tree.header().setSectionResizeMode(1, QHeaderView.ResizeToContents)
        self.file_tree.header().setSectionResizeMode(2, QHeaderView.ResizeToContents)
        self.file_tree.header().setSectionResizeMode(3, QHeaderView.ResizeToContents)
        self.file_tree.header().setSectionResizeMode(4, QHeaderView.ResizeToContents)

        # 预览面板
        preview_panel = QWidget()
        preview_panel.setFixedWidth(260)
        preview_panel.setStyleSheet("background-color: #111111; border-radius: 8px;")
        preview_layout = QVBoxLayout(preview_panel)
        preview_layout.setContentsMargins(12, 16, 12, 16)
        preview_layout.setSpacing(8)

        preview_title = QLabel("预览")
        preview_title.setStyleSheet("font-size: 12px; font-weight: 600; color: #616161;")

        self.preview_label = QLabel("选择文件\n查看预览")
        self.preview_label.setAlignment(Qt.AlignCenter)
        self.preview_label.setMinimumHeight(160)
        self.preview_label.setStyleSheet(
            "QLabel { background-color: #0A0A0A; border: 1px solid rgba(255,255,255,0.06);"
            " border-radius: 6px; color: #404040; font-size: 14px; }"
        )

        self.file_info_label = QLabel()
        self.file_info_label.setStyleSheet("font-size: 11px; color: #808080;")
        self.file_info_label.setWordWrap(True)

        preview_layout.addWidget(preview_title)
        preview_layout.addWidget(self.preview_label)
        preview_layout.addWidget(self.file_info_label)
        preview_layout.addStretch()

        self.splitter.addWidget(self.file_tree)
        self.splitter.addWidget(preview_panel)
        self.splitter.setStretchFactor(0, 3)
        self.splitter.setStretchFactor(1, 1)
        layout.addWidget(self.splitter, 1)

        # 传输进度
        progress_frame = QWidget()
        progress_frame.setStyleSheet("background-color: #111111; border-radius: 8px; padding: 8px;")
        progress_layout = QHBoxLayout(progress_frame)
        progress_layout.setContentsMargins(16, 10, 16, 10)
        progress_layout.setSpacing(12)

        self.transfer_progress = QProgressBar()
        self.transfer_progress.setRange(0, 100)
        self.transfer_progress.setFixedHeight(8)

        self.transfer_speed = QLabel("0 MB/s")
        self.transfer_speed.setStyleSheet("color: #F5B800; font-size: 12px; font-weight: 600; min-width: 60px;")

        self.transfer_status = QLabel("就绪")
        self.transfer_status.setStyleSheet("color: #808080; font-size: 12px; min-width: 80px;")

        progress_layout.addWidget(self.transfer_progress, 1)
        progress_layout.addWidget(self.transfer_speed)
        progress_layout.addWidget(self.transfer_status)
        layout.addWidget(progress_frame)

        # 信号
        self.refresh_btn.clicked.connect(self._on_refresh)
        self.download_btn.clicked.connect(self._on_download)
        self.download_all_btn.clicked.connect(self._on_download_all)
        self.delete_btn.clicked.connect(self._on_delete)
        self.upload_btn.clicked.connect(self._on_upload)
        self.file_tree.itemSelectionChanged.connect(self._on_select)
        self.storage_combo.currentIndexChanged.connect(self._on_refresh)

    def _connect_signals(self):
        self._api.on_file_list = self._on_file_list
        self._api.on_thumbnail = self._on_thumbnail
        self._api.on_transfer_progress = self._on_progress
        self._api.on_transfer_complete = self._on_transfer_done
        self._api.on_new_file = self._on_new_file

    def _on_refresh(self):
        self.file_tree.clear()
        self._files.clear()
        self.preview_label.setText("加载中...")
        storage_id = self.storage_combo.currentData()
        self._api.list_files(storage_id)

    def _on_select(self):
        items = self.file_tree.selectedItems()
        self.download_btn.setEnabled(len(items) > 0)
        self.delete_btn.setEnabled(len(items) > 0)

        if not items:
            self.preview_label.setText("选择文件\n查看预览")
            self.file_info_label.setText("")
            return

        idx = items[0].data(0, Qt.UserRole)
        if idx is None or idx >= len(self._files):
            return

        f = self._files[idx]
        self.file_info_label.setText(
            f"文件名: {f.filename}\n"
            f"大小: {f.size / 1e6:.1f} MB\n"
            f"日期: {f.datetime}\n"
            f"格式: {'RAW' if f.is_raw else ('JPEG' if f.is_jpeg else '?')}\n"
            f"分辨率: {f.width}×{f.height}"
        )
        self.preview_label.setText("加载预览...")
        self._api.get_thumbnail(f.handle)

    def _on_file_list(self, files):
        self._files = files
        self.file_tree.clear()

        if not files:
            self.preview_label.setText("存储卡为空")
            self.download_all_btn.setEnabled(False)
            return

        self.download_all_btn.setEnabled(True)

        for i, f in enumerate(files):
            item = QTreeWidgetItem()
            item.setText(0, f.filename)
            item.setText(1, f"{f.size / 1e6:.1f} MB")
            item.setText(2, f.datetime)
            item.setText(3, "RAW" if f.is_raw else ("JPEG" if f.is_jpeg else "?"))
            item.setText(4, f"{f.width}×{f.height}")
            item.setData(0, Qt.UserRole, i)
            if f.is_raw:
                item.setForeground(3, Qt.GlobalColor(0xFF9300))  # orange for RAW
            self.file_tree.addTopLevelItem(item)

        self.preview_label.setText(f"共 {len(files)} 个文件\n选择文件查看预览")

    def _on_thumbnail(self, handle: int, jpeg_data: bytes):
        pixmap = QPixmap()
        if pixmap.loadFromData(jpeg_data):
            scaled = pixmap.scaled(self.preview_label.size(), Qt.KeepAspectRatio, Qt.SmoothTransformation)
            self.preview_label.setPixmap(scaled)

    def _on_download(self):
        items = self.file_tree.selectedItems()
        if not items:
            return
        dest = QFileDialog.getExistingDirectory(self, "选择保存目录", os.path.expanduser("~/Pictures"))
        if not dest:
            return
        for item in items:
            idx = item.data(0, Qt.UserRole)
            if idx is None or idx >= len(self._files):
                continue
            f = self._files[idx]
            self._api.start_transfer(f.handle, os.path.join(dest, f.filename))
            self._active_transfers += 1
        self.transfer_status.setText(f"传输 {len(items)} 个文件...")
        self.transfer_status.setStyleSheet("color: #F5B800; font-size: 12px;")

    def _on_download_all(self):
        if not self._files:
            return
        dest = QFileDialog.getExistingDirectory(self, "选择保存目录", os.path.expanduser("~/Pictures"))
        if not dest:
            return
        handles = [f.handle for f in self._files]
        self._api.start_batch_transfer(handles, dest)
        self._active_transfers = len(self._files)
        self.transfer_status.setText(f"批量传输 {len(self._files)} 个文件...")
        self.transfer_status.setStyleSheet("color: #F5B800; font-size: 12px;")

    def _on_delete(self):
        items = self.file_tree.selectedItems()
        if not items:
            return
        reply = QMessageBox.question(
            self, "确认删除",
            f"确定要删除选中的 {len(items)} 个文件吗？\n此操作不可撤销。",
            QMessageBox.Yes | QMessageBox.No, QMessageBox.No
        )
        if reply != QMessageBox.Yes:
            return
        for item in items:
            idx = item.data(0, Qt.UserRole)
            if idx is not None and idx < len(self._files):
                self._api.delete_file(self._files[idx].handle)
        QTimer.singleShot(500, self._on_refresh)

    def _on_progress(self, job_id: int, progress):
        self.transfer_progress.setValue(progress.percent)
        self.transfer_speed.setText(f"{progress.speed_mbps:.1f} MB/s")
        self.transfer_status.setText(f"{progress.filename}: {progress.percent}%")

    def _on_transfer_done(self, job_id: int, error_code: int):
        self._active_transfers = max(0, self._active_transfers - 1)
        if self._active_transfers <= 0:
            self._active_transfers = 0
            ok = error_code == 0
            self.transfer_status.setText("传输完成" if ok else "传输出错")
            self.transfer_status.setStyleSheet(
                f"color: {'#4CAF50' if ok else '#E03A3A'}; font-size: 12px;"
            )
            self.transfer_progress.setValue(100 if ok else 0)

    def _on_new_file(self, handle: int, filename: str):
        self.transfer_status.setText(f"新文件: {filename}")
        self.transfer_status.setStyleSheet("color: #4CAF50; font-size: 12px;")

    def _on_upload(self):
        files, _ = QFileDialog.getOpenFileNames(
            self, "选择要上传的文件", os.path.expanduser("~/Pictures"),
            "图片文件 (*.jpg *.jpeg *.nef *.nrw);;视频文件 (*.mp4 *.mov);;所有文件 (*)"
        )
        if not files:
            return
        storage_id = self.storage_combo.currentData() or 0
        for fpath in files:
            self._api.send_file(fpath, storage_id)
        self.transfer_status.setText(f"上传 {len(files)} 个文件...")
        self.transfer_status.setStyleSheet("color: #F5B800; font-size: 12px;")
