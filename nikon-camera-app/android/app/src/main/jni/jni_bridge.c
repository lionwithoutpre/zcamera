/**
 * jni_bridge.c — Android JNI 桥接层
 *
 * 将 Kotlin 层的调用转发给 core 的 CameraAPI。
 * 所有 JNI 函数命名遵循 Java 包路径:
 *   com.nikon.app.jni.CameraBridge
 */
#include "api/camera_api.h"
#include "adapter/wifi_adapter.h"
#include "protocol/ptp.h"
#include <jni.h>
#include <android/log.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define TAG "NikonJNI"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

/* ─── JavaVM 引用(进度回调需要 AttachCurrentThread)── */
static JavaVM *g_jvm = NULL;

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    (void)reserved;
    g_jvm = vm;
    return JNI_VERSION_1_6;
}

/* ─── 传输进度回调桥接 ─── */
static jobject g_progress_callback = NULL;

static void s_transfer_progress_cb(int jobId, TransferProgress *p, void *user) {
    (void)user;
    if (!g_jvm || !g_progress_callback || !p) return;

    JNIEnv *env = NULL;
    jint rc = (*g_jvm)->AttachCurrentThread(g_jvm, (void **)&env, NULL);
    if (rc != JNI_OK || !env) return;

    jclass cls = (*env)->GetObjectClass(env, g_progress_callback);
    jmethodID mid = (*env)->GetMethodID(env, cls, "onProgress", "(IIDI)V");
    if (mid) {
        (*env)->CallVoidMethod(env, g_progress_callback, mid,
            (jint)jobId,
            (jdouble)p->speed_mbps,
            (jint)p->percent,
            (jint)p->status);
    }
    (*env)->DeleteLocalRef(env, cls);
    (*g_jvm)->DetachCurrentThread(g_jvm);
}

/* ─── 全局 API 实例 ──────────────────────────────────────────── */

static CameraAPI *g_api = NULL;

/* ─── 生命周期 ───────────────────────────────────────────────── */

JNIEXPORT jlong JNICALL
Java_com_nikon_app_jni_CameraBridge_nativeCreate(JNIEnv *env, jclass cls,
                                                  jint transport) {
    (void)env; (void)cls;
    g_api = camera_api_create((int)transport);
    LOGD("nativeCreate transport=%d api=%p", transport, (void *)g_api);
    return (jlong)(uintptr_t)g_api;
}

JNIEXPORT void JNICALL
Java_com_nikon_app_jni_CameraBridge_nativeDestroy(JNIEnv *env, jclass cls,
                                                   jlong handle) {
    (void)env; (void)cls;
    CameraAPI *api = (CameraAPI *)(uintptr_t)handle;
    if (api) {
        camera_api_destroy(api);
        LOGD("nativeDestroy ok");
    }
    g_api = NULL;
}

/* ─── 扫描与连接 ─────────────────────────────────────────────── */

JNIEXPORT jobjectArray JNICALL
Java_com_nikon_app_jni_CameraBridge_nativeScan(JNIEnv *env, jclass cls,
                                                jlong handle) {
    (void)cls;
    CameraAPI *api = (CameraAPI *)(uintptr_t)handle;
    if (!api) return NULL;

    int count = 0;
    CameraInfo *cameras = camera_api_scan(api, &count);

    jclass    strClass   = (*env)->FindClass(env, "java/lang/String");
    jobjectArray result   = (*env)->NewObjectArray(env, count, strClass, NULL);

    for (int i = 0; i < count; i++) {
        /* 格式: "id|model|serial|transport|battery|free_gb|total_gb" */
        char buf[256];
        snprintf(buf, sizeof(buf), "%s|%s|%s|%d|%d|%.1f|%.1f",
                 cameras[i].id,
                 cameras[i].model,
                 cameras[i].serial,
                 cameras[i].transport,
                 cameras[i].battery_level,
                 (double)cameras[i].storage_free / (1024*1024*1024),
                 (double)cameras[i].storage_total / (1024*1024*1024));
        (*env)->SetObjectArrayElement(env, result, i,
                                      (*env)->NewStringUTF(env, buf));
    }
    if (cameras) free(cameras);
    return result;
}

