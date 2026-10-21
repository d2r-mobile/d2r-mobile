#!/usr/bin/env python3
"""Narrow actual-JNI start/stop fixture for #183.

Extracts the production controller_hotplug_start/stop/native callback and
compiles them against a small JNI function table. The fake table deliberately
has two classloaders: a boot-like framework loader (Class.getClassLoader
receiver) and an app loader (Activity.getClassLoader receiver). The old
receiver must fail behaviorally; the production receiver must succeed.
"""
from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

from native_callbacks_host import extract_function

FUNCTIONS = ["jni_stage_failed", "controller_hotplug_acquire_owner",
             "controller_hotplug_release_owner", "controller_hotplug_dispatch",
             "native_controller_event", "controller_hotplug_start",
             "controller_hotplug_stop"]

PRELUDE = r'''
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef void *jobject;
typedef void *jclass;
typedef void *jmethodID;
typedef void *jstring;
typedef unsigned char jboolean;
typedef int jint;
typedef long long jlong;
typedef struct JNINativeInterface JNINativeInterface;
typedef const JNINativeInterface *JNIEnv;
typedef struct { const char *name, *signature; void *fnPtr; } JNINativeMethod;

struct JNINativeInterface {
    jobject (*GetObjectClass)(JNIEnv *, jobject);
    jmethodID (*GetMethodID)(JNIEnv *, jclass, const char *, const char *);
    jobject (*CallObjectMethod)(JNIEnv *, jobject, jmethodID, ...);
    jstring (*NewStringUTF)(JNIEnv *, const char *);
    jint (*RegisterNatives)(JNIEnv *, jclass, const JNINativeMethod *, jint);
    jobject (*NewObject)(JNIEnv *, jclass, jmethodID, ...);
    jboolean (*CallBooleanMethod)(JNIEnv *, jobject, jmethodID, ...);
    jobject (*NewGlobalRef)(JNIEnv *, jobject);
    void (*DeleteGlobalRef)(JNIEnv *, jobject);
    void (*DeleteLocalRef)(JNIEnv *, jobject);
    jboolean (*ExceptionCheck)(JNIEnv *);
    void (*ExceptionDescribe)(JNIEnv *);
    void (*ExceptionClear)(JNIEnv *);
};

typedef struct { int id; } FakeObject;
static FakeObject activity_obj = {1}, activity2_obj = {2}, activity_class = {3},
    class_class = {4}, boot_loader = {5}, app_loader = {6}, loader_class = {7},
    boundary_class = {8}, boundary_obj = {9}, string_obj = {10};
static int mid_activity_loader, mid_class_loader, mid_loadclass,
    mid_constructor, mid_register, mid_release;
#define MID_ACTIVITY_LOADER ((jmethodID)&mid_activity_loader)
#define MID_CLASS_LOADER ((jmethodID)&mid_class_loader)
#define MID_LOADCLASS ((jmethodID)&mid_loadclass)
#define MID_CONSTRUCTOR ((jmethodID)&mid_constructor)
#define MID_REGISTER ((jmethodID)&mid_register)
#define MID_RELEASE ((jmethodID)&mid_release)

static int pending, exception_describes, global_ref_calls, globals_deleted;
static int native_registers, boundary_registers, releases, release_result = 1;
static int fail_load, fail_register, fail_activity_loader, fail_global_at;
static int register_args_ok, loader_receiver_calls, callback_rearm_calls,
    callback_touch_calls;
static const char *callback_touch_reason;
static void *loader_receiver, *loaded_from;

static void set_exception(void) { pending = 1; }
static jobject fake_class_loader(JNIEnv *env, jobject receiver, jmethodID mid, va_list ap) {
    (void)env; (void)ap;
    if (mid == MID_ACTIVITY_LOADER) {
        loader_receiver_calls++;
        loader_receiver = receiver;
        if (receiver != &activity_obj && receiver != &activity2_obj) {
            set_exception();
            return NULL;
        }
        if (fail_activity_loader) { set_exception(); return NULL; }
        return &app_loader;
    }
    if (mid == MID_CLASS_LOADER) {
        // Old code called Class.getClassLoader; this is the boot-like loader.
        return &boot_loader;
    }
    set_exception();
    return NULL;
}
static jobject fake_load_class(JNIEnv *env, jobject receiver, jmethodID mid, va_list ap) {
    (void)env; (void)mid;
    (void)va_arg(ap, jstring);
    loaded_from = receiver;
    if (fail_load || receiver != &app_loader) { set_exception(); return NULL; }
    return &boundary_class;
}
static jobject fake_call_object(JNIEnv *env, jobject receiver, jmethodID mid, ...) {
    va_list ap; va_start(ap, mid);
    jobject out;
    if (mid == MID_ACTIVITY_LOADER || mid == MID_CLASS_LOADER)
        out = fake_class_loader(env, receiver, mid, ap);
    else if (mid == MID_LOADCLASS)
        out = fake_load_class(env, receiver, mid, ap);
    else { set_exception(); out = NULL; }
    va_end(ap);
    return out;
}
static jobject fake_get_object_class(JNIEnv *env, jobject obj) {
    (void)env;
    if (obj == &activity_obj || obj == &activity2_obj) return &activity_class;
    if (obj == &activity_class) return &class_class; // old Class receiver path
    if (obj == &app_loader || obj == &boot_loader) return &loader_class;
    set_exception();
    return NULL;
}
static jmethodID fake_get_method_id(JNIEnv *env, jclass cls, const char *name, const char *sig) {
    (void)env; (void)sig;
    if (cls == &activity_class && strcmp(name, "getClassLoader") == 0) return MID_ACTIVITY_LOADER;
    if (cls == &class_class && strcmp(name, "getClassLoader") == 0) return MID_CLASS_LOADER;
    if (cls == &loader_class && strcmp(name, "loadClass") == 0) return MID_LOADCLASS;
    if (cls == &boundary_class) {
        if (strcmp(name, "<init>") == 0) return MID_CONSTRUCTOR;
        if (strcmp(name, "register") == 0) return MID_REGISTER;
        if (strcmp(name, "release") == 0) return MID_RELEASE;
    }
    set_exception();
    return NULL;
}
static jstring fake_new_string(JNIEnv *env, const char *s) {
    (void)env; (void)s; return &string_obj;
}
static jint fake_register_natives(JNIEnv *env, jclass cls, const JNINativeMethod *m, jint n) {
    (void)env; (void)cls; (void)m; (void)n;
    native_registers++;
    if (fail_register) { set_exception(); return -1; }
    return 0;
}
static jobject fake_new_object(JNIEnv *env, jclass cls, jmethodID mid, ...) {
    (void)env; (void)mid;
    if (cls != &boundary_class) { set_exception(); return NULL; }
    va_list ap; va_start(ap, mid);
    jobject activity = va_arg(ap, jobject);
    jlong session = va_arg(ap, jlong);
    va_end(ap);
    register_args_ok = activity == &activity_obj || activity == &activity2_obj;
    (void)session;
    return &boundary_obj;
}
static jboolean fake_call_boolean(JNIEnv *env, jobject obj, jmethodID mid, ...) {
    (void)env;
    if (obj != &boundary_obj) { set_exception(); return 0; }
    if (mid == MID_REGISTER) { boundary_registers++; return 1; }
    if (mid == MID_RELEASE) { releases++; return (jboolean)release_result; }
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
static const JNINativeInterface fake_functions = {
    fake_get_object_class, fake_get_method_id, fake_call_object, fake_new_string,
    fake_register_natives, fake_new_object, fake_call_boolean, fake_new_global,
    fake_delete_global, fake_delete_local, fake_exception_check,
    fake_exception_describe, fake_exception_clear
};

typedef struct ANativeActivity { JNIEnv *env; jobject clazz; } ANativeActivity;
enum { dm_controller_added = 0, dm_controller_removed = 1, dm_controller_changed = 2 };
typedef struct {
    ANativeActivity *activity;
    uint64_t controller_session;
    jobject controller_boundary;
    jclass controller_class;
    jmethodID controller_release;
    int gamepad_admitted;
    uint32_t gamepad_id;
} app_state;
static app_state *g_controller_app;
static uint64_t g_controller_next_session;
static void gamepad_rearm(app_state *s) { (void)s; callback_rearm_calls++; }
static void touch_consume(app_state *s, const char *reason) {
    (void)s; callback_touch_calls++; callback_touch_reason = reason;
}
static void native_controller_event(JNIEnv *, jclass, jlong, jint, jint);
static int controller_hotplug_start(app_state *);
static void controller_hotplug_stop(app_state *);
#define LOG(...) do { } while (0)
#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

static void reset_fake(JNIEnv *env) {
    pending = exception_describes = global_ref_calls = globals_deleted = 0;
    native_registers = boundary_registers = releases = 0; release_result = 1;
    fail_load = fail_register = fail_activity_loader = fail_global_at = 0;
    register_args_ok = loader_receiver_calls = callback_rearm_calls = callback_touch_calls = 0;
    callback_touch_reason = NULL; loader_receiver = loaded_from = NULL;
    g_controller_app = NULL; g_controller_next_session = 0;
    *env = &fake_functions;
}

static int test_success(JNIEnv *env) {
    ANativeActivity act = {env, &activity_obj};
    ANativeActivity act2 = {env, &activity2_obj};
    app_state a; memset(&a, 0, sizeof a); a.activity = &act;
    CHECK(controller_hotplug_start(&a) == 1);
    CHECK(loader_receiver == &activity_obj);
    CHECK(a.controller_session != 0 && g_controller_app == &a);
    CHECK(a.controller_boundary == &boundary_obj && a.controller_class == &boundary_class);
    CHECK(boundary_registers == 1 && native_registers == 1 && register_args_ok);
    a.gamepad_admitted = 1; a.gamepad_id = 42;
    native_controller_event(env, NULL, (jlong)a.controller_session, dm_controller_removed, 42);
    CHECK(callback_rearm_calls == 1 && callback_touch_calls == 1);
    CHECK(callback_touch_reason && strcmp(callback_touch_reason, "controller_removed") == 0);
    native_controller_event(env, NULL, (jlong)a.controller_session, dm_controller_changed, 42);
    CHECK(callback_rearm_calls == 2 && callback_touch_calls == 2);
    native_controller_event(env, NULL, (jlong)a.controller_session, dm_controller_removed, 43);
    CHECK(callback_rearm_calls == 2);
    app_state b; memset(&b, 0, sizeof b); b.activity = &act2;
    CHECK(controller_hotplug_start(&b) == 0);
    CHECK(b.controller_session == 0 && b.controller_boundary == NULL);
    CHECK(g_controller_app == &a && boundary_registers == 1);
    native_controller_event(env, NULL, (jlong)a.controller_session, dm_controller_removed, 42);
    CHECK(callback_rearm_calls == 3);
    controller_hotplug_stop(&a);
    CHECK(releases == 1 && a.controller_boundary == NULL && a.controller_class == NULL);
    CHECK(g_controller_app == NULL && globals_deleted == 2);
    native_controller_event(env, NULL, (jlong)1, dm_controller_removed, 42);
    CHECK(callback_rearm_calls == 3);
    CHECK(controller_hotplug_start(&b) == 1);
    CHECK(g_controller_app == &b && boundary_registers == 2);
    controller_hotplug_stop(&b);
    CHECK(g_controller_app == NULL);
    return 0;
}

static int test_global_ref_failure(JNIEnv *env) {
    ANativeActivity act = {env, &activity_obj};
    app_state a; memset(&a, 0, sizeof a); a.activity = &act;
    fail_global_at = 1;
    CHECK(controller_hotplug_start(&a) == 0);
    CHECK(global_ref_calls == 1);
    CHECK(!pending && g_controller_app == NULL && a.controller_session == 0);
    CHECK(a.controller_boundary == NULL && a.controller_class == NULL);
    return 0;
}

static int test_second_global_ref_failure(JNIEnv *env) {
    ANativeActivity act = {env, &activity_obj};
    app_state a; memset(&a, 0, sizeof a); a.activity = &act;
    fail_global_at = 2;
    CHECK(controller_hotplug_start(&a) == 0);
    CHECK(global_ref_calls == 2 && globals_deleted == 1);
    CHECK(!pending && g_controller_app == NULL && a.controller_boundary == NULL);
    return 0;
}

static int test_register_failure(JNIEnv *env) {
    ANativeActivity act = {env, &activity_obj};
    app_state a; memset(&a, 0, sizeof a); a.activity = &act;
    fail_register = 1;
    CHECK(controller_hotplug_start(&a) == 0);
    CHECK(boundary_registers == 0 && !pending && g_controller_app == NULL);
    CHECK(a.controller_session == 0 && a.controller_boundary == NULL);
    return 0;
}

static int test_load_failure(JNIEnv *env) {
    ANativeActivity act = {env, &activity_obj};
    app_state a; memset(&a, 0, sizeof a); a.activity = &act;
    fail_load = 1;
    CHECK(controller_hotplug_start(&a) == 0);
    CHECK(!pending && g_controller_app == NULL && a.controller_session == 0);
    CHECK(a.controller_boundary == NULL && a.controller_class == NULL);
    return 0;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    JNIEnv env;
    reset_fake(&env);
    if (test_success(&env)) return 1;
    reset_fake(&env);
    if (test_global_ref_failure(&env)) return 1;
    reset_fake(&env);
    if (test_second_global_ref_failure(&env)) return 1;
    reset_fake(&env);
    if (test_register_failure(&env)) return 1;
    reset_fake(&env);
    if (test_load_failure(&env)) return 1;
    puts("PASS controller hotplug JNI start/stop");
    return 0;
}
'''


