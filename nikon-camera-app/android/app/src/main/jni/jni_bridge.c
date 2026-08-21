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

/* ─── 敏感数据安全清零 ───
 * 目标: 凭证清零后编译器不得因"结果未被使用"而把清零优化掉。
 * Android NDK (bionic) 不导出 glibc 的 explicit_bzero, 故用 memset +
 * 内联汇编内存屏障保证清除动作一定保留, 跨 clang/NDK 版本可靠。 */
static void nikon_bzero(void *dst, size_t len) {
    if (!dst || !len) return;
    memset(dst, 0, len);
    /* 编译屏障: 告诉编译器 dst 内存发生了"可能被外部观察"的写入,
     * 防止后续把 memset 当作死代码消除。 */
    __asm__ __volatile__("" : : "r"(dst) : "memory");
}
#define NIKON_BZERO(dst, len) nikon_bzero((dst), (len))

/* ─── JavaVM 引用(进度回调需要 AttachCurrentThread)── */
static JavaVM *g_jvm = NULL;

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    (void)reserved;
    g_jvm = vm;
    return JNI_VERSION_1_6;
}

/* ─── 传输进度回调桥接 ─── */
static jobject g_progress_callback = NULL;
/* 缓存回调对象的类引用与方法 id, 避免每次进度回调重复 GetObjectClass/GetMethodID */
static jclass    g_progress_cls = NULL;
static jmethodID g_progress_mid = NULL;
/* 保护上述全局引用: 进度回调运行在 native 传输线程池, 而注册/销毁在主线程,
 * 若不加以保护, 回调线程可能读到主线程已 DeleteGlobalRef 释放的 method id 或
 * callback 引用, 导致 JNI 崩溃(use-after-free)。 */
static pthread_mutex_t g_progress_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ─── 连接状态回调桥接 ───
 * native 状态机(core/api/camera_api.c _set_status_locked)在任何连接状态
 * 变化时触发回调(连接成功/断开/USB 拔出/传输中…)。Kotlin 侧通过
 * nativeSetStatusCallback 注册, 事件化替代 UI 层 1s 轮询 nativeGetStatus。
 * 与进度回调同构: 全局引用 + mutex 保护, 回调在 native 状态机线程触发。 */
static jobject      g_status_callback = NULL;
static jclass       g_status_cls = NULL;
static jmethodID    g_status_mid = NULL;
static pthread_mutex_t g_status_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ─── JNI 线程附着缓存 ───
 * 进度回调来自 native 传输线程池, 高频触发。旧实现每次回调都
 * AttachCurrentThread + DetachCurrentThread, 开销大且 Detach 会清理
 * 线程局部状态。改为: 每个线程只 attach 一次, 通过 pthread_key 析构
 * 函数在线程退出时自动 detach。 */
static pthread_key_t  g_jni_tls_key;
static pthread_once_t g_jni_tls_once = PTHREAD_ONCE_INIT;

static void _jni_tls_destructor(void *value) {
    if (value && g_jvm) {
        (*g_jvm)->DetachCurrentThread(g_jvm);
    }
}

static void _jni_tls_key_init(void) {
    pthread_key_create(&g_jni_tls_key, _jni_tls_destructor);
}

/** 获取当前线程的 JNIEnv: 已附着则直接返回, 否则附着并注册退出时自动 detach */
static JNIEnv *_get_thread_jni_env(void) {
    JNIEnv *env = NULL;
    if (!g_jvm) return NULL;
    if ((*g_jvm)->GetEnv(g_jvm, (void **)&env, JNI_VERSION_1_6) == JNI_OK && env) {
        return env;
    }
    if ((*g_jvm)->AttachCurrentThread(g_jvm, (void **)&env, NULL) != JNI_OK || !env) {
        return NULL;
    }
    pthread_once(&g_jni_tls_once, _jni_tls_key_init);
    pthread_setspecific(g_jni_tls_key, (void *)1);
    return env;
}

