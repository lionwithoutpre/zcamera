# 硬件抽象层 (HAL) 接口规格书

## 1. USB 设备管理 — `hal/usb.h`

```c
typedef struct {
    uint16_t    vendor_id;       // 厂商 ID (Nikon: 0x04B0)
    uint16_t    product_id;
    char        serial[64];
    char        manufacturer[64];
    char        product_name[128];
    uint8_t     bus_number;
    uint8_t     device_address;
    int         fd;
    void*       priv;
} UsbDeviceInfo;

typedef void (*usb_device_callback)(UsbDeviceInfo* dev, bool connected, void* user_data);

int hal_usb_init(usb_device_callback cb, void* user_data);
int hal_usb_enumerate(UsbDeviceInfo* devices, int max_count);
int hal_usb_request_permission(UsbDeviceInfo* dev);
int hal_usb_open(UsbDeviceInfo* dev);
void hal_usb_close(UsbDeviceInfo* dev);
int hal_usb_bulk_transfer(int fd, uint8_t endpoint, uint8_t* buf, int length, int timeout_ms);
int hal_usb_control_transfer(int fd, uint8_t bmRequestType, uint8_t bRequest,
                             uint16_t wValue, uint16_t wIndex,
                             uint8_t* data, uint16_t wLength, int timeout_ms);
void hal_usb_shutdown(void);

#define HAL_USB_OK              0
#define HAL_USB_ERR_NOT_FOUND   -1
#define HAL_USB_ERR_PERMISSION  -2
#define HAL_USB_ERR_IO          -3
#define HAL_USB_ERR_TIMEOUT     -4
#define HAL_USB_ERR_BUSY        -5
```

## 2. Wi-Fi 连接管理 — `hal/wifi.h`

```c
typedef struct {
    char        ssid[64];
    char        password[64];
    char        ip_address[16];
    uint16_t    port;            // 尼康 Wi-Fi 服务端口 (通常 15740)
    uint8_t     security_type;   // 0=开放, 1=WPA2
    int         rssi;
} WifiConnectionInfo;

int hal_wifi_scan(const char* filter_prefix, WifiConnectionInfo* results, int max_count);
int hal_wifi_connect(WifiConnectionInfo* info);
void hal_wifi_disconnect(void);
int hal_wifi_get_status(void);
int hal_wifi_send(const uint8_t* data, int length, int timeout_ms);
int hal_wifi_recv(uint8_t* buf, int max_length, int timeout_ms);
```

### 2.1 Wi-Fi HAL 实现文件

| 平台 | 文件 | 说明 |
|------|------|------|
| Linux/macOS | `hal/wifi/wifi_posix.c` | POSIX socket 实现 |
| Windows | `hal/wifi/wifi_win.c` | WinHTTP 实现 |

## 3. 蓝牙连接 — `hal/bluetooth.h`

```c
// BLE 连接: 主要用于相机发现和唤醒, 不做大数据传输
// 当前全部为桩代码 (stub)

typedef struct {
    char        address[18];
    char        name[64];
    int         rssi;
} BluetoothDeviceInfo;

int hal_ble_scan(BluetoothDeviceInfo* devices, int max_count, int timeout_ms);
int hal_ble_connect(const char* address);
int hal_ble_send(const uint8_t* data, int length);
int hal_ble_recv(uint8_t* buf, int max_length);
void hal_ble_disconnect(void);
```

## 4. 平台适配要求

| 平台 | USB 后端 | Wi-Fi 后端 | 蓝牙后端 |
|------|---------|-----------|---------|
| Android | android.hardware.usb.UsbManager + JNI | android.net.wifi | android.bluetooth.le |
| Linux | libusb-1.0 | wifi_posix.c (POSIX socket) | BlueZ D-Bus |
| macOS | IOUSBKit / libusb | wifi_posix.c (POSIX socket) | CoreBluetooth |
| Windows | WinUSB / libusb | wifi_win.c (WinHTTP) | WinRT Bluetooth |

## 5. DCIM 路径配置

```c
// DCIM 监听路径优先级:
// 1. 环境变量 NIKON_DCIM_PATH (用户自定义)
// 2. 默认路径 /media (Linux) / /Volumes (macOS)

const char* dcim_path = getenv("NIKON_DCIM_PATH");
if (!dcim_path) dcim_path = "/media";
```
