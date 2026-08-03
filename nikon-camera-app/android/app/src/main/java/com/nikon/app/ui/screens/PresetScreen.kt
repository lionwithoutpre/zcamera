package com.nikon.app.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
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
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.nikon.app.ui.theme.*

/**
 * PresetScreen — 预设中心 / 色彩偏移
 *
 * 手风琴模式: 每个预设卡片内置编辑面板, 点击卡片标题展开/折叠。
 * 支持 Picture Control 参数 + WB 色偏 A-B/G-M 轴调节。
 */
@Composable
fun PresetScreen(
    viewModel: com.nikon.app.viewmodel.CameraViewModel,
) {
    val status by viewModel.status.collectAsStateWithLifecycle()
    val connected = status == com.nikon.app.jni.CameraBridge.STATUS_CONNECTED

    // 预设数据 (UI 状态, 实际通过 camera_api_get/set_pictctrl 同步)
    var presets by remember {
        mutableStateOf(
            listOf(
                PresetData(
                    id = "landscape_a",
                    name = "风景A",
                    desc = "高饱和 · 锐利",
                    active = true,
                    expanded = true,
                    hue = 0, saturation = 65, contrast = 40,
                    clarity = 60, sharpening = 50, brightness = 0,
                    wbA = 3, wbB = 0, wbG = 0, wbM = 2,
                ),
                PresetData(
                    id = "portrait_soft",
                    name = "人像柔光",
                    desc = "低对比 · 柔和肤色",
                    active = false,
                    expanded = false,
                    hue = 0, saturation = 30, contrast = -15,
                    clarity = -10, sharpening = 20, brightness = 5,
                    wbA = 1, wbB = 0, wbG = 2, wbM = -1,
                ),
                PresetData(
                    id = "night_high_iso",
                    name = "夜景高感",
                    desc = "降噪优先 · 高锐度",
                    active = false,
                    expanded = false,
                    hue = 0, saturation = 40, contrast = 25,
                    clarity = 30, sharpening = 70, brightness = -10,
                    wbA = 0, wbB = 2, wbG = -2, wbM = 0,
                ),
            )
        )
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
                "预设中心",
                fontSize = 18.sp,
                fontWeight = FontWeight.ExtraBold,
                color = NikonText,
            )

            Spacer(Modifier.weight(1f))

            // 新建预设
            IconButton(onClick = {
                presets = presets + PresetData(
                    id = "custom_${System.currentTimeMillis()}",
                    name = "自定义 ${presets.size + 1}",
                    desc = "新建预设",
                    active = false,
                    expanded = true,
                )
            }) {
                Icon(Icons.Filled.Add, "新建预设", tint = NikonYellow)
            }
        }

        LazyColumn(
            modifier = Modifier.fillMaxSize(),
            contentPadding = PaddingValues(horizontal = 20.dp, vertical = 8.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            // 未连接提示
            if (!connected) {
                item {
                    Surface(
                        color = NikonYellow.copy(alpha = 0.06f),
                        shape = RoundedCornerShape(10.dp),
                        border = androidx.compose.foundation.BorderStroke(1.dp, NikonYellow.copy(alpha = 0.2f)),
                        modifier = Modifier.fillMaxWidth(),
                    ) {
                        Row(
                            Modifier.padding(12.dp),
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            Icon(Icons.Filled.Info, null, tint = NikonYellow, modifier = Modifier.size(16.dp))
                            Spacer(Modifier.width(10.dp))
                            Text(
                                "未连接相机，预设可编辑但无法应用到相机",
                                fontSize = 12.sp, color = NikonText2,
                            )
                        }
                    }
                }
            }

            items(presets.size) { index ->
                val preset = presets[index]
                PresetCard(
                    preset = preset,
                    onToggleExpand = {
                        presets = presets.toMutableList().apply {
                            set(index, preset.copy(expanded = !preset.expanded))
                        }
                    },
                    onActivate = {
                        // 本地激活:仅切换 active 标记(不下发相机)
                        val newList = presets.toMutableList()
                        newList.forEachIndexed { i, p ->
                            newList[i] = p.copy(active = i == index)
                        }
                        presets = newList
                    },
                    onUpdate = { updated ->
                        presets = presets.toMutableList().apply {
                            set(index, updated)
                        }
                    },
                    onApply = {
                        // 应用到相机:先本地激活,再通过 setProperty 下发 Picture Control 参数
                        val newList = presets.toMutableList()
                        newList.forEachIndexed { i, p ->
                            newList[i] = p.copy(active = i == index)
                        }
                        presets = newList
                        // PTP Picture Control 属性码占位(实际码以 SPEC_PROTOCOL 为准)
                        // 0x5020~0x5026: hue/saturation/contrast/clarity/sharpening/brightness
                        viewModel.setProperty(0x5020, preset.hue.toLong())
                        viewModel.setProperty(0x5021, preset.saturation.toLong())
                        viewModel.setProperty(0x5022, preset.contrast.toLong())
                        viewModel.setProperty(0x5023, preset.clarity.toLong())
                        viewModel.setProperty(0x5024, preset.sharpening.toLong())
                        viewModel.setProperty(0x5025, preset.brightness.toLong())
                        // WB 色偏 0x5030/0x5031
                        viewModel.setProperty(0x5030, preset.wbA.toLong())
                        viewModel.setProperty(0x5031, preset.wbG.toLong())
                    },
                )
            }

            item { Spacer(Modifier.height(16.dp)) }
        }
    }
}