static void s_transfer_progress_cb(int jobId, TransferProgress *p, void *user) {
    (void)user;
    if (!g_jvm || !p) return;

    /* 加锁取局部副本, 释放后再调用, 避免持锁跨 JNI 调用 */
    jobject    cb  = NULL;
    jmethodID mid = NULL;
    pthread_mutex_lock(&g_progress_mutex);
    cb  = g_progress_callback;
    mid = g_progress_mid;
    pthread_mutex_unlock(&g_progress_mutex);
    if (!cb || !mid) return;

    JNIEnv *env = _get_thread_jni_env();
    if (!env) return;

    (*env)->CallVoidMethod(env, cb, mid,
        (jint)jobId,
        (jdouble)p->speed_mbps,
        (jint)p->percent,
        (jint)p->status);
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
    (void)cls;
    /* 释放进度回调的全局引用,避免泄漏。加锁: 避免与传输线程的回调并发读写。 */
    pthread_mutex_lock(&g_progress_mutex);
    if (g_progress_callback) {
        (*env)->DeleteGlobalRef(env, g_progress_callback);
        g_progress_callback = NULL;
    }
    if (g_progress_cls) {
        (*env)->DeleteGlobalRef(env, g_progress_cls);
        g_progress_cls = NULL;
        g_progress_mid = NULL;
    }
    pthread_mutex_unlock(&g_progress_mutex);
    /* 释放状态回调的全局引用(与进度回调同构) */
    pthread_mutex_lock(&g_status_mutex);
    if (g_status_callback) {
        (*env)->DeleteGlobalRef(env, g_status_callback);
        g_status_callback = NULL;
    }
    if (g_status_cls) {
        (*env)->DeleteGlobalRef(env, g_status_cls);
        g_status_cls = NULL;
        g_status_mid = NULL;
    }
    pthread_mutex_unlock(&g_status_mutex);
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
    if (!api) return CAM_ERR_INVALID_PARAM;
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
    if (!api) return CAM_ERR_INVALID_PARAM;
    const char *s   = serial ? (*env)->GetStringUTFChars(env, serial, NULL) : NULL;
    int rc = camera_api_connect_usb_fd(api, (int)fd, s,
                                        PTP_USB_EP_OUT_DEFAULT,
                                        PTP_USB_EP_IN_DEFAULT);
    /* 日志必须在 ReleaseStringUTFChars 之前打印,否则引用已释放的指针 (use-after-free) */
    LOGD("nativeConnectUsbFd fd=%d serial=%s rc=%d", fd, s ? s : "null", rc);
    if (s) (*env)->ReleaseStringUTFChars(env, serial, s);
    return rc;
}

JNIEXPORT void JNICALL
Java_com_nikon_app_jni_CameraBridge_nativeDisconnect(JNIEnv *env, jclass cls,
                                                      jlong handle) {
    (void)env; (void)cls;
    CameraAPI *api = (CameraAPI *)(uintptr_t)handle;
    if (!api) return;
    camera_api_disconnect(api);
}

JNIEXPORT jint JNICALL
Java_com_nikon_app_jni_CameraBridge_nativeGetStatus(JNIEnv *env, jclass cls,
                                                     jlong handle) {
    (void)env; (void)cls;
    CameraAPI *api = (CameraAPI *)(uintptr_t)handle;
    if (!api) return 0;  /* STATUS_DISCONNECTED */
    return (jint)camera_api_get_status(api);
}

/* ─── 拍摄控制 ───────────────────────────────────────────────── */

JNIEXPORT jint JNICALL
Java_com_nikon_app_jni_CameraBridge_nativeCapture(JNIEnv *env, jclass cls,
                                                   jlong handle) {
    (void)env; (void)cls;
    CameraAPI *api = (CameraAPI *)(uintptr_t)handle;
    if (!api) return CAM_ERR_INVALID_PARAM;
    return camera_api_capture(api);
}

JNIEXPORT jint JNICALL
Java_com_nikon_app_jni_CameraBridge_nativeCaptureBurst(JNIEnv *env, jclass cls,
                                                        jlong handle,
                                                        jint count,
                                                        jint interval_ms) {
    (void)env; (void)cls;
    CameraAPI *api = (CameraAPI *)(uintptr_t)handle;
    if (!api) return CAM_ERR_INVALID_PARAM;
    return camera_api_capture_burst(api, (int)count, (int)interval_ms);
}

/* ─── 相机参数 ───────────────────────────────────────────────── */

JNIEXPORT jint JNICALL
Java_com_nikon_app_jni_CameraBridge_nativeSetProperty(JNIEnv *env, jclass cls,
                                                       jlong handle,
                                                       jint prop_id,
                                                       jlong value) {
    (void)env; (void)cls;
    CameraAPI *api = (CameraAPI *)(uintptr_t)handle;
    if (!api) return CAM_ERR_INVALID_PARAM;
    return camera_api_set_property(api, (uint16_t)prop_id, (uint32_t)value);
}

JNIEXPORT jlong JNICALL
Java_com_nikon_app_jni_CameraBridge_nativeGetProperty(JNIEnv *env, jclass cls,
                                                       jlong handle,
                                                       jint prop_id) {
    (void)env; (void)cls;
    CameraAPI *api = (CameraAPI *)(uintptr_t)handle;
    if (!api) return 0;
    uint32_t val = 0;
    camera_api_get_property(api, (uint16_t)prop_id, &val);
    return (jlong)val;
}

