package com.nikon.app.transfer

import com.nikon.app.jni.CameraApi
import com.nikon.model.AppSettings
import com.nikon.model.TransferJob
import com.nikon.model.TransferStatus
import java.util.concurrent.atomic.AtomicInteger
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/**
 * TransferManager — 传输任务队列与并发控制
 *
 * 从 CameraViewModel 拆出的传输状态机(v3 重构):
 *  - [TransferJob.id] 是稳定的 UI 侧 id(单调递增、永不替换),
 *    native 返回的任务 id 存 [TransferJob.nativeJobId] 字段;
 *    进度回调按 nativeJobId 匹配,续传/重传原地更新同一任务 ——
 *    彻底消除旧实现里"负数 tempId 占位再替换"带来的 UI key 抖动、
 *    中间窗口重复项和四处重复的启动代码。
 *  - 所有列表变更走 StateFlow.update 原子操作:JNI 进度回调线程
 *    与主线程并发时不丢更新;promote 的"选定+标记"在同一次 update 内完成。
 *
 * 构造注入均不带 Android 依赖,便于纯 JVM 单元测试。
 */
class TransferManager(
    private val bridge: CameraApi,
    private val scope: CoroutineScope,
    private val ioDispatcher: CoroutineDispatcher,
    /** 当前 native handle(0 = 服务未就绪) */
    private val handleProvider: () -> Long,
    private val settingsProvider: () -> AppSettings,
    /** 错误上报(与 ViewModel 的 reportError 同一通道) */
    private val onError: (String) -> Unit,
    /** 有任务启动(true)/全部结束(false)时回调,ViewModel 据此切 TRANSFERRING/CONNECTED */
    private val onActiveChanged: (Boolean) -> Unit,
    /** 任务完成回调(ViewModel 里做保存相册 + FTP 自动上传) */
    private val onJobDone: (TransferJob) -> Unit,
) {

    private val _jobs = MutableStateFlow<List<TransferJob>>(emptyList())
    val jobs: StateFlow<List<TransferJob>> = _jobs.asStateFlow()

    private val nextId = AtomicInteger(1)

    // ─── 发起 / 排队 ──────────────────────────────────────────

    /**
     * 发起传输。活跃任务数达到 concurrentJobs 上限时进入 WAITING 排队,
     * 由 [promoteNextWaiting] 在有空位时自动启动。
     */
    fun startTransfer(objectHandle: Long, destPath: String) {
        val activeCount = _jobs.value.count { it.status == TransferStatus.ACTIVE }
        if (activeCount >= settingsProvider().concurrentJobs) {
            val job = TransferJob(
                id = nextId.getAndIncrement(),
                objectHandle = objectHandle,
                destPath = destPath,
                filename = destPath.substringAfterLast('/'),
                status = TransferStatus.WAITING,
                percent = 0,
                speedMbps = 0.0,
                note = "等待中",
            )
            _jobs.update { it + job }
            return
        }
        val job = TransferJob(
            id = nextId.getAndIncrement(),
            objectHandle = objectHandle,
            destPath = destPath,
            filename = destPath.substringAfterLast('/'),
            status = TransferStatus.ACTIVE,
            percent = 0,
            speedMbps = 0.0,
            note = "传输中",
        )
        _jobs.update { it + job }
        onActiveChanged(true)
        launchNative(job.id, job.objectHandle, job.destPath)
    }

    // ─── 进度回调(JNI 线程进入)──────────────────────────────

    /**
     * native 进度回调入口。jobId 为 native 任务 id,按 nativeJobId 匹配本地任务。
     * status: 2=完成 -1=失败 其他=进行中。
     */
    fun onNativeProgress(jobId: Int, speedMbps: Double, percent: Int, status: Int) {
        val mapped = when (status) {
            2    -> TransferStatus.DONE
            -1   -> TransferStatus.FAILED
            else -> TransferStatus.ACTIVE
        }
        val note = when (mapped) {
            TransferStatus.DONE   -> "完成"
            TransferStatus.FAILED -> "传输失败"
            else                  -> "传输中"
        }

        // 幂等: 若该 native 任务已是终态(DONE/FAILED), 忽略重复的完成/失败回调,
        // 避免重复执行 onJobDone 的"存相册 + FTP 上传"副作用。
        val prev = _jobs.value.firstOrNull { it.nativeJobId == jobId }
        if (prev != null && prev.status.isTerminal() && mapped.isTerminal()) {
            return
        }

        updateJobByNative(jobId) { it.copy(percent = percent, speedMbps = speedMbps, status = mapped, note = note) }

        if (mapped == TransferStatus.DONE) {
            _jobs.value.firstOrNull { it.nativeJobId == jobId }?.let(onJobDone)
        }
        if (mapped == TransferStatus.DONE || mapped == TransferStatus.FAILED) {
            promoteNextWaiting()
            syncActiveStatus()
        }
    }

    // ─── 取消 / 暂停 ─────────────────────────────────────────

    fun cancelTransfer(jobId: Int) {
        val job = _jobs.value.firstOrNull { it.id == jobId } ?: return
        job.nativeJobId?.let { nid ->
            val h = handleProvider()
            if (h != 0L) {
                scope.launch(ioDispatcher) { bridge.nativeCancelTransfer(h, nid) }
            }
        }
        updateJob(jobId) { it.copy(status = TransferStatus.CANCELLED, note = "已取消") }
        promoteNextWaiting()
    }

    /** 全部暂停:native 层无 pause 语义,等同于取消所有活跃任务 */
    fun pauseAllTransfers() {
        val active = _jobs.value.filter { it.status == TransferStatus.ACTIVE }
        active.forEach { job ->
            job.nativeJobId?.let { nid ->
                val h = handleProvider()
                if (h != 0L) {
                    scope.launch(ioDispatcher) { bridge.nativeCancelTransfer(h, nid) }
                }
            }
            updateJob(job.id) { it.copy(status = TransferStatus.PAUSED, note = "已暂停") }
        }
    }

    /** 全部取消:取消所有非完成态任务 */
    fun cancelAllTransfers() {
        val cancellable = _jobs.value.filter {
            it.status == TransferStatus.ACTIVE || it.status == TransferStatus.PAUSED || it.status == TransferStatus.WAITING
        }
        cancellable.forEach { job ->
            job.nativeJobId?.let { nid ->
                val h = handleProvider()
                if (h != 0L) {
                    scope.launch(ioDispatcher) { bridge.nativeCancelTransfer(h, nid) }
                }
            }
            updateJob(job.id) { it.copy(status = TransferStatus.CANCELLED, note = "已取消") }
        }
    }

    // ─── 续传 / 重传(原地更新,id 不变)─────────────────────

    /** 断点续传:重新对同一 objectHandle 发起传输(native 层支持 offset 续传) */
    fun resumeTransfer(jobId: Int) {
        val job = _jobs.value.firstOrNull { it.id == jobId } ?: return
        updateJob(jobId) {
            it.copy(status = TransferStatus.ACTIVE, nativeJobId = null, note = "断点续传中")
        }
        onActiveChanged(true)
        launchNative(job.id, job.objectHandle, job.destPath, failPrefix = "续传失败")
    }

    /** 重新传输:从头开始(进度清零) */
    fun retryTransfer(jobId: Int) {
        val job = _jobs.value.firstOrNull { it.id == jobId } ?: return
        updateJob(jobId) {
            it.copy(status = TransferStatus.ACTIVE, nativeJobId = null, percent = 0, note = "重新传输中")
        }
        onActiveChanged(true)
        launchNative(job.id, job.objectHandle, job.destPath, failPrefix = "重传失败")
    }

    // ─── 内部 ────────────────────────────────────────────────

    /**
     * 统一的 native 启动路径:start / promote / resume / retry 全部收敛到这里。
     * 启动成功后把 nativeJobId 写入既有任务(不替换 id),失败则标记 FAILED。
     */
    private fun launchNative(
        jobId: Int,
        objectHandle: Long,
        destPath: String,
        failPrefix: String = "传输失败",
    ) {
        scope.launch {
            val h = handleProvider()
            val nativeJobId = if (h == 0L) -1 else withContext(ioDispatcher) {
                bridge.nativeStartTransfer(h, objectHandle, destPath)
            }
            if (nativeJobId < 0) {
                updateJob(jobId) { it.copy(status = TransferStatus.FAILED, note = "$failPrefix (错误码: $nativeJobId)") }
                onError("$failPrefix (错误码: $nativeJobId)")
            } else {
                updateJob(jobId) { it.copy(nativeJobId = nativeJobId) }
            }
            promoteNextWaiting()
            syncActiveStatus()
        }
    }

    /**
     * 把队列里第一个 WAITING 任务提升为 ACTIVE 并启动(带并发校验)。
     * 选定 + 标记 ACTIVE 在同一次原子 update 内完成,避免回调线程与 UI
     * 线程并发时重复提升同一任务。
     */
    private fun promoteNextWaiting() {
        var next: TransferJob? = null
        _jobs.update { list ->
            val activeCount = list.count { it.status == TransferStatus.ACTIVE }
            if (activeCount >= settingsProvider().concurrentJobs) return@update list
            val n = list.firstOrNull { it.status == TransferStatus.WAITING }
                ?: return@update list
            next = n
            list.map { if (it.id == n.id) it.copy(status = TransferStatus.ACTIVE, note = "传输中") else it }
        }
        val selected = next ?: return
        onActiveChanged(true)
        launchNative(selected.id, selected.objectHandle, selected.destPath)
    }

    /** 无活跃任务时通知 ViewModel 回落 CONNECTED */
    private fun syncActiveStatus() {
        if (_jobs.value.none { it.status == TransferStatus.ACTIVE }) {
            onActiveChanged(false)
        }
    }

    /** 按稳定 UI id 原子更新单个任务 */
    private fun updateJob(jobId: Int, transform: (TransferJob) -> TransferJob) {
        _jobs.update { list ->
            list.map { if (it.id == jobId) transform(it) else it }
        }
    }

    /** 按 native 任务 id 原子更新(进度回调路径) */
    private fun updateJobByNative(nativeJobId: Int, transform: (TransferJob) -> TransferJob) {
        _jobs.update { list ->
            list.map { if (it.nativeJobId == nativeJobId) transform(it) else it }
        }
    }
}
