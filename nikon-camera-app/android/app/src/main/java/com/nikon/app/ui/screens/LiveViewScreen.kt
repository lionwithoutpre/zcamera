package com.nikon.app.ui.screens

import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material.icons.filled.BurstMode
import androidx.compose.material.icons.filled.Collections
import androidx.compose.material.icons.filled.Videocam
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.produceState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.nikon.app.ui.components.CameraParamRanges
import com.nikon.app.ui.components.ParameterRuler
import com.nikon.app.ui.theme.*

/**
 * LiveViewScreen — 实时取景全屏页面
 *
 * 对齐 ui-design.html 第 3 页设计:
 *  - 退出 LV 胶囊按钮(毛玻璃)
 *  - 全屏取景占位(渐变背景 + 网格线 + 对焦框 + REC 指示 + 直方图)
 *  - AF 模式三段切换(MF / AF-C / AF-S)
 *  - HUD 参数可点击,弹出参数调节浮层(− 值 ＋)
 *  - 变焦控制(竖排 +/1.0×/-)
 *  - 底部参数展示行 + 快门行(连拍控制 + 连拍计数 + 快门 + 相册)
 *
 * 注:取景画面占位 —— 真实 LiveView 帧需 native 层提供 Surface/纹理回调,
 *     ViewModel 暂未暴露该接口。AF/变焦/参数调节通过 setProperty(PTP 属性码)下发。
 */
