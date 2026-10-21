// #184 实现见头文件契约。| Implementation of the contract documented in the header.
#include "audio_focus_jni.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define DM_AFJ_MAX_OWNERS 16

typedef struct {
    jlong id;
    dm_afj_owner *owner;
    int live;
    int refs;
} dm_afj_slot;

struct dm_afj_owner {
    JavaVM *vm;
    jobject bridge;          // global reference
    jmethodID request, abandon, release;
    jlong id;
    pthread_mutex_t lock;    // protects ring/overflow/released
    dm_afj_event ring[DM_AFJ_RING_CAPACITY];
    int head, tail, overflow, released;
};

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_idle = PTHREAD_COND_INITIALIZER;
static dm_afj_slot g_slots[DM_AFJ_MAX_OWNERS];
static jlong g_next_id = 1;

static int pending_exception(JNIEnv *env) {
    if (!(*env)->ExceptionCheck(env)) return 0;
    (*env)->ExceptionClear(env);
    return 1;
}

static dm_afj_owner *lookup_locked(jlong handle, int *slot_out) {
    if (handle <= 0) return NULL;
    for (int i = 0; i < DM_AFJ_MAX_OWNERS; i++) {
        if (g_slots[i].live && g_slots[i].id == handle && g_slots[i].owner) {
            if (slot_out) *slot_out = i;
            return g_slots[i].owner;
        }
    }
    return NULL;
}

dm_afj_owner *dm_afj_lookup(jlong handle) {
    pthread_mutex_lock(&g_lock);
    dm_afj_owner *owner = lookup_locked(handle, NULL);
    pthread_mutex_unlock(&g_lock);
    return owner;
}

long dm_afj_id(const dm_afj_owner *owner) { return owner ? (long)owner->id : 0; }

// 回调只做 id 查表 + 入环，绝不调用 policy/AAudio。| Callback only looks up the id and enqueues.
void dm_afj_deliver(jlong handle, int generation, int event) {
    pthread_mutex_lock(&g_lock);
    int slot = -1;
    dm_afj_owner *owner = lookup_locked(handle, &slot);
    if (!owner) {                       // 已释放或未知 id：迟到回调安全地丢弃
        pthread_mutex_unlock(&g_lock);
        return;
    }
    g_slots[slot].refs++;
    pthread_mutex_unlock(&g_lock);

    pthread_mutex_lock(&owner->lock);
    if (!owner->released) {
        if (owner->head - owner->tail < DM_AFJ_RING_CAPACITY) {
            owner->ring[owner->head % DM_AFJ_RING_CAPACITY].generation = generation;
            owner->ring[owner->head % DM_AFJ_RING_CAPACITY].event = event;
            owner->head++;
        } else {
            // 明确 fail closed：置溢出标志，drain 会报错；绝不静默丢 LOSS。
            // Fail closed: mark overflow so drain reports it; never silently drop a LOSS.
            owner->overflow = 1;
        }
    }
    pthread_mutex_unlock(&owner->lock);

    pthread_mutex_lock(&g_lock);
    g_slots[slot].refs--;
    if (g_slots[slot].refs == 0) pthread_cond_broadcast(&g_idle);
    pthread_mutex_unlock(&g_lock);
}

static void JNICALL afj_on_focus_event(JNIEnv *env, jclass cls, jlong handle, jint generation, jint event) {
    (void)env;
    (void)cls;
    dm_afj_deliver(handle, (int)generation, (int)event);
}

