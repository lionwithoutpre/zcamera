# Nikon Camera Connect — Agent 开发指南

> 本文档供 AI Agent / 新接手开发者快速理解项目全貌，遵循项目约定，正确产出代码。

## 1. 项目概述

**名称**：Nikon Camera Connect
**目标**：跨平台（Android + Win/Mac/Linux）尼康相机连接 App
**核心能力**：实时取景、边拍边传、零拷贝传输（>30MB/s USB）、断点续传、Picture Control 调节

**技术栈**：
- **核心库**：C99，PTP/MTP 协议栈 + 适配器模式 + HAL 层
- **Android**：Kotlin + Jetpack Compose + JNI 桥接 + 前台服务
- **桌面端**：C++ / Qt6 Widgets + QSS 黑黄配色
- **构建**：CMake 3.22+（核心库 & 桌面 & JNI）、Gradle KTS（Android）

## 2. 目录结构

```
nikon-camera-app/
├── SPEC_*.md              # 10 份规格文档（架构/协议/HAL/适配器/传输/事件/API/Android/桌面/构建）
├── CMakeLists.txt         # 顶层 CMake
├── core/                  # 跨平台 C 核心库
│   ├── include/
│   │   ├── api/camera_api.h          # 统一业务 API（~350 行,所有接口声明）
│   │   ├── protocol/ptp.h            # PTP/MTP 协议常量与结构
│   │   ├── adapter/                  # 适配器接口
│   │   ├── hal/                      # 硬件抽象（USB/WiFi/BLE）
│   │   ├── transfer/                 # 传输引擎（断点续传/零拷贝）
│   │   └── event/                    # 事件回调
│   ├── api/camera_api.c              # CameraAPI 实现（~1300 行）
│   ├── protocol/{ptp.c,mtp.c,session.c}
│   ├── adapter/{nikon_adapter.c,command_map.c,wifi_adapter.c}
│   ├── transfer/{chunked.c,zerocopy.c}
│   ├── hal/{usb_linux.c,usb_android.c,wifi.c}
│   └── event/event.c
├── android/
│   └── app/
│       ├── build.gradle.kts          # Android 构建（compileSdk 34, minSdk 26）
│       └── src/main/
│           ├── AndroidManifest.xml   # USB host + 前台服务 + 通知权限
│           ├── jni/
│           │   ├── jni_bridge.c      # JNI 桥接（~460 行,18 个 native 函数）
│           │   └── CMakeLists.txt    # 链接 core + jnigraphics + log
│           └── java/com/nikon/app/
│               ├── NikonApplication.kt   # 全局 Application,持有 cameraHandle
│               ├── MainActivity.kt       # 唯一 Activity,启动 Service + Compose
│               ├── jni/CameraBridge.kt   # native 声明 + 常量 + TransferProgressCallback
│               ├── service/CameraService.kt  # 前台服务,USB 热插拔 + handle 生命周期
│               ├── viewmodel/CameraViewModel.kt  # ~700 行,所有业务状态与方法
│               └── ui/
│                   ├── navigation/NavGraph.kt   # 5 tab + LV 全屏
│                   ├── theme/                    # 黑黄配色 NikonYellow #F5B800
│                   └── screens/                  # 6 个 Screen
│                       ├── HomeScreen.kt         # 扫描连接 + 仪表盘
│                       ├── LiveViewScreen.kt     # 实时取景全屏
│                       ├── GalleryScreen.kt      # 文件浏览 + 批量传输
│                       ├── TransferScreen.kt     # 传输任务四分区
│                       ├── PresetScreen.kt       # 预设管理
│                       └── SettingsScreen.kt     # 设置 + App 设置
├── desktop/
│   ├── CMakeLists.txt
│   └── src/
│       ├── api/desktop_api.{h,cpp}   # C++ Qt 封装 CameraAPI（~720 行）
│       └── ui/
│           ├── main_window.{h,cpp}   # 主窗口 + QSS 样式
│           └── pages/                # 5 个 Qt 页面
│               ├── scan_page.{h,cpp}
│               ├── dashboard_page.{h,cpp}
│               ├── liveview_page.{h,cpp}
│               ├── file_browser_page.{h,cpp}
│               └── settings_page.{h,cpp}
├── common/              # 跨平台公共代码
├── tests/               # C 单元测试
└── scripts/             # 构建/打包脚本
```

## 3. 四层架构

```
业务层 (CameraAPI)  ← 统一业务接口,所有平台共用
    ↓
适配层 (Adapter)    ← CameraCommand 抽象,尼康私有协议差异适配
    ↓
协议层 (Protocol)   ← PTP 引擎 + MTP 引擎 + 会话管理(心跳保活)
    ↓
硬件抽象层 (HAL)    ← USB / Wi-Fi / Bluetooth 传输后端
```

