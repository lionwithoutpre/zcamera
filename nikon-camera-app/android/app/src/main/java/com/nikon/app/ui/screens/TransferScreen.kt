package com.nikon.app.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.nikon.app.ui.theme.*
import com.nikon.app.viewmodel.CameraViewModel
import androidx.lifecycle.compose.collectAsStateWithLifecycle

/**
 * TransferScreen — 文件传输
 *
 * 对齐 ui-design.html 第 4 页:
 *  - 速度仪表盘(环形 + 数值)
 *  - 格式过滤器 chips (全部/NEF/JPG/NEF+JPG)
 *  - 统计卡 (今日传输 / 成功率)
 *  - 批量操作工具栏 (全部暂停 / 全部取消)
 *  - 传输中 / 传输失败(断点续传+重新传输) / 等待中 / 已完成 分区
 */
@Composable
fun TransferScreen(
    viewModel: CameraViewModel,
) {
    var filter by remember { mutableStateOf(TfFilter.ALL) }

    // 任务数据来自 ViewModel(发起传输时 add,完成/失败时 update)
    val allJobs by viewModel.transferJobs.collectAsStateWithLifecycle()
    // 转换为 UI 类型(TfJob),避免 TransferStatus/TfStatus 混用
    val allJobsUi = allJobs.map { it.toUi() }

    val visible = allJobsUi.filter { j ->
        when (filter) {
            TfFilter.ALL -> true
            TfFilter.NEF -> j.format == "NEF"
            TfFilter.JPG -> j.format == "JPG"
            TfFilter.NEF_JPG -> true
        }
    }

    Column(modifier = Modifier.fillMaxSize().background(NikonBlack)) {
        // 顶部栏
        Row(
            modifier = Modifier
                .fillMaxWidth().padding(horizontal = 20.dp, vertical = 14.dp).padding(top = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text("文件传输", fontSize = 18.sp, fontWeight = FontWeight.ExtraBold, color = NikonText)
        }

        LazyColumn(
            modifier = Modifier.fillMaxSize(),
            contentPadding = PaddingValues(horizontal = 20.dp, vertical = 8.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            // ── 速度仪表盘(从 transferJobs 聚合) ──
            val activeJobs = allJobsUi.filter { it.status == TfStatus.ACTIVE }
            val totalSpeed = activeJobs.sumOf { it.speedMbps }
            val totalTransferred = allJobsUi.sumOf { it.sizeMb * it.percent / 100 }
            val doneCount = allJobsUi.count { it.status == TfStatus.DONE }
            val failedCount = allJobsUi.count { it.status == TfStatus.FAILED }
            val totalAttempted = doneCount + failedCount
            val successRate = if (totalAttempted > 0) "%.1f%%".format(doneCount * 100.0 / totalAttempted) else "--"
            val transferredLabel = if (totalTransferred >= 1024) "%.1f GB".format(totalTransferred / 1024) else "%.0f MB".format(totalTransferred)
            val remainingLabel = if (activeJobs.isNotEmpty()) "${activeJobs.size} 个任务" else "无"

            item { SpeedGaugeCard(speed = totalSpeed, jobs = activeJobs.size, transferred = transferredLabel, remaining = remainingLabel) }

            // ── 格式过滤器 ──
            item {
                Row(
                    Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Text("格式过滤", fontSize = 11.sp, fontWeight = FontWeight.Bold, color = NikonText3)
                    Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                        TfFilter.values().forEach { f ->
                            val active = f == filter
                            Box(
                                modifier = Modifier
                                    .clip(RoundedCornerShape(100.dp))
                                    .background(
                                        if (active) when (f) {
                                            TfFilter.ALL -> NikonYellow
                                            TfFilter.NEF -> NikonOrange
                                            TfFilter.JPG -> NikonBlue
                                            TfFilter.NEF_JPG -> NikonYellow
                                        } else NikonSurface3
                                    )
                                    .clickable { filter = f }
                                    .padding(horizontal = 10.dp, vertical = 4.dp),
                            ) {
                                Text(
                                    f.label,
                                    fontSize = 10.sp,
                                    fontWeight = if (active) FontWeight.ExtraBold else FontWeight.Bold,
                                    color = if (active) NikonBlack else NikonText2,
                                )
                            }
                        }
                    }
                }
            }

            // ── 统计卡(从 transferJobs 聚合) ──
            item {
                Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    StatCard("已完成", "$doneCount", "张 · $transferredLabel", NikonYellow, Modifier.weight(1f))
                    StatCard("成功率", successRate, "断点续传覆盖", NikonGreen, Modifier.weight(1f))
                }
            }

            // ── 批量操作工具栏 ──
            item {
                Surface(
                    color = NikonSurface,
                    shape = RoundedCornerShape(10.dp),
                    modifier = Modifier.fillMaxWidth(),
                ) {
                    Row(
                        Modifier.padding(horizontal = 14.dp, vertical = 10.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Text("已选 ${visible.count { it.status == TfStatus.ACTIVE }} 个任务",
                            fontSize = 12.sp, fontWeight = FontWeight.Bold, color = NikonText,
                            modifier = Modifier.weight(1f))
                        Box(
                            modifier = Modifier
                                .clip(RoundedCornerShape(8.dp))
                                .background(NikonOrange.copy(alpha = 0.15f))
                                .clickable { viewModel.pauseAllTransfers() }
                                .padding(horizontal = 10.dp, vertical = 5.dp),
                        ) { Text("全部暂停", fontSize = 11.sp, fontWeight = FontWeight.Bold, color = NikonOrange) }
                        Spacer(Modifier.width(8.dp))
                        Box(
                            modifier = Modifier
                                .clip(RoundedCornerShape(8.dp))
                                .background(NikonRed.copy(alpha = 0.12f))
                                .clickable { viewModel.cancelAllTransfers() }
                                .padding(horizontal = 10.dp, vertical = 5.dp),
                        ) { Text("全部取消", fontSize = 11.sp, fontWeight = FontWeight.Bold, color = NikonRed) }
                    }
                }
            }

            // ── 传输中 ──
            val active = visible.filter { it.status == TfStatus.ACTIVE }
            if (active.isNotEmpty()) {
                item { SectionLabel("传输中") }
                items(active) { TransferJobCard(it, viewModel) }
            }

            // ── 传输失败 ──
            val failed = visible.filter { it.status == TfStatus.FAILED }
            if (failed.isNotEmpty()) {
                item {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text("传输失败", fontSize = 12.sp, fontWeight = FontWeight.SemiBold, color = NikonRed)
                        Spacer(Modifier.width(6.dp))
                        Text("(${failed.size})", fontSize = 10.sp, color = NikonText3)
                    }
                }
                items(failed) { TransferJobCard(it, viewModel) }
            }

            // ── 等待中 ──
            val waiting = visible.filter { it.status == TfStatus.WAITING }
            if (waiting.isNotEmpty()) {
                item { SectionLabel("等待中") }
                items(waiting) { TransferJobCard(it, viewModel) }
            }

            // ── 已完成 ──
            val done = visible.filter { it.status == TfStatus.DONE }
            if (done.isNotEmpty()) {
                item { SectionLabel("已完成", color = NikonGreen) }
                items(done) { TransferJobCard(it, viewModel) }
            }
        }
    }
}

