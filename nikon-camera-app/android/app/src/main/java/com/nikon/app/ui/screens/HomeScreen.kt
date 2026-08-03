package com.nikon.app.ui.screens

import androidx.compose.animation.core.*
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Bluetooth
import androidx.compose.material.icons.filled.BurstMode
import androidx.compose.material.icons.filled.CameraAlt
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.ChevronRight
import androidx.compose.material.icons.filled.SwapVert
import androidx.compose.material.icons.filled.Usb
import androidx.compose.material.icons.filled.Videocam
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material.icons.filled.Wifi
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.scale
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.nikon.app.jni.CameraBridge
import com.nikon.app.ui.components.StatusCapsule
import com.nikon.app.ui.theme.*
import com.nikon.app.viewmodel.CameraInfo
import com.nikon.app.viewmodel.CameraViewModel

/**
 * HomeScreen — 主页 / 相机状态仪表盘
 *
 * 功能:
 *   - 未连接时显示扫描界面
 *   - 已连接时显示相机状态仪表盘 (电量/存储/参数/快捷操作)
 *   - 进入实时取景入口
 */
@Composable
fun HomeScreen(
    viewModel: CameraViewModel,
    onNavigateToLiveView: () -> Unit,
) {
    val status by viewModel.status.collectAsStateWithLifecycle()
    val cameras by viewModel.cameras.collectAsStateWithLifecycle()
    val error by viewModel.error.collectAsStateWithLifecycle()
    val serviceReady by viewModel.serviceReady.collectAsStateWithLifecycle()
    val selectedCamera = cameras.firstOrNull()

    // 连接方式选择提升到 HomeScreen 层级,避免 SCANNING↔DISCONNECTED 切换时重置
    var transport by remember { mutableStateOf(0) } // 0=USB 1=Wi-Fi 2=BLE

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(NikonBlack)
    ) {
        // ── 顶部栏 ──
        HomeTopBar(
            status = status,
            model = selectedCamera?.model,
            onDisconnect = { viewModel.disconnect() },
        )

        when (status) {
            CameraBridge.STATUS_DISCONNECTED -> {
                ScanContent(
                    isScanning = false,
                    cameras = cameras,
                    error = error,
                    serviceReady = serviceReady,
                    transport = transport,
                    onTransportChange = { transport = it },
                    onScan = { viewModel.scan() },
                    onConnect = { viewModel.connect(it) },
                    onDismissError = { viewModel.clearError() },
                )
            }
            CameraBridge.STATUS_SCANNING -> {
                ScanContent(
                    isScanning = true,
                    cameras = cameras,
                    error = null,
                    serviceReady = serviceReady,
                    transport = transport,
                    onTransportChange = { transport = it },
                    onScan = {},
                    onConnect = { viewModel.connect(it) },
                    onDismissError = {},
                )
            }
            CameraBridge.STATUS_CONNECTING -> {
                ConnectingContent()
            }
            CameraBridge.STATUS_CONNECTED, CameraBridge.STATUS_TRANSFERRING -> {
                val info = selectedCamera
                if (info != null) {
                    DashboardContent(
                        viewModel = viewModel,
                        info = info,
                        isTransferring = status == CameraBridge.STATUS_TRANSFERRING,
                        onCapture = { viewModel.capture() },
                        onBurst = { viewModel.captureBurst(5) },
                        onLiveView = onNavigateToLiveView,
                        onTransferAll = { },
                    )
                } else {
                    // 已连接但无设备信息 (USB fd 直连)
                    DashboardContent(
                        viewModel = viewModel,
                        info = CameraInfo("usb", "Nikon DSLR", "-", 0, 100, 0.0, 0.0),
                        isTransferring = status == CameraBridge.STATUS_TRANSFERRING,
                        onCapture = { viewModel.capture() },
                        onBurst = { viewModel.captureBurst(5) },
                        onLiveView = onNavigateToLiveView,
                        onTransferAll = { },
                    )
                }
            }
            CameraBridge.STATUS_ERROR -> {
                ScanContent(
                    isScanning = false,
                    cameras = cameras,
                    error = error ?: "连接错误",
                    serviceReady = serviceReady,
                    transport = transport,
                    onTransportChange = { transport = it },
                    onScan = { viewModel.scan() },
                    onConnect = { viewModel.connect(it) },
                    onDismissError = { viewModel.clearError() },
                )
            }
        }
    }
}