**关键设计**：
- `CameraAPI` 是唯一业务入口,平台无关
- `CameraCommand` 抽象 + vtable 函数表,不同相机型号通过 adapter 适配
- `PtpSession` 含 transport/ep_out/ep_in,`_Atomic stop` + 重连回调实现心跳
- Android JNI 和 Qt DesktopAPI 都只是 CameraAPI 的薄包装

## 4. 关键约定

### 4.1 native handle 生命周期（Android）

**唯一真源**：`NikonApplication.cameraHandle: Long`（@Volatile）

- **创建**：`CameraService.onCreate()` 调 `CameraBridge.nativeCreate()` 写入
- **销毁**：`CameraService.cleanup()`（onDestroy / onTaskRemoved 共用）调 `nativeDestroy()` 置 0
- **读取**：`CameraViewModel._handle` = `getApplication<NikonApplication>().cameraHandle`（computed property，不缓存）
- **守卫**：所有 public 方法首行 `if (!ensureHandle()) return`，handle=0 时报错"服务未就绪"

**严禁**：
- ViewModel 自己 `nativeCreate` / `nativeDestroy`（破坏单一所有权）
- Service 维护私有 `_handle` 副本（双写不一致）
- 用 `NikonApplication.instance` 静态单例（破坏依赖注入，改用 `getApplication()`）

### 4.2 传输管理

**任务 ID**：用 native 层返回的 `nativeJobId` 作为 job 唯一标识。WAITING 排队任务用负数 tempId 占位，启动后替换为 nativeJobId。

**并发控制**：`startTransfer` 检查 `activeCount >= settings.concurrentJobs`，超限排队 WAITING。任务完成（DONE/FAILED）后 `promoteNextWaiting()` 带并发校验地提升下一个。

**进度回调**：
- JNI 层 `nativeRegisterProgressCallback` 注册 C 回调
- C 回调通过 `AttachCurrentThread` → `CallVoidMethod(onProgress)` 桥接到 Kotlin
- ViewModel 的 `transferProgressCallback` 对象映射 status（0/1→ACTIVE, 2→DONE, -1→FAILED）+ updateJob + promote
- **不要**在 `nativeStartTransfer` 返回后直接标 DONE（那是异步传输刚启动，靠 progress 回调的 status=2 才算完成）

**断点续传**：native 层支持 offset 续传，`resumeTransfer` 重新对同一 objectHandle 调 `nativeStartTransfer`。

### 4.3 LiveView 取景

**帧轮询**：ViewModel `startLiveViewPolling()` 协程循环 `delay(33)` 调 `nativeGetLiveViewFrame`，~30fps 更新 `liveViewFrame: StateFlow<ByteArray?>`。

**Bitmap 内存管理**（关键，防 OOM）：
- UI 层用 `produceState` + `awaitDispose { recycle }`，旧帧在协程生命周期结束时回收
- **严禁**用 `remember(frame) { decodeByteArray }`（旧 Bitmap 不 recycle 会泄漏）
- **严禁**在 `LaunchedEffect` 里手动 recycle 旧帧（竞态：渲染层可能正引用）

**退出清理**：`DisposableEffect(Unit)` 进页面 `startLiveView`，`onDispose` 退 `stopLiveView`；ViewModel `onCleared` 也会 stop。

### 4.4 状态轮询

ViewModel init 启动 `startStatusPolling()`，1s 间隔：
- handle 0→非0：`serviceReady = true`
- handle 非0：调 `nativeGetStatus` 同步 `_status`（感知 USB 拔出等 native 主动断开）
- handle 非0→0：`serviceReady = false` + `_status = DISCONNECTED`

连接成功后另起 `startPropertyPolling()`，2s 间隔轮询 7 个 PTP 属性（快门/光圈/ISO/EV/对焦/WB/画质）。

### 4.5 错误处理

- ViewModel `reportError(msg)` 同时写 `_error: StateFlow` + 发 `_errorEvents: Channel<String>(BUFFERED)`
- NavGraph `LaunchedEffect(Unit) { errorEvents.collect { showSnackbar } }` 排队消费，不丢连续错误
- **不要**用 `LaunchedEffect(error)` + `clearError()`（连续错误会丢失）

### 4.6 存储路径（Android 10+ Scoped Storage）

- **严禁**写公共 `/DCIM/`（EACCES）
- 用 `getExternalFilesDir(Environment.DIRECTORY_DCIM)/NikonConnect/`
- 便捷方法 `viewModel.startTransferToApp(objectHandle, filename)`

### 4.7 USB PTP 接口声明

`tryClaimPtpInterface` 三级优先：
1. `class=6`（Still Imaging）— 标准 PTP
2. `class=255`（Vendor Specific）— 部分尼康型号
3. 第一个可用接口 — 极端回退

**不要**盲目 claim 所有接口（会误声明 MTP 接口导致 PTP 通信失败）。

## 5. UI 设计规范

### 5.1 配色（尼康官方黑黄）

