#!/usr/bin/env python3
"""Actual-source JNI fixture for #190 text start/event/end.

Extracts the production text_begin_ready/text_begin/native_text_event/text_end
from native.c and compiles them against a tiny JNI function table. This does
not reimplement those functions; the table only supplies the Java boundary.
It covers show failure/exception, global-ref rollback, owner busy, session
callback bounds/staleness, release, inactive App rejection and a terminal
callback re-entering synchronously from show().
"""
from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

from native_callbacks_host import extract_function

FUNCTIONS = ["text_jni_stage_failed", "text_payload_valid", "text_payload_clear",
             "native_text_event", "text_end", "text_begin_ready", "text_begin",
             "ime_entry_load_class", "ime_entry_install", "ime_entry_uninstall",
             "native_ime_entry_request", "destroy"]

PRELUDE = r'''
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void *jobject;
typedef void *jclass;
typedef void *jmethodID;
typedef void *jstring;
typedef unsigned char jboolean;
typedef int jint;
typedef long long jlong;
typedef unsigned short jchar;
typedef int jsize;
#define JNI_FALSE 0
#define JNI_TRUE 1
typedef struct JNINativeInterface JNINativeInterface;
typedef const JNINativeInterface *JNIEnv;
typedef struct { const char *name, *signature; void *fnPtr; } JNINativeMethod;

struct JNINativeInterface {
    jobject (*GetObjectClass)(JNIEnv *, jobject);
    jmethodID (*GetMethodID)(JNIEnv *, jclass, const char *, const char *);
    jmethodID (*GetStaticMethodID)(JNIEnv *, jclass, const char *, const char *);
    jobject (*CallObjectMethod)(JNIEnv *, jobject, jmethodID, ...);
    jstring (*NewStringUTF)(JNIEnv *, const char *);
    jint (*RegisterNatives)(JNIEnv *, jclass, const JNINativeMethod *, jint);
    jobject (*NewObject)(JNIEnv *, jclass, jmethodID, ...);
    jboolean (*CallBooleanMethod)(JNIEnv *, jobject, jmethodID, ...);
    jboolean (*CallStaticBooleanMethod)(JNIEnv *, jclass, jmethodID, ...);
    jobject (*NewGlobalRef)(JNIEnv *, jobject);
    void (*DeleteGlobalRef)(JNIEnv *, jobject);
    void (*DeleteLocalRef)(JNIEnv *, jobject);
    jboolean (*ExceptionCheck)(JNIEnv *);
    void (*ExceptionDescribe)(JNIEnv *);
    void (*ExceptionClear)(JNIEnv *);
    jsize (*GetStringLength)(JNIEnv *, jstring);
    void (*GetStringRegion)(JNIEnv *, jstring, jsize, jsize, jchar *);
};

enum { DM_APP_STATE_CREATED = 0, DM_APP_STATE_OWNED_DIAGNOSTIC = 3 };
enum { DM_WSI_EVENT_STOP = 0, DM_WSI_OK = 0, DM_WSI_STAGE_DRAIN = 0, DM_WSI_CODE_UNKNOWN = 0 };
#define DM_TEXT_MAX_UTF16_UNITS 512
#define EVENT_COMMIT 0
#define EVENT_DELETE 1
#define EVENT_SUBMIT 2
#define EVENT_CANCEL 3

typedef struct { int state; void *surface; } dm_app_lifecycle;
typedef struct { int width, height; } ANativeWindow;
typedef struct { int device; } dm_vk;
typedef struct { int enabled; } dm_wsi;
typedef struct { int unused; } dm_wsi_report;
typedef struct { int unused; } dm_input_arbiter;
typedef struct { int buttons; } dm_npad_state;
typedef struct { int unused; } AInputQueue;
typedef struct { int unused; } native_audio;
static int audio_pause_calls;
static int native_audio_pause(native_audio *audio) { (void)audio; audio_pause_calls++; return 0; }
static int native_audio_destroy(native_audio **audio, JNIEnv *env) { (void)audio; (void)env; return 0; }
typedef struct ANativeActivity { JNIEnv *env; jobject clazz; void *instance; } ANativeActivity;

typedef struct {
    ANativeActivity *activity;
    dm_app_lifecycle *lifecycle;
    ANativeWindow *window;
    void *arbiter;
    int resumed, focused, touch_allowed;
    uint64_t text_session;
    jobject text_bridge;
    jclass text_class;
    jmethodID text_show, text_hide, text_release;
    int text_active;
    int text_back_owned;
    int text_show_accepted;
    uint32_t text_commit_events, text_delete_events, text_submit_events, text_cancel_events;
    uint64_t text_event_total;
    int text_last_units;
    int text_payload_event;
    uint64_t text_payload_session, text_payload_sequence;
    int text_payload_units;
    jchar text_payload[DM_TEXT_MAX_UTF16_UNITS];
    jclass ime_entry_class;
    uint64_t ime_entry_token;
    int ime_entry_installed;
    int timer_fd, frame_fd;
    dm_vk vk;
    dm_wsi wsi;
    dm_wsi_report report;
    AInputQueue *input;
    native_audio *audio;
} app_state;

static app_state *g_text_app;
static uint64_t g_text_next_session;
static app_state *g_ime_entry_app;
static uint64_t g_ime_entry_next_token;
static int text_begin(app_state *);
static void text_end(app_state *, const char *);
static int ime_entry_install(app_state *);
static void ime_entry_uninstall(app_state *);
static jboolean native_ime_entry_request(JNIEnv *, jclass, jlong);
static void destroy(ANativeActivity *);
static int touch_cancel_calls;
static void touch_cancel(app_state *s, const char *reason, int64_t now) {
    (void)s; (void)reason; (void)now; touch_cancel_calls++;
}
static int64_t monotonic_ns(void) { return 1000000000LL; }
static int dm_app_lifecycle_state(const dm_app_lifecycle *l) { return l ? l->state : 0; }
static void *dm_app_lifecycle_surface(const dm_app_lifecycle *l) { return l ? l->surface : NULL; }
static void stop_input_timer(app_state *s) { s->timer_fd = -1; }
static void stop_frame_timer(app_state *s) { s->frame_fd = -1; }
static int dm_wsi_stop(dm_wsi *w) { (void)w; return 0; }
static void dm_wsi_report_event(dm_wsi_report *r, int event) { (void)r; (void)event; }
static void wsi_report_fail(app_state *s, int stage, int code, int unsupported, int open_failure) {
    (void)s; (void)stage; (void)code; (void)unsupported; (void)open_failure;
}
static void wsi_report_snapshot(app_state *s, const char *reason) { (void)s; (void)reason; }
static void AInputQueue_detachLooper(AInputQueue *q) { (void)q; }
static void controller_hotplug_stop(app_state *s) { (void)s; }
static void gamepad_rearm(app_state *s) { (void)s; }
static void dm_input_arbiter_destroy(dm_input_arbiter *a, dm_npad_state *n) { (void)a; (void)n; }
static int dm_app_lifecycle_stop(dm_app_lifecycle *l) { (void)l; return 1; }
static int dm_app_lifecycle_destroy(dm_app_lifecycle *l) { (void)l; return 1; }
static void dm_vk_destroy(dm_vk *v) { (void)v; }
#define LOG(...) do { } while (0)
#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)
static int payload_tail_zero(const app_state *a, int from) {
    for (int i = from; i < DM_TEXT_MAX_UTF16_UNITS; i++)
        if (a->text_payload[i] != 0) return 0;
    return 1;
}

typedef struct { int id; } FakeObject;
static FakeObject activity_obj = {1}, activity2_obj = {2}, activity_class = {3},
    loader_class = {4}, app_loader = {5}, bridge_class = {6}, bridge_obj = {7},
    entry_class = {8}, string_obj = {9};
typedef struct { int units; jchar data[DM_TEXT_MAX_UTF16_UNITS]; } FakeString;
static int mid_activity_loader, mid_loadclass, mid_constructor, mid_show, mid_hide, mid_release;
static int mid_entry_install, mid_entry_uninstall;
#define MID_ENTRY_INSTALL ((jmethodID)&mid_entry_install)
#define MID_ENTRY_UNINSTALL ((jmethodID)&mid_entry_uninstall)
#define MID_ACTIVITY_LOADER ((jmethodID)&mid_activity_loader)
#define MID_LOADCLASS ((jmethodID)&mid_loadclass)
#define MID_CONSTRUCTOR ((jmethodID)&mid_constructor)
#define MID_SHOW ((jmethodID)&mid_show)
#define MID_HIDE ((jmethodID)&mid_hide)
#define MID_RELEASE ((jmethodID)&mid_release)

static int root_fail_class;
static int pending, exception_describes, native_registers, show_calls, release_calls;
static int string_length_throws, string_region_throws;
static int release_result = 1, show_result = 1, show_exception;
static int reentrant_event = -1, reentrant_units;
static int global_ref_calls, globals_deleted, fail_global_at;
static int register_signature_ok, loader_receiver_calls, loaded_from_app_loader;
static jlong constructed_session;
static void *loader_receiver, *loaded_from;
static const char *last_load_name;
static int entry_install_calls, entry_uninstall_calls, entry_install_result = 1;
static int entry_uninstall_result = 1;
static jlong entry_install_token, entry_uninstall_token;
static void *entry_install_activity;

static void set_exception(void) { pending = 1; }
static void native_text_event(JNIEnv *, jclass, jlong, jint, jstring, jint);

static jobject fake_get_object_class(JNIEnv *env, jobject obj) {
    (void)env;
    if (root_fail_class) { set_exception(); return NULL; }
    if (obj == &activity_obj || obj == &activity2_obj) return &activity_class;
    if (obj == &app_loader) return &loader_class;
    set_exception();
    return NULL;
}
static jmethodID fake_get_method_id(JNIEnv *env, jclass cls, const char *name, const char *sig) {
    (void)env; (void)sig;
    if (cls == &activity_class && strcmp(name, "getClassLoader") == 0) return MID_ACTIVITY_LOADER;
    if (cls == &loader_class && strcmp(name, "loadClass") == 0) return MID_LOADCLASS;
    if (cls == &bridge_class) {
        if (strcmp(name, "<init>") == 0) return MID_CONSTRUCTOR;
        if (strcmp(name, "show") == 0) return MID_SHOW;
        if (strcmp(name, "hide") == 0) return MID_HIDE;
        if (strcmp(name, "release") == 0) return MID_RELEASE;
    }
    set_exception();
    return NULL;
}
static jmethodID fake_get_static_method_id(JNIEnv *env, jclass cls, const char *name, const char *sig) {
    (void)env; (void)sig;
    if (cls == &entry_class) {
        if (strcmp(name, "install") == 0) return MID_ENTRY_INSTALL;
        if (strcmp(name, "uninstall") == 0) return MID_ENTRY_UNINSTALL;
    }
    set_exception();
    return NULL;
}
static jobject fake_class_loader(JNIEnv *env, jobject receiver, jmethodID mid, va_list ap) {
    (void)env; (void)ap;
    if (mid == MID_ACTIVITY_LOADER) {
        loader_receiver_calls++;
        loader_receiver = receiver;
        if (receiver != &activity_obj && receiver != &activity2_obj) {
            set_exception();
            return NULL;
        }
        return &app_loader;
    }
    set_exception();
    return NULL;
}
static jobject fake_load_class(JNIEnv *env, jobject receiver, jmethodID mid, va_list ap) {
    (void)env; (void)mid;
    jstring name = va_arg(ap, jstring);
    (void)name;
    loaded_from = receiver;
    loaded_from_app_loader = receiver == &app_loader;
    if (receiver != &app_loader) { set_exception(); return NULL; }
    if (last_load_name && strstr(last_load_name, "DmImeTouchEntry")) return &entry_class;
    return &bridge_class;
}
static jobject fake_call_object(JNIEnv *env, jobject receiver, jmethodID mid, ...) {
    va_list ap; va_start(ap, mid);
    jobject out;
    if (mid == MID_ACTIVITY_LOADER) out = fake_class_loader(env, receiver, mid, ap);
    else if (mid == MID_LOADCLASS) out = fake_load_class(env, receiver, mid, ap);
    else { set_exception(); out = NULL; }
    va_end(ap);
    return out;
}
static jstring fake_new_string(JNIEnv *env, const char *s) {
    (void)env;
    last_load_name = s;
    return &string_obj;
}
static jint fake_register_natives(JNIEnv *env, jclass cls, const JNINativeMethod *m, jint n) {
    (void)env; (void)cls; (void)n;
    native_registers++;
    register_signature_ok = m && m[0].signature
            && (strcmp(m[0].signature, "(JILjava/lang/String;I)V") == 0
                || strcmp(m[0].signature, "(J)Z") == 0);
    return register_signature_ok ? 0 : -1;
}
static jobject fake_new_object(JNIEnv *env, jclass cls, jmethodID mid, ...) {
    (void)env; (void)mid;
    if (cls != &bridge_class) { set_exception(); return NULL; }
    va_list ap; va_start(ap, mid);
    jobject activity = va_arg(ap, jobject);
    constructed_session = va_arg(ap, jlong);
    va_end(ap);
    if (activity != &activity_obj && activity != &activity2_obj) {
        set_exception();
        return NULL;
    }
    return &bridge_obj;
}
static jboolean fake_call_boolean(JNIEnv *env, jobject obj, jmethodID mid, ...) {
    if (obj != &bridge_obj) { set_exception(); return 0; }
    if (mid == MID_SHOW) {
        show_calls++;
        if (show_exception) { set_exception(); return 0; }
        if (reentrant_event >= 0)
            native_text_event(env, NULL, constructed_session, reentrant_event, NULL, 0);
        return (jboolean)show_result;
    }
    if (mid == MID_RELEASE) { release_calls++; return (jboolean)release_result; }
    set_exception();
    return 0;
}
static jboolean fake_call_static_boolean(JNIEnv *env, jclass cls, jmethodID mid, ...) {
    (void)env;
    if (cls != &entry_class) { set_exception(); return 0; }
    va_list ap; va_start(ap, mid);
    jobject activity = va_arg(ap, jobject);
    jlong token = va_arg(ap, jlong);
    va_end(ap);
    if (mid == MID_ENTRY_INSTALL) {
        entry_install_calls++;
        entry_install_activity = activity;
        entry_install_token = token;
        return (jboolean)entry_install_result;
    }
    if (mid == MID_ENTRY_UNINSTALL) {
        entry_uninstall_calls++;
        entry_uninstall_token = token;
        return (jboolean)entry_uninstall_result;
    }
    set_exception();
    return 0;
}
static jobject fake_new_global(JNIEnv *env, jobject obj) {
    (void)env;
    global_ref_calls++;
    if (fail_global_at && global_ref_calls == fail_global_at) {
        set_exception();
        return NULL;
    }
    return obj;
}
static void fake_delete_global(JNIEnv *env, jobject obj) { (void)env; (void)obj; globals_deleted++; }
static void fake_delete_local(JNIEnv *env, jobject obj) { (void)env; (void)obj; }
static jboolean fake_exception_check(JNIEnv *env) { (void)env; return (jboolean)pending; }
static void fake_exception_describe(JNIEnv *env) { (void)env; exception_describes++; }
static void fake_exception_clear(JNIEnv *env) { (void)env; pending = 0; }
static jsize fake_get_string_length(JNIEnv *env, jstring value) {
    (void)env;
    if (!value) return 0;
    if (string_length_throws) { set_exception(); return 0; }
    return ((FakeString *)value)->units;
}
static void fake_get_string_region(JNIEnv *env, jstring value, jsize start, jsize len, jchar *out) {
    (void)env;
    FakeString *s = (FakeString *)value;
    if (string_region_throws) { set_exception(); return; }
    if (!s || start < 0 || len < 0 || start + len > s->units || !out) {
        set_exception();
        return;
    }
    memcpy(out, s->data + start, (size_t)len * sizeof(jchar));
}
static const JNINativeInterface fake_functions = {
    fake_get_object_class, fake_get_method_id, fake_get_static_method_id,
    fake_call_object, fake_new_string, fake_register_natives, fake_new_object,
    fake_call_boolean, fake_call_static_boolean, fake_new_global,
    fake_delete_global, fake_delete_local, fake_exception_check,
    fake_exception_describe, fake_exception_clear,
    fake_get_string_length, fake_get_string_region
};

static void reset_fake(JNIEnv *env) {
    pending = exception_describes = native_registers = show_calls = release_calls = 0;
    release_result = show_result = 1; show_exception = 0; reentrant_event = -1; reentrant_units = 0;
    string_length_throws = string_region_throws = 0;
    global_ref_calls = globals_deleted = fail_global_at = 0;
    register_signature_ok = loader_receiver_calls = loaded_from_app_loader = 0;
    loader_receiver = loaded_from = NULL; constructed_session = 0;
    last_load_name = NULL; entry_install_calls = entry_uninstall_calls = 0;
    entry_install_result = 1; entry_uninstall_result = 1;
    entry_install_token = entry_uninstall_token = 0;
    entry_install_activity = NULL; root_fail_class = 0;
    g_text_app = NULL; g_text_next_session = 0; touch_cancel_calls = 0;
    g_ime_entry_app = NULL; g_ime_entry_next_token = 0;
    *env = &fake_functions;
}

static int test_success_and_events(JNIEnv *env) {
    ANativeActivity act = {env, &activity_obj};
    dm_app_lifecycle life = {DM_APP_STATE_OWNED_DIAGNOSTIC, NULL};
    ANativeWindow window = {1, 1};
    app_state a; memset(&a, 0, sizeof a);
    a.activity = &act; a.lifecycle = &life; a.window = &window;
    a.resumed = a.focused = a.touch_allowed = 1;
    a.arbiter = (void *)1;
    life.surface = &window;
    CHECK(text_begin(&a) == 1);
    CHECK(loader_receiver == &activity_obj && loaded_from == &app_loader && loaded_from_app_loader);
    CHECK(native_registers == 1 && register_signature_ok && show_calls == 1);
    CHECK(constructed_session > 0 && a.text_session == (uint64_t)constructed_session);
    CHECK(a.text_active && a.text_show_accepted && a.text_bridge == &bridge_obj);
    CHECK(a.text_class == &bridge_class && a.text_release == MID_RELEASE);
    CHECK(touch_cancel_calls == 1);
    FakeString commit = {4, {0x0041, 0x4F60, 0xD83D, 0xDE00}};
    FakeString deleted = {2, {0xD83D, 0xDE00}};
    FakeString submitted = {2, {0x0041, 0x4F60}};
    native_text_event(env, NULL, (jlong)a.text_session, EVENT_COMMIT, (jstring)&commit, 4);
    native_text_event(env, NULL, (jlong)a.text_session, EVENT_DELETE, (jstring)&deleted, 2);
    CHECK(a.text_commit_events == 1 && a.text_delete_events == 1 && a.text_event_total == 2);
    CHECK(a.text_last_units == 2 && a.text_payload_units == 2);
    CHECK(a.text_payload_event == EVENT_DELETE && a.text_payload_sequence == 2);
    CHECK(a.text_payload[0] == 0xD83D && a.text_payload[1] == 0xDE00);
    CHECK(payload_tail_zero(&a, 2));
    string_region_throws = 1;
    native_text_event(env, NULL, (jlong)a.text_session, EVENT_COMMIT, (jstring)&commit, 4);
    CHECK(a.text_commit_events == 1 && a.text_event_total == 2 && !pending);
    CHECK(a.text_payload_event == EVENT_DELETE && a.text_payload_sequence == 2);
    CHECK(payload_tail_zero(&a, 2) && a.text_payload[0] == 0xD83D);
    string_region_throws = 0;
    string_length_throws = 1;
    native_text_event(env, NULL, (jlong)a.text_session, EVENT_COMMIT, (jstring)&commit, 4);
    CHECK(a.text_commit_events == 1 && a.text_event_total == 2 && !pending);
    CHECK(a.text_payload_event == EVENT_DELETE && a.text_payload_sequence == 2);
    CHECK(payload_tail_zero(&a, 2) && a.text_payload[0] == 0xD83D);
    string_length_throws = 0;
    native_text_event(env, NULL, (jlong)a.text_session, EVENT_COMMIT, NULL, 513);
    native_text_event(env, NULL, (jlong)a.text_session, 4, NULL, 0);
    native_text_event(env, NULL, (jlong)a.text_session + 1, EVENT_COMMIT, NULL, 0);
    CHECK(a.text_commit_events == 1 && a.text_event_total == 2);
    FakeString high = {1, {0xD83D}};
    FakeString mismatch = {2, {0x0041, 0x0042}};
    native_text_event(env, NULL, (jlong)a.text_session, EVENT_COMMIT, (jstring)&high, 1);
    native_text_event(env, NULL, (jlong)a.text_session, EVENT_COMMIT, (jstring)&mismatch, 1);
    CHECK(a.text_commit_events == 1 && a.text_event_total == 2);
    FakeString large = {512, {0}};
    for (int i = 0; i < 510; i++) large.data[i] = 0x0041;
    large.data[510] = 0xD83D; large.data[511] = 0xDE00;
    native_text_event(env, NULL, (jlong)a.text_session, EVENT_COMMIT, (jstring)&large, 512);
    CHECK(a.text_commit_events == 2 && a.text_payload_units == 512);
    CHECK(a.text_payload[510] == 0xD83D && a.text_payload[511] == 0xDE00);
    CHECK(a.text_payload_event == EVENT_COMMIT && a.text_payload_sequence == 3);
    native_text_event(env, NULL, (jlong)a.text_session, EVENT_COMMIT, (jstring)&submitted, 3);
    CHECK(a.text_commit_events == 2 && a.text_event_total == 3);
    native_text_event(env, NULL, (jlong)a.text_session, EVENT_SUBMIT, (jstring)&submitted, 2);
    CHECK(!a.text_active && a.text_session == 0 && a.text_submit_events == 1);
    CHECK(a.text_payload_event == EVENT_SUBMIT && a.text_payload_units == 2);
    CHECK(a.text_payload[0] == 0x0041 && a.text_payload[1] == 0x4F60);
    CHECK(payload_tail_zero(&a, 2));
    native_text_event(env, NULL, (jlong)constructed_session, EVENT_COMMIT, NULL, 0);
    CHECK(a.text_commit_events == 2);
    text_end(&a, "test_end");
    CHECK(release_calls == 1 && a.text_bridge == NULL && a.text_class == NULL);
    CHECK(g_text_app == NULL && touch_cancel_calls >= 2);
    CHECK(a.text_payload_units == 0 && a.text_payload_event == -1 && a.text_payload_sequence == 0);
    CHECK(payload_tail_zero(&a, 0));
    native_text_event(env, NULL, (jlong)constructed_session, EVENT_COMMIT, NULL, 0);
    CHECK(a.text_commit_events == 2);
    CHECK(text_begin(&a) == 1);
    CHECK(payload_tail_zero(&a, 0));
    native_text_event(env, NULL, (jlong)a.text_session, EVENT_COMMIT, (jstring)&submitted, 2);
    CHECK(a.text_payload_units == 2 && a.text_payload_sequence == 1);
    CHECK(payload_tail_zero(&a, 2));
    native_text_event(env, NULL, (jlong)a.text_session, EVENT_CANCEL, NULL, 0);
    CHECK(!a.text_active && a.text_payload_event == EVENT_CANCEL);
    CHECK(a.text_payload_units == 0 && a.text_payload_sequence == 2);
    CHECK(payload_tail_zero(&a, 0));
    text_end(&a, "cancel_end");
    CHECK(payload_tail_zero(&a, 0));
    CHECK(text_begin(&a) == 1);
    native_text_event(env, NULL, (jlong)a.text_session, EVENT_COMMIT, (jstring)&submitted, 2);
    CHECK(a.text_payload_units == 2 && a.text_payload_sequence == 1);
    CHECK(payload_tail_zero(&a, 2));
    native_text_event(env, NULL, (jlong)a.text_session, EVENT_COMMIT, NULL, 0);
    CHECK(a.text_payload_units == 0 && a.text_payload_sequence == 2);
    CHECK(payload_tail_zero(&a, 0));
    text_end(&a, "empty_end");
    CHECK(payload_tail_zero(&a, 0));
    return 0;
}

static int test_show_failure_and_exception(JNIEnv *env) {
    ANativeActivity act = {env, &activity_obj};
    dm_app_lifecycle life = {DM_APP_STATE_OWNED_DIAGNOSTIC, NULL};
    ANativeWindow window = {1, 1};
    app_state a; memset(&a, 0, sizeof a);
    a.activity = &act; a.lifecycle = &life; a.window = &window;
    a.resumed = a.focused = a.touch_allowed = 1; life.surface = &window;
    show_result = 0;
    CHECK(text_begin(&a) == 0);
    CHECK(!a.text_active && !a.text_show_accepted && a.text_bridge == NULL && a.text_class == NULL);
    CHECK(g_text_app == NULL && show_calls == 1 && release_calls == 1);
    show_result = 1; show_exception = 1;
    CHECK(text_begin(&a) == 0);
    CHECK(!pending && !a.text_active && a.text_bridge == NULL && g_text_app == NULL);
    CHECK(show_calls == 2 && exception_describes >= 1);
    return 0;
}

static int test_global_ref_rollback(JNIEnv *env) {
    ANativeActivity act = {env, &activity_obj};
    dm_app_lifecycle life = {DM_APP_STATE_OWNED_DIAGNOSTIC, NULL};
    ANativeWindow window = {1, 1};
    app_state a; memset(&a, 0, sizeof a);
    a.activity = &act; a.lifecycle = &life; a.window = &window;
    a.resumed = a.focused = a.touch_allowed = 1; life.surface = &window;
    fail_global_at = 1;
    CHECK(text_begin(&a) == 0);
    CHECK(global_ref_calls == 1 && release_calls == 1);
    CHECK(!a.text_active && a.text_bridge == NULL && a.text_class == NULL && g_text_app == NULL);
    reset_fake(env);
    a.activity = &act; a.lifecycle = &life; a.window = &window;
    a.resumed = a.focused = a.touch_allowed = 1; life.surface = &window;
    fail_global_at = 2;
    CHECK(text_begin(&a) == 0);
    CHECK(global_ref_calls == 2 && globals_deleted == 1 && release_calls == 1);
    CHECK(!a.text_active && a.text_bridge == NULL && a.text_class == NULL && g_text_app == NULL);
    return 0;
}

static int test_owner_busy_and_inactive(JNIEnv *env) {
    ANativeActivity act = {env, &activity_obj};
    ANativeActivity act2 = {env, &activity2_obj};
    dm_app_lifecycle life = {DM_APP_STATE_OWNED_DIAGNOSTIC, NULL};
    ANativeWindow window = {1, 1};
    app_state a; memset(&a, 0, sizeof a);
    a.activity = &act; a.lifecycle = &life; a.window = &window;
    a.resumed = a.focused = a.touch_allowed = 1; life.surface = &window;
    CHECK(text_begin(&a) == 1);
    app_state b; memset(&b, 0, sizeof b);
    b.activity = &act2; b.lifecycle = &life; b.window = &window;
    b.resumed = b.focused = b.touch_allowed = 1;
    CHECK(text_begin(&b) == 0);
    CHECK(show_calls == 1 && g_text_app == &a && !b.text_active);
    text_end(&a, "busy_done");
    app_state c; memset(&c, 0, sizeof c);
    c.activity = &act; c.lifecycle = &life; c.window = &window;
    c.resumed = 0; c.focused = 1; c.touch_allowed = 1; life.surface = &window;
    CHECK(text_begin(&c) == 0);
    CHECK(show_calls == 1 && !c.text_active && g_text_app == NULL);
    c.resumed = 1; c.focused = 0;
    CHECK(text_begin(&c) == 0);
    c.focused = 1; c.touch_allowed = 0;
    CHECK(text_begin(&c) == 0);
    c.touch_allowed = 1; c.window = NULL;
    CHECK(text_begin(&c) == 0);
    c.window = &window; life.state = DM_APP_STATE_CREATED;
    CHECK(text_begin(&c) == 0);
    life.state = DM_APP_STATE_OWNED_DIAGNOSTIC; life.surface = NULL;
    CHECK(text_begin(&c) == 0);
    CHECK(show_calls == 1 && !c.text_active && g_text_app == NULL);
    life.surface = &window;
    c.window = &window; c.resumed = c.focused = c.touch_allowed = 1;
    CHECK(text_begin(&c) == 1);
    text_end(&c, "pause");
    CHECK(release_calls == 2 && c.text_bridge == NULL && g_text_app == NULL);
    CHECK(text_begin(&c) == 1);
    text_end(&c, "window_destroyed");
    CHECK(release_calls == 3 && c.text_bridge == NULL && g_text_app == NULL);
    return 0;
}

static int test_reentrant_terminal_during_show(JNIEnv *env) {
    ANativeActivity act = {env, &activity_obj};
    dm_app_lifecycle life = {DM_APP_STATE_OWNED_DIAGNOSTIC, NULL};
    ANativeWindow window = {1, 1};
    app_state a; memset(&a, 0, sizeof a);
    a.activity = &act; a.lifecycle = &life; a.window = &window;
    a.resumed = a.focused = a.touch_allowed = 1;
    a.arbiter = (void *)1;
    life.surface = &window;
    reentrant_event = EVENT_CANCEL;
    CHECK(text_begin(&a) == 0);
    CHECK(show_calls == 1 && release_calls == 1);
    CHECK(!a.text_active && !a.text_show_accepted && a.text_session == 0);
    CHECK(a.text_bridge == NULL && a.text_class == NULL && g_text_app == NULL);
    CHECK(a.text_cancel_events == 1 && touch_cancel_calls >= 1);
    return 0;
}

static int test_entry_owner_request(JNIEnv *env) {
    ANativeActivity act = {env, &activity_obj};
    ANativeActivity act2 = {env, &activity2_obj};
    dm_app_lifecycle life = {DM_APP_STATE_OWNED_DIAGNOSTIC, NULL};
    ANativeWindow window = {1, 1};
    app_state a; memset(&a, 0, sizeof a);
    a.activity = &act; a.lifecycle = &life; a.window = &window;
    a.resumed = a.focused = a.touch_allowed = 1; a.arbiter = (void *)1;
    life.surface = &window;
    CHECK(ime_entry_install(&a) == 1);
    CHECK(a.ime_entry_installed && a.ime_entry_token != 0 && a.ime_entry_class == &entry_class);
    CHECK(entry_install_calls == 1 && entry_install_activity == &activity_obj);
    CHECK(entry_install_token == (jlong)a.ime_entry_token);
    app_state b; memset(&b, 0, sizeof b);
    b.activity = &act2; b.lifecycle = &life; b.window = &window;
    b.resumed = b.focused = b.touch_allowed = 1;
    CHECK(ime_entry_install(&b) == 0);
    CHECK(entry_install_calls == 1 && g_ime_entry_app == &a);
    CHECK(native_ime_entry_request(env, NULL, (jlong)a.ime_entry_token) == JNI_TRUE);
    CHECK(a.text_active && a.text_session != 0 && show_calls == 1);
    CHECK(native_ime_entry_request(env, NULL, (jlong)a.ime_entry_token + 1) == JNI_FALSE);
    CHECK(native_ime_entry_request(env, NULL, 0) == JNI_FALSE);
    text_end(&a, "entry_test_end");
    uint64_t token = a.ime_entry_token;
    entry_uninstall_result = 0;
    ime_entry_uninstall(&a);
    CHECK(!a.ime_entry_installed && a.ime_entry_token == token);
    CHECK(a.ime_entry_class == &entry_class);
    CHECK(entry_uninstall_calls == 1);
    CHECK(native_ime_entry_request(env, NULL, (jlong)token) == JNI_FALSE);
    entry_uninstall_result = 1;
    ime_entry_uninstall(&a);
    CHECK(!a.ime_entry_installed && a.ime_entry_token == 0 && a.ime_entry_class == NULL);
    CHECK(g_ime_entry_app == NULL && entry_uninstall_calls == 2);
    CHECK(entry_uninstall_token == (jlong)token);
    CHECK(native_ime_entry_request(env, NULL, (jlong)token) == JNI_FALSE);
    return 0;
}

static int test_entry_exception_cleanup(JNIEnv *env) {
    ANativeActivity act = {env, &activity_obj};
    dm_app_lifecycle life = {DM_APP_STATE_OWNED_DIAGNOSTIC, NULL};
    ANativeWindow window = {1, 1};
    app_state a; memset(&a, 0, sizeof a);
    a.activity = &act; a.lifecycle = &life; a.window = &window;
    a.resumed = a.focused = a.touch_allowed = 1; life.surface = &window;
    root_fail_class = 1;
    CHECK(ime_entry_install(&a) == 0);
    CHECK(!pending && !a.ime_entry_installed && a.ime_entry_class == NULL);
    CHECK(g_ime_entry_app == NULL);
    root_fail_class = 0;
    CHECK(ime_entry_install(&a) == 1);
    return 0;
}

static int test_entry_install_false_retained_rollback(JNIEnv *env) {
    ANativeActivity act = {env, &activity_obj, NULL};
    dm_app_lifecycle life = {DM_APP_STATE_OWNED_DIAGNOSTIC, NULL};
    ANativeWindow window = {1, 1};
    app_state *a = calloc(1, sizeof *a);
    CHECK(a != NULL);
    a->activity = &act; a->lifecycle = &life; a->window = &window;
    a->resumed = a->focused = a->touch_allowed = 1;
    a->timer_fd = a->frame_fd = -1;
    life.surface = &window;
    act.instance = a;
    entry_install_result = 0;
    entry_uninstall_result = 0;
    CHECK(ime_entry_install(a) == 0);
    CHECK(a->ime_entry_class == &entry_class && a->ime_entry_token != 0);
    CHECK(!a->ime_entry_installed && entry_uninstall_calls == 1);
    CHECK(native_ime_entry_request(env, NULL, (jlong)a->ime_entry_token) == JNI_FALSE);
    entry_install_result = 1;
    entry_uninstall_result = 1;
    CHECK(ime_entry_install(a) == 1);
    CHECK(a->ime_entry_installed && entry_uninstall_calls == 2);
    ime_entry_uninstall(a);
    CHECK(a->ime_entry_class == NULL && a->ime_entry_token == 0);
    free(a);
    return 0;
}

static int test_destroy_cleanup_retry(JNIEnv *env) {
    ANativeActivity act = {env, &activity_obj, NULL};
    dm_app_lifecycle life = {DM_APP_STATE_OWNED_DIAGNOSTIC, NULL};
    ANativeWindow window = {1, 1};
    app_state *a = calloc(1, sizeof *a);
    CHECK(a != NULL);
    a->activity = &act; a->lifecycle = &life; a->window = &window;
    a->resumed = a->focused = a->touch_allowed = 1;
    a->timer_fd = 41; a->frame_fd = 42;
    life.surface = &window;
    act.instance = a;
    CHECK(ime_entry_install(a) == 1);
    entry_uninstall_result = 0;
    int pauses_before = audio_pause_calls;
    destroy(&act);
    CHECK(act.instance == a);
    CHECK(a->timer_fd == -1 && a->frame_fd == -1);
    CHECK(!a->resumed && !a->focused && !a->touch_allowed);
    CHECK(audio_pause_calls == pauses_before + 1);
    CHECK(a->ime_entry_class == &entry_class && a->ime_entry_token != 0);
    CHECK(!a->ime_entry_installed && entry_uninstall_calls == 1);
    CHECK(native_ime_entry_request(env, NULL, (jlong)a->ime_entry_token) == JNI_FALSE);
    destroy(&act);
    CHECK(act.instance == a);
    CHECK(a->ime_entry_class == &entry_class && entry_uninstall_calls == 2);
    entry_uninstall_result = 1;
    destroy(&act);
    CHECK(act.instance == NULL);
    CHECK(entry_uninstall_calls == 3);
    return 0;
}

int main(void) {
    JNIEnv env;
    reset_fake(&env); if (test_success_and_events(&env)) return 1;
    reset_fake(&env); if (test_show_failure_and_exception(&env)) return 1;
    reset_fake(&env); if (test_global_ref_rollback(&env)) return 1;
    reset_fake(&env); if (test_owner_busy_and_inactive(&env)) return 1;
    reset_fake(&env); if (test_reentrant_terminal_during_show(&env)) return 1;
    reset_fake(&env); if (test_entry_owner_request(&env)) return 1;
    reset_fake(&env); if (test_entry_exception_cleanup(&env)) return 1;
    reset_fake(&env); if (test_entry_install_false_retained_rollback(&env)) return 1;
    reset_fake(&env); if (test_destroy_cleanup_retry(&env)) return 1;
    puts("PASS text JNI start/event/end + entry cleanup");
    return 0;
}
'''


