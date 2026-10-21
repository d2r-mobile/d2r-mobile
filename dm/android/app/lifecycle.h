// Android app lifecycle controller: owned diagnostic backend only.
// This is the production controller used by dm/android/app/native.c. It is
// deliberately independent of Android headers so its transition/ownership
// behavior can be tested on the host with an injected engine I/O boundary.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum dm_app_state {
    DM_APP_STATE_CREATED = 0,
    DM_APP_STATE_STARTING,
    DM_APP_STATE_LOADING,
    DM_APP_STATE_OWNED_DIAGNOSTIC,
    // Reserved for an admitted game backend. This controller has no game
    // admission and never enters this state; diagnostic startup cannot claim it.
    DM_APP_STATE_RUNNING,
    DM_APP_STATE_PAUSED,
    DM_APP_STATE_STOPPING,
    DM_APP_STATE_STOPPED,
    DM_APP_STATE_FAILED
} dm_app_state;

// Host/engine boundary. load and stop are required. pause/resume are optional.
// surface_acquire/surface_release must both be present if surfaces are used;
// they transfer an owned reference (for example ANativeWindow_acquire/release),
// not a callback-scoped borrowed pointer.
typedef struct dm_app_host_ops {
    void *ctx;
    int (*load)(void *ctx);
    int (*stop)(void *ctx);
    int (*pause)(void *ctx);
    int (*resume)(void *ctx);
    int (*surface_acquire)(void *ctx, void *surface);
    void (*surface_release)(void *ctx, void *surface);
} dm_app_host_ops;

typedef void (*dm_app_state_observer)(void *ctx, dm_app_state state);

typedef struct dm_app_lifecycle dm_app_lifecycle;

// Creates the controller in CREATED. The caller owns host_ops and its context;
// the controller only calls them. Returns NULL for missing load/stop or a
// one-sided surface acquire/release pair.
dm_app_lifecycle *dm_app_lifecycle_create(const dm_app_host_ops *host_ops,
                                          dm_app_state_observer observer,
                                          void *observer_ctx);
// Attempts to free the controller. Returns 1 only when the controller is
// CREATED, STOPPED, FAILED with no pending work, or STOPPING with no pending
// work after final cleanup. Returns 0 and leaves the pointer valid when work is
// still outstanding or the active controller has not been stopped: the caller
// must retain it, call work_end/stop as appropriate, and retry later.
int dm_app_lifecycle_destroy(dm_app_lifecycle *lifecycle);

// Starts the owned diagnostic backend: CREATED/STOPPED -> STARTING -> LOADING
// -> OWNED_DIAGNOSTIC. Duplicate starts while active are rejected (0). A load
// failure is FAILED after one host stop/cleanup. This never enters RUNNING.
int dm_app_lifecycle_start(dm_app_lifecycle *lifecycle);

// Requests stop. An active controller enters STOPPING and waits for pending
// work; with no pending work it releases the owned surface, calls host stop
// once, and enters STOPPED. Repeated stop is idempotent (1, no duplicate host
// stop). CREATED/STOPPED return 1. In FAILED, stop acknowledges the failure:
// pending work is preserved and cleanup is deferred until the last work_end,
// then the controller becomes STOPPED. A second cleanup is never performed.
int dm_app_lifecycle_stop(dm_app_lifecycle *lifecycle);

int dm_app_lifecycle_pause(dm_app_lifecycle *lifecycle);
int dm_app_lifecycle_resume(dm_app_lifecycle *lifecycle);

// Registers an owned surface reference. A replacement is acquired before the
// old reference is released. While work is outstanding, the old reference is
// retained in one retired slot until final work_end; matching destroy callbacks
// are deferred and stale callbacks are ignored. A second replacement while the
// retired slot is occupied is rejected (explicit backpressure), not silently
// dropped. The pointer is treated as an opaque token and is never dereferenced.
int dm_app_lifecycle_surface_created(dm_app_lifecycle *lifecycle, void *surface);
int dm_app_lifecycle_surface_destroyed(dm_app_lifecycle *lifecycle, void *surface);
void *dm_app_lifecycle_surface(const dm_app_lifecycle *lifecycle);

// Async work accounting. New work is rejected while STOPPING or FAILED. The
// final work_end completes a pending stop; in FAILED it completes deferred
// failure cleanup while preserving the failed state unless stop was requested.
// These calls must stay on the lifecycle control thread; they are not a queue
// and do not block.
int dm_app_lifecycle_work_begin(dm_app_lifecycle *lifecycle);
int dm_app_lifecycle_work_end(dm_app_lifecycle *lifecycle);
uint32_t dm_app_lifecycle_pending_work(const dm_app_lifecycle *lifecycle);

dm_app_state dm_app_lifecycle_state(const dm_app_lifecycle *lifecycle);
const char *dm_app_lifecycle_state_name(dm_app_state state);
const char *dm_app_lifecycle_backend_name(void);

#ifdef __cplusplus
}
#endif
