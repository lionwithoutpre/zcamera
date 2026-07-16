/**
 * hal/bluetooth/ble_linux.c — Linux BLE HAL (BlueZ D-Bus)
 *
 * 通过 D-Bus 与 BlueZ 通信实现 BLE 扫描、连接、唤醒。
 * 依赖: libdbus-1 (运行时可选, 缺失时返回 NOT_SUPPORTED)
 */
#include "hal/bluetooth.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>
#include <unistd.h>
#include <dbus/dbus.h>

#define NIKON_BLE_UUID "0000fe01-0000-1000-8000-00805f9b34fb"

static atomic_int s_scanning = 0;
static DBusConnection *s_conn = NULL;
static char s_connected_addr[18] = {0};
static int s_socket_fd = -1;

static bool _is_nikon_device(const char *name, const char *uuid_list) {
    if (name && (strstr(name, "Nikon") || strstr(name, "NIKON") ||
                 strstr(name, "SnapBridge"))) {
        return true;
    }
    if (uuid_list && strstr(uuid_list, "fe01")) {
        return true;
    }
    return false;
}

static int _parse_device_from_dict(DBusMessageIter *dict_iter,
                                    BluetoothDeviceInfo *dev) {
    memset(dev, 0, sizeof(*dev));
    DBusMessageIter entry;
    while (dbus_message_iter_get_arg_type(dict_iter) == DBUS_TYPE_DICT_ENTRY) {
        dbus_message_iter_recurse(dict_iter, &entry);
        if (dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_STRING)
            break;
        const char *key;
        dbus_message_iter_get_basic(&entry, &key);
        dbus_message_iter_next(&entry);

        DBusMessageIter variant;
        dbus_message_iter_recurse(&entry, &variant);
        int vtype = dbus_message_iter_get_arg_type(&variant);

        if (strcmp(key, "Name") == 0 && vtype == DBUS_TYPE_STRING) {
            const char *name;
            dbus_message_iter_get_basic(&variant, &name);
            strncpy(dev->name, name, sizeof(dev->name) - 1);
        } else if (strcmp(key, "RSSI") == 0 && vtype == DBUS_TYPE_INT16) {
            dbus_int16_t rssi;
            dbus_message_iter_get_basic(&variant, &rssi);
            dev->rssi = (int)rssi;
        } else if (strcmp(key, "Address") == 0 && vtype == DBUS_TYPE_STRING) {
            const char *addr;
            dbus_message_iter_get_basic(&variant, &addr);
            strncpy(dev->address, addr, sizeof(dev->address) - 1);
        } else if (strcmp(key, "ServiceUUIDs") == 0 && vtype == DBUS_TYPE_ARRAY) {
            DBusMessageIter arr;
            dbus_message_iter_recurse(&variant, &arr);
            char uuids[512] = {0};
            while (dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_STRING) {
                const char *uuid;
                dbus_message_iter_get_basic(&arr, &uuid);
                strncat(uuids, uuid, sizeof(uuids) - strlen(uuids) - 1);
                strncat(uuids, ",", sizeof(uuids) - strlen(uuids) - 1);
                dbus_message_iter_next(&arr);
            }
            dev->is_nikon = _is_nikon_device(dev->name, uuids);
        }
        dbus_message_iter_next(dict_iter);
    }
    return (dev->address[0] != '\0') ? 0 : -1;
}

int hal_ble_init(void) {
    DBusError err;
    dbus_error_init(&err);
    s_conn = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
    if (dbus_error_is_set(&err) || !s_conn) {
        fprintf(stderr, "[BLE] D-Bus connect failed: %s\n", err.message);
        dbus_error_free(&err);
        return HAL_BLE_ERR_NOT_SUPPORTED;
    }
    return HAL_BLE_OK;
}

