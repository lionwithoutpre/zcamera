package com.nikon.app

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.content.ContextCompat
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import com.nikon.app.service.CameraService
import com.nikon.app.viewmodel.CameraViewModelFactory
import com.nikon.app.ui.navigation.NikonNavGraph
import com.nikon.app.ui.theme.NikonTheme
import com.nikon.app.viewmodel.CameraViewModel

/**
 * MainActivity — 唯一 Activity, Compose 单入口
 *
 * 职责:
 *   1. 启动 CameraService(前台服务,持有 native handle + USB 监听)
 *   2. Android 13+ 请求通知权限(前台服务通知可见)
 *   3. 承载 Compose UI
 */
class MainActivity : ComponentActivity() {

    private val notificationPermissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { /* 用户授予或拒绝都不阻塞,Service 照常启动 */ }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()

        // Android 13+ 请求通知权限(前台服务通知需要)
        requestNotificationPermission()

        // 启动前台服务 — Service.onCreate 会 nativeCreate 并写入 Application.cameraHandle
        startCameraService()

        // 自定义工厂: CameraViewModel 构造函数带默认参数,默认 AndroidViewModelFactory
        // 反射找不到 (Application) 单参构造,会抛 "Cannot create an instance of CameraViewModel"。
        val cameraViewModelFactory = CameraViewModelFactory(application)

        setContent {
            NikonTheme {
                val viewModel: CameraViewModel = viewModel(factory = cameraViewModelFactory)
                NikonNavGraph(viewModel = viewModel)
            }
        }
    }

    private fun requestNotificationPermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            val granted = ContextCompat.checkSelfPermission(
                this, Manifest.permission.POST_NOTIFICATIONS
            ) == PackageManager.PERMISSION_GRANTED
            if (!granted) {
                notificationPermissionLauncher.launch(Manifest.permission.POST_NOTIFICATIONS)
            }
        }
    }

    private fun startCameraService() {
        // 若 handle 已非 0, 说明 CameraService 前台服务已在运行(如 Activity 重建/旋转),
        // 此时重复 startForegroundService 会无谓触发 onStartCommand + 通知刷新, 直接跳过。
        // 冷启动(handle==0)才真正需要拉起 Service, 由其 onCreate 调 nativeCreate。
        if ((application as NikonApplication).cameraHandle != 0L) return
        val intent = Intent(this, CameraService::class.java)
        startForegroundService(intent)
    }
}