static jclass load_bridge_class(JNIEnv *env, jobject context, int *error) {
    *error = DM_AFJ_E_JNI;
    jclass ctx_cls = (*env)->GetObjectClass(env, context);
    if (!ctx_cls) { pending_exception(env); return NULL; }
    jmethodID get_loader = (*env)->GetMethodID(env, ctx_cls, "getClassLoader",
                                               "()Ljava/lang/ClassLoader;");
    if (!get_loader) { pending_exception(env); (*env)->DeleteLocalRef(env, ctx_cls); return NULL; }
    (*env)->DeleteLocalRef(env, ctx_cls);

    jobject loader = (*env)->CallObjectMethod(env, context, get_loader);
    if (pending_exception(env)) {       // 每次失败先 clear，再允许后续 JNI 调用
        if (loader) (*env)->DeleteLocalRef(env, loader);
        return NULL;
    }
    if (!loader) return NULL;

    jclass loader_cls = (*env)->GetObjectClass(env, loader);
    if (!loader_cls) {
        pending_exception(env);
        (*env)->DeleteLocalRef(env, loader);
        return NULL;
    }
    jmethodID load_class = (*env)->GetMethodID(env, loader_cls, "loadClass",
                                               "(Ljava/lang/String;)Ljava/lang/Class;");
    if (!load_class) {
        pending_exception(env);
        (*env)->DeleteLocalRef(env, loader_cls);
        (*env)->DeleteLocalRef(env, loader);
        return NULL;
    }
    (*env)->DeleteLocalRef(env, loader_cls);

    jstring name = (*env)->NewStringUTF(env, "org.d2rmobile.android.diagnostic.AudioFocusJniBridge");
    if (!name) {
        pending_exception(env);
        (*env)->DeleteLocalRef(env, loader);
        return NULL;
    }
    jclass bridge_cls = (jclass)(*env)->CallObjectMethod(env, loader, load_class, name);
    if (pending_exception(env)) {
        if (bridge_cls) (*env)->DeleteLocalRef(env, bridge_cls);
        (*env)->DeleteLocalRef(env, name);
        (*env)->DeleteLocalRef(env, loader);
        return NULL;
    }
    (*env)->DeleteLocalRef(env, name);
    (*env)->DeleteLocalRef(env, loader);
    if (!bridge_cls) return NULL;
    return bridge_cls;
}

static void unregister_owner(dm_afj_owner *owner) {
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < DM_AFJ_MAX_OWNERS; i++) {
        if (g_slots[i].owner == owner) {
            g_slots[i].live = 0;
            g_slots[i].id = 0;
            while (g_slots[i].refs != 0) pthread_cond_wait(&g_idle, &g_lock);
            g_slots[i].owner = NULL;
            break;
        }
    }
    pthread_mutex_unlock(&g_lock);
}