@Composable
fun LiveViewScreen(
    viewModel: com.nikon.app.viewmodel.CameraViewModel,
    onBack: () -> Unit,
    onNavigateToGallery: () -> Unit = {},
) {
    // ── 本地交互状态 ──
    var afMode by remember { mutableStateOf("AF-C") }
    var zoom by remember { mutableStateOf(0.5f) }          // 0..1, 0.5 = 1.0×
    var burstCount by remember { mutableIntStateOf(5) }
    var adjustingParam by remember { mutableStateOf<String?>(null) }  // null=隐藏浮层

    // 拍摄参数来自 ViewModel 轮询(连接后每 2s 刷新)
    val props by viewModel.cameraProperties.collectAsStateWithLifecycle()
    val cameras by viewModel.cameras.collectAsStateWithLifecycle()
    val status by viewModel.status.collectAsStateWithLifecycle()
    val connected = status == com.nikon.app.jni.CameraBridge.STATUS_CONNECTED ||
                    status == com.nikon.app.jni.CameraBridge.STATUS_TRANSFERRING
    val batteryLevel = cameras.firstOrNull()?.batteryLevel ?: 0
    val shutter = props.shutterSpeed
    val aperture = props.aperture
    val iso = props.iso
    val ev = props.ev

    // 实时取景帧:进页面 startLiveView,退出 stopLiveView(仅连接时)
    val lvFrame by viewModel.liveViewFrame.collectAsStateWithLifecycle()
    val lvActive by viewModel.liveViewActive.collectAsStateWithLifecycle()
    DisposableEffect(Unit) {
        if (connected) viewModel.startLiveView()
        onDispose { viewModel.stopLiveView() }
    }

    // 未连接时显示提示并允许返回
    if (!connected) {
        Box(
            modifier = Modifier.fillMaxSize().background(Color.Black),
            contentAlignment = Alignment.Center,
        ) {
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                Icon(
                    Icons.Filled.Videocam,
                    null,
                    tint = NikonText3,
                    modifier = Modifier.size(48.dp),
                )
                Spacer(Modifier.height(16.dp))
                Text("未连接相机", fontSize = 16.sp, fontWeight = FontWeight.Bold, color = NikonText2)
                Spacer(Modifier.height(6.dp))
                Text("请先在主页连接相机后再进入实时取景", fontSize = 12.sp, color = NikonText3)
                Spacer(Modifier.height(24.dp))
                Surface(
                    onClick = onBack,
                    color = NikonYellow,
                    shape = RoundedCornerShape(10.dp),
                ) {
                    Text(
                        "返回主页",
                        modifier = Modifier.padding(horizontal = 24.dp, vertical = 10.dp),
                        color = NikonBlack,
                        fontWeight = FontWeight.Bold,
                        fontSize = 14.sp,
                    )
                }
            }
        }
        return
    }

    Box(modifier = Modifier.fillMaxSize().background(Color.Black)) {

        // ── 1. 取景画面:有帧时渲染 JPEG,无帧时占位 ──
        if (lvFrame != null) {
            LiveViewFrame(frame = lvFrame!!)
        } else {
            ViewfinderPlaceholder(loading = lvActive)
        }

        // ── 2. 顶部浮层:退出 LV 胶囊 ──
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp, vertical = 12.dp)
                .padding(top = 24.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            IconButton(
                onClick = onBack,
                modifier = Modifier
                    .size(40.dp)
                    .clip(CircleShape)
                    .background(Color.Black.copy(alpha = 0.5f)),
            ) {
                Icon(Icons.Filled.ArrowBack, "返回", tint = NikonText, modifier = Modifier.size(20.dp))
            }
            Spacer(Modifier.weight(1f))
            // 退出 LV 胶囊(毛玻璃模拟)
            Surface(
                color = Color.Black.copy(alpha = 0.6f),
                shape = RoundedCornerShape(8.dp),
                border = androidx.compose.foundation.BorderStroke(1.dp, Color.White.copy(alpha = 0.12f)),
                modifier = Modifier.clickable(onClick = onBack),
            ) {
                Row(
                    modifier = Modifier.padding(horizontal = 10.dp, vertical = 5.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(5.dp),
                ) {
                    Icon(
                        Icons.Filled.ArrowBack, "退出 LV",
                        tint = NikonText, modifier = Modifier.size(14.dp),
                    )
                    Text("退出 LV", fontSize = 11.sp, fontWeight = FontWeight.Bold, color = NikonText)
                }
            }
            Spacer(Modifier.width(8.dp))
            Text("${batteryLevel}%", fontSize = 11.sp, fontWeight = FontWeight.Bold, color = NikonText)
        }

        // ── 3. AF 模式三段切换(顶部居中)──
        AfModeSelector(
            current = afMode,
            onSelect = { mode ->
                afMode = mode
                // AF 模式映射:MF=0, AF-S=1, AF-C=2, AF-F=3 (对应 camera_api.h FocusMode)
                val propVal = when (mode) {
                    "MF"   -> 0L
                    "AF-S" -> 1L
                    "AF-C" -> 2L
                    "AF-F" -> 3L
                    else   -> 2L
                }
                viewModel.setProperty(com.nikon.app.jni.CameraBridge.PROP_FOCUS_MODE, propVal)
            },
            modifier = Modifier
                .align(Alignment.TopCenter)
                .padding(top = 76.dp),
        )

        // ── 4. 变焦控制(右侧竖排)──
        ZoomControl(
            zoom = zoom,
            onZoomIn = {
                if (zoom < 1f) {
                    zoom = (zoom + 0.1f).coerceAtMost(1f)
                    viewModel.setProperty(LvProp.ZOOM, (zoom * 10).toLong())
                }
            },
            onZoomOut = {
                if (zoom > 0f) {
                    zoom = (zoom - 0.1f).coerceAtLeast(0f)
                    viewModel.setProperty(LvProp.ZOOM, (zoom * 10).toLong())
                }
            },
            modifier = Modifier
                .align(Alignment.CenterEnd)
                .padding(end = 10.dp),
        )

        // ── 5. 参数刻度尺(点击参数时展开) ──
        // (已移至底部控制区内)

        // ── 6. 底部控制区:参数刻度尺 + 参数展示行 + 快门行 ──
        Column(
            modifier = Modifier
                .align(Alignment.BottomCenter)
                .fillMaxWidth()
                .padding(bottom = 28.dp)
                .padding(horizontal = 18.dp),
        ) {
            // 参数刻度尺 (点击参数后展开)
            adjustingParam?.let { param ->
                val (label, values, current) = when (param) {
                    "快门" -> Triple("SHUTTER", CameraParamRanges.SHUTTER, shutter)
                    "光圈" -> Triple("APERTURE", CameraParamRanges.APERTURE, aperture)
                    "ISO"  -> Triple("ISO", CameraParamRanges.ISO, iso)
                    "EV"   -> Triple("EV", CameraParamRanges.EV, ev)
                    else   -> Triple("", emptyList(), "")
                }
                if (values.isNotEmpty()) {
                    ParameterRuler(
                        label = label,
                        values = values,
                        currentValue = current,
                        onValueSelected = { newVal ->
                            // 模拟设置参数 (实际应通过 PTP 下发)
                            adjustingParam = null
                        },
                        modifier = Modifier.padding(bottom = 8.dp),
                    )
                }
            }

            // HUD 参数展示行(4 列,可点击触发刻度尺)
            ParamDisplayRow(
                shutter = shutter,
                aperture = aperture,
                iso = iso,
                ev = ev,
                activeParam = adjustingParam ?: "",
                onParamClick = { param ->
                    adjustingParam = if (adjustingParam == param) null else param
                },
            )
            Spacer(Modifier.height(12.dp))
            // 快门行:连拍控制 + 连拍计数 + 快门按钮 + 相册
            ShutterRow(
                burstCount = burstCount,
                onShutter = { viewModel.capture() },
                onBurstToggle = {
                    viewModel.captureBurst(burstCount)
                },
                onGallery = onNavigateToGallery,
            )
        }
    }
}

