// #184 焦点策略的实例化 JNI 适配：C owner 持 Java bridge 全局引用，Java 回调只把 generation 事件交给本 owner
// 的有界待处理环；不调用 AAudio、不调用 dm_af_policy。所有控制调用必须在 Android 主线程（Java 边界强制），
// 回调也只能在主线程到达；drain 由调用方的串行控制步骤执行。
// #184 instance-owned JNI adapter for the focus policy: the C owner holds a global reference to the Java bridge and
// Java callbacks only enqueue generation-tagged events into the owner's bounded ring. AAudio and dm_af_policy are
// never called from the callback. Control calls must run on the Android main thread (enforced by the Java boundary);
// drain is performed by the caller's serialized control step.
//
// Handle safety: Java only ever sees a monotonically increasing id, never a C pointer. Callbacks look the id up in a
// small owner table under a mutex and take an in-flight reference, so a late callback after release cannot
// dereference freed memory. Ids are never reused.
#pragma once

#include <jni.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DM_AFJ_RING_CAPACITY 64

typedef struct {
    int generation;
    int event;
} dm_afj_event;

typedef struct dm_afj_owner dm_afj_owner;

enum {
    DM_AFJ_OK = 0,
    DM_AFJ_E_INVALID = -1,   // unknown/released handle
    DM_AFJ_E_JNI = -2,       // lookup/allocation/registration/exception failure
    DM_AFJ_E_THREAD = -3,    // control call arrived off the Android main thread
    DM_AFJ_E_OVERFLOW = -4,  // pending ring overflowed; fail closed, caller must retire
    DM_AFJ_E_ARGS = -5
};

// Create an owner bound to the packaged AudioFocusJniBridge class loaded through the activity's classloader.
// Returns DM_AFJ_OK and a non-NULL owner, or an error after rolling back every partial resource.
int dm_afj_create(JNIEnv *env, jobject activity_context, dm_afj_owner **out);

// Platform results are returned verbatim: AUDIOFOCUS_REQUEST_GRANTED/DELAYED/FAILED (1/2/0).
int dm_afj_request(dm_afj_owner *owner, int generation, int accepts_delayed, int *platform_result);
int dm_afj_abandon(dm_afj_owner *owner, int *abandoned);

// Copy up to max events. Returns DM_AFJ_E_OVERFLOW (and zero events) if the bounded ring overflowed, so a permanent
// LOSS can never be silently lost; the caller must retire the owner.
int dm_afj_drain(dm_afj_owner *owner, dm_afj_event *events, int max, int *count);

// 平台回调的统一入口：只做 id 查表与入环；stale/已释放 id 安全丢弃。JNI 回调与 host 迟到投递共用。
// Single platform-callback entry: id lookup and enqueue only; stale/released ids are safely dropped. Shared by the
// JNI callback and the host late-delivery control.
void dm_afj_deliver(jlong handle, int generation, int event);

// Release must be called on the main thread. It invalidates the Java side first, then unregisters the id, waits for
// in-flight callbacks and releases the global reference. Idempotent via a NULL pointer.
int dm_afj_release(dm_afj_owner **owner_ref);

// Test/diagnostic lookup: returns a borrowed owner only while the id is live. Production callers keep their own owner.
dm_afj_owner *dm_afj_lookup(jlong handle);
long dm_afj_id(const dm_afj_owner *owner);

#ifdef __cplusplus
}
#endif
