#include "lifecycle.h"

#include <stdlib.h>

struct dm_app_lifecycle {
    dm_app_host_ops host;
    dm_app_state_observer observer;
    void *observer_ctx;
    dm_app_state state;
    dm_app_state resume_state;
    void *surface;
    void *retired_surface;
    uint32_t pending_work;
    int host_stopped;
    int cleanup_done;
    int cleanup_failed;
    int stop_after_drain;
    int current_destroy_pending;
};

static void notify(dm_app_lifecycle *lifecycle, dm_app_state state) {
    if (lifecycle->state == state) return;
    lifecycle->state = state;
    if (lifecycle->observer) lifecycle->observer(lifecycle->observer_ctx, state);
}

static void release_surface(dm_app_lifecycle *lifecycle) {
    if (lifecycle->surface) {
        lifecycle->host.surface_release(lifecycle->host.ctx, lifecycle->surface);
        lifecycle->surface = NULL;
    }
    lifecycle->current_destroy_pending = 0;
}

static void release_retired_surface(dm_app_lifecycle *lifecycle) {
    if (!lifecycle->retired_surface) return;
    lifecycle->host.surface_release(lifecycle->host.ctx, lifecycle->retired_surface);
    lifecycle->retired_surface = NULL;
}

static void release_all_surfaces(dm_app_lifecycle *lifecycle) {
    release_surface(lifecycle);
    release_retired_surface(lifecycle);
}

static void settle_retired_after_drain(dm_app_lifecycle *lifecycle) {
    release_retired_surface(lifecycle);
    if (lifecycle->current_destroy_pending) release_surface(lifecycle);
}

static void finish_failure_cleanup(dm_app_lifecycle *lifecycle) {
    if (lifecycle->cleanup_done) return;
    release_all_surfaces(lifecycle);
    if (!lifecycle->host_stopped) {
        lifecycle->host_stopped = 1;
        if (lifecycle->host.stop(lifecycle->host.ctx)) lifecycle->cleanup_failed = 1;
    }
    lifecycle->cleanup_done = 1;
}

static void fail(dm_app_lifecycle *lifecycle) {
    notify(lifecycle, DM_APP_STATE_FAILED);
    if (lifecycle->pending_work == 0) finish_failure_cleanup(lifecycle);
}

static void finish_stop(dm_app_lifecycle *lifecycle) {
    release_all_surfaces(lifecycle);
    if (!lifecycle->host_stopped) {
        lifecycle->host_stopped = 1;
        if (lifecycle->host.stop(lifecycle->host.ctx)) {
            lifecycle->cleanup_done = 1;
            lifecycle->cleanup_failed = 1;
            lifecycle->pending_work = 0;
            notify(lifecycle, DM_APP_STATE_FAILED);
            return;
        }
    }
    lifecycle->cleanup_done = 1;
    lifecycle->pending_work = 0;
    notify(lifecycle, DM_APP_STATE_STOPPED);
}

dm_app_lifecycle *dm_app_lifecycle_create(const dm_app_host_ops *host_ops,
                                          dm_app_state_observer observer,
                                          void *observer_ctx) {
    if (!host_ops || !host_ops->load || !host_ops->stop) return NULL;
    if (!!host_ops->surface_acquire != !!host_ops->surface_release) return NULL;
    dm_app_lifecycle *lifecycle = calloc(1, sizeof(*lifecycle));
    if (!lifecycle) return NULL;
    lifecycle->host = *host_ops;
    lifecycle->observer = observer;
    lifecycle->observer_ctx = observer_ctx;
    lifecycle->state = DM_APP_STATE_CREATED;
    if (observer) observer(observer_ctx, DM_APP_STATE_CREATED);
    return lifecycle;
}

int dm_app_lifecycle_destroy(dm_app_lifecycle *lifecycle) {
    if (!lifecycle) return 0;
    if (lifecycle->pending_work != 0) return 0;  /* caller keeps the pointer */
    switch (lifecycle->state) {
    case DM_APP_STATE_CREATED:
    case DM_APP_STATE_STOPPED:
        free(lifecycle);
        return 1;
    case DM_APP_STATE_FAILED:
        finish_failure_cleanup(lifecycle);
        free(lifecycle);
        return 1;
    case DM_APP_STATE_STOPPING:
        finish_stop(lifecycle);
        free(lifecycle);
        return 1;
    default:
        return 0;  /* active controller must be stopped first */
    }
}