// ─── 预设数据类 ─────────────────────────────────────────────

data class PresetData(
    val id: String,
    val name: String,
    val desc: String,
    val active: Boolean,
    val expanded: Boolean,
    val hue: Int = 0,
    val saturation: Int = 0,
    val contrast: Int = 0,
    val clarity: Int = 0,
    val sharpening: Int = 0,
    val brightness: Int = 0,
    val wbA: Int = 0,
    val wbB: Int = 0,
    val wbG: Int = 0,
    val wbM: Int = 0,
)

// ─── 预设卡片 (手风琴模式) ─────────────────────────────────

@Composable
private fun PresetCard(
    preset: PresetData,
    onToggleExpand: () -> Unit,
    onActivate: () -> Unit,
    onApply: () -> Unit,
    onUpdate: (PresetData) -> Unit,
) {
    val borderColor = if (preset.active) NikonYellow.copy(alpha = 0.5f) else Color.Transparent
    val bgColor = if (preset.active) NikonYellow.copy(alpha = 0.04f) else NikonSurface

    Surface(
        color = bgColor,
        shape = RoundedCornerShape(14.dp),
        border = if (preset.active)
            androidx.compose.foundation.BorderStroke(1.dp, NikonYellow.copy(alpha = 0.5f))
        else null,
        modifier = Modifier
            .fillMaxWidth(),
    ) {
        Column {
            // ── 卡片头部 (点击展开/折叠) ──
            Row(
                modifier = Modifier
                    .clickable(onClick = onToggleExpand)
                    .padding(16.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                // 预设图标
                Box(
                    modifier = Modifier
                        .size(36.dp)
                        .clip(CircleShape)
                        .background(NikonYellow.copy(alpha = 0.15f)),
                    contentAlignment = Alignment.Center,
                ) {
                    Icon(
                        Icons.Filled.Style,
                        null,
                        tint = if (preset.active) NikonYellow else NikonText3,
                        modifier = Modifier.size(18.dp),
                    )
                }

                Spacer(Modifier.width(14.dp))

                Column(Modifier.weight(1f)) {
                    Text(
                        preset.name,
                        fontWeight = FontWeight.Bold,
                        fontSize = 16.sp,
                        color = NikonText,
                    )
                    Spacer(Modifier.height(2.dp))
                    Text(
                        preset.desc,
                        fontSize = 12.sp,
                        color = NikonText2,
                    )
                }

                if (preset.active) {
                    Surface(
                        color = NikonYellow.copy(alpha = 0.12f),
                        shape = RoundedCornerShape(6.dp),
                    ) {
                        Text(
                            "激活",
                            modifier = Modifier.padding(horizontal = 8.dp, vertical = 2.dp),
                            fontSize = 10.sp,
                            fontWeight = FontWeight.Bold,
                            color = NikonYellow,
                        )
                    }
                    Spacer(Modifier.width(8.dp))
                }

                Icon(
                    if (preset.expanded) Icons.Filled.ExpandLess else Icons.Filled.ExpandMore,
                    null,
                    tint = NikonText3,
                    modifier = Modifier.size(20.dp),
                )
            }

            // ── 编辑面板 (手风琴展开) ──
            // 注意:此前用 Surface.animateContentSize() 在 LazyColumn item 内会因尺寸变化反复
            // 触发 measure,导致测试 idle 资源永远 busy(measure 循环),真机也掉帧。改为「即时
            // 展开」(无尺寸动画),惰性列表内安全且可测试。
            if (preset.expanded) {
                Divider(color = NikonBorder, thickness = 0.5.dp)

                Column(
                    modifier = Modifier.padding(16.dp),
                ) {
                    // Picture Control 参数
                    SectionLabel("Picture Control")

                    ParamSlider("色相", preset.hue, -6, 6) {
                        onUpdate(preset.copy(hue = it))
                    }
                    ParamSlider("饱和度", preset.saturation, -100, 100) {
                        onUpdate(preset.copy(saturation = it))
                    }
                    ParamSlider("对比度", preset.contrast, -100, 100) {
                        onUpdate(preset.copy(contrast = it))
                    }
                    ParamSlider("清晰度", preset.clarity, -100, 100) {
                        onUpdate(preset.copy(clarity = it))
                    }
                    ParamSlider("锐化", preset.sharpening, -100, 100) {
                        onUpdate(preset.copy(sharpening = it))
                    }
                    ParamSlider("亮度", preset.brightness, -100, 100) {
                        onUpdate(preset.copy(brightness = it))
                    }

                    Spacer(Modifier.height(12.dp))

                    // 白平衡色偏
                    SectionLabel("白平衡色偏")
                    Text(
                        "A-B (琥珀-蓝) / G-M (绿-洋红)",
                        fontSize = 11.sp,
                        color = NikonText3,
                        modifier = Modifier.padding(bottom = 8.dp),
                    )

                    ParamSlider("A ←→ B", preset.wbA - preset.wbB, -20, 20) {
                        onUpdate(preset.copy(wbA = it, wbB = -it))
                    }
                    ParamSlider("G ←→ M", preset.wbG - preset.wbM, -20, 20) {
                        onUpdate(preset.copy(wbG = it, wbM = -it))
                    }

                    Spacer(Modifier.height(16.dp))

                    // ── 操作按钮 ──
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(10.dp),
                    ) {
                        OutlinedButton(
                            onClick = onActivate,
                            modifier = Modifier.weight(1f),
                            colors = ButtonDefaults.outlinedButtonColors(
                                contentColor = NikonYellow,
                            ),
                            border = androidx.compose.foundation.BorderStroke(
                                1.dp, NikonYellow.copy(alpha = 0.3f)
                            ),
                        ) {
                            Text(
                                if (preset.active) "已激活" else "激活此预设",
                                fontSize = 13.sp,
                            )
                        }

                        Button(
                            onClick = onApply,
                            modifier = Modifier.weight(1f),
                            colors = ButtonDefaults.buttonColors(
                                containerColor = NikonYellow,
                                contentColor = NikonBlack,
                            ),
                        ) {
                            Text("应用到相机", fontSize = 13.sp, fontWeight = FontWeight.Bold)
                        }
                    }
                }
            }
        }
    }
}

// ─── 参数滑块 ───────────────────────────────────────────────

@Composable
private fun ParamSlider(
    label: String,
    value: Int,
    min: Int,
    max: Int,
    onChange: (Int) -> Unit,
) {
    Column(modifier = Modifier.padding(vertical = 2.dp)) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
        ) {
            Text(label, fontSize = 12.sp, color = NikonText2)
            Text(
                value.toString(),
                fontSize = 12.sp,
                fontWeight = FontWeight.Bold,
                color = NikonYellow,
            )
        }
        Slider(
            value = value.toFloat(),
            onValueChange = { onChange(it.toInt()) },
            valueRange = min.toFloat()..max.toFloat(),
            modifier = Modifier.height(36.dp),
            colors = SliderDefaults.colors(
                thumbColor = NikonYellow,
                activeTrackColor = NikonYellow,
                inactiveTrackColor = NikonSurface3,
            ),
        )
    }
}

@Composable
private fun SectionLabel(text: String) {
    Text(
        text,
        fontSize = 11.sp,
        fontWeight = FontWeight.SemiBold,
        color = NikonYellowDim,
        letterSpacing = 2.sp,
        modifier = Modifier.padding(bottom = 6.dp),
    )
}