JNIEXPORT jlongArray JNICALL
Java_com_nikon_app_jni_CameraBridge_nativeGetProperties(JNIEnv *env, jclass cls,
                                                         jlong handle,
                                                         jintArray prop_ids) {
    (void)cls;
    CameraAPI *api = (CameraAPI *)(uintptr_t)handle;
    if (!api || !prop_ids) return NULL;

    jsize count = (*env)->GetArrayLength(env, prop_ids);
    if (count <= 0) return NULL;

    jint     *ids = (*env)->GetIntArrayElements(env, prop_ids, NULL);
    if (!ids) return NULL;

    uint16_t *cids = (uint16_t *)malloc(sizeof(uint16_t) * (size_t)count);
    uint32_t *vals = (uint32_t *)calloc((size_t)count, sizeof(uint32_t));
    if (!cids || !vals) {
        free(cids); free(vals);
        (*env)->ReleaseIntArrayElements(env, prop_ids, ids, JNI_ABORT);
        return NULL;
    }
    for (jsize i = 0; i < count; i++) cids[i] = (uint16_t)ids[i];
    (*env)->ReleaseIntArrayElements(env, prop_ids, ids, JNI_ABORT);

    camera_api_get_properties(api, cids, vals, (int)count);

    jlongArray result = (*env)->NewLongArray(env, count);
    if (result) {
        jlong tmp[64];
        /* 栈缓冲足够覆盖实际用法(轮询 ≤10 个属性); 超出时走堆 */
        jlong *out = (count <= 64) ? tmp : (jlong *)malloc(sizeof(jlong) * (size_t)count);
        if (out) {
            for (jsize i = 0; i < count; i++) out[i] = (jlong)vals[i];
            (*env)->SetLongArrayRegion(env, result, 0, count, out);
            if (out != tmp) free(out);
        }
    }
    free(cids);
    free(vals);
    return result;
}

/* ─── 文件传输 ───────────────────────────────────────────────── */

JNIEXPORT jint JNICALL
Java_com_nikon_app_jni_CameraBridge_nativeStartTransfer(JNIEnv *env, jclass cls,
                                                         jlong handle,
                                                         jlong object_handle,
                                                         jstring dest_path) {
    (void)cls;
    CameraAPI  *api  = (CameraAPI *)(uintptr_t)handle;
    if (!api) return CAM_ERR_INVALID_PARAM;
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
    CameraAPI *api = (CameraAPI *)(uintptr_t)handle;
    if (!api) return CAM_ERR_INVALID_PARAM;
    return camera_api_cancel_transfer(api, (int)job_id);
}

/* ─── Picture Control (预设 / 色彩偏移) ─────────────────────── */

JNIEXPORT jbyteArray JNICALL
Java_com_nikon_app_jni_CameraBridge_nativeGetPictCtrl(JNIEnv *env, jclass cls,
                                                       jlong handle) {
    (void)cls;
    CameraAPI    *api = (CameraAPI *)(uintptr_t)handle;
    if (!api) return NULL;
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
    if (!api) return CAM_ERR_INVALID_PARAM;
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
    if (!api) return CAM_ERR_INVALID_PARAM;
    const char *ip  = (*env)->GetStringUTFChars(env, ip_addr, NULL);
    int rc = camera_api_connect_wifi(api, ip, (uint16_t)port);
    /* 日志必须在 ReleaseStringUTFChars 之前打印,否则引用已释放的指针 (use-after-free) */
    LOGD("nativeConnectWifi ip=%s port=%d rc=%d", ip, port, rc);
    (*env)->ReleaseStringUTFChars(env, ip_addr, ip);
    return rc;
}

/* ─── 重连回调 ───────────────────────────────────────────────── */

static void _reconnect_callback(ConnectionStatus status, void *user_data) {
    (void)user_data;
    LOGD("_reconnect_callback status=%d", status);
    if (!g_jvm) return;

    /* 加锁取局部副本, 释放后再调用, 避免持锁跨 JNI 调用(与进度回调同构) */
    jobject    cb  = NULL;
    jmethodID mid = NULL;
    pthread_mutex_lock(&g_status_mutex);
    cb  = g_status_callback;
    mid = g_status_mid;
    pthread_mutex_unlock(&g_status_mutex);
    if (!cb || !mid) return;

    JNIEnv *env = _get_thread_jni_env();
    if (!env) return;

    (*env)->CallVoidMethod(env, cb, mid, (jint)status);
}