JNIEXPORT jint JNICALL
Java_com_nikon_app_jni_CameraBridge_nativeConnect(JNIEnv *env, jclass cls,
                                                   jlong handle,
                                                   jstring camera_id) {
    (void)cls;
    CameraAPI  *api = (CameraAPI *)(uintptr_t)handle;
    const char *id  = (*env)->GetStringUTFChars(env, camera_id, NULL);
    int rc = camera_api_connect(api, id);
    (*env)->ReleaseStringUTFChars(env, camera_id, id);
    LOGD("nativeConnect rc=%d", rc);
    return rc;
}

JNIEXPORT jint JNICALL
Java_com_nikon_app_jni_CameraBridge_nativeConnectUsbFd(JNIEnv *env, jclass cls,
                                                        jlong handle,
                                                        jint fd,
                                                        jstring serial) {
    (void)cls;
    CameraAPI  *api = (CameraAPI *)(uintptr_t)handle;
    const char *s   = serial ? (*env)->GetStringUTFChars(env, serial, NULL) : NULL;
    int rc = camera_api_connect_usb_fd(api, (int)fd, s,
                                        PTP_USB_EP_OUT_DEFAULT,
                                        PTP_USB_EP_IN_DEFAULT);
    if (s) (*env)->ReleaseStringUTFChars(env, serial, s);
    LOGD("nativeConnectUsbFd fd=%d serial=%s rc=%d", fd, s ? s : "null", rc);
    return rc;
}

JNIEXPORT void JNICALL
Java_com_nikon_app_jni_CameraBridge_nativeDisconnect(JNIEnv *env, jclass cls,
                                                      jlong handle) {
    (void)env; (void)cls;
    camera_api_disconnect((CameraAPI *)(uintptr_t)handle);
}

JNIEXPORT jint JNICALL
Java_com_nikon_app_jni_CameraBridge_nativeGetStatus(JNIEnv *env, jclass cls,
                                                     jlong handle) {
    (void)env; (void)cls;
    return (jint)camera_api_get_status((CameraAPI *)(uintptr_t)handle);
}

/* ─── 拍摄控制 ───────────────────────────────────────────────── */

JNIEXPORT jint JNICALL
Java_com_nikon_app_jni_CameraBridge_nativeCapture(JNIEnv *env, jclass cls,
                                                   jlong handle) {
    (void)env; (void)cls;
    return camera_api_capture((CameraAPI *)(uintptr_t)handle);
}

JNIEXPORT jint JNICALL
Java_com_nikon_app_jni_CameraBridge_nativeCaptureBurst(JNIEnv *env, jclass cls,
                                                        jlong handle,
                                                        jint count,
                                                        jint interval_ms) {
    (void)env; (void)cls;
    return camera_api_capture_burst((CameraAPI *)(uintptr_t)handle,
                                    (int)count, (int)interval_ms);
}

/* ─── 相机参数 ───────────────────────────────────────────────── */

JNIEXPORT jint JNICALL
Java_com_nikon_app_jni_CameraBridge_nativeSetProperty(JNIEnv *env, jclass cls,
                                                       jlong handle,
                                                       jint prop_id,
                                                       jlong value) {
    (void)env; (void)cls;
    return camera_api_set_property((CameraAPI *)(uintptr_t)handle,
                                   (uint16_t)prop_id, (uint32_t)value);
}

JNIEXPORT jlong JNICALL
Java_com_nikon_app_jni_CameraBridge_nativeGetProperty(JNIEnv *env, jclass cls,
                                                       jlong handle,
                                                       jint prop_id) {
    (void)env; (void)cls;
    uint32_t val = 0;
    camera_api_get_property((CameraAPI *)(uintptr_t)handle,
                            (uint16_t)prop_id, &val);
    return (jlong)val;
}

/* ─── 文件传输 ───────────────────────────────────────────────── */