int dm_app_lifecycle_start(dm_app_lifecycle *lifecycle) {
    if (!lifecycle) return 0;
    if (lifecycle->state != DM_APP_STATE_CREATED &&
        lifecycle->state != DM_APP_STATE_STOPPED) {
        return 0;
    }
    lifecycle->host_stopped = 0;
    lifecycle->cleanup_done = 0;
    lifecycle->cleanup_failed = 0;
    lifecycle->stop_after_drain = 0;
    lifecycle->pending_work = 0;
    lifecycle->retired_surface = NULL;
    lifecycle->current_destroy_pending = 0;
    lifecycle->resume_state = DM_APP_STATE_OWNED_DIAGNOSTIC;
    notify(lifecycle, DM_APP_STATE_STARTING);
    notify(lifecycle, DM_APP_STATE_LOADING);
    if (lifecycle->host.load(lifecycle->host.ctx)) {
        fail(lifecycle);
        return 0;
    }
    notify(lifecycle, DM_APP_STATE_OWNED_DIAGNOSTIC);
    return 1;
}

int dm_app_lifecycle_stop(dm_app_lifecycle *lifecycle) {
    if (!lifecycle) return 0;
    switch (lifecycle->state) {
    case DM_APP_STATE_CREATED:
        notify(lifecycle, DM_APP_STATE_STOPPED);
        return 1;
    case DM_APP_STATE_STOPPED:
        return 1;
    case DM_APP_STATE_FAILED:
        lifecycle->stop_after_drain = 1;
        if (lifecycle->pending_work != 0) return 1;
        finish_failure_cleanup(lifecycle);
        if (!lifecycle->cleanup_failed) notify(lifecycle, DM_APP_STATE_STOPPED);
        return 1;
    case DM_APP_STATE_STOPPING:
        if (lifecycle->pending_work == 0) finish_stop(lifecycle);
        return 1;
    case DM_APP_STATE_STARTING:
    case DM_APP_STATE_LOADING:
    case DM_APP_STATE_OWNED_DIAGNOSTIC:
    case DM_APP_STATE_RUNNING:
    case DM_APP_STATE_PAUSED:
        notify(lifecycle, DM_APP_STATE_STOPPING);
        if (lifecycle->pending_work != 0) return 1;
        finish_stop(lifecycle);
        return 1;
    }
    return 0;
}

int dm_app_lifecycle_pause(dm_app_lifecycle *lifecycle) {
    if (!lifecycle) return 0;
    if (lifecycle->state == DM_APP_STATE_PAUSED) return 1;
    if (lifecycle->state != DM_APP_STATE_OWNED_DIAGNOSTIC &&
        lifecycle->state != DM_APP_STATE_RUNNING) {
        return 0;
    }
    if (lifecycle->host.pause && lifecycle->host.pause(lifecycle->host.ctx)) {
        fail(lifecycle);
        return 0;
    }
    lifecycle->resume_state = lifecycle->state;
    notify(lifecycle, DM_APP_STATE_PAUSED);
    return 1;
}

int dm_app_lifecycle_resume(dm_app_lifecycle *lifecycle) {
    if (!lifecycle) return 0;
    if (lifecycle->state == DM_APP_STATE_OWNED_DIAGNOSTIC ||
        lifecycle->state == DM_APP_STATE_RUNNING) {
        return 1;
    }
    if (lifecycle->state != DM_APP_STATE_PAUSED) return 0;
    if (lifecycle->host.resume && lifecycle->host.resume(lifecycle->host.ctx)) {
        fail(lifecycle);
        return 0;
    }
    notify(lifecycle, lifecycle->resume_state);
    return 1;
}

