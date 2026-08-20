package com.nikon.app.ui.screens

import androidx.compose.animation.animateContentSize
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.platform.LocalContext
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import android.content.Intent
import android.provider.Settings
import com.nikon.app.jni.CameraBridge
import com.nikon.app.ui.theme.*
import com.nikon.app.viewmodel.CameraViewModel

/**
 * SettingsScreen — 设置页面
 *
 * 传输设置、文件格式过滤、高级传输策略、FTP、通知设置。
 */
@Composable
fun SettingsScreen(
    viewModel: com.nikon.app.viewmodel.CameraViewModel,
) {
    val status by viewModel.status.collectAsStateWithLifecycle()
    val settings by viewModel.settings.collectAsStateWithLifecycle()
    val cameras by viewModel.cameras.collectAsStateWithLifecycle()
    val connected = status == CameraBridge.STATUS_CONNECTED
    val currentCamera = cameras.firstOrNull()

    var showFtpConfig by remember { mutableStateOf(false) }
    var showStorageDialog by remember { mutableStateOf(false) }
    var showBlockSizeDialog by remember { mutableStateOf(false) }
    var showWifiDialog by remember { mutableStateOf(false) }
    var showNotifDialog by remember { mutableStateOf(false) }

    val context = LocalContext.current
    fun openNotificationSettings() = runCatching {
        val intent = Intent(Settings.ACTION_APP_NOTIFICATION_SETTINGS).apply {
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            putExtra(Settings.EXTRA_APP_PACKAGE, context.packageName)
        }
        context.startActivity(intent)
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(NikonBlack)
    ) {
        // ── 顶部栏 ──
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 20.dp, vertical = 14.dp)
                .padding(top = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(
                "设置",
                fontSize = 18.sp,
                fontWeight = FontWeight.ExtraBold,
                color = NikonText,
            )
        }

        LazyColumn(
            modifier = Modifier.fillMaxSize(),
            contentPadding = PaddingValues(horizontal = 20.dp, vertical = 8.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            // ── 设备信息 chip ──
            item {
                Surface(
                    color = NikonSurface,
                    shape = RoundedCornerShape(14.dp),
                    modifier = Modifier.fillMaxWidth(),
                ) {
                    Row(
                        Modifier.padding(14.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Box(
                            modifier = Modifier
                                .size(40.dp)
                                .clip(RoundedCornerShape(20.dp))
                                .background(NikonYellow),
                            contentAlignment = Alignment.Center,
                        ) { Text("N", fontSize = 16.sp, fontWeight = FontWeight.Black, color = NikonBlack) }
                        Spacer(Modifier.width(12.dp))
                        Column(Modifier.weight(1f)) {
                            Text(
                                if (connected && currentCamera != null)
                                    "${currentCamera.model} · 已连接"
                                else "未连接相机",
                                fontSize = 14.sp, fontWeight = FontWeight.SemiBold, color = NikonText,
                            )
                            Spacer(Modifier.height(2.dp))
                            Text(
                                if (currentCamera != null)
                                    "S/N: ${currentCamera.serial} · ${currentCamera.transportLabel}"
                                else "S/N: -- · --",
                                fontSize = 11.sp, color = NikonText3,
                            )
                        }
                        // 在线状态 pill
                        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                            Box(
                                modifier = Modifier
                                    .size(6.dp)
                                    .clip(CircleShape)
                                    .background(if (connected) NikonGreen else NikonText3),
                            )
                            Text(
                                if (connected) "在线" else "离线",
                                fontSize = 11.sp, color = if (connected) NikonGreen else NikonText3,
                            )
                        }
                    }
                }
            }

            // ── 传输设置 ──
            item { SectionLabel("传输设置") }

            item {
                ToggleRow(
                    title = "边拍边传",
                    subtitle = "新文件自动传输",
                    checked = settings.autoTransfer,
                    onToggle = { viewModel.updateSettings { s -> s.copy(autoTransfer = it) } },
                )
            }

            item {
                NavRow(
                    title = "存储目标",
                    subtitle = settings.storageTarget,
                    value = "更改",
                    onClick = { showStorageDialog = true },
                )
            }

            item {
                ToggleRow(
                    title = "断点续传",
                    subtitle = "中断后自动恢复",
                    checked = settings.resumeTransfer,
                    onToggle = { viewModel.updateSettings { s -> s.copy(resumeTransfer = it) } },
                )
            }

            item {
                ConcurrencySelector(
                    value = settings.concurrentJobs,
                    onChange = { viewModel.updateSettings { s -> s.copy(concurrentJobs = it) } }
                )
            }

            // ── 文件格式过滤 ──
            item { SectionLabel("文件格式过滤") }

            item {
                Surface(
                    color = NikonSurface,
                    shape = RoundedCornerShape(12.dp),
                    modifier = Modifier.fillMaxWidth(),
                ) {
                    Column(Modifier.padding(16.dp)) {
                        Text(
                            "选择需要传输的文件格式",
                            fontSize = 12.sp,
                            color = NikonText2,
                        )
                        Spacer(Modifier.height(12.dp))
                        FormatChip(text = "JPEG", checked = settings.formatJpg, onToggle = { viewModel.updateSettings { s -> s.copy(formatJpg = it) } })
                        Spacer(Modifier.height(8.dp))
                        FormatChip(text = "NEF (RAW)", checked = settings.formatNef, onToggle = { viewModel.updateSettings { s -> s.copy(formatNef = it) } })
                        Spacer(Modifier.height(8.dp))
                        FormatChip(text = "MOV (视频)", checked = settings.formatMov, onToggle = { viewModel.updateSettings { s -> s.copy(formatMov = it) } })
                        Spacer(Modifier.height(10.dp))
                        Text(
                            "当前: ${listOfNotNull(
                                if (settings.formatJpg) "JPEG" else null,
                                if (settings.formatNef) "NEF" else null,
                                if (settings.formatMov) "MOV" else null,
                            ).ifEmpty { listOf("无") }.joinToString(" + ")}",
                            fontSize = 11.sp,
                            color = NikonText3,
                        )
                    }
                }
            }

            // ── 高级传输策略 ──
            item { SectionLabel("高级传输策略") }

            item {
                BlockSizeRow(
                    value = settings.transferBlockSize,
                    onClick = { showBlockSizeDialog = true },
                )
            }

            item {
                ToggleRow(
                    title = "速度自适应",
                    subtitle = "根据传输速度调整块大小",
                    checked = settings.speedAdaptive,
                    onToggle = { viewModel.updateSettings { s -> s.copy(speedAdaptive = it) } },
                )
            }

            item {
                ToggleRow(
                    title = "小文件优先",
                    subtitle = "< 4MB 文件优先传输 (JPG)",
                    checked = settings.smallFileFirst,
                    onToggle = { viewModel.updateSettings { s -> s.copy(smallFileFirst = it) } },
                )
            }

            // ── FTP 设置 ──
            item { SectionLabel("FTP 设置") }

            item {
                ExpandableCard(
                    title = "FTP 服务器",
                    subtitle = "192.168.1.100:21",
                    expanded = showFtpConfig,
                    onToggle = { showFtpConfig = it },
                ) {
                    Column(Modifier.padding(top = 12.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                        // 加密存储不可用时的安全警告(密码将明文落盘)
                        if (!viewModel.secretStorageAvailable) {
                            Row(
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .clip(RoundedCornerShape(8.dp))
                                    .background(NikonRed.copy(alpha = 0.12f))
                                    .padding(horizontal = 10.dp, vertical = 8.dp),
                                verticalAlignment = Alignment.CenterVertically,
                            ) {
                                Icon(
                                    Icons.Filled.Warning, null,
                                    tint = NikonRed, modifier = Modifier.size(18.dp),
                                )
                                Spacer(Modifier.width(8.dp))
                                Text(
                                    "系统加密不可用, 密码将以明文存储, 请注意安全",
                                    fontSize = 12.sp,
                                    color = NikonRed,
                                )
                            }
                        }
                        OutlinedTextField(
                            value = settings.ftpHost,
                            onValueChange = { viewModel.updateSettings { s -> s.copy(ftpHost = it) } },
                            label = { Text("FTP 主机", fontSize = 12.sp) },
                            singleLine = true,
                            modifier = Modifier.fillMaxWidth(),
                        )
                        OutlinedTextField(
                            value = settings.ftpPort.toString(),
                            onValueChange = { text ->
                                val p = text.toIntOrNull() ?: 21
                                viewModel.updateSettings { s -> s.copy(ftpPort = p) }
                            },
                            label = { Text("端口", fontSize = 12.sp) },
                            singleLine = true,
                            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                            modifier = Modifier.fillMaxWidth(),
                        )
                        OutlinedTextField(
                            value = settings.ftpUsername,
                            onValueChange = { viewModel.updateSettings { s -> s.copy(ftpUsername = it) } },
                            label = { Text("用户名 (空 = anonymous)", fontSize = 12.sp) },
                            singleLine = true,
                            modifier = Modifier.fillMaxWidth(),
                        )
                        OutlinedTextField(
                            value = settings.ftpPassword,
                            onValueChange = { viewModel.updateSettings { s -> s.copy(ftpPassword = it) } },
                            label = { Text("密码", fontSize = 12.sp) },
                            singleLine = true,
                            visualTransformation = PasswordVisualTransformation(),
                            modifier = Modifier.fillMaxWidth(),
                        )
                        OutlinedTextField(
                            value = settings.ftpRemotePath,
                            onValueChange = { viewModel.updateSettings { s -> s.copy(ftpRemotePath = it) } },
                            label = { Text("远端目录", fontSize = 12.sp) },
                            singleLine = true,
                            modifier = Modifier.fillMaxWidth(),
                        )
                        Spacer(Modifier.height(2.dp))
                        ToggleRow(
                            title = "FTPS 加密",
                            subtitle = "TLS 加密传输 (需 TLS 库, 当前降级明文)",
                            checked = settings.ftpsEncryption,
                            onToggle = { viewModel.updateSettings { s -> s.copy(ftpsEncryption = it) } },
                            compact = true,
                        )
                        ToggleRow(
                            title = "自动上传",
                            subtitle = "传输完成后上传至 FTP",
                            checked = settings.ftpAutoUpload,
                            onToggle = { viewModel.updateSettings { s -> s.copy(ftpAutoUpload = it) } },
                            compact = true,
                        )
                    }
                }
            }

            // ── 连接偏好 ──
            item { SectionLabel("连接偏好") }

            item {
                ToggleRow(
                    title = "优先 USB 连接",
                    subtitle = "USB 可用时自动切换",
                    checked = settings.preferUsb,
                    onToggle = { viewModel.updateSettings { s -> s.copy(preferUsb = it) } },
                )
            }

            item {
                NavRow(
                    title = "Wi-Fi 轮询间隔",
                    subtitle = "无线模式检测频率",
                    value = "${settings.wifiPollIntervalMs}ms",
                    onClick = { showWifiDialog = true },
                )
            }

            // ── 通知设置 ──
            item { SectionLabel("通知设置") }

            item {
                ToggleRow(
                    title = "传输完成通知",
                    subtitle = "批量传输完成后推送",
                    checked = settings.notifyComplete,
                    onToggle = { viewModel.updateSettings { s -> s.copy(notifyComplete = it) } },
                )
            }

            item {
                ToggleRow(
                    title = "传输失败通知",
                    subtitle = "失败时立即推送报警",
                    checked = settings.notifyFail,
                    onToggle = { viewModel.updateSettings { s -> s.copy(notifyFail = it) } },
                )
            }

            item {
                NavRow(
                    title = "通知权限",
                    subtitle = "系统通知已授权",
                    value = "已允许",
                    valueColor = NikonGreen,
                    onClick = { showNotifDialog = true },
                )
            }

            // ── 设备 ──
            item { SectionLabel("设备") }

            item {
                Surface(
                    color = NikonSurface,
                    shape = RoundedCornerShape(12.dp),
                    modifier = Modifier
                        .fillMaxWidth()
                        .clickable { viewModel.disconnect() },
                ) {
                    Row(
                        Modifier.padding(16.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Icon(Icons.Filled.PowerSettingsNew, null, tint = NikonRed, modifier = Modifier.size(20.dp))
                        Spacer(Modifier.width(12.dp))
                        Column(Modifier.weight(1f)) {
                            Text("断开连接", fontSize = 14.sp, fontWeight = FontWeight.Medium, color = NikonRed)
                            Spacer(Modifier.height(2.dp))
                            Text("终止当前相机会话", fontSize = 12.sp, color = NikonText2)
                        }
                        Icon(Icons.Filled.ChevronRight, null, tint = NikonText3, modifier = Modifier.size(18.dp))
                    }
                }
            }

            // 版本信息
            item {
                Spacer(Modifier.height(16.dp))
                Text(
                    "Nikon Camera Connect v2.0.0",
                    fontSize = 12.sp,
                    color = NikonText3,
                    modifier = Modifier.fillMaxWidth().padding(bottom = 4.dp),
                )
            }
            item {
                Text(
                    "© 2026 · 基于 PTP/MTP 协议 · 全功能版",
                    fontSize = 10.sp,
                    color = NikonText3,
                    modifier = Modifier.fillMaxWidth().padding(bottom = 40.dp),
                )
            }

        }

        // ── 设置对话框:把原本带箭头却点不动的入口接上真实交互 ──
        if (showStorageDialog) {
            AlertDialog(
                onDismissRequest = { showStorageDialog = false },
                title = { Text("选择存储位置", color = NikonText) },
                text = {
                    Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
                        listOf(
                            "/DCIM/NikonConnect",
                            "/DCIM",
                            "/Pictures/Nikon",
                            "/storage/emulated/0/Nikon",
                        ).forEach { path ->
                            TextButton(onClick = {
                                viewModel.updateSettings { it.copy(storageTarget = path) }
                                showStorageDialog = false
                            }) {
                                Text(
                                    path,
                                    color = if (settings.storageTarget == path) NikonYellow else NikonText,
                                )
                            }
                        }
                    }
                },
                confirmButton = {
                    TextButton(onClick = { showStorageDialog = false }) { Text("取消") }
                },
            )
        }

        if (showBlockSizeDialog) {
            AlertDialog(
                onDismissRequest = { showBlockSizeDialog = false },
                title = { Text("传输块大小", color = NikonText) },
                text = {
                    Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
                        listOf("自动", "1MB", "2MB", "4MB").forEach { opt ->
                            TextButton(onClick = {
                                viewModel.updateSettings { it.copy(transferBlockSize = opt) }
                                showBlockSizeDialog = false
                            }) {
                                Text(
                                    opt,
                                    color = if (settings.transferBlockSize == opt) NikonYellow else NikonText,
                                )
                            }
                        }
                    }
                },
                confirmButton = {
                    TextButton(onClick = { showBlockSizeDialog = false }) { Text("取消") }
                },
            )
        }

        if (showWifiDialog) {
            AlertDialog(
                onDismissRequest = { showWifiDialog = false },
                title = { Text("Wi-Fi 轮询间隔", color = NikonText) },
                text = {
                    Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
                        listOf(
                            500 to "500ms",
                            1000 to "1000ms",
                            2000 to "2000ms",
                            5000 to "5000ms",
                        ).forEach { (ms, label) ->
                            TextButton(onClick = {
                                viewModel.updateSettings { it.copy(wifiPollIntervalMs = ms) }
                                showWifiDialog = false
                            }) {
                                Text(
                                    label,
                                    color = if (settings.wifiPollIntervalMs == ms) NikonYellow else NikonText,
                                )
                            }
                        }
                    }
                },
                confirmButton = {
                    TextButton(onClick = { showWifiDialog = false }) { Text("取消") }
                },
            )
        }

        if (showNotifDialog) {
            AlertDialog(
                onDismissRequest = { showNotifDialog = false },
                title = { Text("通知权限", color = NikonText) },
                text = {
                    Text(
                        "系统通知已授权。你可以前往系统设置调整 Nikon Camera Connect 的通知偏好。",
                        color = NikonText2,
                    )
                },
                confirmButton = {
                    TextButton(onClick = { showNotifDialog = false }) { Text("确定") }
                },
                dismissButton = {
                    TextButton(onClick = {
                        showNotifDialog = false
                        openNotificationSettings()
                    }) { Text("打开系统设置") }
                },
            )
        }
    }
}