def build(text: str) -> str:
    return PRELUDE + "\n".join(extract_function(text, name) for name in FUNCTIONS)


def run(cc: str, source: str, work: Path, label: str) -> int:
    c = work / f"{label}.c"
    exe = work / label
    c.write_text(source)
    subprocess.run([cc, "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                    "-Wno-unused-parameter", "-o", str(exe), str(c)], check=True)
    return subprocess.run([str(exe)], check=False).returncode


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--native", default="dm/android/app/native.c")
    ap.add_argument("--cc", default="cc")
    ap.add_argument("--mutants", action="store_true")
    args = ap.parse_args()
    text = Path(args.native).read_text()
    text = text.replace("static void controller_hotplug_stop(app_state*s);\n", "")
    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        if run(args.cc, build(text), work, "controller-hotplug-jni") != 0:
            print("FAIL: positive JNI start/stop", file=sys.stderr)
            return 1
        if args.mutants:
            old_receiver = "CallObjectMethod(env,s->activity->clazz,get_loader)"
            mutants = [
                ("class-loader-receiver", old_receiver,
                 "CallObjectMethod(env,activity_class,get_loader)"),
                ("consecutive-globalref",
                 's->controller_boundary=(*env)->NewGlobalRef(env,boundary);\n    if(!s->controller_boundary){\n        jni_stage_failed(env,"boundary global ref");\n        goto fail;\n    }\n    s->controller_class=(jclass)(*env)->NewGlobalRef(env,boundary_class);\n    if(!s->controller_class){\n        jni_stage_failed(env,"class global ref");\n        goto fail;\n    }',
                 's->controller_boundary=(*env)->NewGlobalRef(env,boundary);\n    s->controller_class=(jclass)(*env)->NewGlobalRef(env,boundary_class);\n    if(!s->controller_boundary||!s->controller_class)goto fail;'),
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