// ─── 顶部栏 ─────────────────────────────────────────────────

@Composable
private fun HomeTopBar(
    status: Int,
    model: String?,
    onDisconnect: () -> Unit,
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 20.dp, vertical = 14.dp)
            .padding(top = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        // Logo
        Box(
            modifier = Modifier
                .size(28.dp)
                .clip(RoundedCornerShape(6.dp))
                .background(NikonYellow),
            contentAlignment = Alignment.Center,
        ) {
            Text("N", fontSize = 14.sp, fontWeight = FontWeight.Black, color = NikonBlack)
        }
        Spacer(Modifier.width(10.dp))
        Text("Nikon Connect", fontWeight = FontWeight.Bold, fontSize = 18.sp, color = NikonText)

        Spacer(Modifier.weight(1f))

        // 全局状态胶囊
        StatusCapsule(status = status, modelName = model)

        // 已连接时显示断开按钮
        if (status == CameraBridge.STATUS_CONNECTED || status == CameraBridge.STATUS_TRANSFERRING) {
            Spacer(Modifier.width(8.dp))
            TextButton(onClick = onDisconnect) {
                Text("断开", color = NikonRed, fontSize = 12.sp)
            }
        }
    }
}

// ─── 扫描界面 ───────────────────────────────────────────────

@Composable
private fun ScanContent(
    isScanning: Boolean,
    cameras: List<CameraInfo>,
    error: String?,
    serviceReady: Boolean = true,
    transport: Int = 0,
    onTransportChange: (Int) -> Unit = {},
    onScan: () -> Unit,
    onConnect: (String) -> Unit,
    onDismissError: () -> Unit,
) {

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 24.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Spacer(Modifier.height(40.dp))

        // ✅ 错误横幅(失败时)
        if (error != null && !isScanning) {
            ErrorBanner(
                title = "连接失败",
                desc = "无法建立 USB PTP 会话",
                code = error,
                onRetry = { onScan() },
                onClose = onDismissError,
            )
            Spacer(Modifier.height(20.dp))
        }

        // 扫描动画圈
        ScanPulse(isScanning = isScanning)

        Spacer(Modifier.height(24.dp))

        if (isScanning) {
            Text("正在扫描…", fontSize = 17.sp, fontWeight = FontWeight.Bold, color = NikonText)
            Spacer(Modifier.height(8.dp))
            Text("检测 USB / Wi-Fi / 蓝牙 尼康相机设备", fontSize = 12.sp, color = NikonText2)
        } else if (error == null) {
            Text(
                when {
                    !serviceReady -> "服务初始化中…"
                    cameras.isEmpty() -> "未发现尼康设备"
                    else -> "发现的设备"
                },
                fontSize = 17.sp, fontWeight = FontWeight.Bold, color = NikonText,
            )
            Spacer(Modifier.height(6.dp))
            if (cameras.isEmpty()) {
                Text(
                    if (!serviceReady) "正在启动相机服务,请稍候…"
                    else "请连接相机后选择连接方式",
                    fontSize = 12.sp, color = NikonText2,
                )
            }
        }

        Spacer(Modifier.height(24.dp))

        // ✅ 连接方式切换 (USB / Wi-Fi / 蓝牙)
        Text(
            "连接方式",
            fontSize = 11.sp, fontWeight = FontWeight.SemiBold, color = NikonText3,
            letterSpacing = 1.5.sp,
            modifier = Modifier.fillMaxWidth().padding(bottom = 8.dp),
        )
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            listOf(Triple(0, "USB", Icons.Filled.Usb),
                Triple(1, "Wi-Fi", Icons.Filled.Wifi),
                Triple(2, "蓝牙", Icons.Filled.Bluetooth)).forEach { (idx, label, icon) ->
                val active = transport == idx
                Surface(
                    color = if (active) NikonYellow else NikonSurface,
                    shape = RoundedCornerShape(10.dp),
                    modifier = Modifier
                        .weight(1f)
                        .clickable { onTransportChange(idx) },
                ) {
                    Row(
                        Modifier.padding(vertical = 10.dp),
                        horizontalArrangement = Arrangement.Center,
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Icon(icon, null, tint = if (active) NikonBlack else NikonText, modifier = Modifier.size(15.dp))
                        Spacer(Modifier.width(5.dp))
                        Text(label, fontSize = 12.sp, fontWeight = FontWeight.Bold, color = if (active) NikonBlack else NikonText)
                    }
                }
            }
        }

        Spacer(Modifier.height(20.dp))

        // ✅ BLE 唤醒提示
        Surface(
            color = NikonYellow.copy(alpha = 0.04f),
            shape = RoundedCornerShape(12.dp),
            border = androidx.compose.foundation.BorderStroke(1.dp, NikonYellow.copy(alpha = 0.2f)),
            modifier = Modifier.fillMaxWidth(),
        ) {
            Row(Modifier.padding(14.dp), verticalAlignment = Alignment.Top) {
                Icon(
                    Icons.Filled.Bluetooth,
                    "BLE",
                    tint = NikonYellow,
                    modifier = Modifier.size(16.dp),
                )
                Spacer(Modifier.width(10.dp))
                Column {
                    Text("蓝牙 BLE 唤醒", fontSize = 12.sp, fontWeight = FontWeight.Bold, color = NikonYellow)
                    Spacer(Modifier.height(3.dp))
                    Text(
                        "相机处于休眠时,蓝牙可唤醒相机并自动切换至 USB/Wi-Fi 连接。需在相机菜单中提前启用「通过蓝牙遥控」。",
                        fontSize = 11.sp, color = NikonText2, lineHeight = 18.sp,
                    )
                }
            }
        }

        Spacer(Modifier.height(28.dp))

        // 扫描按钮(服务未就绪时禁用)
        Button(
            onClick = onScan,
            enabled = serviceReady,
            colors = ButtonDefaults.buttonColors(
                containerColor = NikonYellow,
                disabledContainerColor = NikonYellow.copy(alpha = 0.3f),
            ),
            shape = RoundedCornerShape(12.dp),
            modifier = Modifier.fillMaxWidth().height(50.dp),
        ) {
            Text(
                when {
                    !serviceReady -> "服务启动中…"
                    cameras.isEmpty() && error == null -> "扫描设备"
                    else -> "重新扫描"
                },
                color = NikonBlack, fontWeight = FontWeight.Bold, fontSize = 16.sp,
            )
        }

        // 发现的设备列表
        if (cameras.isNotEmpty()) {
            Spacer(Modifier.height(24.dp))
            cameras.forEach { cam ->
                CameraDeviceCard(camera = cam, onConnect = { onConnect(cam.id) })
                Spacer(Modifier.height(10.dp))
            }
        }
    }
}