// ─── 数据 ────────────────────────────────────────────────────

enum class TfFilter(val label: String) {
    ALL("全部"), NEF("NEF"), JPG("JPG"), NEF_JPG("NEF+JPG")
}

enum class TfStatus { ACTIVE, FAILED, WAITING, DONE, PAUSED, CANCELLED }

/** ViewModel.TransferJob → UI TfJob 映射 */
private fun com.nikon.app.viewmodel.TransferJob.toUi(): TfJob {
    val uiStatus = when (status) {
        com.nikon.app.viewmodel.TransferStatus.ACTIVE -> TfStatus.ACTIVE
        com.nikon.app.viewmodel.TransferStatus.WAITING -> TfStatus.WAITING
        com.nikon.app.viewmodel.TransferStatus.DONE -> TfStatus.DONE
        com.nikon.app.viewmodel.TransferStatus.FAILED -> TfStatus.FAILED
        com.nikon.app.viewmodel.TransferStatus.PAUSED -> TfStatus.PAUSED
        com.nikon.app.viewmodel.TransferStatus.CANCELLED -> TfStatus.CANCELLED
    }
    return TfJob(
        id = id,
        filename = filename,
        format = format,
        sizeMb = sizeMb,
        percent = percent,
        speedMbps = speedMbps,
        note = note,
        status = uiStatus,
    )
}

data class TfJob(
    val id: Int,
    val filename: String,
    val format: String,
    val sizeMb: Double,
    val percent: Int,
    val speedMbps: Double,
    val note: String,
    val status: TfStatus,
)

// ─── 速度仪表盘 ──────────────────────────────────────────────

