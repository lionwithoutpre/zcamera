@file:OptIn(ExperimentalFoundationApi::class)

package com.nikon.app.ui.screens

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.ExperimentalFoundationApi
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
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.nikon.app.jni.CameraBridge
import com.nikon.app.ui.theme.*
import com.nikon.app.viewmodel.CameraViewModel

/**
 * GalleryScreen — 相册 / 浏览相机文件
 *
 * 对齐 ui-design.html 第 6 页:
 *  - 存储卡切换 (CF-A / SD-B)
 *  - 网格 / 列表 视图切换
 *  - 过滤器 chips (全部/RAW/JPEG/已传/未传)
 *  - 多选工具栏 (传输/删除/取消)
 *  - 3 列缩略图网格 + 日期分组 + 更多占位
 *  - 文件详情卡片
 *  - 底部统计栏 (总/已传/未传)
 */
@Composable
fun GalleryScreen(
    viewModel: CameraViewModel,
) {
    val status by viewModel.status.collectAsStateWithLifecycle()
    val connected = status == CameraBridge.STATUS_CONNECTED
    val files by viewModel.fileList.collectAsStateWithLifecycle()
    val loading by viewModel.fileListLoading.collectAsStateWithLifecycle()
    val transferJobs by viewModel.transferJobs.collectAsStateWithLifecycle()

    var activeCard by remember { mutableStateOf("CF-A") }
    var gridMode by remember { mutableStateOf(true) }
    var filter by remember { mutableStateOf(GalFilter.ALL) }
    var selected by remember { mutableStateOf(setOf<Long>()) }
    var detailId by remember { mutableStateOf<Long?>(null) }

    // 进页面拉取文件列表;存储卡切换重新拉
    val storageId = when (activeCard) {
        "CF-A" -> CameraBridge.STORAGE_CF
        "SD-B" -> CameraBridge.STORAGE_SD
        else -> CameraBridge.STORAGE_ALL
    }
    LaunchedEffect(activeCard, connected) {
        if (connected) viewModel.listFiles(storageId)
    }

    // 已传输的 objectHandle 集合(来自 transferJobs DONE 状态)
    val transferredHandles = transferJobs
        .filter { it.status == com.nikon.app.viewmodel.TransferStatus.DONE }
        .map { it.objectHandle }.toSet()

    val sampleFiles = files.map { it.toGalFile(it.objectHandle in transferredHandles) }

    val visible = sampleFiles.filter { f ->
        when (filter) {
            GalFilter.ALL -> true
            GalFilter.NEF -> f.format == "NEF"
            GalFilter.JPG -> f.format == "JPG"
            GalFilter.TRANSFERRED -> f.transferred
            GalFilter.UNTRANSFERRED -> !f.transferred
        }
    }
    val grouped = visible.groupBy { it.dateGroup }

    Column(
        modifier = Modifier.fillMaxSize().background(NikonBlack),
    ) {
        // ── 顶部 AppBar: 标题 + 存储卡切换 + 视图切换 ──
        GalleryAppBar(
            activeCard = activeCard,
            photoCount = sampleFiles.size,
            onCardChange = { activeCard = it },
            gridMode = gridMode,
            onToggleView = { gridMode = !gridMode },
        )

        // ── 过滤器 chips ──
        FilterChipsRow(current = filter, onSelect = { filter = it })

        // ── 多选工具栏(选中时) ──
        if (selected.isNotEmpty()) {
            MultiSelectToolbar(
                count = selected.size,
                onTransfer = {
                    selected.forEach { id ->
                        sampleFiles.firstOrNull { it.id == id }?.let {
                            viewModel.startTransferToApp(it.objectHandle, it.name)
                        }
                    }
                    selected = emptySet()
                },
                onDelete = {
                    selected.forEach { id ->
                        viewModel.deleteFile(id)
                    }
                    selected = emptySet()
                },
                onCancel = { selected = emptySet() },
            )
        }

        // ── 内容区(空态/loading/数据) ──
        if (loading) {
            Box(
                modifier = Modifier.weight(1f).fillMaxWidth(),
                contentAlignment = Alignment.Center,
            ) {
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    androidx.compose.material3.CircularProgressIndicator(
                        color = NikonYellow, strokeWidth = 2.dp, modifier = Modifier.size(32.dp),
                    )
                    Spacer(Modifier.height(12.dp))
                    Text("加载文件列表…", fontSize = 12.sp, color = NikonText3)
                }
            }
        } else if (!connected) {
            Box(
                modifier = Modifier.weight(1f).fillMaxWidth(),
                contentAlignment = Alignment.Center,
            ) {
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    Icon(
                        Icons.Filled.CameraAlt,
                        "未连接",
                        tint = NikonText3, modifier = Modifier.size(40.dp),
                    )
                    Spacer(Modifier.height(12.dp))
                    Text("未连接相机", fontSize = 14.sp, fontWeight = FontWeight.SemiBold, color = NikonText2)
                    Spacer(Modifier.height(4.dp))
                    Text("请先在主页连接相机", fontSize = 11.sp, color = NikonText3)
                }
            }
        } else if (sampleFiles.isEmpty()) {
            Box(
                modifier = Modifier.weight(1f).fillMaxWidth(),
                contentAlignment = Alignment.Center,
            ) {
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    Icon(
                        Icons.Filled.PhotoLibrary,
                        "无照片",
                        tint = NikonText3, modifier = Modifier.size(40.dp),
                    )
                    Spacer(Modifier.height(12.dp))
                    Text("存储卡无照片", fontSize = 14.sp, fontWeight = FontWeight.SemiBold, color = NikonText2)
                    Spacer(Modifier.height(4.dp))
                    Text("拍摄后文件将自动显示在此处", fontSize = 11.sp, color = NikonText3)
                }
            }
        } else if (gridMode) {
            GridContent(
                grouped = grouped,
                selected = selected,
                onToggle = { id ->
                    selected = if (id in selected) selected - id else selected + id
                },
                onOpenDetail = { detailId = it },
                modifier = Modifier.weight(1f),
            )
        } else {
            ListContent(
                files = visible,
                selected = selected,
                onToggle = { id ->
                    selected = if (id in selected) selected - id else selected + id
                },
                onOpenDetail = { detailId = it },
                modifier = Modifier.weight(1f),
            )
        }

        // ── 详情卡(点击单张触发)──
        detailId?.let { id ->
            sampleFiles.firstOrNull { it.id == id }?.let { file ->
                FileDetailCard(
                    file = file,
                    onTransfer = {
                        viewModel.startTransferToApp(file.objectHandle, file.name)
                    },
                    onClose = { detailId = null },
                )
            }
        }

        // ── 底部统计栏 ──
        GalleryStatsBar(
            total = sampleFiles.size,
            transferred = sampleFiles.count { it.transferred },
            untransferred = sampleFiles.count { !it.transferred },
        )
    }
}