int dm_app_lifecycle_surface_created(dm_app_lifecycle *lifecycle, void *surface) {
    if (!lifecycle || !surface) return 0;
    if (lifecycle->state != DM_APP_STATE_OWNED_DIAGNOSTIC &&
        lifecycle->state != DM_APP_STATE_RUNNING &&
        lifecycle->state != DM_APP_STATE_PAUSED) {
        return 0;
    }
    if (lifecycle->surface == surface) {
        lifecycle->current_destroy_pending = 0;
        return 1;
    }
    /* One retired ownership slot is enough for the old Surface while work is
       outstanding. A second replacement is explicit backpressure, not a
       release of the only old reference. */
    if (lifecycle->pending_work != 0 && lifecycle->retired_surface) return 0;
    if (!lifecycle->host.surface_acquire ||
        lifecycle->host.surface_acquire(lifecycle->host.ctx, surface)) {
        fail(lifecycle);  /* keep the current Surface when pending */
        return 0;
    }
    if (lifecycle->pending_work != 0) {
        if (lifecycle->surface) {
            lifecycle->retired_surface = lifecycle->surface;
            lifecycle->current_destroy_pending = 0;
        }
        lifecycle->surface = surface;
    } else {
        release_surface(lifecycle);
        release_retired_surface(lifecycle);
        lifecycle->surface = surface;
    }
    return 1;
}

int dm_app_lifecycle_surface_destroyed(dm_app_lifecycle *lifecycle, void *surface) {
    if (!lifecycle || !surface) return 0;
    if (lifecycle->surface == surface) {
        if (lifecycle->pending_work != 0) {
            lifecycle->current_destroy_pending = 1;
            return 1;  /* retain the owned reference until final work_end */
        }
        release_surface(lifecycle);
        return 1;
    }
    if (lifecycle->retired_surface == surface) return 1; /* already owned until drain */
    return 0;  /* stale/non-matching window */
}

void *dm_app_lifecycle_surface(const dm_app_lifecycle *lifecycle) {
    return lifecycle ? lifecycle->surface : NULL;
}

int dm_app_lifecycle_work_begin(dm_app_lifecycle *lifecycle) {
    if (!lifecycle) return 0;
    if (lifecycle->pending_work == UINT32_MAX) return 0;
    switch (lifecycle->state) {
    case DM_APP_STATE_OWNED_DIAGNOSTIC:
    case DM_APP_STATE_RUNNING:
    case DM_APP_STATE_PAUSED:
        lifecycle->pending_work++;
        return 1;
    default:
        return 0;
    }
}

int dm_app_lifecycle_work_end(dm_app_lifecycle *lifecycle) {
    if (!lifecycle || lifecycle->pending_work == 0) return 0;
    lifecycle->pending_work--;
    if (lifecycle->pending_work == 0) {
        settle_retired_after_drain(lifecycle);
        if (lifecycle->state == DM_APP_STATE_STOPPING) {
            finish_stop(lifecycle);
        } else if (lifecycle->state == DM_APP_STATE_FAILED) {
            finish_failure_cleanup(lifecycle);
            if (lifecycle->stop_after_drain && !lifecycle->cleanup_failed)
                notify(lifecycle, DM_APP_STATE_STOPPED);
        }
    }
    return 1;
}

uint32_t dm_app_lifecycle_pending_work(const dm_app_lifecycle *lifecycle) {
    return lifecycle ? lifecycle->pending_work : 0;
}

dm_app_state dm_app_lifecycle_state(const dm_app_lifecycle *lifecycle) {
    return lifecycle ? lifecycle->state : DM_APP_STATE_FAILED;
}

const char *dm_app_lifecycle_state_name(dm_app_state state) {
    switch (state) {
    case DM_APP_STATE_CREATED: return "created";
    case DM_APP_STATE_STARTING: return "starting";
    case DM_APP_STATE_LOADING: return "loading";
    case DM_APP_STATE_OWNED_DIAGNOSTIC: return "owned-diagnostic";
    case DM_APP_STATE_RUNNING: return "running";
    case DM_APP_STATE_PAUSED: return "paused";
    case DM_APP_STATE_STOPPING: return "stopping";
    case DM_APP_STATE_STOPPED: return "stopped";
    case DM_APP_STATE_FAILED: return "failed";
    }
    return "unknown";
}

const char *dm_app_lifecycle_backend_name(void) {
    return "owned-diagnostic";
}