/** 连接失败错误横幅 */
@Composable
private fun ErrorBanner(
    title: String,
    desc: String,
    code: String,
    onRetry: () -> Unit,
    onClose: () -> Unit,
) {
    Surface(
        color = NikonRed.copy(alpha = 0.08f),
        shape = RoundedCornerShape(12.dp),
        border = androidx.compose.foundation.BorderStroke(1.dp, NikonRed.copy(alpha = 0.3f)),
        modifier = Modifier.fillMaxWidth(),
    ) {
        Row(Modifier.padding(14.dp), verticalAlignment = Alignment.Top) {
            Icon(
                Icons.Filled.Warning,
                null, tint = NikonRed, modifier = Modifier.size(20.dp),
            )
            Spacer(Modifier.width(10.dp))
            Column(Modifier.weight(1f)) {
                Text(title, fontSize = 13.sp, fontWeight = FontWeight.Bold, color = NikonRed)
                Spacer(Modifier.height(2.dp))
                Text(desc, fontSize = 12.sp, color = NikonText2)
                Spacer(Modifier.height(2.dp))
                Text(code, fontSize = 10.sp, color = NikonText3)
            }
            Spacer(Modifier.width(8.dp))
            Column(horizontalAlignment = Alignment.End) {
                TextButton(onClick = onRetry, contentPadding = PaddingValues(0.dp)) {
                    Text("重试", color = NikonYellow, fontSize = 12.sp, fontWeight = FontWeight.Bold)
                }
                TextButton(onClick = onClose, contentPadding = PaddingValues(0.dp)) {
                    Text("关闭", color = NikonText3, fontSize = 11.sp)
                }
            }
        }
    }
}