/**
 * 注册连接状态回调(事件化替代 UI 轮询 nativeGetStatus)。
 * 与 nativeRegisterProgressCallback 同构: 全局引用 + 缓存 method id,
 * 注册时释放旧引用避免泄漏。
 */
JNIEXPORT void JNICALL
Java_com_nikon_app_jni_CameraBridge_nativeSetStatusCallback(JNIEnv *env,
                                                            jclass cls,
                                                            jlong handle,
                                                            jobject callback) {
    (void)cls;
    CameraAPI *api = (CameraAPI *)(uintptr_t)handle;
    if (!api || !callback) return;

    pthread_mutex_lock(&g_status_mutex);
    if (g_status_callback) {
        (*env)->DeleteGlobalRef(env, g_status_callback);
        g_status_callback = NULL;
    }
    if (g_status_cls) {
        (*env)->DeleteGlobalRef(env, g_status_cls);
        g_status_cls = NULL;
        g_status_mid = NULL;
    }
    g_status_callback = (*env)->NewGlobalRef(env, callback);

    jclass cls_local = (*env)->GetObjectClass(env, callback);
    if (cls_local) {
        g_status_mid = (*env)->GetMethodID(env, cls_local, "onStatusChanged", "(I)V");
        g_status_cls = (jclass)(*env)->NewGlobalRef(env, cls_local);
        (*env)->DeleteLocalRef(env, cls_local);
    }
    pthread_mutex_unlock(&g_status_mutex);

    camera_api_on_status_change(api, _reconnect_callback, NULL);
    LOGD("nativeSetStatusCallback registered");
}

/* 保留旧符号: nativeSetReconnectCallback — 历史 JNI 命名, 现委托到状态回调注册 */
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
    /* 日志必须在 ReleaseStringUTFChars 之前打印,否则引用已释放的指针 (use-after-free) */
    LOGD("nativeSendFile path=%s storage=%d rc=%d", path, storage_id, rc);
    (*env)->ReleaseStringUTFChars(env, local_path, path);
    if (name) (*env)->ReleaseStringUTFChars(env, remote_name, name);
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
    if (!data || size <= 0) return;
    pthread_mutex_lock(&g_lv_mutex);
    if (g_lv_frame) free(g_lv_frame);
    g_lv_frame = NULL;
    g_lv_size  = 0;
    uint8_t *tmp = (uint8_t *)malloc((size_t)size);
    if (tmp) {
        memcpy(tmp, data, (size_t)size);
        g_lv_frame = tmp;
        g_lv_size  = size;
    }
    pthread_mutex_unlock(&g_lv_mutex);
}

JNIEXPORT jint JNICALL
Java_com_nikon_app_jni_CameraBridge_nativeStartLiveView(JNIEnv *env, jclass cls,
                                                         jlong handle) {
    (void)env; (void)cls;
    CameraAPI *api = (CameraAPI *)(uintptr_t)handle;
    if (!api) return CAM_ERR_INVALID_PARAM;
    int rc = camera_api_start_liveview(api);
    LOGD("nativeStartLiveView rc=%d", rc);
    return rc;
}

JNIEXPORT jint JNICALL
Java_com_nikon_app_jni_CameraBridge_nativeStopLiveView(JNIEnv *env, jclass cls,
                                                        jlong handle) {
    (void)env; (void)cls;
    CameraAPI *api = (CameraAPI *)(uintptr_t)handle;
    if (!api) return CAM_ERR_INVALID_PARAM;
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

    /* 先加锁取出帧数据(拷到栈上持有的临时指针), 解锁后再做 JNI 分配。
     * 不要在持锁时调用 NewByteArray: JNI 分配可能触发 GC/停顿, 与取帧线程
     * 竞争同一把锁时极端情况下造成卡顿/死锁。 */
    uint8_t *frame = NULL;
    int      fsize = 0;
    pthread_mutex_lock(&g_lv_mutex);
    if (g_lv_frame && g_lv_size > 0) {
        frame = g_lv_frame;   /* 所有权转移给调用方, 置空避免重复 free */
        fsize = g_lv_size;
        g_lv_frame = NULL;
        g_lv_size  = 0;
    }
    pthread_mutex_unlock(&g_lv_mutex);

    if (!frame) return NULL;
    jbyteArray arr = (*env)->NewByteArray(env, fsize);
    if (arr) {
        (*env)->SetByteArrayRegion(env, arr, 0, fsize, (jbyte *)frame);
    }
    free(frame);
    return arr;
}

/* ─── 删除文件 ───────────────────────────────────────────────── */