JNIEXPORT jint JNICALL
Java_com_nikon_app_jni_CameraBridge_nativeStartTransfer(JNIEnv *env, jclass cls,
                                                         jlong handle,
                                                         jlong object_handle,
                                                         jstring dest_path) {
    (void)cls;
    CameraAPI  *api  = (CameraAPI *)(uintptr_t)handle;
    const char *path = (*env)->GetStringUTFChars(env, dest_path, NULL);
    int job = camera_api_start_transfer(api, (uint32_t)object_handle, path);
    (*env)->ReleaseStringUTFChars(env, dest_path, path);
    return job;
}

JNIEXPORT jint JNICALL
Java_com_nikon_app_jni_CameraBridge_nativeCancelTransfer(JNIEnv *env, jclass cls,
                                                          jlong handle,
                                                          jint job_id) {
    (void)env; (void)cls;
    return camera_api_cancel_transfer((CameraAPI *)(uintptr_t)handle, (int)job_id);
}

/* ─── Picture Control (预设 / 色彩偏移) ─────────────────────── */

JNIEXPORT jbyteArray JNICALL
Java_com_nikon_app_jni_CameraBridge_nativeGetPictCtrl(JNIEnv *env, jclass cls,
                                                       jlong handle) {
    (void)cls;
    CameraAPI    *api = (CameraAPI *)(uintptr_t)handle;
    PictureControl pc;
    int rc = camera_api_get_pictctrl(api, &pc);
    if (rc != CAM_OK) return NULL;

    jbyteArray arr = (*env)->NewByteArray(env, sizeof(PictureControl));
    (*env)->SetByteArrayRegion(env, arr, 0, sizeof(PictureControl),
                               (jbyte *)&pc);
    return arr;
}

JNIEXPORT jint JNICALL
Java_com_nikon_app_jni_CameraBridge_nativeSetPictCtrl(JNIEnv *env, jclass cls,
                                                        jlong handle,
                                                        jbyteArray data) {
    (void)cls;
    CameraAPI *api = (CameraAPI *)(uintptr_t)handle;
    PictureControl pc;
    (*env)->GetByteArrayRegion(env, data, 0, sizeof(PictureControl),
                               (jbyte *)&pc);
    return camera_api_set_pictctrl(api, &pc);
}

/* ─── Wi-Fi 连接 ─────────────────────────────────────────────── */

JNIEXPORT jint JNICALL
Java_com_nikon_app_jni_CameraBridge_nativeConnectWifi(JNIEnv *env, jclass cls,
                                                       jlong handle,
                                                       jstring ip_addr,
                                                       jint port) {
    (void)cls;
    CameraAPI  *api = (CameraAPI *)(uintptr_t)handle;
    const char *ip  = (*env)->GetStringUTFChars(env, ip_addr, NULL);
    int rc = camera_api_connect_wifi(api, ip, (uint16_t)port);
    (*env)->ReleaseStringUTFChars(env, ip_addr, ip);
    LOGD("nativeConnectWifi ip=%s port=%d rc=%d", ip, port, rc);
    return rc;
}

/* ─── 重连回调 ───────────────────────────────────────────────── */

static void _reconnect_callback(ConnectionStatus status, void *user_data) {
    (void)user_data;
    LOGD("_reconnect_callback status=%d", status);
}

JNIEXPORT void JNICALL
Java_com_nikon_app_jni_CameraBridge_nativeSetReconnectCallback(JNIEnv *env,
                                                                jclass cls,
                                                                jlong handle) {
    (void)env; (void)cls;
    CameraAPI *api = (CameraAPI *)(uintptr_t)handle;
    if (api) {
        camera_api_on_status_change(api, _reconnect_callback, NULL);
        LOGD("nativeSetReconnectCallback registered");
    }
}

/* ─── 发送文件到相机 ─────────────────────────────────────────── */