@Composable
private fun SpeedGaugeCard(
    speed: Double,
    jobs: Int,
    transferred: String,
    remaining: String,
) {
    Surface(
        color = NikonSurface,
        shape = RoundedCornerShape(16.dp),
        modifier = Modifier.fillMaxWidth(),
    ) {
        Row(
            Modifier.padding(20.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(20.dp),
        ) {
            // 环形仪表(用 Canvas 简化)
            Box(
                modifier = Modifier.size(96.dp),
                contentAlignment = Alignment.Center,
            ) {
                androidx.compose.foundation.Canvas(Modifier.fillMaxSize()) {
                    val r = size.minDimension / 2f
                    drawCircle(
                        color = Color.White.copy(alpha = 0.06f),
                        radius = r - 8f,
                        style = androidx.compose.ui.graphics.drawscope.Stroke(width = 8f),
                    )
                    // 进度弧(占 72%)
                    drawArc(
                        brush = Brush.linearGradient(listOf(NikonYellowDim, NikonYellow)),
                        startAngle = -90f,
                        sweepAngle = 360f * 0.72f,
                        useCenter = false,
                        style = androidx.compose.ui.graphics.drawscope.Stroke(width = 8f, cap = androidx.compose.ui.graphics.StrokeCap.Round),
                    )
                }
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    Text(speed.toString(), fontSize = 22.sp, fontWeight = FontWeight.Black, color = NikonYellow)
                    Text("MB/s", fontSize = 10.sp, fontWeight = FontWeight.Bold, color = NikonYellowDim)
                }
            }
            // 信息列
            Column(Modifier.weight(1f)) {
                Text("当前传输速度", fontSize = 12.sp, color = NikonText3)
                Spacer(Modifier.height(4.dp))
                Text("$jobs 个任务进行中", fontSize = 14.sp, fontWeight = FontWeight.Bold, color = NikonText)
                Spacer(Modifier.height(6.dp))
                Text("已传输 $transferred · 剩余 $remaining", fontSize = 11.sp, color = NikonText2)
            }
        }
    }
}

// ─── 统计卡 ──────────────────────────────────────────────────

@Composable
private fun StatCard(label: String, value: String, sub: String, color: Color, modifier: Modifier = Modifier) {
    Surface(
        color = NikonSurface,
        shape = RoundedCornerShape(12.dp),
        modifier = modifier,
    ) {
        Column(Modifier.padding(14.dp)) {
            Text(label, fontSize = 11.sp, color = NikonText3)
            Spacer(Modifier.height(4.dp))
            Text(value, fontSize = 20.sp, fontWeight = FontWeight.Black, color = color)
            Spacer(Modifier.height(2.dp))
            Text(sub, fontSize = 10.sp, color = NikonText3)
        }
    }
}

// ─── 传输任务卡片 ────────────────────────────────────────────