@Composable
private fun CameraDeviceCard(camera: CameraInfo, onConnect: () -> Unit) {
    Surface(
        color = NikonSurface,
        shape = RoundedCornerShape(14.dp),
        modifier = Modifier.fillMaxWidth(),
    ) {
        Row(
            modifier = Modifier
                .clickable(onClick = onConnect)
                .padding(16.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(Modifier.weight(1f)) {
                Text(camera.model, fontWeight = FontWeight.Bold, fontSize = 16.sp, color = NikonText)
                Spacer(Modifier.height(4.dp))
                Row {
                    Text(camera.transportLabel, color = NikonBlue, fontSize = 12.sp)
                    Spacer(Modifier.width(12.dp))
                    Text("SN: ${camera.serial}", color = NikonText3, fontSize = 11.sp)
                }
                Spacer(Modifier.height(6.dp))
                Row(verticalAlignment = Alignment.CenterVertically) {
                    // 电量条
                    BatteryIndicator(level = camera.batteryLevel)
                    Spacer(Modifier.width(12.dp))
                    Text(
                        "${camera.batteryLevel}%",
                        color = if (camera.batteryLevel > 20) NikonGreen else NikonRed,
                        fontSize = 12.sp,
                        fontWeight = FontWeight.Medium,
                    )
                }
            }
            Icon(
                imageVector = Icons.Filled.ChevronRight,
                contentDescription = null,
                tint = NikonText3,
                modifier = Modifier.size(20.dp),
            )
        }
    }
}

// ─── 连接中 ─────────────────────────────────────────────────

@Composable
private fun ConnectingContent() {
    Column(
        modifier = Modifier.fillMaxSize(),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        CircularProgressIndicator(color = NikonYellow, modifier = Modifier.size(48.dp))
        Spacer(Modifier.height(24.dp))
        Text("正在连接…", fontWeight = FontWeight.Bold, fontSize = 17.sp, color = NikonText)
        Spacer(Modifier.height(8.dp))
        Text("建立 PTP 会话中", fontSize = 13.sp, color = NikonText2)
    }
}

// ─── 扫描脉冲动画 ───────────────────────────────────────────

@Composable
private fun ScanPulse(isScanning: Boolean) {
    // 旋转扫描弧动画
    val sweepAngle by rememberInfiniteTransition(label = "sweep").animateFloat(
        initialValue = 0f,
        targetValue = 360f,
        animationSpec = infiniteRepeatable(
            animation = tween(if (isScanning) 1200 else 4000, easing = LinearEasing),
        ),
        label = "sweep_angle",
    )
    // 脉冲环动画
    val pulseScale by rememberInfiniteTransition(label = "pulse").animateFloat(
        initialValue = 0.5f,
        targetValue = 1.3f,
        animationSpec = infiniteRepeatable(
            animation = tween(1800, easing = EaseOut),
        ),
        label = "pulse_scale",
    )
    val pulseAlpha by rememberInfiniteTransition(label = "pulse_a").animateFloat(
        initialValue = 0.7f,
        targetValue = 0f,
        animationSpec = infiniteRepeatable(
            animation = tween(1800, easing = EaseOut),
        ),
        label = "pulse_alpha",
    )

    Box(
        modifier = Modifier.size(172.dp),
        contentAlignment = Alignment.Center,
    ) {
        // 外环 (虚线旋转)
        Box(
            modifier = Modifier
                .size(172.dp)
                .graphicsLayer { rotationZ = sweepAngle * 0.1f }
                .border(
                    width = 1.5.dp,
                    brush = Brush.sweepGradient(
                        listOf(
                            Color(0x30F5B800),
                            Color.Transparent,
                            Color.Transparent,
                            Color(0x10F5B800),
                        )
                    ),
                    shape = CircleShape,
                ),
        )
        // 内环
        Box(
            modifier = Modifier
                .size(136.dp)
                .border(1.dp, Color(0xFF252528), CircleShape),
        )
        // 脉冲环 (仅扫描时)
        if (isScanning) {
            Box(
                modifier = Modifier
                    .size(172.dp)
                    .scale(pulseScale)
                    .border(2.dp, NikonYellow.copy(alpha = pulseAlpha * 0.5f), CircleShape),
            )
        }
        // 中心图标容器
        Box(
            modifier = Modifier
                .size(72.dp)
                .clip(CircleShape)
                .background(NikonSurface)
                .border(
                    2.dp,
                    if (isScanning) NikonYellow.copy(alpha = 0.5f) else Color(0xFF2A2A2E),
                    CircleShape,
                ),
            contentAlignment = Alignment.Center,
        ) {
            Icon(
                imageVector = Icons.Filled.CameraAlt,
                contentDescription = null,
                tint = if (isScanning) NikonYellow else NikonText3,
                modifier = Modifier.size(32.dp),
            )
        }
    }
}

// ─── 电池指示器 ─────────────────────────────────────────────

@Composable
fun BatteryIndicator(level: Int) {
    val color = when {
        level > 60 -> NikonGreen
        level > 20 -> NikonYellow
        else -> NikonRed
    }
    Row(verticalAlignment = Alignment.CenterVertically) {
        Box(
            modifier = Modifier
                .width(28.dp)
                .height(14.dp)
                .clip(RoundedCornerShape(3.dp))
                .background(NikonSurface3),
        ) {
            Box(
                modifier = Modifier
                    .fillMaxHeight()
                    .fillMaxWidth(level / 100f)
                    .clip(RoundedCornerShape(3.dp))
                    .background(color),
            )
        }
        // 电池帽
        Box(
            modifier = Modifier
                .width(3.dp)
                .height(6.dp)
                .clip(RoundedCornerShape(1.dp))
                .background(NikonSurface3),
        )
    }
}

// ─── 仪表盘内容 ─────────────────────────────────────────────

@Composable
private fun DashboardContent(
    viewModel: CameraViewModel,
    info: CameraInfo,
    isTransferring: Boolean,
    onCapture: () -> Unit,
    onBurst: () -> Unit,
    onLiveView: () -> Unit,
    onTransferAll: () -> Unit,
) {
    val settings by viewModel.settings.collectAsStateWithLifecycle()
    val autoTransfer = settings.autoTransfer

    LazyColumn(
        modifier = Modifier.fillMaxSize(),
        contentPadding = PaddingValues(horizontal = 20.dp, vertical = 8.dp),
        verticalArrangement = Arrangement.spacedBy(14.dp),
    ) {
        // 相机信息卡片
        item { StatusCard(info = info, isTransferring = isTransferring) }

        // 当前拍摄参数标题 + 实时同步脉冲点
        item {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text("当前拍摄参数", fontSize = 12.sp, fontWeight = FontWeight.SemiBold, color = NikonText3)
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                    SyncPulseDot()
                    Text("实时同步", fontSize = 10.sp, color = NikonYellow)
                }
            }
        }

        // 6 格参数网格(2 行 3 列,光圈/模式黄色高亮)
        item { ParamGrid(viewModel) }

        // 快速操作(2x2)
        item {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text("快速操作", fontSize = 12.sp, fontWeight = FontWeight.SemiBold, color = NikonText3)
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.fillMaxWidth()) {
                    QuickActionButton("立即拍摄", Icons.Filled.CameraAlt, NikonYellow, onCapture, Modifier.weight(1f))
                    QuickActionButton("连拍模式", Icons.Filled.BurstMode, NikonYellow, onBurst, Modifier.weight(1f), ghost = true)
                }
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.fillMaxWidth()) {
                    QuickActionButton("实时取景", Icons.Filled.Videocam, NikonBlue, onLiveView, Modifier.weight(1f))
                    QuickActionButton("传输全部", Icons.Filled.SwapVert, NikonText2, onTransferAll, Modifier.weight(1f))
                }
            }
        }

        // 边拍边传 toggle 行(接通 ViewModel)
        item {
            AutoTransferRow(
                checked = autoTransfer,
                onToggle = { viewModel.updateSettings { s -> s.copy(autoTransfer = it) } },
            )
        }
    }
}

