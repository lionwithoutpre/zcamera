package com.nikon.app.viewmodel

import android.app.Application
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import com.nikon.app.jni.CameraApi
import com.nikon.app.jni.CameraBridge
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers

/**
 * CameraViewModel 的工厂。
 *
 * 根因: CameraViewModel 的构造函数除了 Application 还带有默认参数
 * (bridge / ioDispatcher / enablePolling)。系统默认的 AndroidViewModelFactory
 * 只会用反射寻找 "(Application)" 单参构造函数;而 Kotlin 默认参数靠合成方法实现,
 * 反射拿不到,于是 getConstructor(Application) 抛 NoSuchMethodException,被包装成
 * "Cannot create an instance of CameraViewModel",进程直接闪退。
 *
 * 这里显式提供工厂,直接调用 4 参主构造函数(生产默认值:bridge=CameraBridge,
 * ioDispatcher=Dispatchers.IO, enablePolling=true),规避反射限制。
 *
 * 测试侧不过此工厂(直接 new CameraViewModel),故不影响 UI 测试。
 */
class CameraViewModelFactory(
    private val application: Application,
    private val bridge: CameraApi = CameraBridge,
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
    private val enablePolling: Boolean = true,
) : ViewModelProvider.Factory {

    @Suppress("UNCHECKED_CAST")
    override fun <T : ViewModel> create(modelClass: Class<T>): T {
        if (modelClass.isAssignableFrom(CameraViewModel::class.java)) {
            return CameraViewModel(application, bridge, ioDispatcher, enablePolling) as T
        }
        throw IllegalArgumentException("Unknown ViewModel class: $modelClass")
    }
}
