package com.nikon.app.ui.components

import androidx.compose.animation.core.*
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.nikon.app.jni.CameraBridge
import com.nikon.app.ui.theme.*

/**
 * 全局连接状态胶囊 — 常驻顶栏右侧
 *
 * 根据连接状态实时变化颜色和动画:
 *  - DISCONNECTED: 灰点 + "未连接"
 *  - SCANNING/CONNECTING: 黄色脉冲点 + 状态文案
 *  - CONNECTED: 绿点 + 型号
 *  - TRANSFERRING: 黄色脉冲点 + "传输中"
 *  - ERROR: 红点 + "错误"
 */
@Composable
fun StatusCapsule(
    status: Int,
    modelName: String? = null,
    modifier: Modifier = Modifier,
) {
    val (dotColor, label, borderColor) = when (status) {
        CameraBridge.STATUS_SCANNING -> Triple(
            NikonYellow, "扫描中…", Color(0x40F5B800)
        )
        CameraBridge.STATUS_CONNECTING -> Triple(
            NikonYellow, "连接中…", Color(0x40F5B800)
        )
        CameraBridge.STATUS_CONNECTED -> Triple(
            NikonGreen, modelName ?: "已连接", Color(0x404CAF50)
        )
        CameraBridge.STATUS_TRANSFERRING -> Triple(
            NikonYellow, "传输中", Color(0x60F5B800)
        )
        CameraBridge.STATUS_ERROR -> Triple(
            NikonRed, "错误", Color(0x60E03A3A)
        )
        else -> Triple(
            Color(0xFF555555), "未连接", Color(0xFF2A2A2C)
        )
    }

    // 脉冲动画 (扫描/连接/传输时)
    val isPulsing = status == CameraBridge.STATUS_SCANNING ||
            status == CameraBridge.STATUS_CONNECTING ||
            status == CameraBridge.STATUS_TRANSFERRING

    val pulseAlpha by if (isPulsing) {
        rememberInfiniteTransition(label = "capsule_pulse").animateFloat(
            initialValue = 1f,
            targetValue = 0.3f,
            animationSpec = infiniteRepeatable(
                animation = tween(800, easing = EaseInOut),
                repeatMode = RepeatMode.Reverse,
            ),
            label = "pulse_alpha",
        )
    } else {
        remember { mutableStateOf(1f) }
    }

    Row(
        modifier = modifier
            .clip(RoundedCornerShape(100.dp))
            .background(NikonSurface)
            .border(1.dp, borderColor, RoundedCornerShape(100.dp))
            .padding(horizontal = 12.dp, vertical = 5.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(7.dp),
    ) {
        // 状态点
        Box(
            modifier = Modifier
                .size(7.dp)
                .clip(CircleShape)
                .background(dotColor.copy(alpha = pulseAlpha)),
        )
        // 文案
        Text(
            text = label,
            fontSize = 11.sp,
            fontWeight = FontWeight.SemiBold,
            color = when (status) {
                CameraBridge.STATUS_CONNECTED -> Color(0xFFCFE9D0)
                CameraBridge.STATUS_TRANSFERRING -> NikonYellow
                CameraBridge.STATUS_ERROR -> Color(0xFFF3C2C2)
                else -> NikonText2
            },
            maxLines = 1,
        )
    }
}
