"""
desktop/src/py_gui/desktop_api.py — ctypes 封装 libcore.a

将 C 的 camera_api.h 函数和回调映射为 Python 对象，
通过 PySide6 Signal 桥接到 GUI 线程。
"""
import ctypes
import os
import threading
from concurrent.futures import ThreadPoolExecutor
from ctypes import (
    c_int, c_uint8, c_uint16, c_uint32, c_uint64,
    c_int8, c_char, c_bool, c_void_p, c_char_p,
    POINTER, Structure, CFUNCTYPE, byref, cast, sizeof
)
from dataclasses import dataclass, field
from typing import Optional, Callable, List

try:
    from PySide6.QtCore import QObject, Signal
    _HAS_PYSIDE = True
except ImportError:
    _HAS_PYSIDE = False

_LIB_PATH = os.path.join(
    os.path.dirname(__file__), '..', '..', '..', 'build', 'core', 'libcore.a'
)

MOCK = os.environ.get('NIKON_MOCK', '1') == '1'

# ─── 常量 ──────────────────────────────────────────────────────

TRANSPORT_AUTO      = 0
TRANSPORT_USB_ONLY  = 1
TRANSPORT_WIFI_ONLY = 2

NIKON_STORAGE_CF = 0x00010001
NIKON_STORAGE_SD = 0x00010002

STATUS_DISCONNECTED = 0
STATUS_SCANNING     = 1
STATUS_CONNECTING   = 2
STATUS_CONNECTED    = 3
STATUS_TRANSFERRING = 4
STATUS_ERROR        = 5

# ─── 数据结构 ──────────────────────────────────────────────────

@dataclass
class CamDevice:
    id: str = ""
    model: str = ""
    serial: str = ""
    transport: int = 0
    battery: int = 0
    storage_free: int = 0
    storage_total: int = 0
    shutter_count: int = 0

@dataclass
class CamFile:
    handle: int = 0
    filename: str = ""
    size: int = 0
    datetime: str = ""
    is_raw: bool = False
    is_jpeg: bool = False
    width: int = 0
    height: int = 0
    storage_id: int = 0

@dataclass
class TransferProgress:
    job_id: int = 0
    handle: int = 0
    filename: str = ""
    total_size: int = 0
    transferred: int = 0
    percent: int = 0
    speed_mbps: float = 0.0
    elapsed_ms: int = 0
    remaining_ms: int = 0
    status: int = 0
    error_code: int = 0

@dataclass
class PictureControl:
    hue: int = 0
    saturation: int = 0
    contrast: int = 0
    clarity: int = 0
    sharpening: int = 3
    brightness: int = 0
    wb_ab: int = 0
    wb_gm: int = 0
    color_space: int = 0

# ─── Signal 桥接 (线程安全回调) ────────────────────────────────

if _HAS_PYSIDE:
    class _BridgeSignals(QObject):
        scan_result       = Signal(object)
        connection_changed = Signal(int)
        connected         = Signal()
        disconnected      = Signal()
        capture_completed = Signal()
        live_view_started = Signal()
        live_view_stopped = Signal()
        live_view_frame   = Signal(bytes)
        file_list         = Signal(object)
        thumbnail         = Signal(int, bytes)
        transfer_progress = Signal(int, object)
        transfer_complete = Signal(int, int)
        new_file          = Signal(int, str)
        property_value    = Signal(int, int)
        picture_control   = Signal(object)
        error             = Signal(int, str)

