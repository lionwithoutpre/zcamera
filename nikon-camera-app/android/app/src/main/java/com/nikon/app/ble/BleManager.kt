package com.nikon.app.ble

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattService
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.os.Build
import android.util.Log
import java.util.UUID

/**
 * BleManager — Android BLE 相机发现与唤醒 (SnapBridge)
 *
 * 之前 Android 端 BLE 无后端 (core 只有 ble_linux/macos/win, 没有 ble_android.c),
 * HomeScreen 的「蓝牙」连接方式只是本地状态切换。这里用系统 `android.bluetooth.le`
 * API 补齐真正的 BLE 扫描与 GATT 连接。
 *
 * 说明:
 *   - 发现: 扫描 BLE 广播, 过滤名称含 "Nikon" 的设备。
 *   - 唤醒: 连接 GATT 后寻找 SnapBridge 服务 (FE01, 与 core ble_linux.c 一致),
 *     向可写特征写入唤醒指令。尼康 SnapBridge 的精确唤醒字节为私有协议,
 *     这里使用保守的 0x01 触发尝试, 实际相机可能需按机型调整特征/负载。
 *
 * 权限 (已在 Manifest 声明):
 *   - Android 12+: BLUETOOTH_SCAN / BLUETOOTH_CONNECT (运行时)
 *   - Android 11- : ACCESS_FINE_LOCATION (运行时)
 */
class BleManager(context: Context) {

    companion object {
        private const val TAG = "BleManager"
        /** Nikon SnapBridge BLE 服务 UUID (与 core/hal/bluetooth/ble_linux.c 一致) */
        private val NIKON_SNAPBRIDGE_SERVICE_UUID =
            UUID.fromString("0000fe01-0000-1000-8000-00805f9b34fb")
    }

    /** BLE 设备 (扫描结果) */
    data class Device(val address: String, val name: String, val rssi: Int) {
        val isNikon: Boolean get() = name.contains("Nikon", ignoreCase = true)
    }

    private val appContext = context.applicationContext
    // 空安全链: 无蓝牙硬件 / 纯 JVM 测试环境下 appContext 或 getSystemService
    // 可能为 null, 不能直接解引用, 否则 ViewModel 构造时就会崩。
    private val bluetoothManager =
        appContext?.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager
    private val adapter: BluetoothAdapter? get() = bluetoothManager?.adapter

    private var scanCallback: ScanCallback? = null
    private var gatt: BluetoothGatt? = null
    private var connectCallback: ((Boolean, String) -> Unit)? = null
    private var wakeTarget: BluetoothGattCharacteristic? = null

    /** 是否支持 BLE (无蓝牙硬件 / 关闭蓝牙时 false) */
    val isBleSupported: Boolean get() = adapter != null

    /** 蓝牙是否已开启 */
    val isBluetoothEnabled: Boolean get() = adapter?.isEnabled == true

    // ─── 扫描 ───────────────────────────────────────────────────

    /**
     * 开始 BLE 扫描。每发现一个设备回调一次 (可重复)。
     * @return 是否成功启动
     */
    @SuppressLint("MissingPermission")
    fun startScan(onDevice: (Device) -> Unit): Boolean {
        val scanner = adapter?.bluetoothLeScanner ?: return false
        stopScan()

        val cb = object : ScanCallback() {
            override fun onScanResult(callbackType: Int, result: ScanResult) {
                val d = result.device
                val name = d.name ?: "未知设备"
                onDevice(Device(d.address, name, result.rssi))
            }

            override fun onBatchScanResults(results: MutableList<ScanResult>) {
                for (r in results) {
                    val d = r.device
                    onDevice(Device(d.address, d.name ?: "未知设备", r.rssi))
                }
            }

            override fun onScanFailed(errorCode: Int) {
                Log.w(TAG, "BLE 扫描失败 errorCode=$errorCode")
            }
        }
        scanCallback = cb
        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build()
        return try {
            scanner.startScan(null, settings, cb)
            true
        } catch (e: Exception) {
            Log.w(TAG, "startScan 异常: ${e.message}")
            false
        }
    }

    @SuppressLint("MissingPermission")
    fun stopScan() {
        scanCallback?.let { cb ->
            try { adapter?.bluetoothLeScanner?.stopScan(cb) } catch (_: Exception) {}
        }
        scanCallback = null
    }

    // ─── 连接 + 唤醒 ───────────────────────────────────────────

    /**
     * 连接指定 BLE 设备并尝试唤醒。
     * @param onResult 回调 (成功?, 描述)
     */
    @SuppressLint("MissingPermission")
    fun connectAndWake(address: String, onResult: (Boolean, String) -> Unit): Boolean {
        val a = adapter ?: run { onResult(false, "蓝牙不可用"); return false }
        val ctx = appContext ?: run { onResult(false, "蓝牙不可用"); return false }
        val device: BluetoothDevice = try {
            a.getRemoteDevice(address)
        } catch (e: Exception) {
            onResult(false, "无效地址: ${e.message}")
            return false
        }

        gatt?.close()
        gatt = null
        wakeTarget = null
        connectCallback = onResult

        gatt = device.connectGatt(ctx, false, gattCallback)
        return gatt != null
    }

    fun close() {
        stopScan()
        connectCallback = null
        wakeTarget = null
        try { gatt?.disconnect() } catch (_: Exception) {}
        try { gatt?.close() } catch (_: Exception) {}
        gatt = null
    }

    // ─── GATT 回调 ─────────────────────────────────────────────

    private val gattCallback = object : BluetoothGattCallback() {
        @SuppressLint("MissingPermission")
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                connectCallback?.invoke(false, "GATT 连接失败 status=$status")
                return
            }
            when (newState) {
                BluetoothProfile.STATE_CONNECTED -> {
                    Log.i(TAG, "GATT 已连接, 发现服务…")
                    g.discoverServices()
                }
                BluetoothProfile.STATE_DISCONNECTED -> {
                    connectCallback?.invoke(false, "连接已断开")
                }
            }
        }

        @SuppressLint("MissingPermission")
        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                connectCallback?.invoke(false, "服务发现失败 status=$status")
                return
            }

            val service = findNikonService(g)
            if (service == null) {
                connectCallback?.invoke(false, "未找到 SnapBridge 服务 (FE01)")
                return
            }

            val writable = service.characteristics.firstOrNull { ch ->
                (ch.properties and (BluetoothGattCharacteristic.PROPERTY_WRITE or
                    BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE)) != 0
            }

            if (writable == null) {
                connectCallback?.invoke(false, "未找到可写唤醒特征")
                return
            }

            wakeTarget = writable
            // 尼康 SnapBridge 唤醒指令为私有协议, 这里用保守 0x01 触发尝试
            writable.value = byteArrayOf(0x01)
            writable.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
            if (!g.writeCharacteristic(writable)) {
                connectCallback?.invoke(false, "写入唤醒指令失败")
            }
        }

        @SuppressLint("MissingPermission")
        override fun onCharacteristicWrite(
            g: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            status: Int,
        ) {
            val ok = status == BluetoothGatt.GATT_SUCCESS
            connectCallback?.invoke(ok, if (ok) "BLE 唤醒指令已发送" else "唤醒写入失败 status=$status")
            // 唤醒是一次性操作, 完成后断开
            try { g.disconnect() } catch (_: Exception) {}
            try { g.close() } catch (_: Exception) {}
            gatt = null
        }
    }

    private fun findNikonService(g: BluetoothGatt): BluetoothGattService? {
        return g.services.firstOrNull { it.uuid == NIKON_SNAPBRIDGE_SERVICE_UUID }
    }
}
