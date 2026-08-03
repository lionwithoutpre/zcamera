# Nikon Camera Connect

跨平台（Android + Windows / macOS / Linux）尼康相机连接应用，基于 PTP/MTP 协议栈实现：

- **实时取景** — 约 30fps LiveView 帧流（尼康扩展操作码 0x9201~0x9203）
- **边拍边传** — 快门事件毫秒级感知（PTP 事件轮询 / inotify / HTTP 轮询三通道）
- **高速传输** — USB 零拷贝（splice / fcopyfile），目标 >30MB/s
- **断点续传** — 分块传输 + 检查点恢复，自适应块大小（64KB~4MB）
- **相机控制** — 快门 / 光圈 / ISO / 曝光补偿 / 白平衡 / Picture Control

## 技术栈

| 层 | 技术 |
|---|---|
| 核心库 | C11（PTP/MTP 协议栈、适配器模式、HAL） |
| Android | Kotlin + Jetpack Compose + JNI（compileSdk 34 / minSdk 26） |
| 桌面端 | C++17 / Qt6（可选）、PySide6 Python GUI、CLI 工具 |
| 构建 | CMake 3.20+、Gradle KTS |

## 目录结构

```
nikon-camera-app/
├── SPEC_*.md           # 10 份规格文档（架构/协议/HAL/适配器/传输/事件/API/Android/桌面/构建）
├── core/               # 跨平台 C 核心库（libcore 静态库）
│   ├── api/            #   CameraAPI 统一业务接口
│   ├── adapter/        #   适配器层（USB PTP / Wi-Fi，vtable 模式）
│   ├── protocol/       #   PTP / MTP / 会话管理（心跳保活）
│   ├── transfer/       #   零拷贝 + 分块传输引擎
│   ├── event/          #   EventWatcher 事件监听
│   └── hal/            #   硬件抽象：USB / Wi-Fi / BLE（按平台选择实现）
├── android/            # Android App（Gradle + NDK）
├── desktop/            # 桌面端：nikon-cli、Qt6 GUI、Python GUI
└── tests/              # C 单元测试（CHECK 宏框架）
```

## 架构

四层设计，`CameraAPI` 是唯一业务入口，各平台层（JNI / Qt / CLI）只做薄包装：

```
CameraAPI（业务层）→ Adapter（适配层）→ Protocol（协议层）→ HAL（硬件抽象层）
```

支持 USB（首选，实时控制 + 零拷贝）与 Wi-Fi（备用，HTTP + keep-alive）双通道，详见 `SPEC_ARCHITECTURE.md`。

## 构建

### 桌面 / 核心库（CMake）

```bash
cd nikon-camera-app
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_DESKTOP=ON
cmake --build . -j
```

macOS 依赖：`brew install libusb pkg-config`

启用 Qt6 GUI（需本机安装 Qt6）：追加 `-DUSE_QT=ON`

### Android（Gradle）

```bash
cd nikon-camera-app/android
./gradlew assembleDebug
```

依赖：NDK r26+、CMake 3.22.1、JDK 17（注意 AGP 8.x 要求 JDK 17，必要时在 `gradle.properties` 配置 `org.gradle.java.home`）。

## 测试

```bash
cd nikon-camera-app/build
ctest --output-on-failure          # 全量
ctest -R test_ptp --output-on-failure   # 单个测试
```

覆盖范围：PTP 会话、命令映射、分块传输、事件监听、Wi-Fi 适配器、CameraAPI。

## CLI 使用

构建产物 `nikon-cli`：

```bash
nikon-cli scan                      # 扫描相机
nikon-cli connect --usb             # 连接
nikon-cli info                      # 相机信息
nikon-cli capture                   # 拍摄
nikon-cli list                      # 列出文件
nikon-cli get 0x00010001 -o ~/Pictures/   # 下载
nikon-cli watch -o ~/Pictures/      # 边拍边传
nikon-cli set iso 800               # 设置参数
```

## 性能指标

| 指标 | 目标 |
|---|---|
| 连接建立 | < 3s |
| 拍照事件延迟 | < 100ms |
| USB 传输速度 | > 30MB/s |
| Wi-Fi 传输速度 | > 5MB/s |
| 断点续传成功率 | > 95% |

## 已知限制

- FTP 上传为占位实现（返回 `CAM_ERR_NOT_SUPPORTED`）
- BLE 唤醒 HAL 层为桩代码
- Windows 零拷贝降级为 read/write
- 单连接模式，不支持多相机同时连接

## 开发约定

改动前先阅读对应 `SPEC_*.md` 规格文档；核心库改动需同步检查 Android JNI（`jni_bridge.c`）与桌面封装（`desktop_api.cpp`）。AI Agent 协作指引见仓库根目录 [AGENTS.md](AGENTS.md)。