JNIEXPORT jint JNICALL
Java_com_nikon_app_jni_CameraBridge_nativeSendFile(JNIEnv *env, jclass cls,
                                                     jlong handle,
                                                     jstring local_path,
                                                     jint storage_id,
                                                     jstring remote_name) {
    (void)cls;
    CameraAPI  *api  = (CameraAPI *)(uintptr_t)handle;
    const char *path = (*env)->GetStringUTFChars(env, local_path, NULL);
    const char *name = remote_name
                       ? (*env)->GetStringUTFChars(env, remote_name, NULL)
                       : NULL;
    int rc = camera_api_send_file(api, path, (uint32_t)storage_id, name);
    (*env)->ReleaseStringUTFChars(env, local_path, path);
    if (name) (*env)->ReleaseStringUTFChars(env, remote_name, name);
    LOGD("nativeSendFile path=%s storage=%d rc=%d", path, storage_id, rc);
    return rc;
}

/* ─── 文件列举 ───────────────────────────────────────────────── */
/* 返回 String[],每行序列化 FileInfo:
 * "object_handle|filename|size|datetime|is_raw|is_jpeg|width|height|storage_id" */
JNIEXPORT jobjectArray JNICALL
Java_com_nikon_app_jni_CameraBridge_nativeListFiles(JNIEnv *env, jclass cls,
                                                     jlong handle,
                                                     jint storage_id) {
    (void)cls;
    CameraAPI *api = (CameraAPI *)(uintptr_t)handle;
    if (!api) return NULL;

    int count = 0;
    FileInfo *files = camera_api_list_files(api, (uint32_t)storage_id, &count);
    if (!files || count <= 0) {
        if (files) free(files);
        return NULL;
    }

    jclass strClass = (*env)->FindClass(env, "java/lang/String");
    jobjectArray result = (*env)->NewObjectArray(env, count, strClass, NULL);

    for (int i = 0; i < count; i++) {
        char buf[512];
        snprintf(buf, sizeof(buf), "%u|%s|%llu|%s|%d|%d|%d|%d|%u",
                 (unsigned)files[i].object_handle,
                 files[i].filename,
                 (unsigned long long)files[i].size,
                 files[i].datetime,
                 files[i].is_raw ? 1 : 0,
                 files[i].is_jpeg ? 1 : 0,
                 files[i].width,
                 files[i].height,
                 (unsigned)files[i].storage_id);
        (*env)->SetObjectArrayElement(env, result, i,
                                      (*env)->NewStringUTF(env, buf));
    }
    free(files);
    LOGD("nativeListFiles storage=%d count=%d", storage_id, count);
    return result;
}

/* ─── 缩略图 ─────────────────────────────────────────────────── */

JNIEXPORT jbyteArray JNICALL
Java_com_nikon_app_jni_CameraBridge_nativeGetThumbnail(JNIEnv *env, jclass cls,
                                                        jlong handle,
                                                        jlong object_handle) {
    (void)cls;
    CameraAPI *api = (CameraAPI *)(uintptr_t)handle;
    if (!api) return NULL;

    uint8_t *data = NULL;
    uint32_t size = 0;
    int rc = camera_api_get_thumbnail(api, (uint32_t)object_handle, &data, &size);
    if (rc != CAM_OK || !data || size == 0) {
        if (data) free(data);
        return NULL;
    }
    jbyteArray arr = (*env)->NewByteArray(env, (jsize)size);
    (*env)->SetByteArrayRegion(env, arr, 0, (jsize)size, (jbyte *)data);
    free(data);
    return arr;
}

/* ─── 实时取景 ───────────────────────────────────────────────── */
/* 帧缓冲:camera_api_get_liveview_frame 是同步阻塞取一帧,cb 内拷数据,
 * nativeGetLiveViewFrame 主函数在 cb 返回后从缓冲区拷到 ByteArray 返回。
 * 用 mutex 保护,避免多线程竞争(虽然实际只有 LV 协程会调)。 */

static uint8_t *g_lv_frame = NULL;
static int       g_lv_size  = 0;
static pthread_mutex_t g_lv_mutex = PTHREAD_MUTEX_INITIALIZER;

