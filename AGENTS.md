---
AIGC:
  ContentProducer: '001191110102MAD55U9H0F10002'
  ContentPropagator: '001191110102MAD55U9H0F10002'
  Label: '1'
  ProduceID: '57a6bbb3-09e3-412b-a17e-326995936780'
  PropagateID: '57a6bbb3-09e3-412b-a17e-326995936780'
  ReservedCode1: '9cdf5498-7d29-4b41-9468-a53e3e040d56'
  ReservedCode2: '9cdf5498-7d29-4b41-9468-a53e3e040d56'
---

# AGENTS.md

This file provides guidance to Lingma (lingma.aliyun.com) when working with code in this repository.

## 项目概述

**Nikon Camera Connect** — 跨平台（Android + Win/Mac/Linux）尼康相机连接应用，核心能力包括实时取景、边拍边传、零拷贝传输（USB >30MB/s）、断点续传。

**技术栈**：C11 核心库（PTP/MTP 协议栈 + 适配器模式 + HAL）、Kotlin/Jetpack Compose（Android）、C++17/Qt6（桌面）、CMake 3.20+、Gradle KTS。

## 构建命令

### 核心库 + 桌面端（CMake）

```bash
cd nikon-camera-app
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_DESKTOP=ON
cmake --build . -j
```

macOS 需先安装依赖：`brew install libusb pkg-config`

### Android（Gradle）

```bash
cd nikon-camera-app/android
./gradlew assembleDebug
```

依赖：NDK r26+、CMake 3.22.1、JDK 17。

### Qt6 桌面 GUI（可选）

```bash
cmake .. -DBUILD_DESKTOP=ON -DUSE_QT=ON
```

## 测试命令

```bash
cd nikon-camera-app/build
ctest --output-on-failure
```

运行单个测试：

```bash
ctest -R test_ptp --output-on-failure    # 或 test_session / test_chunked 等
./tests/test_ptp                          # 直接执行二进制
```

测试使用自定义 CHECK 宏框架（无外部依赖），模式：
```c
static int passed = 0, failed = 0;
#define CHECK(expr, msg) do { if (expr) { printf("  PASS: %s\n", msg); passed++; } else { printf("  FAIL: %s\n", msg); failed++; } } while(0)
```

## 四层架构

```
CameraAPI (api/camera_api.c)       ← 唯一业务入口，平台无关
    ↓
Adapter (adapter/)                 ← CameraCommand 抽象 + vtable 函数表
    ↓
Protocol (protocol/)               ← PTP 引擎 + MTP 引擎 + 会话管理
    ↓
HAL (hal/)                         ← USB / Wi-Fi / BLE 传输后端
```

**关键设计要点**：

1. **CameraAPI** 是全部平台（JNI / Qt / CLI）的唯一调用目标，平台层只做薄包装
2. **CameraAdapter** 使用 C 虚表模式（execute/get_status/register_event/destroy），USB PTP 和 Wi-Fi 是两套独立实现
3. **PtpSession** 参数化初始化（fd + transport + ep_out + ep_in），`ManagedSession` 封装心跳线程 + `_Atomic stop` + 重连回调
4. **EventWatcher** 三通道统一监听：PTP Poll（USB 首选）/ inotify（Linux MTP 备选）/ HTTP Poll（Wi-Fi 备用）
5. **TransferEngine** 线程池 + FIFO 队列，支持断点续传（检查已有文件大小续传）和自适应块大小（64KB~4MB）

## 平台 HAL 选择逻辑

CMake 根据 `CMAKE_SYSTEM_NAME` 自动选择 HAL 源文件：
- macOS: `usb_macos.c` + `wifi_posix.c` + `ble_macos.c`，链接 libusb + IOKit + CoreFoundation + pthread
- Linux: `usb_linux.c` + `wifi_posix.c` + `ble_linux.c`
- Android: `usb_stub.c`（JNI 层直接传 fd）+ `wifi_posix.c`
- Windows: `usb_win.c` + `wifi_posix.c` + `ble_win.c`，链接 ws2_32

## 关键约定

### 开发流程
- 改动前先读对应 `SPEC_*.md` 规格文档
- 核心库改动需同步检查 `jni_bridge.c`（Android）和 `desktop_api.cpp`（Qt）是否需要补充包装
- 新增 `CameraAPI` 函数时需在 `core/include/api/camera_api.h` 声明并在 `camera_api.c` 实现

### PTP 协议常量
- 尼康 Vendor ID: `0x04B0`
- Wi-Fi 端口: `15740`
- 尼康扩展操作码: StartLiveView=0x9201, EndLiveView=0x9202, GetLiveViewImage=0x9203, AutoFocus=0x90C0, Capture=0x90C1
- 设备属性码: ShutterSpeed=0xD00C, Aperture=0xD00E, ISO=0xD010, WhiteBalance=0xD00A

### Android 端关键约束
- native handle 唯一真源: `NikonApplication.cameraHandle`，ViewModel 只读不创建
- 传输任务用 native 返回的 `nativeJobId`，异步传输靠 progress 回调 status=2 判定完成
- LiveView Bitmap 必须用 `produceState` + `awaitDispose { recycle }`，禁止 `remember(frame)`
- 存储路径用 `getExternalFilesDir(DCIM)/NikonConnect/`，禁止写公共 `/DCIM/`
- USB 接口声明三级优先: class=6 → class=255 → 第一个可用接口

### Android 模块划分（阶段1-4 重构后）
- `:core:model` — 纯 Kotlin 领域模型（CameraInfo/CameraFile/CameraProperties/PictureControl/TransferJob/AppSettings），无 Android 依赖
- `:core:data` — Android 库：`SettingsRepository`（设置持久化/加密存储）、`StorageManager`（分区存储适配）
- `:app` — UI + 业务编排：
  - `jni/` — `CameraBridge`（不可移动，C 层 JNI 静态命名绑定）+ `CameraApi` 接口
  - `viewmodel/CameraViewModel` — 门面类，公有 API 与 StateFlow 稳定，内部委托给领域 Manager
  - `viewmodel/ConnectionManager` — 扫描/连接(USB/Wi-Fi)/断开/BLE/状态事件
  - `viewmodel/CameraMediaManager` — 文件列举/缩略图/删除/传输路径
  - `viewmodel/LiveViewManager` — 实时取景启停/帧轮询
  - `viewmodel/CameraSettingsManager` — 参数轮询/格式化/设置持久化/FTP/拍摄
  - `transfer/TransferManager` — 传输任务状态机（并发控制/排队/续传）
- 约束：`CameraBridge` 类名与包名（`com.nikon.app.jni`）不可移动；领域 Manager 不持有 handle 生命周期，只读 `handleProvider`
- 单测通过反射访问 ViewModel 私有成员（`transferProgressCallback`/`buildDestPath`/`formatXxx`），门面类须保留这些兼容点

### 错误码体系
- `CAM_OK=0`，业务错误 -100~-105，传输错误 -200~-201，内存/权限 -300~-301，硬件 -400~-402，协议 -500

## 桌面 CLI 工具

构建产物 `nikon-cli`，常用命令：`scan`、`connect --usb`、`capture`、`list`、`get <handle>`、`watch`、`set iso/aperture/shutter`。

## 已知限制

- BLE 唤醒指令为保守 0x01 触发（SnapBridge 精确唤醒字节为私有协议，部分机型可能需调整）
- Windows 零拷贝降级为 read/write（未用 TransmitFile）
- 单连接模式，不支持多相机同时连接

> AI生成