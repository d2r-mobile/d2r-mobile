// NativeActivity：窗口生命周期、多点触控与原有纯 C 触屏状态机。
// NativeActivity window lifecycle and multitouch wired to the existing pure C touch state machine.
#include "runtime.h"
#include "vulkan_front.h"
#include "../../../dm/ios/Sources/dm_touch_controls.h"
#include <android/native_activity.h>
#include <android/window.h>
#include <android/input.h>
#include <android/looper.h>
#include <android/log.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "strings_generated.h"

typedef struct {
    ANativeActivity *activity;
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t changed;
    ANativeWindow *window;
    AInputQueue *input;
    int stop, paused, runtime_ok;
    dm_tc_state touch;
} app_state;
extern int dm_android_probe(const char *, const char *, char *, size_t);
static void log_line(const char *s) { __android_log_print(ANDROID_LOG_INFO, "D2RAndroid", "%s", s); }
static double seconds(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }

static void copy_asset(ANativeActivity *activity, const char *name) {
    AAsset *asset = AAssetManager_open(activity->assetManager, name, AASSET_MODE_STREAMING);
    if (!asset) return;
    char path[1024]; snprintf(path, sizeof path, "%s/%s", activity->internalDataPath, name);
    FILE *out = fopen(path, "wb");
    if (out) { char buffer[4096]; int n; while ((n = AAsset_read(asset, buffer, sizeof buffer)) > 0) fwrite(buffer, 1, n, out); fclose(out); }
    AAsset_close(asset);
}

static void *render(void *arg) {
    app_state *s = arg;
    char report[2048];
    s->runtime_ok = dm_android_runtime_selftest(report, sizeof report) == 0; log_line(report);
    copy_asset(s->activity, "probe.fixups.tsv"); copy_asset(s->activity, "probe.gates.tsv");
    // 当前库路径由 dladdr 取得，支持包管理器实际安装目录。
    // Resolve the installed library directory through dladdr.
    extern int dm_android_app_probe(ANativeActivity *, char *, size_t);
    int probe = dm_android_app_probe(s->activity, report, sizeof report); log_line(report);
    s->runtime_ok &= probe == 0;
    extern int dm_android_posix_selftest(const char *);
    int posix = dm_android_posix_selftest(s->activity->internalDataPath);
    s->runtime_ok &= posix == 0;
    log_line(posix == 0 ? "Android save transactions and safe memory reads: PASS" : "Android POSIX host: FAIL");
    const char *modules[] = {"main.fixups.tsv", "sdk.fixups.tsv", "subsdk0.fixups.tsv"};
    for (unsigned i = 0; i < 3; ++i) copy_asset(s->activity, modules[i]);
    extern int dm_android_app_game(ANativeActivity *, char *, size_t);
    if (!access(s->activity->internalDataPath, F_OK)) {
        int game = dm_android_app_game(s->activity, report, sizeof report);
        (void)game; log_line(report);
    }
    dm_vk_front *v = NULL; ANativeWindow *current = NULL;
    pthread_mutex_lock(&s->lock);
    while (!s->stop) {
        if (s->window != current) {
            dm_vk_front_destroy(v); v = NULL;
            if (current) ANativeWindow_release(current);
            current = s->window;
            if (current) { ANativeWindow_acquire(current); v = dm_vk_front_create(current); }
        }
        if (!current || s->paused) { pthread_cond_wait(&s->changed, &s->lock); continue; }
        if (!v) v = dm_vk_front_create(current);
        dm_tc_tick(&s->touch, seconds());
        if (v && dm_vk_front_draw(v, s->runtime_ok ? dmAndroidRuntimePass : dmAndroidRuntimeFail, s->runtime_ok,
                                 s->touch.lx, s->touch.ly, s->touch.buttons)) {
            dm_vk_front_destroy(v); v = NULL;
        }
        pthread_mutex_unlock(&s->lock);
        usleep(16000);
        pthread_mutex_lock(&s->lock);
    }
    dm_vk_front_destroy(v);
    if (current) ANativeWindow_release(current);
    pthread_mutex_unlock(&s->lock);
    return NULL;
}