static void _lv_frame_cb(const uint8_t *data, int size, void *user_data) {
    (void)user_data;
    pthread_mutex_lock(&g_lv_mutex);
    if (g_lv_frame) free(g_lv_frame);
    g_lv_frame = (uint8_t *)malloc(size);
    if (g_lv_frame) {
        memcpy(g_lv_frame, data, size);
        g_lv_size = size;
    }
    pthread_mutex_unlock(&g_lv_mutex);
}

JNIEXPORT jint JNICALL
Java_com_nikon_app_jni_CameraBridge_nativeStartLiveView(JNIEnv *env, jclass cls,
                                                         jlong handle) {
    (void)env; (void)cls;
    CameraAPI *api = (CameraAPI *)(uintptr_t)handle;
    int rc = camera_api_start_liveview(api);
    LOGD("nativeStartLiveView rc=%d", rc);
    return rc;
}

JNIEXPORT jint JNICALL
Java_com_nikon_app_jni_CameraBridge_nativeStopLiveView(JNIEnv *env, jclass cls,
                                                        jlong handle) {
    (void)env; (void)cls;
    CameraAPI *api = (CameraAPI *)(uintptr_t)handle;
    int rc = camera_api_stop_liveview(api);
    /* 清理帧缓冲 */
    pthread_mutex_lock(&g_lv_mutex);
    if (g_lv_frame) { free(g_lv_frame); g_lv_frame = NULL; g_lv_size = 0; }
    pthread_mutex_unlock(&g_lv_mutex);
    LOGD("nativeStopLiveView rc=%d", rc);
    return rc;
}

JNIEXPORT jbyteArray JNICALL
Java_com_nikon_app_jni_CameraBridge_nativeGetLiveViewFrame(JNIEnv *env, jclass cls,
                                                            jlong handle) {
    (void)cls;
    CameraAPI *api = (CameraAPI *)(uintptr_t)handle;
    if (!api) return NULL;

    /* 同步取一帧,cb 会写入 g_lv_frame */
    int rc = camera_api_get_liveview_frame(api, _lv_frame_cb, NULL);
    if (rc != CAM_OK) return NULL;

    pthread_mutex_lock(&g_lv_mutex);
    jbyteArray arr = NULL;
    if (g_lv_frame && g_lv_size > 0) {
        arr = (*env)->NewByteArray(env, g_lv_size);
        (*env)->SetByteArrayRegion(env, arr, 0, g_lv_size, (jbyte *)g_lv_frame);
        free(g_lv_frame);
        g_lv_frame = NULL;
        g_lv_size  = 0;
    }
    pthread_mutex_unlock(&g_lv_mutex);
    return arr;
}

/* ─── 删除文件 ───────────────────────────────────────────────── */

JNIEXPORT jint JNICALL
Java_com_nikon_app_jni_CameraBridge_nativeDeleteFile(JNIEnv *env, jclass cls,
                                                      jlong handle,
                                                      jlong object_handle) {
    (void)env; (void)cls;
    CameraAPI *api = (CameraAPI *)(uintptr_t)handle;
    return camera_api_delete_file(api, (uint32_t)object_handle);
}

/* ─── 传输进度回调注册 ──────────────────────────────────────── */

JNIEXPORT void JNICALL
Java_com_nikon_app_jni_CameraBridge_nativeRegisterProgressCallback(JNIEnv *env, jclass cls,
                                                                     jlong handle,
                                                                     jobject callback) {
    (void)cls;
    CameraAPI *api = (CameraAPI *)(uintptr_t)handle;
    if (!api || !callback) return;

    /* 释放旧的回调引用 */
    if (g_progress_callback) {
        (*env)->DeleteGlobalRef(env, g_progress_callback);
        g_progress_callback = NULL;
    }
    g_progress_callback = (*env)->NewGlobalRef(env, callback);
    camera_api_on_transfer_progress(api, s_transfer_progress_cb, NULL);
    LOGD("nativeRegisterProgressCallback registered");
}