// ─── 组件 ───────────────────────────────────────────────────

@Composable
private fun SectionLabel(text: String) {
    Text(
        text.uppercase(),
        fontSize = 11.sp,
        fontWeight = FontWeight.SemiBold,
        color = NikonText3,
        letterSpacing = 1.5.sp,
        modifier = Modifier.padding(top = 16.dp, bottom = 4.dp),
    )
}

@Composable
private fun ToggleRow(
    title: String,
    subtitle: String,
    checked: Boolean,
    onToggle: (Boolean) -> Unit,
    compact: Boolean = false,
) {
    Surface(
        color = NikonSurface,
        shape = RoundedCornerShape(12.dp),
        modifier = Modifier.fillMaxWidth(),
    ) {
        Row(
            modifier = Modifier
                .clickable { onToggle(!checked) }
                .padding(horizontal = 16.dp, vertical = if (compact) 8.dp else 14.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(Modifier.weight(1f)) {
                Text(title, fontWeight = FontWeight.Medium, fontSize = 14.sp, color = NikonText)
                Spacer(Modifier.height(2.dp))
                Text(subtitle, fontSize = 12.sp, color = NikonText2)
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

@Composable
private fun ConcurrencySelector(value: Int, onChange: (Int) -> Unit) {
    Surface(
        color = NikonSurface,
        shape = RoundedCornerShape(12.dp),
        modifier = Modifier.fillMaxWidth(),
    ) {
        Row(
            modifier = Modifier.padding(16.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(Modifier.weight(1f)) {
                Text("并发传输数", fontWeight = FontWeight.Medium, fontSize = 14.sp, color = NikonText)
                Text("最大同时任务数", fontSize = 12.sp, color = NikonText2)
            }
            Row(verticalAlignment = Alignment.CenterVertically) {
                IconButton(
                    onClick = { if (value > 1) onChange(value - 1) },
                    modifier = Modifier.size(32.dp),
                ) {
                    Icon(Icons.Filled.Remove, null, tint = NikonText2, modifier = Modifier.size(18.dp))
                }
                Text(
                    "$value",
                    fontWeight = FontWeight.Bold,
                    fontSize = 18.sp,
                    color = NikonYellow,
                    modifier = Modifier.width(32.dp),
                )
                IconButton(
                    onClick = { if (value < 8) onChange(value + 1) },
                    modifier = Modifier.size(32.dp),
                ) {
                    Icon(Icons.Filled.Add, null, tint = NikonText2, modifier = Modifier.size(18.dp))
                }
            }
        }
    }
}

@Composable
private fun FormatChip(text: String, checked: Boolean, onToggle: (Boolean) -> Unit) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clickable { onToggle(!checked) },
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.SpaceBetween,
    ) {
        Text(text, fontSize = 14.sp, color = if (checked) NikonText else NikonText2)
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

@Composable
private fun NavRow(
    title: String,
    subtitle: String,
    value: String,
    valueColor: Color = NikonText2,
    onClick: () -> Unit,
) {
    Surface(
        color = NikonSurface,
        shape = RoundedCornerShape(12.dp),
        modifier = Modifier
            .fillMaxWidth()
            .clickable { onClick() },
    ) {
        Row(
            modifier = Modifier.padding(16.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(Modifier.weight(1f)) {
                Text(title, fontWeight = FontWeight.Medium, fontSize = 14.sp, color = NikonText)
                Spacer(Modifier.height(2.dp))
                Text(subtitle, fontSize = 12.sp, color = NikonText2)
            }
            Text(value, fontSize = 13.sp, fontWeight = FontWeight.Medium, color = valueColor)
            Spacer(Modifier.width(4.dp))
            Icon(Icons.Filled.ChevronRight, null, tint = NikonText3, modifier = Modifier.size(18.dp))
        }
    }
}

@Composable
private fun BlockSizeRow(
    value: String,
    onClick: () -> Unit,
) {
    Surface(
        color = NikonSurface,
        shape = RoundedCornerShape(12.dp),
        modifier = Modifier
            .fillMaxWidth()
            .clickable { onClick() },
    ) {
        Row(
            modifier = Modifier.padding(16.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(Modifier.weight(1f)) {
                Text("传输块大小", fontWeight = FontWeight.Medium, fontSize = 14.sp, color = NikonText)
                Text("当前: $value (1MB~4MB)", fontSize = 12.sp, color = NikonText2)
            }
            Icon(Icons.Filled.ChevronRight, null, tint = NikonText3, modifier = Modifier.size(18.dp))
        }
    }
}

@Composable
private fun ExpandableCard(
    title: String,
    subtitle: String,
    expanded: Boolean,
    onToggle: (Boolean) -> Unit,
    content: @Composable () -> Unit,
) {
    Surface(
        color = NikonSurface,
        shape = RoundedCornerShape(12.dp),
        modifier = Modifier
            .fillMaxWidth()
            .animateContentSize(),
    ) {
        Column {
            Row(
                modifier = Modifier
                    .clickable { onToggle(!expanded) }
                    .padding(16.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Column(Modifier.weight(1f)) {
                    Text(title, fontWeight = FontWeight.Medium, fontSize = 14.sp, color = NikonText)
                    Text(subtitle, fontSize = 12.sp, color = NikonText2)
                }
                Icon(
                    if (expanded) Icons.Filled.ExpandLess else Icons.Filled.ExpandMore,
                    null,
                    tint = NikonText3,
                    modifier = Modifier.size(20.dp),
                )
            }
            if (expanded) {
                Box(Modifier.padding(start = 16.dp, end = 16.dp, bottom = 16.dp)) {
                    content()
                }
            }
        }
    }
}
