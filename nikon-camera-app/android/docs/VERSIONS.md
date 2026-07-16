# 版本声明 / Version Declaration

本项目所有工具链与依赖版本集中在此声明，并与以下文件保持一致：

- `gradle.properties` —— 版本常量（单一事实来源）
- `gradle/wrapper/gradle-wrapper.properties` —— Gradle 发行版与镜像
- `settings.gradle.kts` —— AGP / Kotlin 插件版本
- `app/build.gradle.kts` —— 仅引用 `gradle.properties` 中的常量

> **修改任何版本前，请同步更新本文件。**

## 工具链版本

| 项目 | 版本 | 说明 |
|------|------|------|
| **Gradle** | **8.5** | 由 `gradle-wrapper.properties` 的 `distributionUrl` 锁定 |
| **Java (JDK)** | **17** | Azul Zulu 17（`JAVA_HOME` 指向该版本） |
| **Android Gradle Plugin (AGP)** | **8.2.2** | 在 `settings.gradle.kts` 的 `pluginManagement.plugins` 声明 |
| **Kotlin** | **1.9.22** | 插件版本同上 |
| **Kotlin 编译器扩展** | **1.5.8** | `composeOptions.kotlinCompilerExtensionVersion` |
| **Compose BOM** | **2024.02.00** | 统一管理 Compose 库版本 |
| **NDK** | **25.2.9519653** | `externalNativeBuild.cmake` |
| **compileSdk** | **34** | |
| **minSdk** | **26** | Android 8.0+，USB Host API |
| **targetSdk** | **34** | |

## ⚠️ 为什么锁定 Gradle 8.5（请勿升级到 9.x）

1. **内存限制**：本机可用内存紧张，Gradle 9.x 默认配置与 AGP 8.2.2 的组合未经验证，且 9.x 在部分环境下内存占用更高。
2. **兼容性**：AGP 8.2.2 官方验证的 Gradle 区间为 8.0–8.9，8.5 在此范围内且已实测可编译、可跑测试。
3. **镜像已就位**：`distributionUrl` 已指向腾讯云镜像（`mirrors.cloud.tencent.com/gradle/gradle-8.5-bin.zip`），无需从 `services.gradle.org` 慢速下载。

如果你在某个终端看到 `Downloading https://services.gradle.org/.../gradle-9.3.0-bin.zip`，
**那不是本项目触发的** —— 本项目永远走 wrapper 的 8.5。该下载通常来自：
- IDE（Android Studio / IntelliJ）在后台下载它默认捆绑的 Gradle 发行版；
- 你在别的、未指定 wrapper 的项目目录里跑了 `gradle` / `gradlew`。

## 如何避免 IDE 偷偷下载别的 Gradle 版本

在 IDE 设置中强制使用本项目的 wrapper：

- **Android Studio / IntelliJ**：`Settings → Build, Execution, Deployment → Build Tools → Gradle`
  - *Gradle distribution* 选择 **"Use default Gradle wrapper (recommended)"**
  - 不要选 "Gradle (default)" 或指定一个本地 9.x 安装

这样 IDE 会严格使用 `gradle-wrapper.properties` 锁定的 8.5。

## 国内镜像源（已在配置中）

- Gradle 发行版：`https://mirrors.cloud.tencent.com/gradle/`
- Gradle 插件 / 依赖：`mirrors.cloud.tencent.com/nexus/repository/maven-public/` 与 `maven.aliyun.com/repository/{google,public,gradle-plugin}`

## 标准构建命令

```bash
export JAVA_HOME=/Users/liutao/Library/Java/JavaVirtualMachines/azul-17.0.15/Contents/Home
export ANDROID_HOME=/Users/liutao/Library/Android/sdk
export PATH="$JAVA_HOME/bin:$ANDROID_HOME/platform-tools:$PATH"
cd /Volumes/LT/my_agents/z_camera/nikon-camera-app/android

# 纯 JVM 单元测试（无需设备）
./gradlew testDebugUnitTest

# 仪表化 UI 测试（需真机 / 模拟器，且必须用 ./gradlew 走 wrapper 的 8.5）
./gradlew connectedDebugAndroidTest
```

> 始终用 `./gradlew` 而非全局 `gradle`，以保证使用锁定的 Gradle 8.5。