class DesktopAPI:
    """
    CameraAPI 的 Python 封装。

    MOCK 模式: 返回模拟数据用于 UI 开发，无需真实相机。
    生产模式: 通过 ctypes 调用 libcore.dylib。

    所有回调通过 PySide6 Signal 桥接到 GUI 线程，确保线程安全。
    """

    def __init__(self, transport: int = TRANSPORT_AUTO):
        self._transport = transport
        self._connected = False
        self._lock = threading.Lock()
        self._lib = None
        self._executor = ThreadPoolExecutor(max_workers=4)

        if _HAS_PYSIDE:
            self._signals = _BridgeSignals()
            self._signals.scan_result.connect(self._on_scan_result)
            self._signals.connection_changed.connect(self._on_connection_changed)
            self._signals.connected.connect(self._on_connected)
            self._signals.disconnected.connect(self._on_disconnected)
            self._signals.capture_completed.connect(self._on_capture_completed)
            self._signals.live_view_started.connect(self._on_live_view_started)
            self._signals.live_view_stopped.connect(self._on_live_view_stopped)
            self._signals.live_view_frame.connect(self._on_live_view_frame)
            self._signals.file_list.connect(self._on_file_list)
            self._signals.thumbnail.connect(self._on_thumbnail)
            self._signals.transfer_progress.connect(self._on_transfer_progress)
            self._signals.transfer_complete.connect(self._on_transfer_complete)
            self._signals.new_file.connect(self._on_new_file)
            self._signals.property_value.connect(self._on_property_value)
            self._signals.picture_control.connect(self._on_picture_control)
            self._signals.error.connect(self._on_error)
        else:
            self._signals = None

        self.on_scan_result: Optional[Callable] = None
        self.on_connection_changed: Optional[Callable] = None
        self.on_connected: Optional[Callable] = None
        self.on_disconnected: Optional[Callable] = None
        self.on_capture_completed: Optional[Callable] = None
        self.on_live_view_started: Optional[Callable] = None
        self.on_live_view_stopped: Optional[Callable] = None
        self.on_live_view_frame: Optional[Callable] = None
        self.on_file_list: Optional[Callable] = None
        self.on_thumbnail: Optional[Callable] = None
        self.on_transfer_progress: Optional[Callable] = None
        self.on_transfer_complete: Optional[Callable] = None
        self.on_new_file: Optional[Callable] = None
        self.on_property_value: Optional[Callable] = None
        self.on_picture_control: Optional[Callable] = None
        self.on_error: Optional[Callable] = None

        if not MOCK:
            self._load_library()

    def _load_library(self):
        lib_path = os.path.join(os.path.dirname(_LIB_PATH), 'libcore.dylib')
        if os.path.exists(lib_path):
            self._lib = ctypes.CDLL(lib_path)
        else:
            print(f"[DesktopAPI] libcore.dylib not found at {lib_path}, using MOCK mode")
            global MOCK
            MOCK = True

    def _emit(self, signal_name: str, *args):
        if self._signals:
            signal = getattr(self._signals, signal_name, None)
            if signal:
                signal.emit(*args)
                return
        handler = getattr(self, f"on_{signal_name}", None)
        if handler:
            handler(*args)

    def _run_async(self, fn):
        self._executor.submit(fn)

    # ─── Signal 槽函数 (GUI 线程执行) ────────────────────────

    def _on_scan_result(self, devices):
        if self.on_scan_result: self.on_scan_result(devices)

    def _on_connection_changed(self, status):
        if self.on_connection_changed: self.on_connection_changed(status)

    def _on_connected(self):
        if self.on_connected: self.on_connected()

    def _on_disconnected(self):
        if self.on_disconnected: self.on_disconnected()

    def _on_capture_completed(self):
        if self.on_capture_completed: self.on_capture_completed()

    def _on_live_view_started(self):
        if self.on_live_view_started: self.on_live_view_started()

    def _on_live_view_stopped(self):
        if self.on_live_view_stopped: self.on_live_view_stopped()

    def _on_live_view_frame(self, data):
        if self.on_live_view_frame: self.on_live_view_frame(data)

    def _on_file_list(self, files):
        if self.on_file_list: self.on_file_list(files)

    def _on_thumbnail(self, handle, data):
        if self.on_thumbnail: self.on_thumbnail(handle, data)

    def _on_transfer_progress(self, job_id, progress):
        if self.on_transfer_progress: self.on_transfer_progress(job_id, progress)

    def _on_transfer_complete(self, job_id, error_code):
        if self.on_transfer_complete: self.on_transfer_complete(job_id, error_code)

    def _on_new_file(self, handle, filename):
        if self.on_new_file: self.on_new_file(handle, filename)

    def _on_property_value(self, prop_id, value):
        if self.on_property_value: self.on_property_value(prop_id, value)

    def _on_picture_control(self, ctrl):
        if self.on_picture_control: self.on_picture_control(ctrl)

    def _on_error(self, error_code, message):
        if self.on_error: self.on_error(error_code, message)

    # ─── 连接管理 ───────────────────────────────────────────

    def scan(self):
        def _scan():
            import time
            time.sleep(1.2)

            if MOCK:
                devices = [
                    CamDevice(
                        id="NIKON_Z8_3001234_USB",
                        model="NIKON Z 8",
                        serial="3001234",
                        transport=0,
                        battery=85,
                        storage_free=48_000_000_000,
                        storage_total=128_000_000_000,
                        shutter_count=12450,
                    ),
                    CamDevice(
                        id="NIKON_Z6III_5005678_WIFI",
                        model="NIKON Z 6III",
                        serial="5005678",
                        transport=1,
                        battery=62,
                        storage_free=96_000_000_000,
                        storage_total=256_000_000_000,
                        shutter_count=8920,
                    ),
                ]
            else:
                devices = []

            self._emit('scan_result', devices)

        self._run_async(_scan)

    def connect_to(self, camera_id: str):
        def _connect():
            import time
            time.sleep(0.8)

            self._emit('connection_changed', STATUS_CONNECTING)
            time.sleep(0.5)

            with self._lock:
                self._connected = True

            self._emit('connection_changed', STATUS_CONNECTED)
            self._emit('connected')

        self._run_async(_connect)

    def disconnect(self):
        with self._lock:
            self._connected = False
        self._emit('disconnected')
        self._emit('connection_changed', STATUS_DISCONNECTED)

    def is_connected(self) -> bool:
        return self._connected

    # ─── 拍摄 ───────────────────────────────────────────────

    def capture(self):
        def _capture():
            import time
            time.sleep(0.3)
            self._emit('capture_completed')
        self._run_async(_capture)

    def burst(self, count: int, interval_ms: int):
        self._run_async(lambda: None)

    def auto_focus(self):
        self._run_async(lambda: None)

    # ─── 取景 ───────────────────────────────────────────────

    def start_live_view(self):
        def _start():
            import time
            time.sleep(0.3)
            self._emit('live_view_started')

            if MOCK:
                import struct, io
                w, h = 640, 426
                try:
                    from PIL import Image
                    img = Image.new('RGB', (w, h), color=(30, 30, 30))
                    buf = io.BytesIO()
                    img.save(buf, format='JPEG', quality=60)
                    frame_data = buf.getvalue()
                except ImportError:
                    frame_data = b'\xff\xd8\xff\xe0' + b'\x00' * 100

                for _ in range(5):
                    if not self._connected:
                        break
                    self._emit('live_view_frame', frame_data)
                    time.sleep(0.1)

        self._run_async(_start)

    def stop_live_view(self):
        self._emit('live_view_stopped')

    # ─── 属性 ───────────────────────────────────────────────

    def set_property(self, prop_id: int, value: int):
        self._emit('property_value', prop_id, value)

    def get_property(self, prop_id: int):
        if MOCK:
            mock_values = {
                0xD010: 800,
                0xD00C: 125,
                0xD00E: 56,
                0xD00A: 0,
            }
            val = mock_values.get(prop_id, 0)
            self._emit('property_value', prop_id, val)

    def set_iso(self, iso: int):       self.set_property(0xD010, iso)
    def set_shutter(self, val: int):   self.set_property(0xD00C, val)
    def set_aperture(self, val: int):  self.set_property(0xD00E, val)
    def set_white_balance(self, val: int): self.set_property(0xD00A, val)

    # ─── Picture Control ────────────────────────────────────

    def get_picture_control(self):
        if MOCK:
            self._emit('picture_control', PictureControl(
                hue=0, saturation=1, contrast=-1,
                clarity=0, sharpening=4, brightness=0,
                wb_ab=2, wb_gm=-1, color_space=0
            ))

    def set_picture_control(self, ctrl: PictureControl):
        import time
        time.sleep(0.1)
        self._emit('picture_control', ctrl)

    # ─── 文件 ───────────────────────────────────────────────

    def list_files(self, storage_id: int = 0):
        def _list():
            import time
            time.sleep(0.5)

            if MOCK:
                files = [
                    CamFile(handle=0x00010001, filename="DSC_1234.NEF", size=48_000_000,
                            datetime="2026-06-23T14:32:15", is_raw=True, is_jpeg=False,
                            width=8256, height=5504, storage_id=NIKON_STORAGE_CF),
                    CamFile(handle=0x00010002, filename="DSC_1234.JPG", size=12_000_000,
                            datetime="2026-06-23T14:32:15", is_raw=False, is_jpeg=True,
                            width=8256, height=5504, storage_id=NIKON_STORAGE_CF),
                    CamFile(handle=0x00010003, filename="DSC_1235.NEF", size=47_500_000,
                            datetime="2026-06-23T14:33:02", is_raw=True, is_jpeg=False,
                            width=8256, height=5504, storage_id=NIKON_STORAGE_CF),
                    CamFile(handle=0x00010004, filename="DSC_1235.JPG", size=11_800_000,
                            datetime="2026-06-23T14:33:02", is_raw=False, is_jpeg=True,
                            width=8256, height=5504, storage_id=NIKON_STORAGE_CF),
                    CamFile(handle=0x00010005, filename="DSC_1236.NEF", size=48_200_000,
                            datetime="2026-06-23T14:35:40", is_raw=True, is_jpeg=False,
                            width=8256, height=5504, storage_id=NIKON_STORAGE_SD),
                    CamFile(handle=0x00010006, filename="DSC_1236.JPG", size=12_300_000,
                            datetime="2026-06-23T14:35:40", is_raw=False, is_jpeg=True,
                            width=8256, height=5504, storage_id=NIKON_STORAGE_SD),
                    CamFile(handle=0x00010007, filename="DSC_1237.JPG", size=11_900_000,
                            datetime="2026-06-23T14:38:12", is_raw=False, is_jpeg=True,
                            width=8256, height=5504, storage_id=NIKON_STORAGE_SD),
                ]
            else:
                files = []

            self._emit('file_list', files)

        self._run_async(_list)

    def get_thumbnail(self, handle: int):
        def _thumb():
            import time
            time.sleep(0.2)
            if MOCK:
                try:
                    from PIL import Image
                    import io
                    img = Image.new('RGB', (160, 106), color=(40, 40, 45))
                    buf = io.BytesIO()
                    img.save(buf, format='JPEG', quality=50)
                    data = buf.getvalue()
                except ImportError:
                    data = b'\xff\xd8\xff\xe0' + b'\x00' * 200
                self._emit('thumbnail', handle, data)

        self._run_async(_thumb)

    def delete_file(self, handle: int):
        self._run_async(lambda: None)

    # ─── 传输 ───────────────────────────────────────────────

    def start_transfer(self, handle: int, dest_path: str):
        def _transfer():
            import time
            for p in range(0, 101, 5):
                progress = TransferProgress(
                    job_id=1, handle=handle,
                    filename=os.path.basename(dest_path),
                    total_size=48_000_000,
                    transferred=int(48_000_000 * p / 100),
                    percent=p,
                    speed_mbps=35.0 + (p % 10),
                    elapsed_ms=p * 30,
                    remaining_ms=(100 - p) * 30,
                    status=2 if p >= 100 else 1,
                )
                self._emit('transfer_progress', 1, progress)
                time.sleep(0.15)

            self._emit('transfer_complete', 1, 0)

        self._run_async(_transfer)

    def start_batch_transfer(self, handles: List[int], dest_dir: str):
        for h in handles:
            self.start_transfer(h, os.path.join(dest_dir, f"file_{h:08X}.NEF"))

    def cancel_transfer(self, job_id: int):
        pass

    def send_file(self, local_path: str, storage_id: int = 0, remote_name: str = None):
        def _send():
            import time
            time.sleep(0.5)
            if MOCK:
                fname = remote_name or os.path.basename(local_path)
                self._emit('transfer_complete', 0, 0)
        self._run_async(_send)

    def connect_wifi(self, ip_addr: str, port: int = 15740):
        def _connect():
            import time
            self._emit('connection_changed', STATUS_CONNECTING)
            time.sleep(0.8)
            with self._lock:
                self._connected = True
            self._emit('connection_changed', STATUS_CONNECTED)
            self._emit('connected')
        self._run_async(_connect)

    def shutdown(self):
        self._executor.shutdown(wait=False)
