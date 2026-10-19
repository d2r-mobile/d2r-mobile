// #184 host JVM 入口：真实 C 适配器的窄 JNI wrapper；handle 仍是 owner 表中的不透明 id。
// #184 host JVM entry points: narrow JNI wrappers over the actual C adapter; handles stay opaque ids in the owner table.
#include "audio_focus_jni.h"

#include <stddef.h>

JNIEXPORT jlong JNICALL Java_AudioFocusJniHarness_nativeCreate(JNIEnv *env, jclass cls, jobject context) {
    (void)cls;
    if (!env || !context) return 0;
    dm_afj_owner *owner = NULL;
    if (dm_afj_create(env, context, &owner) != DM_AFJ_OK || !owner) return 0;
    return (jlong)dm_afj_id(owner);
}

JNIEXPORT jint JNICALL Java_AudioFocusJniHarness_nativeRequest(JNIEnv *env, jclass cls, jlong handle,
                                                               jint generation, jboolean accepts_delayed) {
    (void)env;
    (void)cls;
    dm_afj_owner *owner = dm_afj_lookup(handle);
    if (!owner) return DM_AFJ_E_INVALID;
    int result = 0;
    int rc = dm_afj_request(owner, (int)generation, accepts_delayed == JNI_TRUE, &result);
    return rc == DM_AFJ_OK ? result : rc;
}

JNIEXPORT jint JNICALL Java_AudioFocusJniHarness_nativeAbandon(JNIEnv *env, jclass cls, jlong handle) {
    (void)env;
    (void)cls;
    dm_afj_owner *owner = dm_afj_lookup(handle);
    if (!owner) return DM_AFJ_E_INVALID;
    int abandoned = 0;
    int rc = dm_afj_abandon(owner, &abandoned);
    return rc == DM_AFJ_OK ? abandoned : rc;
}

JNIEXPORT jint JNICALL Java_AudioFocusJniHarness_nativeDrain(JNIEnv *env, jclass cls, jlong handle,
                                                             jintArray out) {
    (void)cls;
    if (!env || !out) return DM_AFJ_E_ARGS;
    dm_afj_owner *owner = dm_afj_lookup(handle);
    if (!owner) return DM_AFJ_E_INVALID;
    jsize capacity = (*env)->GetArrayLength(env, out);
    int max = (int)(capacity / 2);
    if (max <= 0) return DM_AFJ_E_ARGS;
    dm_afj_event events[DM_AFJ_RING_CAPACITY];
    int count = 0;
    int rc = dm_afj_drain(owner, events, max < DM_AFJ_RING_CAPACITY ? max : DM_AFJ_RING_CAPACITY, &count);
    if (rc != DM_AFJ_OK) return rc;
    jint pairs[DM_AFJ_RING_CAPACITY * 2];
    for (int i = 0; i < count; i++) {
        pairs[i * 2] = events[i].generation;
        pairs[i * 2 + 1] = events[i].event;
    }
    if (count > 0) (*env)->SetIntArrayRegion(env, out, 0, (jsize)(count * 2), pairs);
    return count;
}

JNIEXPORT void JNICALL Java_AudioFocusJniHarness_nativeDeliver(JNIEnv *env, jclass cls, jlong handle,
                                                               jint generation, jint event) {
    (void)env;
    (void)cls;
    dm_afj_deliver(handle, (int)generation, (int)event);
}

JNIEXPORT jint JNICALL Java_AudioFocusJniHarness_nativeRelease(JNIEnv *env, jclass cls, jlong handle) {
    (void)env;
    (void)cls;
    dm_afj_owner *owner = dm_afj_lookup(handle);
    if (!owner) return DM_AFJ_E_INVALID;
    return dm_afj_release(&owner);
}