// ─── 状态卡片 ───────────────────────────────────────────────

@Composable
private fun StatusCard(info: CameraInfo, isTransferring: Boolean) {
    Surface(
        color = NikonSurface,
        shape = RoundedCornerShape(16.dp),
        modifier = Modifier.fillMaxWidth(),
    ) {
        Column(Modifier.padding(20.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Text(info.model, fontWeight = FontWeight.Bold, fontSize = 20.sp, color = NikonText)
                    Spacer(Modifier.height(4.dp))
                    Text("序列号: ${info.serial}", color = NikonText3, fontSize = 12.sp)
                }
                // 连接状态灯
                Box(
                    modifier = Modifier
                        .size(12.dp)
                        .clip(CircleShape)
                        .background(NikonGreen),
                )
            }

            Spacer(Modifier.height(18.dp))

            // 电量 + 存储
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
            ) {
                Column {
                    Text("电量", fontSize = 11.sp, color = NikonText3)
                    Spacer(Modifier.height(6.dp))
                    BatteryIndicator(level = info.batteryLevel)
                    Spacer(Modifier.height(4.dp))
                    Text(
                        "${info.batteryLevel}%",
                        fontSize = 13.sp,
                        fontWeight = FontWeight.SemiBold,
                        color = when {
                            info.batteryLevel > 60 -> NikonGreen
                            info.batteryLevel > 20 -> NikonYellow
                            else -> NikonRed
                        },
                    )
                }

                Column(horizontalAlignment = Alignment.End) {
                    Text("存储空间", fontSize = 11.sp, color = NikonText3)
                    Spacer(Modifier.height(6.dp))
                    Text(
                        "${info.storageFreeGb.toInt()} GB 可用",
                        fontSize = 15.sp,
                        fontWeight = FontWeight.Bold,
                        color = NikonText,
                    )
                    Spacer(Modifier.height(2.dp))
                    Text(
                        "共 ${info.storageTotalGb.toInt()} GB",
                        fontSize = 12.sp,
                        color = NikonText2,
                    )
                }
            }

            // 存储进度条
            Spacer(Modifier.height(12.dp))
            val usedRatio = if (info.storageTotalGb > 0)
                ((info.storageTotalGb - info.storageFreeGb) / info.storageTotalGb).toFloat()
            else 0f

            LinearProgressIndicator(
                progress = usedRatio,
                modifier = Modifier
                    .fillMaxWidth()
                    .height(4.dp)
                    .clip(RoundedCornerShape(2.dp)),
                color = if (usedRatio < 0.7f) NikonYellow else NikonOrange,
                trackColor = NikonSurface3,
            )
        }
    }
}