// ─── 数据 ────────────────────────────────────────────────────

enum class GalFilter(val label: String) {
    ALL("全部"), NEF("RAW (NEF)"), JPG("JPEG"),
    TRANSFERRED("★ 已传"), UNTRANSFERRED("未传")
}

data class GalFile(
    val id: Long,                    // objectHandle
    val name: String,
    val format: String,              // NEF / JPG
    val size: String,
    val time: String,
    val transferred: Boolean,
    val dateGroup: String,           // "今天 · 2026/06/23"
    val shutter: String = "--",
    val aperture: String = "--",
    val iso: String = "--",
    val resolution: String = "--",
    val objectHandle: Long = 0L,
)

/** CameraFile(ViewModel)→ GalFile(UI)映射 */
private fun com.nikon.app.viewmodel.CameraFile.toGalFile(transferred: Boolean): GalFile {
    val timePart = datetime.substringAfter('T').take(8).ifEmpty { "--:--:--" }
    val datePart = datetime.take(10).replace('-', '/')
    val groupLabel = formatDateGroup(datePart)
    return GalFile(
        id = objectHandle,
        name = filename,
        format = format,
        size = sizeLabel,
        time = timePart,
        transferred = transferred,
        dateGroup = groupLabel,
        resolution = if (width > 0 && height > 0) "$width × $height" else "--",
        objectHandle = objectHandle,
    )
}

/** "2026/06/23" → "今天 · 2026/06/23"(简化:只格式化,不真实判断今天/昨天) */
private fun formatDateGroup(datePart: String): String {
    return datePart  // 简化:直接用日期,UI 层不强行标"今天/昨天"
}

// ─── 顶部 AppBar ─────────────────────────────────────────────