// ─── PTP 属性码占位(实际码以 SPEC_PROTOCOL 为准)──
private object LvProp {
    const val AF_MODE   = 0x500C
    const val ZOOM      = 0x5010
    const val SHUTTER   = 0x500D
    const val APERTURE  = 0x500E
    const val ISO       = 0x500F
    const val EV        = 0x5011
}

/** 参数名 → PTP 属性码 */
private fun propIdFor(param: String): Int = when (param) {
    "快门" -> LvProp.SHUTTER
    "光圈" -> LvProp.APERTURE
    "ISO"  -> LvProp.ISO
    "EV"   -> LvProp.EV
    else   -> 0
}

// ────────────────────────────────────────────────────────────
// 子组件
// ────────────────────────────────────────────────────────────

/** 真实取景帧渲染:JPEG 字节 → Bitmap → Image,fillMaxSize 裁剪居中
 *  内存管理:用 produceState 持有当前 Bitmap,帧切换时回收旧帧,避免竞态 */
@Composable
private fun BoxScope.LiveViewFrame(frame: ByteArray) {
    // produceState 保证:每次 frame 变化时在新协程解码,recycle 上一帧
    // 旧 Bitmap 一定在 produceState 的 awaitDispose 里被回收,渲染层不会引用已 recycle 的
    val bitmapState by produceState<android.graphics.Bitmap?>(initialValue = null, frame) {
        val newBitmap = android.graphics.BitmapFactory.decodeByteArray(frame, 0, frame.size)
        value = newBitmap
        awaitDispose {
            newBitmap?.let { if (!it.isRecycled) it.recycle() }
        }
    }

    val bmp = bitmapState
    if (bmp != null && !bmp.isRecycled) {
        Image(
            bitmap = bmp.asImageBitmap(),
            contentDescription = "实时取景画面",
            contentScale = androidx.compose.ui.layout.ContentScale.Crop,
            modifier = Modifier.fillMaxSize(),
        )
        // 网格线叠加(三分法)
        CanvasGridOverlay(modifier = Modifier.fillMaxSize())
        // 对焦框
        Box(
            modifier = Modifier
                .align(Alignment.Center)
                .size(80.dp)
                .border(1.5.dp, NikonYellow.copy(alpha = 0.8f)),
        )
        // REC 指示
        Row(
            modifier = Modifier
                .align(Alignment.TopStart)
                .padding(top = 130.dp, start = 16.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(5.dp),
        ) {
            PulsingDot(color = Color(0xFFE03A3A))
            Text("LV", fontSize = 12.sp, fontWeight = FontWeight.Bold, color = NikonText)
        }
    } else {
        // JPEG 解码失败,回退占位
        ViewfinderPlaceholder(loading = true)
    }
}

/** 取景占位:渐变背景 + 网格线 + 对焦框 + REC 指示 + 直方图 */
@Composable
private fun BoxScope.ViewfinderPlaceholder(loading: Boolean = false) {
    Box(modifier = Modifier.fillMaxSize()) {
        // 渐变背景模拟取景画面
        Box(
            modifier = Modifier
                .fillMaxSize()
                .background(
                    Brush.linearGradient(
                        colors = listOf(
                            Color(0xFF1A2332),
                            Color(0xFF0D1A2A),
                            Color(0xFF1A1500),
                            Color(0xFF0A0A0A),
                        ),
                    ),
                ),
        )
        // 网格线(三分法)
        CanvasGridOverlay(modifier = Modifier.fillMaxSize())
        // 对焦框
        Box(
            modifier = Modifier
                .align(Alignment.Center)
                .size(80.dp)
                .border(1.5.dp, NikonYellow.copy(alpha = 0.8f)),
        )
        // REC 指示
        Row(
            modifier = Modifier
                .align(Alignment.TopStart)
                .padding(top = 130.dp, start = 16.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(5.dp),
        ) {
            PulsingDot(color = Color(0xFFE03A3A))
            Text("LV", fontSize = 12.sp, fontWeight = FontWeight.Bold, color = NikonText)
        }
        // 直方图(9 根柱)
        HistogramBars(
            modifier = Modifier
                .align(Alignment.BottomStart)
                .padding(bottom = 160.dp, start = 16.dp),
        )
    }
}

/** 网格线三分法 */
@Composable
private fun CanvasGridOverlay(modifier: Modifier = Modifier) {
    androidx.compose.foundation.Canvas(modifier = modifier) {
        val w = size.width
        val h = size.height
        val grid = Color.White.copy(alpha = 0.15f)
        // 竖线
        for (i in 1..2) {
            drawLine(grid, start = androidx.compose.ui.geometry.Offset(w * i / 3, 0f),
                end = androidx.compose.ui.geometry.Offset(w * i / 3, h), strokeWidth = 1f)
        }
        // 横线
        for (i in 1..2) {
            drawLine(grid, start = androidx.compose.ui.geometry.Offset(0f, h * i / 3),
                end = androidx.compose.ui.geometry.Offset(w, h * i / 3), strokeWidth = 1f)
        }
    }
}

/** 直方图柱 */
@Composable
private fun HistogramBars(modifier: Modifier = Modifier) {
    val heights = listOf(0.20f, 0.35f, 0.60f, 0.90f, 0.80f, 0.65f, 0.45f, 0.25f, 0.15f)
    Row(
        modifier = modifier
            .width(72.dp)
            .height(40.dp),
        horizontalArrangement = Arrangement.spacedBy(2.dp),
        verticalAlignment = Alignment.Bottom,
    ) {
        heights.forEach { h ->
            Box(
                modifier = Modifier
                    .weight(1f)
                    .fillMaxHeight(h)
                    .background(NikonYellow.copy(alpha = 0.6f)),
            )
        }
    }
}

/** 心跳脉冲点 */
@Composable
private fun PulsingDot(color: Color) {
    val transition = rememberInfiniteTransition(label = "rec")
    val alpha by transition.animateFloat(
        initialValue = 0.3f,
        targetValue = 1f,
        animationSpec = infiniteRepeatable(tween(800), RepeatMode.Reverse),
        label = "rec-alpha",
    )
    Box(
        modifier = Modifier
            .size(8.dp)
            .clip(CircleShape)
            .background(color.copy(alpha = alpha)),
    )
}

/** AF 模式三段切换 */
@Composable
private fun AfModeSelector(
    current: String,
    onSelect: (String) -> Unit,
    modifier: Modifier = Modifier,
) {
    val modes = listOf("MF", "AF-C", "AF-S")
    Surface(
        color = Color.Black.copy(alpha = 0.5f),
        shape = RoundedCornerShape(8.dp),
        modifier = modifier,
    ) {
        Row(modifier = Modifier.padding(3.dp)) {
            modes.forEach { mode ->
                val active = mode == current
                Box(
                    modifier = Modifier
                        .clip(RoundedCornerShape(6.dp))
                        .background(if (active) NikonYellow else Color.Transparent)
                        .clickable { onSelect(mode) }
                        .padding(horizontal = 14.dp, vertical = 5.dp),
                ) {
                    Text(
                        mode,
                        fontSize = 12.sp,
                        fontWeight = FontWeight.Bold,
                        color = if (active) NikonBlack else NikonText2,
                    )
                }
            }
        }
    }
}

/** 变焦控制(竖排 +/1.0×/-) */
@Composable
private fun ZoomControl(
    zoom: Float,
    onZoomIn: () -> Unit,
    onZoomOut: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val label = "%.1f×".format(0.5f + zoom)  // 0.5→1.0×, 1.0→1.5×
    Surface(
        color = Color.Black.copy(alpha = 0.5f),
        shape = RoundedCornerShape(8.dp),
        modifier = modifier,
    ) {
        Column(
            modifier = Modifier.padding(vertical = 6.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            ZoomBtn("＋", onZoomIn)
            Text(label, fontSize = 11.sp, fontWeight = FontWeight.Bold, color = NikonYellow)
            ZoomBtn("－", onZoomOut)
        }
    }
}

@Composable
private fun ZoomBtn(text: String, onClick: () -> Unit) {
    Box(
        modifier = Modifier
            .size(32.dp)
            .clip(CircleShape)
            .background(NikonSurface3)
            .clickable(onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Text(text, fontSize = 16.sp, fontWeight = FontWeight.Bold, color = NikonText)
    }
}

/** 参数调节浮层(− 标签 值 ＋) */
@Composable
private fun ParamAdjusterOverlay(
    param: String,
    value: String,
    onDecrement: () -> Unit,
    onIncrement: () -> Unit,
    onDismiss: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Surface(
        color = Color.Black.copy(alpha = 0.75f),
        shape = RoundedCornerShape(12.dp),
        border = androidx.compose.foundation.BorderStroke(1.dp, NikonYellow.copy(alpha = 0.5f)),
        modifier = modifier.clickable(onClick = onDismiss),
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            AdjBtn("−", onDecrement)
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                Text(param, fontSize = 10.sp, color = NikonText3)
                Text(value, fontSize = 16.sp, fontWeight = FontWeight.Bold, color = NikonYellow)
            }
            AdjBtn("＋", onIncrement)
        }
    }
}

@Composable
private fun AdjBtn(text: String, onClick: () -> Unit) {
    Box(
        modifier = Modifier
            .size(36.dp)
            .clip(CircleShape)
            .background(NikonYellow.copy(alpha = 0.15f))
            .clickable(onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Text(text, fontSize = 20.sp, fontWeight = FontWeight.Bold, color = NikonYellow)
    }
}

/** 底部参数展示行(4 列,可点击) */
@Composable
private fun ParamDisplayRow(
    shutter: String,
    aperture: String,
    iso: String,
    ev: String,
    activeParam: String,
    onParamClick: (String) -> Unit,
) {
    val params = listOf(
        Triple("快门", shutter, false),
        Triple("光圈", aperture, true),
        Triple("ISO", iso, false),
        Triple("EV", ev, false),
    )
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        params.forEach { (label, value, _) ->
            val isActive = label == activeParam
            Column(
                modifier = Modifier
                    .weight(1f)
                    .clickable { onParamClick(label) }
                    .padding(vertical = 5.dp, horizontal = 4.dp)
                    .then(
                        if (isActive) Modifier
                            .background(NikonYellow.copy(alpha = 0.12f), RoundedCornerShape(8.dp))
                            .border(1.dp, NikonYellow.copy(alpha = 0.4f), RoundedCornerShape(8.dp))
                        else Modifier
                    ),
                horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                Text(
                    label,
                    fontSize = 9.sp,
                    fontWeight = FontWeight.Bold,
                    color = if (isActive) NikonYellow else Color(0x80FFFFFF),
                    letterSpacing = 0.8.sp,
                )
                Text(
                    value,
                    fontSize = 15.sp,
                    fontWeight = FontWeight.ExtraBold,
                    fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace,
                    color = if (isActive) NikonYellow else NikonText,
                )
            }
        }
    }
}

/** 快门行:连拍控制 + 连拍计数 + 快门按钮 + 相册 */
@Composable
private fun ShutterRow(
    burstCount: Int,
    onShutter: () -> Unit,
    onBurstToggle: () -> Unit,
    onGallery: () -> Unit,
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        // 连拍控制按钮
        CtrlBtn(icon = Icons.Filled.BurstMode, label = "连拍", active = true, onClick = onBurstToggle)
        // 连拍计数 + 快门按钮
        Row(
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            // 连拍计数标识
            Surface(
                color = NikonYellow.copy(alpha = 0.15f),
                shape = RoundedCornerShape(8.dp),
                border = androidx.compose.foundation.BorderStroke(1.dp, NikonYellow),
            ) {
                Column(
                    modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp),
                    horizontalAlignment = Alignment.CenterHorizontally,
                ) {
                    Text("连拍", fontSize = 9.sp, fontWeight = FontWeight.Bold, color = NikonYellow)
                    Text("×$burstCount", fontSize = 13.sp, fontWeight = FontWeight.ExtraBold, color = NikonYellow)
                }
            }
            // 快门按钮(大圆)
            Box(
                modifier = Modifier
                    .size(64.dp)
                    .clip(CircleShape)
                    .background(NikonYellow.copy(alpha = 0.15f)),
                contentAlignment = Alignment.Center,
            ) {
            Box(
                modifier = Modifier
                    .size(52.dp)
                    .clip(CircleShape)
                    .background(NikonYellow)
                    .semantics {
                        contentDescription = "快门"
                        role = androidx.compose.ui.semantics.Role.Button
                    }
                    .clickable(onClick = onShutter),
            )
            }
        }
        // 相册按钮
        CtrlBtn(icon = Icons.Filled.Collections, label = "相册", active = false, onClick = onGallery)
    }
}

/** 控制按钮(图标纵排) */
@Composable
private fun CtrlBtn(
    icon: androidx.compose.ui.graphics.vector.ImageVector,
    label: String,
    active: Boolean,
    onClick: () -> Unit,
) {
    Column(
        horizontalAlignment = Alignment.CenterHorizontally,
        modifier = Modifier
            .width(56.dp)
            .clickable(onClick = onClick)
            .padding(4.dp),
    ) {
        Box(
            modifier = Modifier
                .size(40.dp)
                .clip(CircleShape)
                .background(if (active) NikonYellow.copy(alpha = 0.15f) else NikonSurface3),
            contentAlignment = Alignment.Center,
        ) {
            Icon(
                icon, label,
                tint = if (active) NikonYellow else NikonText2,
                modifier = Modifier.size(22.dp),
            )
        }
        Spacer(Modifier.height(4.dp))
        Text(label, fontSize = 10.sp, color = if (active) NikonYellow else NikonText2)
    }
}