JNIEXPORT jint JNICALL
Java_com_nikon_app_jni_CameraBridge_nativeDeleteFile(JNIEnv *env, jclass cls,
                                                      jlong handle,
                                                      jlong object_handle) {
    (void)env; (void)cls;
    CameraAPI *api = (CameraAPI *)(uintptr_t)handle;
    if (!api) return CAM_ERR_INVALID_PARAM;
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

    /* 释放旧的回调引用 (加锁, 避免与回调线程并发) */
    pthread_mutex_lock(&g_progress_mutex);
    if (g_progress_callback) {
        (*env)->DeleteGlobalRef(env, g_progress_callback);
        g_progress_callback = NULL;
    }
    if (g_progress_cls) {
        (*env)->DeleteGlobalRef(env, g_progress_cls);
        g_progress_cls = NULL;
        g_progress_mid = NULL;
    }
    g_progress_callback = (*env)->NewGlobalRef(env, callback);

    /* 缓存类引用 + methodID, 回调路径不再每次查找 */
    jclass cls_local = (*env)->GetObjectClass(env, callback);
    if (cls_local) {
        g_progress_mid = (*env)->GetMethodID(env, cls_local, "onProgress", "(IIDI)V");
        g_progress_cls = (jclass)(*env)->NewGlobalRef(env, cls_local);
        (*env)->DeleteLocalRef(env, cls_local);
    }
    pthread_mutex_unlock(&g_progress_mutex);

    camera_api_on_transfer_progress(api, s_transfer_progress_cb, NULL);
    LOGD("nativeRegisterProgressCallback registered");
}

/* ─── FTP 自动化 ──────────────────────────────────────────────── */

JNIEXPORT jint JNICALL
Java_com_nikon_app_jni_CameraBridge_nativeSetFtpConfig(JNIEnv *env, jclass cls,
                                                       jlong handle,
                                                       jstring host, jint port,
                                                       jstring username, jstring password,
                                                       jstring remote_path,
                                                       jboolean use_tls, jboolean auto_upload) {
    (void)cls;
    CameraAPI *api = (CameraAPI *)(uintptr_t)handle;
    if (!api) return CAM_ERR_INVALID_PARAM;

    FtpConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.port       = (uint16_t)port;
    cfg.use_tls    = use_tls ? true : false;
    cfg.auto_upload = auto_upload ? true : false;

    const char *h = host        ? (*env)->GetStringUTFChars(env, host, NULL)        : NULL;
    const char *u = username    ? (*env)->GetStringUTFChars(env, username, NULL)    : NULL;
    const char *p = password    ? (*env)->GetStringUTFChars(env, password, NULL)    : NULL;
    const char *r = remote_path ? (*env)->GetStringUTFChars(env, remote_path, NULL) : NULL;

    if (h) snprintf(cfg.host,        sizeof(cfg.host),        "%s", h);
    if (u) snprintf(cfg.username,    sizeof(cfg.username),    "%s", u);
    if (p) snprintf(cfg.password,    sizeof(cfg.password),    "%s", p);
    if (r) snprintf(cfg.remote_path, sizeof(cfg.remote_path), "%s", r);

    if (h) (*env)->ReleaseStringUTFChars(env, host, h);
    if (u) (*env)->ReleaseStringUTFChars(env, username, u);
    if (p) (*env)->ReleaseStringUTFChars(env, password, p);
    if (r) (*env)->ReleaseStringUTFChars(env, remote_path, r);

    int rc = camera_api_set_ftp_config(api, &cfg);
    /* 密码已拷贝进 cfg 并被 camera_api 消费, 使用后清零栈上敏感数据,
     * 避免凭证残留在栈内存中。 */
    NIKON_BZERO(cfg.password, sizeof(cfg.password));
    NIKON_BZERO(cfg.username, sizeof(cfg.username));
    LOGD("nativeSetFtpConfig host=%s port=%d rc=%d",
         cfg.host, (int)cfg.port, rc);
    return rc;
}

JNIEXPORT jint JNICALL
Java_com_nikon_app_jni_CameraBridge_nativeExportToFtp(JNIEnv *env, jclass cls,
                                                      jlong handle, jstring local_path) {
    (void)cls;
    CameraAPI *api = (CameraAPI *)(uintptr_t)handle;
    if (!api) return CAM_ERR_INVALID_PARAM;

    const char *path = (*env)->GetStringUTFChars(env, local_path, NULL);
    int rc = camera_api_export_to_ftp(api, path);
    /* 日志必须在 ReleaseStringUTFChars 之前打印,否则引用已释放的指针 (use-after-free) */
    LOGD("nativeExportToFtp path=%s rc=%d", path, rc);
    (*env)->ReleaseStringUTFChars(env, local_path, path);
    return rc;
}