@Composable
private fun GalleryAppBar(
    activeCard: String,
    photoCount: Int,
    onCardChange: (String) -> Unit,
    gridMode: Boolean,
    onToggleView: () -> Unit,
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 18.dp, vertical = 12.dp)
            .padding(top = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f)) {
            Text("相册", fontSize = 18.sp, fontWeight = FontWeight.ExtraBold, color = NikonText)
            Spacer(Modifier.height(1.dp))
            Text("$activeCard · $photoCount 张照片", fontSize = 11.sp, color = NikonText3)
        }
        // 存储卡切换
        listOf("CF-A", "SD-B").forEach { card ->
            val active = card == activeCard
            Box(
                modifier = Modifier
                    .clip(RoundedCornerShape(8.dp))
                    .background(if (active) NikonYellow else NikonSurface3)
                    .clickable { onCardChange(card) }
                    .padding(horizontal = 10.dp, vertical = 5.dp),
            ) {
                Text(
                    card,
                    fontSize = 10.sp,
                    fontWeight = FontWeight.ExtraBold,
                    color = if (active) NikonBlack else NikonText2,
                )
            }
            Spacer(Modifier.width(6.dp))
        }
        // 视图切换
        Box(
            modifier = Modifier
                .size(28.dp)
                .clip(RoundedCornerShape(7.dp))
                .background(if (gridMode) NikonYellow else NikonSurface3)
                .clickable { if (!gridMode) onToggleView() },
            contentAlignment = Alignment.Center,
        ) {
            Icon(Icons.Filled.Apps, "网格", tint = if (gridMode) NikonBlack else NikonText2, modifier = Modifier.size(14.dp))
        }
        Spacer(Modifier.width(4.dp))
        Box(
            modifier = Modifier
                .size(28.dp)
                .clip(RoundedCornerShape(7.dp))
                .background(if (!gridMode) NikonYellow else NikonSurface3)
                .clickable { if (gridMode) onToggleView() },
            contentAlignment = Alignment.Center,
        ) {
            Icon(Icons.Filled.List, "列表", tint = if (!gridMode) NikonBlack else NikonText2, modifier = Modifier.size(14.dp))
        }
    }
}

// ─── 过滤器 chips ────────────────────────────────────────────

@Composable
private fun FilterChipsRow(current: GalFilter, onSelect: (GalFilter) -> Unit) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 18.dp, vertical = 6.dp),
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        GalFilter.values().forEach { f ->
            val active = f == current
            Box(
                modifier = Modifier
                    .clip(RoundedCornerShape(100.dp))
                    .background(if (active) NikonYellow else NikonSurface3)
                    .clickable { onSelect(f) }
                    .padding(horizontal = 12.dp, vertical = 5.dp),
            ) {
                Text(
                    f.label,
                    fontSize = 11.sp,
                    fontWeight = if (active) FontWeight.ExtraBold else FontWeight.Bold,
                    color = if (active) NikonBlack else NikonText2,
                )
            }
        }
    }
}

// ─── 多选工具栏 ──────────────────────────────────────────────

@Composable
private fun MultiSelectToolbar(
    count: Int,
    onTransfer: () -> Unit,
    onDelete: () -> Unit,
    onCancel: () -> Unit,
) {
    Surface(
        color = NikonYellow.copy(alpha = 0.12f),
        shape = RoundedCornerShape(12.dp),
        border = androidx.compose.foundation.BorderStroke(1.dp, NikonYellow.copy(alpha = 0.35f)),
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 18.dp, vertical = 6.dp),
    ) {
        Row(
            Modifier.padding(horizontal = 14.dp, vertical = 10.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            Icon(Icons.Filled.CheckBox, null, tint = NikonYellow, modifier = Modifier.size(16.dp))
            Text("已选 $count 张", fontSize = 12.sp, fontWeight = FontWeight.Bold, color = NikonYellow, modifier = Modifier.weight(1f))
            // 传输
            Box(
                modifier = Modifier
                    .clip(RoundedCornerShape(8.dp))
                    .background(NikonYellow)
                    .clickable(onClick = onTransfer)
                    .padding(horizontal = 10.dp, vertical = 5.dp),
                contentAlignment = Alignment.Center,
            ) {
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                    Icon(Icons.Filled.Download, null, tint = NikonBlack, modifier = Modifier.size(12.dp))
                    Text("传输", fontSize = 11.sp, fontWeight = FontWeight.ExtraBold, color = NikonBlack)
                }
            }
            // 删除
            Box(
                modifier = Modifier
                    .clip(RoundedCornerShape(8.dp))
                    .background(NikonRed.copy(alpha = 0.12f))
                    .clickable(onClick = onDelete)
                    .padding(5.dp),
                contentAlignment = Alignment.Center,
            ) {
                Icon(Icons.Filled.Delete, "删除", tint = NikonRed, modifier = Modifier.size(14.dp))
            }
            // 取消
            Box(
                modifier = Modifier
                    .clip(RoundedCornerShape(8.dp))
                    .background(NikonSurface3)
                    .clickable(onClick = onCancel)
                    .padding(5.dp),
                contentAlignment = Alignment.Center,
            ) {
                Icon(Icons.Filled.Close, "取消", tint = NikonText2, modifier = Modifier.size(14.dp))
            }
        }
    }
}