// ─── 快捷操作按钮 ───────────────────────────────────────────

@Composable
private fun QuickActionButton(
    text: String,
    icon: androidx.compose.ui.graphics.vector.ImageVector,
    color: androidx.compose.ui.graphics.Color,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    ghost: Boolean = false,
) {
    Surface(
        onClick = onClick,
        color = if (ghost) NikonYellow.copy(alpha = 0.08f) else NikonSurface,
        shape = RoundedCornerShape(14.dp),
        border = if (ghost) androidx.compose.foundation.BorderStroke(1.dp, NikonYellow.copy(alpha = 0.4f)) else null,
        modifier = modifier,
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(16.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Icon(
                imageVector = icon,
                contentDescription = text,
                tint = color,
                modifier = Modifier.size(24.dp),
            )
            Spacer(Modifier.height(6.dp))
            Text(text, fontSize = 12.sp, fontWeight = FontWeight.Medium, color = NikonText)
        }
    }
}

// ─── 参数网格(6 格 2x3 + 实时同步脉冲)─────────────────────

@Composable
private fun ParamGrid(viewModel: CameraViewModel) {
    val props by viewModel.cameraProperties.collectAsStateWithLifecycle()
    val params = listOf(
        ParamItem("快门", props.shutterSpeed, highlight = false),
        ParamItem("光圈", props.aperture, highlight = true, refreshing = true),
        ParamItem("ISO", props.iso, highlight = false),
        ParamItem("EV", props.ev, highlight = false),
        ParamItem("对焦", props.focusMode, highlight = false),
        ParamItem("画质", props.imageQuality, highlight = true),
    )
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        listOf(0, 3).forEach { start ->
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.fillMaxWidth()) {
                params.subList(start, start + 3).forEach { p ->
                    ParamCell(p, Modifier.weight(1f))
                }
            }
        }
    }
}