@Composable
private fun TransferJobCard(job: TfJob, viewModel: CameraViewModel) {
    val color = when (job.status) {
        TfStatus.ACTIVE -> NikonYellow
        TfStatus.FAILED -> NikonRed
        TfStatus.WAITING -> NikonText3
        TfStatus.DONE -> NikonGreen
        TfStatus.PAUSED -> NikonOrange
        TfStatus.CANCELLED -> NikonText3
    }
    Surface(
        color = NikonSurface,
        shape = RoundedCornerShape(12.dp),
        modifier = Modifier.fillMaxWidth(),
    ) {
        Column(Modifier.padding(14.dp)) {
            Row(
                Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.weight(1f)) {
                    // 缩略图块
                    Box(
                        modifier = Modifier
                            .size(36.dp)
                            .clip(RoundedCornerShape(8.dp))
                            .background(
                                when (job.status) {
                                    TfStatus.FAILED -> SolidColor(NikonRed.copy(alpha = 0.15f))
                                    TfStatus.DONE -> SolidColor(NikonGreen.copy(alpha = 0.1f))
                                    else -> if (job.format == "NEF") Brush.linearGradient(listOf(Color(0xFF1A3A5C), Color(0xFF0A1A2A)))
                                        else Brush.linearGradient(listOf(Color(0xFF2A1A0A), Color(0xFF1A1000)))
                                }
                            ),
                        contentAlignment = Alignment.Center,
                    ) {
                        if (job.status == TfStatus.FAILED) {
                            Icon(Icons.Filled.Error, null, tint = NikonRed, modifier = Modifier.size(16.dp))
                        } else {
                            Text(job.format, fontSize = 8.sp, fontWeight = FontWeight.ExtraBold, color = Color.White.copy(alpha = 0.6f))
                        }
                    }
                    Spacer(Modifier.width(10.dp))
                    Column {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Text(job.filename, fontSize = 13.sp, fontWeight = FontWeight.SemiBold, color = NikonText)
                            Spacer(Modifier.width(6.dp))
                            FormatTag(job.format)
                        }
                        Spacer(Modifier.height(2.dp))
                        Text(
                            job.note,
                            fontSize = 11.sp,
                            color = if (job.status == TfStatus.FAILED) NikonRed else NikonText2,
                        )
                    }
                }
                when (job.status) {
                    TfStatus.ACTIVE -> Text("${job.percent}%", fontSize = 13.sp, fontWeight = FontWeight.Bold, color = color)
                    TfStatus.FAILED -> Text("失败", fontSize = 12.sp, fontWeight = FontWeight.Bold, color = NikonRed)
                    TfStatus.WAITING -> Text("排队中", fontSize = 11.sp, color = NikonText3)
                    TfStatus.DONE -> Text("✓ 完成", fontSize = 12.sp, color = NikonGreen)
                    TfStatus.PAUSED -> Text("已暂停", fontSize = 11.sp, color = NikonOrange)
                    TfStatus.CANCELLED -> Text("已取消", fontSize = 11.sp, color = NikonText3)
                }
            }

            // 进度条
            Spacer(Modifier.height(10.dp))
            LinearProgressIndicator(
                progress = job.percent / 100f,
                modifier = Modifier.fillMaxWidth().height(4.dp).clip(RoundedCornerShape(2.dp)),
                color = color,
                trackColor = NikonSurface3,
            )
            Spacer(Modifier.height(6.dp))
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                val pctText = when (job.status) {
                    TfStatus.DONE -> "100%"
                    TfStatus.FAILED -> "${job.percent}% · 已传 ${"%.1f".format(job.sizeMb * job.percent / 100)} MB"
                    else -> "${job.percent}% · ${"%.1f".format(job.speedMbps)} / ${"%.1f".format(job.sizeMb)} MB"
                }
                Text(pctText, fontSize = 11.sp, color = NikonText2)
                when (job.status) {
                    TfStatus.ACTIVE -> Text("剩余 ~0:23", fontSize = 11.sp, color = NikonText2)
                    TfStatus.DONE -> Text("均速 ${job.speedMbps} MB/s", fontSize = 11.sp, color = NikonText2)
                    TfStatus.WAITING -> Text("等待前置任务", fontSize = 11.sp, color = NikonText3)
                    TfStatus.FAILED -> {
                        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                            Box(
                                modifier = Modifier
                                    .clip(RoundedCornerShape(8.dp))
                                    .background(NikonYellow.copy(alpha = 0.15f))
                                    .clickable { viewModel.resumeTransfer(job.id) }
                                    .padding(horizontal = 10.dp, vertical = 4.dp),
                            ) { Text("断点续传", fontSize = 11.sp, fontWeight = FontWeight.Bold, color = NikonYellow) }
                            Box(
                                modifier = Modifier
                                    .clip(RoundedCornerShape(8.dp))
                                    .background(NikonYellow)
                                    .clickable { viewModel.retryTransfer(job.id) }
                                    .padding(horizontal = 10.dp, vertical = 4.dp),
                            ) { Text("重新传输", fontSize = 11.sp, fontWeight = FontWeight.ExtraBold, color = NikonBlack) }
                        }
                    }
                    TfStatus.PAUSED -> Text("已暂停", fontSize = 11.sp, color = NikonOrange)
                    TfStatus.CANCELLED -> Text("已取消", fontSize = 11.sp, color = NikonText3)
                }
            }
        }
    }
}

@Composable
private fun FormatTag(format: String) {
    val color = if (format == "NEF") NikonOrange else NikonBlue
    Text(
        format,
        fontSize = 8.sp,
        fontWeight = FontWeight.ExtraBold,
        color = NikonBlack,
        modifier = Modifier
            .clip(RoundedCornerShape(3.dp))
            .background(color)
            .padding(horizontal = 4.dp, vertical = 1.dp),
    )
}

@Composable
private fun SectionLabel(text: String, color: Color = NikonText3) {
    Text(
        text.uppercase(),
        fontSize = 11.sp,
        fontWeight = FontWeight.SemiBold,
        color = color,
        letterSpacing = 1.5.sp,
        modifier = Modifier.padding(top = 4.dp, bottom = 2.dp),
    )
}