int dm_afj_create(JNIEnv *env, jobject activity_context, dm_afj_owner **out) {
    if (!env || !activity_context || !out) return DM_AFJ_E_ARGS;
    *out = NULL;
    int error = DM_AFJ_E_JNI;
    jclass bridge_cls = load_bridge_class(env, activity_context, &error);
    if (!bridge_cls) return error;

    static const JNINativeMethod native_methods[] = {
        {"nativeOnFocusEvent", "(JII)V", (void *)afj_on_focus_event},
    };
    if ((*env)->RegisterNatives(env, bridge_cls, native_methods, 1) != 0) {
        pending_exception(env);
        (*env)->DeleteLocalRef(env, bridge_cls);
        return DM_AFJ_E_JNI;
    }
    if (pending_exception(env)) {   // RegisterNatives 报错后再确认一次，避免遗留异常
        (*env)->DeleteLocalRef(env, bridge_cls);
        return DM_AFJ_E_JNI;
    }

    jmethodID ctor = (*env)->GetMethodID(env, bridge_cls, "<init>",
                                         "(Landroid/content/Context;J)V");
    if (!ctor) { pending_exception(env); (*env)->DeleteLocalRef(env, bridge_cls); return DM_AFJ_E_JNI; }
    jmethodID request = (*env)->GetMethodID(env, bridge_cls, "request", "(IZ)I");
    if (!request) { pending_exception(env); (*env)->DeleteLocalRef(env, bridge_cls); return DM_AFJ_E_JNI; }
    jmethodID abandon = (*env)->GetMethodID(env, bridge_cls, "abandon", "()Z");
    if (!abandon) { pending_exception(env); (*env)->DeleteLocalRef(env, bridge_cls); return DM_AFJ_E_JNI; }
    jmethodID release = (*env)->GetMethodID(env, bridge_cls, "release", "()V");
    if (!release) { pending_exception(env); (*env)->DeleteLocalRef(env, bridge_cls); return DM_AFJ_E_JNI; }

    dm_afj_owner *owner = calloc(1, sizeof *owner);
    if (!owner) { (*env)->DeleteLocalRef(env, bridge_cls); return DM_AFJ_E_JNI; }
    if (pthread_mutex_init(&owner->lock, NULL) != 0) {
        free(owner);
        (*env)->DeleteLocalRef(env, bridge_cls);
        return DM_AFJ_E_JNI;
    }
    owner->vm = NULL;
    if ((*env)->GetJavaVM(env, &owner->vm) != 0 || !owner->vm) {
        pending_exception(env);
        pthread_mutex_destroy(&owner->lock);
        free(owner);
        (*env)->DeleteLocalRef(env, bridge_cls);
        return DM_AFJ_E_JNI;
    }
    owner->request = request;
    owner->abandon = abandon;
    owner->release = release;

    pthread_mutex_lock(&g_lock);
    int slot = -1;
    for (int i = 0; i < DM_AFJ_MAX_OWNERS; i++) {
        if (!g_slots[i].live && !g_slots[i].owner) { slot = i; break; }
    }
    if (slot < 0) {
        pthread_mutex_unlock(&g_lock);
        pthread_mutex_destroy(&owner->lock);
        free(owner);
        (*env)->DeleteLocalRef(env, bridge_cls);
        return DM_AFJ_E_JNI;
    }
    owner->id = g_next_id++;
    g_slots[slot].id = owner->id;
    g_slots[slot].owner = owner;
    g_slots[slot].live = 1;
    g_slots[slot].refs = 0;
    pthread_mutex_unlock(&g_lock);

    jobject bridge = (*env)->NewObject(env, bridge_cls, ctor, activity_context, (jlong)owner->id);
    (*env)->DeleteLocalRef(env, bridge_cls);
    if (!bridge) {
        pending_exception(env);         // 构造异常必须清掉，调用方才会看到 DM_AFJ_E_JNI 而不是残留异常
        unregister_owner(owner);
        pthread_mutex_destroy(&owner->lock);
        free(owner);
        return DM_AFJ_E_JNI;
    }
    owner->bridge = (*env)->NewGlobalRef(env, bridge);
    (*env)->DeleteLocalRef(env, bridge);
    if (!owner->bridge) {
        pending_exception(env);
        unregister_owner(owner);
        pthread_mutex_destroy(&owner->lock);
        free(owner);
        return DM_AFJ_E_JNI;
    }
    *out = owner;
    return DM_AFJ_OK;
}

// 控制调用来自 Java 线程（主线程），直接取当前 JNIEnv；不在此 attach，避免无主的长期附加。
// Control calls come from the Java main thread; take the current JNIEnv. No attach here, so no unattached-thread leak.
static JNIEnv *current_env(dm_afj_owner *owner) {
    void *raw = NULL;
    if (!owner || !owner->vm) return NULL;
    if ((*owner->vm)->GetEnv(owner->vm, &raw, JNI_VERSION_1_6) != JNI_OK || !raw) return NULL;
    return (JNIEnv *)raw;
}

int dm_afj_request(dm_afj_owner *owner, int generation, int accepts_delayed, int *platform_result) {
    if (!owner || !platform_result) return DM_AFJ_E_ARGS;
    JNIEnv *env = current_env(owner);
    if (!env) return DM_AFJ_E_JNI;
    jint result = (*env)->CallIntMethod(env, owner->bridge, owner->request,
                                        (jint)generation, accepts_delayed ? JNI_TRUE : JNI_FALSE);
    if ((*env)->ExceptionCheck(env)) {
        jthrowable thrown = (*env)->ExceptionOccurred(env);
        (*env)->ExceptionClear(env);
        jclass ise = (*env)->FindClass(env, "java/lang/IllegalStateException");
        int off_main = ise && (*env)->IsInstanceOf(env, thrown, ise);
        if (ise) (*env)->DeleteLocalRef(env, ise);
        if (thrown) (*env)->DeleteLocalRef(env, thrown);
        return off_main ? DM_AFJ_E_THREAD : DM_AFJ_E_JNI;
    }
    *platform_result = (int)result;
    return DM_AFJ_OK;
}