private data class ParamItem(
    val label: String,
    val value: String,
    val highlight: Boolean = false,
    val refreshing: Boolean = false,
)

@Composable
private fun ParamCell(item: ParamItem, modifier: Modifier = Modifier) {
    Surface(
        color = NikonSurface,
        shape = RoundedCornerShape(10.dp),
        modifier = modifier,
    ) {
        Box(
            modifier = Modifier.fillMaxWidth().padding(vertical = 12.dp, horizontal = 8.dp),
            contentAlignment = Alignment.Center,
        ) {
            if (item.refreshing) {
                Box(
                    modifier = Modifier
                        .align(Alignment.TopEnd)
                        .padding(4.dp)
                        .size(5.dp)
                        .clip(CircleShape)
                        .background(NikonYellow),
                )
            }
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                Text(item.label, fontSize = 10.sp, color = NikonText3)
                Spacer(Modifier.height(3.dp))
                Text(
                    item.value,
                    fontSize = 14.sp,
                    fontWeight = FontWeight.Bold,
                    color = if (item.highlight) NikonYellow else NikonText,
                )
            }
        }
    }
}

/** 实时同步脉冲点 */
@Composable
private fun SyncPulseDot() {
    val transition = rememberInfiniteTransition(label = "sync")
    val alpha by transition.animateFloat(
        initialValue = 0.3f,
        targetValue = 1f,
        animationSpec = infiniteRepeatable(tween(1200), RepeatMode.Reverse),
        label = "sync-alpha",
    )
    Box(
        modifier = Modifier
            .size(5.dp)
            .clip(CircleShape)
            .background(NikonYellow.copy(alpha = alpha)),
    )
}

/** 边拍边传 toggle 行 */
@Composable
private fun AutoTransferRow(checked: Boolean, onToggle: (Boolean) -> Unit) {
    Surface(
        color = if (checked) NikonYellow.copy(alpha = 0.08f) else NikonSurface,
        shape = RoundedCornerShape(12.dp),
        modifier = Modifier.fillMaxWidth(),
    ) {
        Row(
            Modifier.fillMaxWidth().padding(14.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Box(
                modifier = Modifier
                    .size(32.dp)
                    .clip(CircleShape)
                    .background(if (checked) NikonYellow.copy(alpha = 0.2f) else NikonSurface3),
                contentAlignment = Alignment.Center,
            ) {
                Icon(
                    imageVector = Icons.Filled.Check,
                    contentDescription = null,
                    tint = NikonYellow,
                    modifier = Modifier.size(18.dp),
                )
            }
            Spacer(Modifier.width(12.dp))
            Column(Modifier.weight(1f)) {
                Text("自动传输已开启", fontSize = 13.sp, fontWeight = FontWeight.Medium, color = NikonText)
                Spacer(Modifier.height(2.dp))
                Text("新文件 → 自动导出到 /DCIM/NikonConnect", fontSize = 11.sp, color = NikonText3)
            }
            Switch(
                checked = checked,
                onCheckedChange = onToggle,
                colors = SwitchDefaults.colors(
                    checkedThumbColor = NikonBlack,
                    checkedTrackColor = NikonYellow,
                    uncheckedThumbColor = NikonText3,
                    uncheckedTrackColor = NikonSurface3,
                ),
            )
        }
    }
}
