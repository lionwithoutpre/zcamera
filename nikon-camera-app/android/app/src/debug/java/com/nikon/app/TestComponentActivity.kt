package com.nikon.app

import androidx.activity.ComponentActivity

/**
 * 仪表化(androidTest)UI 测试的宿主 Activity。
 *
 * 背景: createAndroidComposeRule 默认用 androidx.activity.ComponentActivity 作测试宿主。
 * 本项目为修掉 "Intent resolved to different process" 报错,在
 * androidTest/AndroidManifest.xml 里把该宿主 Activity 强制到 app 进程
 * (android:process="com.nikon.cameraconnect")。但直接声明 androidx.activity.ComponentActivity
 * 会引发 ClassNotFoundException —— 该 Activity 以测试包 com.nikon.cameraconnect.test 名义注册,
 * 在 app 进程里由测试 APK 的 classloader 加载,而 androidx.activity.ComponentActivity 实际位于
 * app APK 的 dex 中,测试 APK 找不到。
 *
 * 解法:在本测试源码里写一个空的 ComponentActivity 子类。它自身位于测试 APK 的 dex
 * (能被测试 classloader 直接加载),其父类 androidx.activity.ComponentActivity 则经由
 * parent classloader(= app APK)正常加载。这与 HiltTestActivity 等官方测试模式一致。
 */
class TestComponentActivity : ComponentActivity()
