# 桌面端规格书 (Win/Mac/Linux)

## 1. 桌面端架构

```
┌────────────────────────────────────────┐
│            UI 层 (可选)                  │
│  ┌─────────┐ ┌──────────┐ ┌────────┐  │
│  │ CLI 工具  │ │ Qt/GTK  │ │Python  │  │
│  │          │ │         │ │GUI     │  │
│  └────┬────┘ └────┬─────┘ └───┬────┘  │
├───────┼───────────┼───────────┼───────┤
│  ┌────┴───────────┴───────────┴────┐  │
│  │    DesktopAPI (业务层封装)        │  │
│  │  • 连接管理                     │  │
│  │  • 传输调度 (ThreadPoolExecutor) │  │
│  │  • FTP 自动导出                 │  │
│  │  • Signal 桥接 (GUI 线程安全)    │  │
│  └──────────────────────────────┬──┘  │
├─────────────────────────────────┼────┤
│  ┌──────────────────────────────┴──┐  │
│  │    Core Library (libcore.a)      │  │
│  │  • HAL (libusb / IOUSB / WinUSB)│  │
│  │  • PTP/MTP 协议                 │  │
│  │  • 零拷贝传输引擎               │  │
│  │  • 事件监听                    │  │
│  └─────────────────────────────────┘  │
└────────────────────────────────────────┘
```

## 2. Python GUI (PySide6 + ctypes)

### 2.1 DesktopAPI 架构

```python
# desktop/src/py_gui/desktop_api.py

# 核心设计:
# 1. _BridgeSignals(QObject) — 统一 Signal 桥接到 GUI 线程
# 2. _emit() — 统一发射方法, 兼容有/无 PySide6 环境
# 3. ThreadPoolExecutor(max_workers=4) — 替代裸 threading.Thread
# 4. ctypes FFI — 调用 libcore.so / libcore.a
```

### 2.2 Signal 桥接

```python
from PySide6.QtCore import QObject, Signal

class _BridgeSignals(QObject):
    """统一 Signal 桥接, 确保回调在 GUI 线程执行"""
    status_changed  = Signal(int)
    new_file        = Signal(int, str)      # object_handle, filename
    transfer_progress = Signal(int, int)    # job_id, percent
    transfer_complete = Signal(int, int)    # job_id, error_code
    capture_complete = Signal()
    property_changed = Signal(int)          # prop_id
    device_changed   = Signal()
    error            = Signal(int, str)     # error_code, message

class DesktopAPI:
    def __init__(self):
        self._signals = _BridgeSignals()
        self._executor = ThreadPoolExecutor(max_workers=4)

    def _emit(self, signal_name: str, *args):
        """统一发射 Signal, 兼容无 PySide6 环境"""
        signal = getattr(self._signals, signal_name, None)
        if signal is not None:
            signal.emit(*args)

    def _run_async(self, fn, *args, **kwargs):
        """提交到线程池 (替代 threading.Thread)"""
        self._executor.submit(fn, *args, **kwargs)
```

### 2.3 线程池

```python
from concurrent.futures import ThreadPoolExecutor

# DesktopAPI 使用 ThreadPoolExecutor(max_workers=4)
# 所有异步操作通过 _run_async() 提交到线程池
# 避免每次操作创建新线程的开销
# 线程池自动管理线程生命周期
```

## 3. CLI 工具设计

```c
// cli/main.c — 命令行工具接口

// nikon-cli 命令一览:
//   scan       扫描连接的尼康相机
//   connect    连接相机
//   info       显示相机信息
//   capture    拍摄单张
//   liveview   启动实时取景
//   list       列出文件
//   get        下载文件
//   watch      监听新文件 (边拍边传)
//   set        设置相机参数
//   format     格式化存储卡

// 使用示例:
//   nikon-cli scan
//   nikon-cli connect --usb
//   nikon-cli info
//   nikon-cli capture --wait
//   nikon-cli list --storage 0
//   nikon-cli get 0x00010001 --output ~/Pictures/
//   nikon-cli watch --output ~/Pictures/ --autorename
//   nikon-cli set iso 800
//   nikon-cli set aperture 2.8
```