int dm_afj_abandon(dm_afj_owner *owner, int *abandoned) {
    if (!owner || !abandoned) return DM_AFJ_E_ARGS;
    JNIEnv *env = current_env(owner);
    if (!env) return DM_AFJ_E_JNI;
    jboolean result = (*env)->CallBooleanMethod(env, owner->bridge, owner->abandon);
    if ((*env)->ExceptionCheck(env)) {
        jthrowable thrown = (*env)->ExceptionOccurred(env);
        (*env)->ExceptionClear(env);
        jclass ise = (*env)->FindClass(env, "java/lang/IllegalStateException");
        int off_main = ise && (*env)->IsInstanceOf(env, thrown, ise);
        if (ise) (*env)->DeleteLocalRef(env, ise);
        if (thrown) (*env)->DeleteLocalRef(env, thrown);
        return off_main ? DM_AFJ_E_THREAD : DM_AFJ_E_JNI;   // 与 request/release 的线程错误映射一致
    }
    *abandoned = result == JNI_TRUE;
    return DM_AFJ_OK;
}

int dm_afj_drain(dm_afj_owner *owner, dm_afj_event *events, int max, int *count) {
    if (!owner || !events || max < 0 || !count) return DM_AFJ_E_ARGS;
    *count = 0;
    pthread_mutex_lock(&owner->lock);
    if (owner->overflow) {
        owner->overflow = 0;
        pthread_mutex_unlock(&owner->lock);
        return DM_AFJ_E_OVERFLOW;
    }
    int available = owner->head - owner->tail;
    int take = available < max ? available : max;
    for (int i = 0; i < take; i++) {
        events[i] = owner->ring[(owner->tail + i) % DM_AFJ_RING_CAPACITY];
    }
    owner->tail += take;
    if (owner->tail == owner->head) { owner->head = 0; owner->tail = 0; }
    pthread_mutex_unlock(&owner->lock);
    *count = take;
    return DM_AFJ_OK;
}

int dm_afj_release(dm_afj_owner **owner_ref) {
    if (!owner_ref) return DM_AFJ_E_ARGS;
    dm_afj_owner *owner = *owner_ref;
    if (!owner) return DM_AFJ_OK;

    JNIEnv *env = current_env(owner);
    if (!env) return DM_AFJ_E_JNI;
    (*env)->CallVoidMethod(env, owner->bridge, owner->release);
    if ((*env)->ExceptionCheck(env)) {
        jthrowable thrown = (*env)->ExceptionOccurred(env);
        (*env)->ExceptionClear(env);
        jclass ise = (*env)->FindClass(env, "java/lang/IllegalStateException");
        int off_main = ise && (*env)->IsInstanceOf(env, thrown, ise);
        if (ise) (*env)->DeleteLocalRef(env, ise);
        if (thrown) (*env)->DeleteLocalRef(env, thrown);
        return off_main ? DM_AFJ_E_THREAD : DM_AFJ_E_JNI;   // 保留 owner，主线程可重试
    }

    pthread_mutex_lock(&owner->lock);
    owner->released = 1;
    owner->head = owner->tail = 0;
    owner->overflow = 0;
    pthread_mutex_unlock(&owner->lock);

    unregister_owner(owner);
    (*env)->DeleteGlobalRef(env, owner->bridge);   // 先失效 Java 身份，再释放全局引用
    pthread_mutex_destroy(&owner->lock);
    free(owner);
    *owner_ref = NULL;
    return DM_AFJ_OK;
}