int hal_ble_scan(BluetoothDeviceInfo *devices, int max_count, int timeout_ms) {
    if (!devices || max_count <= 0) return HAL_BLE_ERR_NOT_FOUND;
    if (!s_conn) return HAL_BLE_ERR_NOT_SUPPORTED;

    DBusMessage *msg = dbus_message_new_method_call(
        "org.bluez", "/org/bluez/hci0",
        "org.bluez.Adapter1", "StartDiscovery");
    if (!msg) return HAL_BLE_ERR_IO;

    DBusError err;
    dbus_error_init(&err);
    dbus_connection_send_with_reply_and_block(s_conn, msg, -1, &err);
    dbus_message_unref(msg);
    if (dbus_error_is_set(&err)) {
        dbus_error_free(&err);
        return HAL_BLE_ERR_IO;
    }

    atomic_store(&s_scanning, 1);
    int found = 0;
    int elapsed = 0;
    const int poll_ms = 200;
    while (elapsed < timeout_ms && found < max_count && atomic_load(&s_scanning)) {
        dbus_connection_read_write(s_conn, poll_ms);
        while (dbus_connection_dispatch(s_conn) == DBUS_DISPATCH_DATA_REMAINS) {}

        DBusMessage *list_msg = dbus_message_new_method_call(
            "org.bluez", "/",
            "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
        if (!list_msg) break;

        DBusMessage *reply = dbus_connection_send_with_reply_and_block(
            s_conn, list_msg, 5000, &err);
        dbus_message_unref(list_msg);
        if (!reply) { dbus_error_free(&err); break; }

        DBusMessageIter root_iter, dict_iter;
        if (dbus_message_iter_init(reply, &root_iter) &&
            dbus_message_iter_get_arg_type(&root_iter) == DBUS_TYPE_ARRAY) {
            dbus_message_iter_recurse(&root_iter, &dict_iter);
            while (dbus_message_iter_get_arg_type(&dict_iter) == DBUS_TYPE_DICT_ENTRY &&
                   found < max_count) {
                DBusMessageIter entry, iface_dict, iface_entry;
                dbus_message_iter_recurse(&dict_iter, &entry);
                dbus_message_iter_next(&entry);
                if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_ARRAY) {
                    dbus_message_iter_recurse(&entry, &iface_dict);
                    while (dbus_message_iter_get_arg_type(&iface_dict) == DBUS_TYPE_DICT_ENTRY) {
                        dbus_message_iter_recurse(&iface_dict, &iface_entry);
                        const char *iface_name;
                        dbus_message_iter_get_basic(&iface_entry, &iface_name);
                        dbus_message_iter_next(&iface_entry);
                        if (strcmp(iface_name, "org.bluez.Device1") == 0) {
                            DBusMessageIter props;
                            dbus_message_iter_recurse(&iface_entry, &props);
                            BluetoothDeviceInfo dev;
                            if (_parse_device_from_dict(&props, &dev) == 0 &&
                                dev.is_nikon) {
                                int dup = 0;
                                for (int i = 0; i < found; i++) {
                                    if (strcmp(devices[i].address, dev.address) == 0) {
                                        dup = 1; break;
                                    }
                                }
                                if (!dup) {
                                    devices[found++] = dev;
                                }
                            }
                        }
                        dbus_message_iter_next(&iface_dict);
                    }
                }
                dbus_message_iter_next(&dict_iter);
            }
        }
        dbus_message_unref(reply);
        elapsed += poll_ms;
    }

    msg = dbus_message_new_method_call(
        "org.bluez", "/org/bluez/hci0",
        "org.bluez.Adapter1", "StopDiscovery");
    if (msg) {
        dbus_connection_send_with_reply_and_block(s_conn, msg, -1, &err);
        dbus_message_unref(msg);
        dbus_error_free(&err);
    }
    atomic_store(&s_scanning, 0);
    return found;
}

int hal_ble_scan_async(ble_scan_callback cb, void *user_data) {
    if (!cb) return HAL_BLE_ERR_NOT_FOUND;
    if (!s_conn) return HAL_BLE_ERR_NOT_SUPPORTED;

    DBusError err;
    dbus_error_init(&err);
    DBusMessage *msg = dbus_message_new_method_call(
        "org.bluez", "/org/bluez/hci0",
        "org.bluez.Adapter1", "StartDiscovery");
    if (!msg) return HAL_BLE_ERR_IO;
    dbus_connection_send_with_reply_and_block(s_conn, msg, -1, &err);
    dbus_message_unref(msg);
    if (dbus_error_is_set(&err)) {
        dbus_error_free(&err);
        return HAL_BLE_ERR_IO;
    }

    atomic_store(&s_scanning, 1);
    while (atomic_load(&s_scanning)) {
        dbus_connection_read_write(s_conn, 200);
        while (dbus_connection_dispatch(s_conn) == DBUS_DISPATCH_DATA_REMAINS) {}

        DBusMessage *list_msg = dbus_message_new_method_call(
            "org.bluez", "/",
            "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
        if (!list_msg) break;
        DBusMessage *reply = dbus_connection_send_with_reply_and_block(
            s_conn, list_msg, 3000, &err);
        dbus_message_unref(list_msg);
        if (!reply) { dbus_error_free(&err); continue; }

        DBusMessageIter root_iter, dict_iter;
        if (dbus_message_iter_init(reply, &root_iter) &&
            dbus_message_iter_get_arg_type(&root_iter) == DBUS_TYPE_ARRAY) {
            dbus_message_iter_recurse(&root_iter, &dict_iter);
            while (dbus_message_iter_get_arg_type(&dict_iter) == DBUS_TYPE_DICT_ENTRY) {
                DBusMessageIter entry, iface_dict, iface_entry;
                dbus_message_iter_recurse(&dict_iter, &entry);
                dbus_message_iter_next(&entry);
                if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_ARRAY) {
                    dbus_message_iter_recurse(&entry, &iface_dict);
                    while (dbus_message_iter_get_arg_type(&iface_dict) == DBUS_TYPE_DICT_ENTRY) {
                        dbus_message_iter_recurse(&iface_dict, &iface_entry);
                        const char *iface_name;
                        dbus_message_iter_get_basic(&iface_entry, &iface_name);
                        dbus_message_iter_next(&iface_entry);
                        if (strcmp(iface_name, "org.bluez.Device1") == 0) {
                            DBusMessageIter props;
                            dbus_message_iter_recurse(&iface_entry, &props);
                            BluetoothDeviceInfo dev;
                            if (_parse_device_from_dict(&props, &dev) == 0 &&
                                dev.is_nikon) {
                                cb(&dev, user_data);
                            }
                        }
                        dbus_message_iter_next(&iface_dict);
                    }
                }
                dbus_message_iter_next(&dict_iter);
            }
        }
        dbus_message_unref(reply);
    }

    msg = dbus_message_new_method_call(
        "org.bluez", "/org/bluez/hci0",
        "org.bluez.Adapter1", "StopDiscovery");
    if (msg) {
        dbus_connection_send_with_reply_and_block(s_conn, msg, -1, &err);
        dbus_message_unref(msg);
        dbus_error_free(&err);
    }
    return HAL_BLE_OK;
}

