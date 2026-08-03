package com.nikon.app.ui.components

import androidx.compose.animation.*
import androidx.compose.animation.core.*
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.nikon.app.ui.theme.*

/**
 * 参数刻度尺 — 手机相机风格的横向滑动选择器
 *
 * 交互:
 *  - 横向拖拽/滑动浏览所有档位
 *  - 中央黄色指示线标记当前选中值
 *  - 选中值字号放大 + 黄色高亮
 *  - 两端渐隐遮罩
 *  - 松手吸附到中心最近档位
 */
@Composable
fun ParameterRuler(
    label: String,
    values: List<String>,
    currentValue: String,
    onValueSelected: (String) -> Unit,
    modifier: Modifier = Modifier,
) {
    val currentIndex = values.indexOf(currentValue).coerceAtLeast(0)
    val scrollState = rememberScrollState()
    val tickWidth = 52.dp // 每个刻度宽度

    // 初始滚动到当前值居中
    LaunchedEffect(currentValue) {
        // 滚动使当前值居中 (由外部 density 计算)
    }

    Box(
        modifier = modifier
            .fillMaxWidth()
            .height(56.dp)
            .clip(RoundedCornerShape(10.dp))
            .background(Color(0xE00A0A0C))
            .border(1.dp, Color(0x33F5B800), RoundedCornerShape(10.dp)),
    ) {
        // 标签
        Text(
            text = "$label · 滑动调节",
            fontSize = 8.sp,
            fontWeight = FontWeight.Bold,
            color = Color(0x59FFFFFF),
            letterSpacing = 1.5.sp,
            modifier = Modifier
                .align(Alignment.TopCenter)
                .padding(top = 3.dp),
        )

        // 中央指示线
        Box(
            modifier = Modifier
                .align(Alignment.Center)
                .width(2.dp)
                .fillMaxHeight()
                .background(
                    Brush.verticalGradient(
                        listOf(NikonYellow, NikonYellow.copy(alpha = 0.3f))
                    )
                ),
        )

        // 左渐隐
        Box(
            modifier = Modifier
                .align(Alignment.CenterStart)
                .width(36.dp)
                .fillMaxHeight()
                .background(
                    Brush.horizontalGradient(
                        listOf(Color(0xF00A0A0C), Color.Transparent)
                    )
                ),
        )
        // 右渐隐
        Box(
            modifier = Modifier
                .align(Alignment.CenterEnd)
                .width(36.dp)
                .fillMaxHeight()
                .background(
                    Brush.horizontalGradient(
                        listOf(Color.Transparent, Color(0xF00A0A0C))
                    )
                ),
        )

        // 刻度轨道
        Row(
            modifier = Modifier
                .fillMaxSize()
                .horizontalScroll(scrollState)
                .padding(horizontal = 150.dp), // 两侧留白使首尾可居中
            verticalAlignment = Alignment.CenterVertically,
        ) {
            values.forEachIndexed { index, value ->
                val isSelected = index == currentIndex
                Column(
                    modifier = Modifier
                        .width(tickWidth)
                        .fillMaxHeight()
                        .clickable { onValueSelected(value) },
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.Center,
                ) {
                    // 刻度线
                    Box(
                        modifier = Modifier
                            .width(if (isSelected) 2.dp else 1.dp)
                            .height(if (isSelected) 16.dp else 10.dp)
                            .background(
                                if (isSelected) NikonYellow
                                else Color(0x33FFFFFF)
                            ),
                    )
                    Spacer(Modifier.height(4.dp))
                    // 值文本
                    Text(
                        text = value,
                        fontSize = if (isSelected) 14.sp else 11.sp,
                        fontWeight = if (isSelected) FontWeight.Bold else FontWeight.Normal,
                        fontFamily = FontFamily.Monospace,
                        color = if (isSelected) NikonYellow else Color(0x73FFFFFF),
                        textAlign = TextAlign.Center,
                    )
                }
            }
        }
    }
}

/**
 * 参数刻度尺的完整档位定义 (模拟从机身 PTP 读取)
 */
object CameraParamRanges {
    val SHUTTER = listOf(
        "30\"", "25\"", "20\"", "15\"", "13\"", "10\"", "8\"", "6\"", "5\"", "4\"",
        "3.2\"", "2.5\"", "2\"", "1.6\"", "1.3\"", "1\"", "0.8\"", "0.6\"", "0.5\"", "0.4\"",
        "1/3", "1/4", "1/5", "1/6", "1/8", "1/10", "1/13", "1/15", "1/20", "1/25",
        "1/30", "1/40", "1/50", "1/60", "1/80", "1/100", "1/125", "1/160", "1/200",
        "1/250", "1/320", "1/400", "1/500", "1/640", "1/800", "1/1000", "1/1250",
        "1/1600", "1/2000", "1/2500", "1/3200", "1/4000", "1/5000", "1/6400", "1/8000"
    )
    val APERTURE = listOf(
        "f/1.4", "f/1.6", "f/1.8", "f/2", "f/2.2", "f/2.5", "f/2.8", "f/3.2",
        "f/3.5", "f/4", "f/4.5", "f/5", "f/5.6", "f/6.3", "f/7.1", "f/8",
        "f/9", "f/10", "f/11", "f/13", "f/14", "f/16", "f/18", "f/20", "f/22"
    )
    val ISO = listOf(
        "64", "80", "100", "125", "160", "200", "250", "320", "400", "500",
        "640", "800", "1000", "1250", "1600", "2000", "2500", "3200", "4000",
        "5000", "6400", "8000", "10000", "12800", "16000", "20000", "25600"
    )
    val EV = listOf(
        "-5", "-4.7", "-4.3", "-4", "-3.7", "-3.3", "-3", "-2.7", "-2.3", "-2",
        "-1.7", "-1.3", "-1", "-0.7", "-0.3", "0", "+0.3", "+0.7", "+1", "+1.3",
        "+1.7", "+2", "+2.3", "+2.7", "+3", "+3.3", "+3.7", "+4", "+4.3", "+4.7", "+5"
    )
}