## 4. macOS 特定适配

```c
// hal/usb/usb_macos.c — macOS USB 实现
// 推荐方案:
//   1. IOUSBKit.framework (macOS 12+)
//   2. libusb 1.0 (通过 Homebrew)
//   3. IOKit 直接 C API

#include <IOKit/IOKitLib.h>
#include <IOKit/usb/IOUSBLib.h>

int hal_usb_enumerate(UsbDeviceInfo* devices, int max_count) {
    CFMutableDictionaryRef matching = IOServiceMatching(kIOUSBDeviceClassName);
    io_iterator_t iterator;
    IOServiceGetMatchingServices(kIOMasterPortDefault, matching, &iterator);

    io_service_t usbDevice;
    int count = 0;

    while ((usbDevice = IOIteratorNext(iterator)) && count < max_count) {
        uint16_t vendor_id, product_id;
        // ... 获取属性, 过滤 Nikon VID ...
        count++;
        IOObjectRelease(usbDevice);
    }
    IOObjectRelease(iterator);
    return count;
}
```

## 5. Windows 特定适配

```c
// hal/usb/usb_win.c — Windows USB 实现 (WinUSB)

#include <windows.h>
#include <setupapi.h>
#include <winusb.h>
#include <devguid.h>

int hal_usb_enumerate(UsbDeviceInfo* devices, int max_count) {
    HDEVINFO deviceInfoSet = SetupDiGetClassDevs(
        &GUID_DEVCLASS_USB, NULL, NULL,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    // ... 枚举设备, 过滤 VID_04B0 ...
    SetupDiDestroyDeviceInfoList(deviceInfoSet);
    return count;
}

int hal_usb_bulk_transfer(int fd, uint8_t endpoint,
                          uint8_t* buf, int length, int timeout_ms) {
    WINUSB_INTERFACE_HANDLE winusbHandle = (WINUSB_INTERFACE_HANDLE)fd;
    ULONG bytesTransferred = 0;
    BOOL result = WinUsb_ReadPipe(winusbHandle, endpoint,
                                   buf, length, &bytesTransferred, NULL);
    if (!result) return -GetLastError();
    return (int)bytesTransferred;
}
```

## 6. macOS 权限处理

```c
// 方案 A: IOUSBKit/IOKit (无需额外权限, macOS 12+)
// 方案 B: libusb (需要授权: 系统设置 → 隐私与安全性 → USB)
// 方案 C: System Extension (生产环境, 需申请 com.apple.developer.usb.realtime)
// 最小实现建议: 方案 A
```

## 7. 自动化脚本接口

```bash
#!/bin/bash
# watch_and_upload.sh — 监听并自动上传

nikon-cli watch --output ~/NikonPhotos/ --format "IMG_%Y%m%d_%H%M%S" &
WATCH_PID=$!

inotifywait -m ~/NikonPhotos/ -e create -e moved_to |
    while read dir action file; do
        if [[ "$file" =~ \.(NEF|JPG|JPEG)$ ]]; then
            echo "新文件: $file, 上传至 FTP..."
            nikon-cli ftp-upload "$dir/$file" --remote "/remote/photos/$file"
        fi
    done
```

## 8. 桌面端性能对比

| 操作系统 | USB 栈 | 最大理论速度 | 零拷贝支持 | 开发复杂度 |
|---------|--------|-------------|-----------|-----------|
| Linux | libusb | > 40MB/s | splice/sendfile | 低 |
| macOS | IOUSBKit | > 35MB/s | fcopyfile | 中 |
| Windows | WinUSB | > 30MB/s | 降级 read/write | 中 |
