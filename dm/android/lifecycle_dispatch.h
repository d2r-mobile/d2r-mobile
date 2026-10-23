// 生命周期最新状态派发：UI不等GPU，单一宿主pthread按顺序执行挂起/恢复。
// Latest-state lifecycle dispatch: the UI never waits for GPU; one host pthread serializes suspend/resume.
#pragma once
#include <pthread.h>
#include <stdint.h>
#include <string.h>

typedef struct dm_lifecycle_ops {
    void (*focus)(int);
    void (*audio)(int);
    void (*graphics)(int);
} dm_lifecycle_ops;

typedef struct dm_lifecycle_dispatch {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    pthread_t thread;
    dm_lifecycle_ops ops;
    uint64_t requested, completed;
    int background, focus, stopping, started;
} dm_lifecycle_dispatch;

static void *dm_lifecycle_run(void *arg) {
    dm_lifecycle_dispatch *d = arg;
    int applied_background = 0;
    for (;;) {
        pthread_mutex_lock(&d->mutex);
        while (!d->stopping && d->requested == d->completed) pthread_cond_wait(&d->changed, &d->mutex);
        if (d->stopping) { pthread_mutex_unlock(&d->mutex); return NULL; }
        uint64_t request = d->requested;
        int background = d->background, focus = d->focus;
        pthread_mutex_unlock(&d->mutex);

        // GPU挂起可等Surface；这个锁必须已经释放，UI才能继续提供新Surface和前台状态。
        // Graphics suspend can wait for a Surface; release this lock so UI can supply it and post foreground state.
        if (background != applied_background) {
            if (background) { d->ops.focus(3); d->ops.audio(1); d->ops.graphics(1); }
            else { d->ops.graphics(0); d->ops.audio(0); }
            applied_background = background;
        }
        pthread_mutex_lock(&d->mutex);
        int current = request == d->requested;
        pthread_mutex_unlock(&d->mutex);
        // 挂起期间已恢复时，不再发布过期焦点；下一轮先解开图形门，再恢复音频和最新焦点。
        // If state changed while suspending, skip stale focus; the next turn opens graphics, then audio and latest focus.
        if (current) d->ops.focus(focus);
        pthread_mutex_lock(&d->mutex);
        d->completed = request;
        pthread_cond_broadcast(&d->changed);
        pthread_mutex_unlock(&d->mutex);
    }
}

static int dm_lifecycle_start(dm_lifecycle_dispatch *d, dm_lifecycle_ops ops) {
    memset(d, 0, sizeof *d);
    d->ops = ops; d->focus = 1;
    int rc = pthread_mutex_init(&d->mutex, NULL);
    if (rc) return rc;
    rc = pthread_cond_init(&d->changed, NULL);
    if (rc) { pthread_mutex_destroy(&d->mutex); return rc; }
    rc = pthread_create(&d->thread, NULL, dm_lifecycle_run, d);
    if (rc) { pthread_cond_destroy(&d->changed); pthread_mutex_destroy(&d->mutex); return rc; }
    d->started = 1;
    return 0;
}

static void dm_lifecycle_post(dm_lifecycle_dispatch *d, int event) {
    if (event < 0 || event > 3) return;
    pthread_mutex_lock(&d->mutex);
    if (event == 1) d->background = 1;
    else if (event == 2 || event == 3) d->background = 0;
    d->focus = event == 1 ? 3 : event == 3 ? 1 : 2;
    ++d->requested;
    pthread_cond_signal(&d->changed);
    pthread_mutex_unlock(&d->mutex);
}

// 只供显式宿主关闭/CPU测试；UI生命周期回调绝不join或等待它。
// For explicit host shutdown / CPU tests only; UI lifecycle callbacks never join or wait here.
static void dm_lifecycle_stop(dm_lifecycle_dispatch *d) {
    if (!d->started) return;
    pthread_mutex_lock(&d->mutex); d->stopping = 1; pthread_cond_signal(&d->changed); pthread_mutex_unlock(&d->mutex);
    pthread_join(d->thread, NULL);
    pthread_cond_destroy(&d->changed); pthread_mutex_destroy(&d->mutex); d->started = 0;
}