```kotlin
val NikonYellow = Color(0xFFF5B800)   // 主色,按钮/高亮/选中
val NikonBlack   = Color(0xFF0E0E0E)   // 背景
val NikonSurface = Color(0xFF1A1A1A)   // 卡片
val NikonSurface2 = Color(0xFF111111)  // 底栏
val NikonText    = Color(0xFFF0F0F0)   // 主文字
val NikonText2   = Color(0xFFA0A0A0)   // 次文字
val NikonText3   = Color(0xFF616161)   // 弱文字
val NikonGreen   = Color(0xFF4CAF50)   // 成功/在线
val NikonRed     = Color(0xFFE03A3A)   // 危险/错误
```

### 5.2 导航

- 底部 5 tab：主页 / 相册 / 传输 / 预设 / 设置
- LiveView 全屏无底栏
- 扫描连接为 Home 内态（STATUS_DISCONNECTED 时显示 ScanContent）

### 5.3 Compose 约定

- 状态收集统一用 `collectAsStateWithLifecycle()`（需 `lifecycle-runtime-compose` 依赖）
- 副作用用 `LaunchedEffect` / `DisposableEffect` / `produceState`
- 页面级 ViewModel 通过 `viewModel()` 获取，单例共享

## 6. PTP 协议常量速查

| 操作码 | 名称 | 用途 |
|---|---|---|
| 0x1004 | GetStorageIDs | 列举存储卡 |
| 0x1007 | GetObjectHandles | 列举文件 |
| 0x1008 | GetObjectInfo | 文件元信息 |
| 0x1009 | GetObject | 下载整个文件 |
| 0x1019 | GetPartialObject | 断点续传关键 |
| 0x9201 | StartLiveView | 尼康扩展:启动取景 |
| 0x9202 | EndLiveView | 尼康扩展:停止取景 |
| 0x9203 | GetLiveViewImage | 尼康扩展:取一帧 |
| 0x90C0 | AutoFocus | 尼康扩展:自动对焦 |
| 0x90CC/0x90CD | Get/SetPictureControl | 尼康扩展:Picture Control |

**设备属性码**：
- 0xD00A WhiteBalance / 0xD00C ShutterSpeed / 0xD00E Aperture
- 0xD010 ISO / 0xD012 ExposureComp / 0xD014 FocusMode / 0xD01A ImageQuality

## 7. 构建命令

### 7.1 核心库 + 桌面端（CMake）

```bash
cd nikon-camera-app
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j
```

### 7.2 Android（Gradle）

```bash
cd nikon-camera-app/android
./gradlew assembleDebug
# 或在 Android Studio 中 Sync + Run
```

**依赖**：NDK r25+，CMake 3.22.1，JDK 17

### 7.3 测试

```bash
cd nikon-camera-app/build
ctest --output-on-failure
```

## 8. 开发流程约定

1. **先读 SPEC**：改动前先读对应规格文档（`SPEC_*.md`），理解设计意图
2. **先规格再实现**：新功能先写/更新规格，再写代码
3. **跨平台改动**：核心库改动要同时验证 Android JNI 和 Qt DesktopAPI 两端
4. **native 改动**：改 `core/` 后要同步检查 `jni_bridge.c` 和 `desktop_api.cpp` 的包装是否需要补
5. **UI 改动**：Compose 和 Qt 两端功能对齐，配色用主题常量不硬编码
6. **提交前自检**：grep 残留的硬编码、TODO、未声明引用

## 9. 已知限制 & 后续 TODO

- **传输进度**：已通过 JNI 回调实现实时更新
- **多相机支持**：当前单连接，多相机切换需断开重连
- **FTP 上传**：core 层 `camera_api_export_to_ftp` 返回 NOT_SUPPORTED（占位）
- **BLE 唤醒**：UI 有入口，HAL 层未实现
- **Picture Control 预设持久化**：桌面端用 QSettings，Android 端待加

## 10. 常见陷阱

| 陷阱 | 后果 | 正确做法 |
|---|---|---|
| ViewModel 自己 nativeCreate | 两个 handle 互不相通 | 只读 Application.cameraHandle |
| `remember(frame){decodeByteArray}` | 30fps OOM | `produceState` + `awaitDispose recycle` |
| 写公共 /DCIM | Android 10+ EACCES | `getExternalFilesDir(DCIM)` |
| PFD 不持有引用 | GC 回收 fd，native 读写失败 | Service 存 `usbPfd` 成员 |
| `LaunchedEffect(error)` + clearError | 连续错误丢失 | Channel + collect |
| 盲目 claim 所有 USB 接口 | 误声明 MTP，PTP 通信失败 | 三级优先 class=6/255/回退 |
| nativeStartTransfer 后直接标 DONE | 进度条 0→100 跳变 | 靠 progress 回调 status=2 |

---

**文档版本**：v1.0 · 2026-07-02
**代码规模**：核心 C ~2200 行 + JNI ~460 行 + Kotlin ~4500 行 + Qt ~3200 行 ≈ 10000 行