def build(text: str) -> str:
    return PRELUDE + "\n".join(extract_function(text, name) for name in FUNCTIONS)


def run(cc: str, source: str, work: Path, label: str) -> int:
    c = work / f"{label}.c"
    exe = work / label
    c.write_text(source)
    result = subprocess.run([cc, "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                    "-Wno-unused-parameter", "-Wno-missing-field-initializers",
                    "-o", str(exe), str(c)], check=False, capture_output=True, text=True)
    if result.returncode:
        print(result.stderr, file=sys.stderr)
        return result.returncode
    return subprocess.run([str(exe)], check=False).returncode


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--native", default="dm/android/app/native.c")
    ap.add_argument("--cc", default="cc")
    ap.add_argument("--mutants", action="store_true")
    args = ap.parse_args()
    text = Path(args.native).read_text()
    text = text.replace("static int text_begin(app_state*s);\n", "")
    text = text.replace("static void text_end(app_state*s,const char*reason);\n", "")
    text = text.replace("static int ime_entry_install(app_state*s);\n", "")
    text = text.replace("static void ime_entry_uninstall(app_state*s);\n", "")
    text = text.replace("static jboolean native_ime_entry_request(JNIEnv*env,jclass cls,jlong token);\n", "")
    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        if run(args.cc, build(text), work, "text-jni") != 0:
            print("FAIL: actual text JNI start/event/end", file=sys.stderr)
            return 1
        if args.mutants:
            mutants = [
                ("bypass-ready-gate",
                 "if(!s->resumed||!s->focused||!s->window||!s->touch_allowed)return 0;",
                 "if(0)return 0;"),
                ("wrong-loader-receiver",
                 "CallObjectMethod(env, s->activity->clazz, get_loader)",
                 "CallObjectMethod(env, loader, get_loader)"),
                ("omit-payload-copy",
                 "if(units>0)memcpy(s->text_payload,payload,(size_t)units*sizeof(jchar));",
                 "if(units>0)memset(s->text_payload,0,(size_t)units*sizeof(jchar));"),
                ("omit-event-tail-clear",
                 "memset(s->text_payload,0,sizeof(s->text_payload));\n    s->text_payload_event=event;",
                 "s->text_payload_event=event;"),
                ("omit-clear-buffer",
                 "s->text_payload_units=0;\n    memset(s->text_payload,0,sizeof(s->text_payload));",
                 "s->text_payload_units=0;"),
            ]
            for name, old, new in mutants:
                if old not in text:
                    print(f"fail closed: mutant anchor missing: {name}", file=sys.stderr)
                    return 1
                if run(args.cc, build(text.replace(old, new, 1)), work, f"mutant-{name}") == 0:
                    print(f"FAIL: mutant survived: {name}", file=sys.stderr)
                    return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