static int input_ready(int fd, int events, void *data) {
    (void)fd; (void)events;
    app_state *s = data; AInputEvent *event;
    while (AInputQueue_getEvent(s->input, &event) >= 0) {
        if (AInputQueue_preDispatchEvent(s->input, event)) continue;
        int handled = 0;
        if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
            pthread_mutex_lock(&s->lock);
            int action = AMotionEvent_getAction(event), masked = action & AMOTION_EVENT_ACTION_MASK;
            unsigned index = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
            size_t count = AMotionEvent_getPointerCount(event);
            double now = seconds();
            for (size_t i = 0; i < count; ++i) {
                int id = AMotionEvent_getPointerId(event, i);
                if (id < 0 || id >= DM_TC_MAX_TOUCH) continue;
                float x = AMotionEvent_getX(event, i), y = AMotionEvent_getY(event, i);
                if ((masked == AMOTION_EVENT_ACTION_DOWN || masked == AMOTION_EVENT_ACTION_POINTER_DOWN) && i == index) dm_tc_begin_at(&s->touch, id, x, y, now);
                else if (masked == AMOTION_EVENT_ACTION_MOVE) dm_tc_move_at(&s->touch, id, x, y, now);
                else if ((masked == AMOTION_EVENT_ACTION_UP || masked == AMOTION_EVENT_ACTION_POINTER_UP) && i == index) dm_tc_end_at(&s->touch, id, now);
            }
            if (masked == AMOTION_EVENT_ACTION_CANCEL) dm_tc_cancel_all(&s->touch);
            __android_log_print(ANDROID_LOG_INFO, "D2RAndroid", "Touch action=%d pointers=%zu axis=%d,%d buttons=%llu", masked, count, s->touch.lx, s->touch.ly, (unsigned long long)s->touch.buttons);
            pthread_mutex_unlock(&s->lock); handled = 1;
        }
        AInputQueue_finishEvent(s->input, event, handled);
    }
    return 1;
}

static void window_created(ANativeActivity *a, ANativeWindow *w) {
    app_state *s = a->instance; pthread_mutex_lock(&s->lock);
    s->window = w;
    dm_tc_resize(&s->touch, ANativeWindow_getWidth(w), ANativeWindow_getHeight(w));
    pthread_cond_signal(&s->changed); pthread_mutex_unlock(&s->lock);
}
static void window_destroyed(ANativeActivity *a, ANativeWindow *w) {
    app_state *s = a->instance; pthread_mutex_lock(&s->lock);
    if (s->window == w) s->window = NULL;
    dm_tc_cancel_all(&s->touch); pthread_cond_signal(&s->changed); pthread_mutex_unlock(&s->lock);
}
static void input_created(ANativeActivity *a, AInputQueue *q) {
    app_state *s = a->instance; s->input = q;
    AInputQueue_attachLooper(q, ALooper_forThread(), 0, input_ready, s);
}
static void input_destroyed(ANativeActivity *a, AInputQueue *q) {
    app_state *s = a->instance; AInputQueue_detachLooper(q); s->input = NULL;
    pthread_mutex_lock(&s->lock); dm_tc_cancel_all(&s->touch); pthread_mutex_unlock(&s->lock);
}
static void paused(ANativeActivity *a) {
    app_state *s = a->instance; pthread_mutex_lock(&s->lock); s->paused = 1;
    dm_tc_cancel_all(&s->touch); pthread_mutex_unlock(&s->lock); log_line("Activity paused");
}
static void resumed(ANativeActivity *a) {
    app_state *s = a->instance; pthread_mutex_lock(&s->lock); s->paused = 0;
    pthread_cond_signal(&s->changed); pthread_mutex_unlock(&s->lock); log_line("Activity resumed");
}
static void destroyed(ANativeActivity *a) {
    app_state *s = a->instance; pthread_mutex_lock(&s->lock); s->stop = 1;
    pthread_cond_signal(&s->changed); pthread_mutex_unlock(&s->lock); pthread_join(s->thread, NULL);
    pthread_cond_destroy(&s->changed); pthread_mutex_destroy(&s->lock); free(s);
}

__attribute__((visibility("default"))) void ANativeActivity_onCreate(ANativeActivity *a, void *saved, size_t size) {
    (void)saved; (void)size;
    app_state *s = calloc(1, sizeof *s);
    if (!s) { ANativeActivity_finish(a); return; }
    s->activity = a; a->instance = s;
    pthread_mutex_init(&s->lock, NULL); pthread_cond_init(&s->changed, NULL);
    dm_tc_set_tall_scale(1);
    dm_tc_init(&s->touch, 1280, 720);
    dm_tc_set_navigation_mode(&s->touch, 0);
    a->callbacks->onNativeWindowCreated = window_created;
    a->callbacks->onNativeWindowDestroyed = window_destroyed;
    a->callbacks->onInputQueueCreated = input_created;
    a->callbacks->onInputQueueDestroyed = input_destroyed;
    a->callbacks->onPause = paused; a->callbacks->onResume = resumed; a->callbacks->onDestroy = destroyed;
    ANativeActivity_setWindowFlags(a, AWINDOW_FLAG_FULLSCREEN | AWINDOW_FLAG_KEEP_SCREEN_ON, 0);
    if (pthread_create(&s->thread, NULL, render, s)) ANativeActivity_finish(a);
}