// ─── 网格内容 ────────────────────────────────────────────────

@Composable
private fun GridContent(
    grouped: Map<String, List<GalFile>>,
    selected: Set<Long>,
    onToggle: (Long) -> Unit,
    onOpenDetail: (Long) -> Unit,
    modifier: Modifier = Modifier,
) {
    LazyColumn(modifier = modifier.fillMaxWidth().padding(horizontal = 18.dp)) {
        grouped.forEach { (dateLabel, files) ->
            item {
                Row(
                    Modifier.fillMaxWidth().padding(vertical = 8.dp),
                    horizontalArrangement = Arrangement.SpaceBetween,
                ) {
                    Text(dateLabel, fontSize = 11.sp, fontWeight = FontWeight.SemiBold, color = NikonText3)
                    Text("${files.size} 张", fontSize = 10.sp, color = NikonText3)
                }
            }
            // 3 列网格 —— 手动分块
            val rows = files.chunked(3)
            rows.forEach { rowFiles ->
                item {
                    Row(
                        Modifier.fillMaxWidth().padding(bottom = 3.dp),
                        horizontalArrangement = Arrangement.spacedBy(3.dp),
                    ) {
                        repeat(3) { idx ->
                            if (idx < rowFiles.size) {
                                val f = rowFiles[idx]
                                ThumbCell(
                                    file = f,
                                    selected = f.id in selected,
                                    onToggle = { onToggle(f.id) },
                                    onOpen = { onOpenDetail(f.id) },
                                    modifier = Modifier.weight(1f),
                                )
                            } else {
                                Spacer(Modifier.weight(1f))
                            }
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun ThumbCell(
    file: GalFile,
    selected: Boolean,
    onToggle: () -> Unit,
    onOpen: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Box(
        modifier = modifier
            .aspectRatio(1f)
            .clip(RoundedCornerShape(6.dp))
            .background(
                Brush.linearGradient(
                    listOf(
                        if (file.format == "NEF") Color(0xFF1A3A5C) else Color(0xFF2A1A0D),
                        if (file.format == "NEF") Color(0xFF0D1F33) else Color(0xFF1A1500),
                    )
                )
            )
            .combinedClickable(
            onClick = onToggle,
            onLongClick = onOpen,
        ),
    ) {
        Icon(
            Icons.Filled.Image, null,
            tint = Color.White.copy(alpha = 0.15f),
            modifier = Modifier.size(28.dp).align(Alignment.Center),
        )
        // 格式标签
        Text(
            file.format,
            fontSize = 9.sp,
            fontWeight = FontWeight.ExtraBold,
            color = if (file.format == "NEF") NikonYellow else NikonText2,
            modifier = Modifier
                .align(Alignment.BottomStart)
                .padding(4.dp)
                .clip(RoundedCornerShape(4.dp))
                .background(Color.Black.copy(alpha = 0.75f))
                .padding(horizontal = 5.dp, vertical = 1.dp),
        )
        // 传输状态点
        Box(
            modifier = Modifier
                .align(Alignment.TopStart)
                .padding(4.dp)
                .size(7.dp)
                .clip(CircleShape)
                .background(if (file.transferred) NikonGreen else NikonOrange),
        )
        // 选中角标
        Box(
            modifier = Modifier
                .align(Alignment.TopEnd)
                .padding(4.dp)
                .size(18.dp)
                .clip(CircleShape)
                .background(if (selected) NikonYellow else Color.Transparent)
                .then(
                    if (!selected) Modifier.borderCircle() else Modifier
                ),
            contentAlignment = Alignment.Center,
        ) {
            if (selected) {
                Icon(Icons.Filled.Check, null, tint = NikonBlack, modifier = Modifier.size(11.dp))
            }
        }
    }
}

private fun Modifier.borderCircle(): Modifier =
    this.border(2.dp, Color.White.copy(alpha = 0.5f), CircleShape)

// ─── 列表内容 ────────────────────────────────────────────────

@Composable
private fun ListContent(
    files: List<GalFile>,
    selected: Set<Long>,
    onToggle: (Long) -> Unit,
    onOpenDetail: (Long) -> Unit,
    modifier: Modifier = Modifier,
) {
    LazyColumn(
        modifier = modifier.fillMaxWidth().padding(horizontal = 18.dp),
        verticalArrangement = Arrangement.spacedBy(6.dp),
        contentPadding = PaddingValues(vertical = 8.dp),
    ) {
        items(files) { f ->
            GalleryListRow(
                file = f,
                selected = f.id in selected,
                onToggle = { onToggle(f.id) },
                onOpen = { onOpenDetail(f.id) },
            )
        }
    }
}

@Composable
private fun GalleryListRow(
    file: GalFile,
    selected: Boolean,
    onToggle: () -> Unit,
    onOpen: () -> Unit,
) {
    Surface(
        color = if (selected) NikonYellow.copy(alpha = 0.06f) else NikonSurface,
        shape = RoundedCornerShape(12.dp),
        border = if (selected) androidx.compose.foundation.BorderStroke(1.5.dp, NikonYellow) else null,
        modifier = Modifier.fillMaxWidth().combinedClickable(
            onClick = onToggle,
            onLongClick = onOpen,
        ),
    ) {
        Row(Modifier.padding(12.dp), verticalAlignment = Alignment.CenterVertically) {
            Box(
                modifier = Modifier
                    .size(40.dp)
                    .clip(RoundedCornerShape(8.dp))
                    .background(
                        if (file.format == "NEF") NikonOrange.copy(alpha = 0.15f)
                        else NikonBlue.copy(alpha = 0.15f)
                    ),
                contentAlignment = Alignment.Center,
            ) {
                Text(file.format, fontSize = 9.sp, fontWeight = FontWeight.ExtraBold,
                    color = if (file.format == "NEF") NikonOrange else NikonBlue)
            }
            Spacer(Modifier.width(12.dp))
            Column(Modifier.weight(1f)) {
                Text(file.name, fontSize = 13.sp, fontWeight = FontWeight.SemiBold, color = NikonText)
                Spacer(Modifier.height(2.dp))
                Row {
                    Text(file.time, fontSize = 11.sp, color = NikonText3)
                    Spacer(Modifier.width(8.dp))
                    Text(file.size, fontSize = 11.sp, color = NikonText2)
                }
            }
            Box(
                modifier = Modifier.size(7.dp).clip(CircleShape)
                    .background(if (file.transferred) NikonGreen else NikonOrange)
            )
        }
    }
}

// ─── 文件详情卡 ──────────────────────────────────────────────

@Composable
private fun FileDetailCard(
    file: GalFile,
    onTransfer: () -> Unit,
    onClose: () -> Unit,
) {
    Surface(
        color = NikonSurface2,
        shape = RoundedCornerShape(16.dp),
        border = androidx.compose.foundation.BorderStroke(1.dp, NikonBorder),
        modifier = Modifier.fillMaxWidth().padding(horizontal = 18.dp, vertical = 8.dp),
    ) {
        Column {
            // 标题栏
            Row(
                Modifier.fillMaxWidth().background(NikonSurface3).padding(horizontal = 14.dp, vertical = 10.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Icon(Icons.Filled.Image, null, tint = NikonYellow, modifier = Modifier.size(14.dp))
                Spacer(Modifier.width(8.dp))
                Text("文件详情", fontSize = 12.sp, fontWeight = FontWeight.ExtraBold, color = NikonText)
                Spacer(Modifier.weight(1f))
                Icon(Icons.Filled.Close, "关闭", tint = NikonText3, modifier = Modifier.size(14.dp).clickable(onClick = onClose))
            }
            // 预览大图
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(120.dp)
                    .background(
                        Brush.linearGradient(listOf(Color(0xFF1A3A5C), Color(0xFF0D1F33)))
                    ),
                contentAlignment = Alignment.Center,
            ) {
                Icon(Icons.Filled.Image, null, tint = Color.White.copy(alpha = 0.12f), modifier = Modifier.size(48.dp))
                // RAW badge
                Text(
                    file.format,
                    fontSize = 10.sp, fontWeight = FontWeight.Black, color = NikonBlack,
                    modifier = Modifier
                        .align(Alignment.TopEnd).padding(8.dp)
                        .clip(RoundedCornerShape(6.dp))
                        .background(NikonYellow).padding(horizontal = 8.dp, vertical = 3.dp),
                )
                // 未传输 badge
                if (!file.transferred) {
                    Text(
                        "未传输",
                        fontSize = 10.sp, fontWeight = FontWeight.Bold, color = Color.White,
                        modifier = Modifier
                            .align(Alignment.TopStart).padding(8.dp)
                            .clip(RoundedCornerShape(6.dp))
                            .background(NikonOrange.copy(alpha = 0.9f)).padding(horizontal = 8.dp, vertical = 3.dp),
                    )
                }
            }
            // 元信息 2x4
            Column(Modifier.padding(14.dp)) {
                listOf(
                    "文件名" to file.name,
                    "文件大小" to file.size,
                    "分辨率" to file.resolution,
                    "拍摄时间" to file.time,
                    "快门速度" to file.shutter,
                    "光圈" to file.aperture,
                    "ISO" to file.iso,
                    "Object Handle" to "0x${file.objectHandle.toString(16).uppercase()}",
                ).chunked(2).forEach { row ->
                    Row(Modifier.fillMaxWidth().padding(bottom = 8.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        row.forEach { (label, value) ->
                            Column(Modifier.weight(1f)) {
                                Text(label, fontSize = 10.sp, fontWeight = FontWeight.Bold, color = NikonText3)
                                Spacer(Modifier.height(2.dp))
                                Text(value, fontSize = 11.sp, fontWeight = FontWeight.Bold, color = NikonText)
                            }
                        }
                        if (row.size == 1) Spacer(Modifier.weight(1f))
                    }
                }
                Spacer(Modifier.height(4.dp))
                // 操作按钮
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(
                        onClick = onTransfer,
                        colors = ButtonDefaults.buttonColors(containerColor = NikonYellow, contentColor = NikonBlack),
                        shape = RoundedCornerShape(10.dp),
                        modifier = Modifier.weight(1f),
                    ) {
                        Icon(Icons.Filled.Download, null, modifier = Modifier.size(14.dp))
                        Spacer(Modifier.width(5.dp))
                        Text("传输到手机", fontSize = 12.sp, fontWeight = FontWeight.ExtraBold)
                    }
                    Box(
                        modifier = Modifier
                            .clip(RoundedCornerShape(10.dp))
                            .background(NikonBlue.copy(alpha = 0.85f))
                            .padding(10.dp),
                        contentAlignment = Alignment.Center,
                    ) { Icon(Icons.Filled.Send, "FTP", tint = Color.White, modifier = Modifier.size(14.dp)) }
                    Box(
                        modifier = Modifier
                            .clip(RoundedCornerShape(10.dp))
                            .background(NikonRed.copy(alpha = 0.12f))
                            .padding(10.dp),
                        contentAlignment = Alignment.Center,
                    ) { Icon(Icons.Filled.Delete, "删除", tint = NikonRed, modifier = Modifier.size(14.dp)) }
                }
            }
        }
    }
}

// ─── 底部统计栏 ──────────────────────────────────────────────

@Composable
private fun GalleryStatsBar(total: Int, transferred: Int, untransferred: Int) {
    Surface(
        color = NikonSurface2,
        shape = RoundedCornerShape(12.dp),
        border = androidx.compose.foundation.BorderStroke(1.dp, NikonBorder),
        modifier = Modifier.fillMaxWidth().padding(horizontal = 18.dp, vertical = 8.dp),
    ) {
        Row(Modifier.fillMaxWidth().padding(vertical = 10.dp)) {
            StatCell("总照片", total.toString(), NikonYellow, showBorder = true)
            StatCell("已传输", transferred.toString(), NikonGreen, showBorder = true)
            StatCell("未传输", untransferred.toString(), NikonOrange, showBorder = false)
        }
    }
}

@Composable
private fun RowScope.StatCell(label: String, value: String, color: Color, showBorder: Boolean) {
    Column(
        modifier = Modifier.weight(1f).then(
            if (showBorder) Modifier.padding(end = 0.dp) else Modifier
        ),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Text(value, fontSize = 16.sp, fontWeight = FontWeight.ExtraBold, color = color)
        Spacer(Modifier.height(2.dp))
        Text(label, fontSize = 10.sp, color = NikonText3)
    }
}