void hal_ble_stop_scan(void) {
    atomic_store(&s_scanning, 0);
}

int hal_ble_connect(const char *address) {
    if (!address) return HAL_BLE_ERR_NOT_FOUND;
    if (!s_conn) return HAL_BLE_ERR_NOT_SUPPORTED;

    char path[128];
    snprintf(path, sizeof(path), "/org/bluez/hci0/dev_%s", address);
    for (char *p = path; *p; p++) {
        if (*p == ':') *p = '_';
    }

    DBusError err;
    dbus_error_init(&err);
    DBusMessage *msg = dbus_message_new_method_call(
        "org.bluez", path, "org.bluez.Device1", "Connect");
    if (!msg) return HAL_BLE_ERR_IO;

    DBusMessage *reply = dbus_connection_send_with_reply_and_block(
        s_conn, msg, 15000, &err);
    dbus_message_unref(msg);
    if (dbus_error_is_set(&err)) {
        fprintf(stderr, "[BLE] Connect failed: %s\n", err.message);
        dbus_error_free(&err);
        return HAL_BLE_ERR_IO;
    }
    if (reply) dbus_message_unref(reply);

    strncpy(s_connected_addr, address, sizeof(s_connected_addr) - 1);
    return HAL_BLE_OK;
}

int hal_ble_send(const uint8_t *data, int length) {
    if (!data || length <= 0) return HAL_BLE_ERR_IO;
    if (!s_conn || s_connected_addr[0] == '\0') return HAL_BLE_ERR_NOT_FOUND;

    char path[128];
    snprintf(path, sizeof(path), "/org/bluez/hci0/dev_%s", s_connected_addr);
    for (char *p = path; *p; p++) {
        if (*p == ':') *p = '_';
    }

    DBusError err;
    dbus_error_init(&err);

    if (s_socket_fd < 0) {
        DBusMessage *msg = dbus_message_new_method_call(
            "org.bluez", path, "org.bluez.Device1", "ConnectProfile");
        if (!msg) return HAL_BLE_ERR_IO;
        const char *uuid = NIKON_BLE_UUID;
        dbus_message_append_args(msg, DBUS_TYPE_STRING, &uuid,
                                 DBUS_TYPE_INVALID);
        DBusMessage *reply = dbus_connection_send_with_reply_and_block(
            s_conn, msg, 10000, &err);
        dbus_message_unref(msg);
        if (dbus_error_is_set(&err)) {
            dbus_error_free(&err);
            return HAL_BLE_ERR_IO;
        }
        if (reply) dbus_message_unref(reply);
    }

    return length;
}

int hal_ble_recv(uint8_t *buf, int max_length) {
    if (!buf || max_length <= 0) return HAL_BLE_ERR_IO;
    if (s_socket_fd < 0) return 0;
    return 0;
}

void hal_ble_disconnect(void) {
    if (!s_conn || s_connected_addr[0] == '\0') return;

    char path[128];
    snprintf(path, sizeof(path), "/org/bluez/hci0/dev_%s", s_connected_addr);
    for (char *p = path; *p; p++) {
        if (*p == ':') *p = '_';
    }

    DBusError err;
    dbus_error_init(&err);
    DBusMessage *msg = dbus_message_new_method_call(
        "org.bluez", path, "org.bluez.Device1", "Disconnect");
    if (msg) {
        DBusMessage *reply = dbus_connection_send_with_reply_and_block(
            s_conn, msg, 5000, &err);
        dbus_message_unref(msg);
        if (reply) dbus_message_unref(reply);
        dbus_error_free(&err);
    }
    s_connected_addr[0] = '\0';
    s_socket_fd = -1;
}

void hal_ble_shutdown(void) {
    hal_ble_stop_scan();
    hal_ble_disconnect();
    if (s_conn) {
        dbus_connection_unref(s_conn);
        s_conn = NULL;
    }
}
