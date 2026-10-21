// Host tests for the actual Android app lifecycle controller. The engine/surface
// boundary is injected; there is no shadow state machine and no Android runtime.
#include "../app/lifecycle.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

typedef struct {
    int load_result, stop_result, pause_result, resume_result, acquire_result;
    int acquire_fail_at;
    int loads, stops, pauses, resumes, acquires, releases;
    void *acquired[16];
    void *released[16];
    int state_count;
    dm_app_state states[64];
} fake_host;

static int fake_load(void *ctx) { fake_host *h = ctx; h->loads++; return h->load_result; }
static int fake_stop(void *ctx) { fake_host *h = ctx; h->stops++; return h->stop_result; }
static int fake_pause(void *ctx) { fake_host *h = ctx; h->pauses++; return h->pause_result; }
static int fake_resume(void *ctx) { fake_host *h = ctx; h->resumes++; return h->resume_result; }
static int fake_acquire(void *ctx, void *surface) {
    fake_host *h = ctx;
    h->acquires++;
    if (h->acquires <= 16) h->acquired[h->acquires - 1] = surface;
    return h->acquire_result || (h->acquire_fail_at && h->acquires == h->acquire_fail_at);
}
static void fake_release(void *ctx, void *surface) {
    fake_host *h = ctx;
    h->releases++;
    if (h->releases <= 16) h->released[h->releases - 1] = surface;
}
static void observe(void *ctx, dm_app_state state) {
    fake_host *h = ctx;
    if (h->state_count < 64) h->states[h->state_count] = state;
    h->state_count++;
}

static dm_app_host_ops ops_for(fake_host *h) {
    dm_app_host_ops ops = {
        .ctx = h,
        .load = fake_load,
        .stop = fake_stop,
        .pause = fake_pause,
        .resume = fake_resume,
        .surface_acquire = fake_acquire,
        .surface_release = fake_release,
    };
    return ops;
}

static void reset_host(fake_host *h) {
    memset(h, 0, sizeof(*h));
}

static void start_contract(void) {
    fake_host h; reset_host(&h);
    dm_app_host_ops ops = ops_for(&h);
    dm_app_lifecycle *lc = dm_app_lifecycle_create(&ops, observe, &h);
    CHECK(lc != NULL);
    CHECK(dm_app_lifecycle_state(lc) == DM_APP_STATE_CREATED);
    CHECK(h.loads == 0 && h.stops == 0);

    CHECK(dm_app_lifecycle_start(lc) == 1);
    CHECK(dm_app_lifecycle_state(lc) == DM_APP_STATE_OWNED_DIAGNOSTIC);
    CHECK(dm_app_lifecycle_state(lc) != DM_APP_STATE_RUNNING);
    CHECK(h.loads == 1);
    CHECK(h.stops == 0);
    CHECK(h.state_count == 4);
    CHECK(h.states[0] == DM_APP_STATE_CREATED);
    CHECK(h.states[1] == DM_APP_STATE_STARTING);
    CHECK(h.states[2] == DM_APP_STATE_LOADING);
    CHECK(h.states[3] == DM_APP_STATE_OWNED_DIAGNOSTIC);
    CHECK(!strcmp(dm_app_lifecycle_backend_name(), "owned-diagnostic"));
    CHECK(!strcmp(dm_app_lifecycle_state_name(dm_app_lifecycle_state(lc)), "owned-diagnostic"));

    CHECK(dm_app_lifecycle_start(lc) == 0);       /* duplicate start */
    CHECK(h.loads == 1);
    CHECK(dm_app_lifecycle_stop(lc) == 1);
    CHECK(dm_app_lifecycle_state(lc) == DM_APP_STATE_STOPPED);
    CHECK(h.stops == 1);
    CHECK(dm_app_lifecycle_stop(lc) == 1);        /* idempotent stop */
    CHECK(h.stops == 1);

    CHECK(dm_app_lifecycle_start(lc) == 1);       /* restart after stop */
    CHECK(dm_app_lifecycle_state(lc) == DM_APP_STATE_OWNED_DIAGNOSTIC);
    CHECK(h.loads == 2);
    CHECK(dm_app_lifecycle_stop(lc) == 1);
    CHECK(h.stops == 2);
    dm_app_lifecycle_destroy(lc);
}

static void pause_resume_contract(void) {
    fake_host h; reset_host(&h);
    dm_app_host_ops ops = ops_for(&h);
    dm_app_lifecycle *lc = dm_app_lifecycle_create(&ops, observe, &h);
    CHECK(lc != NULL);
    CHECK(dm_app_lifecycle_pause(lc) == 0);       /* not active */
    CHECK(dm_app_lifecycle_resume(lc) == 0);
    CHECK(dm_app_lifecycle_start(lc) == 1);

    CHECK(dm_app_lifecycle_pause(lc) == 1);
    CHECK(dm_app_lifecycle_state(lc) == DM_APP_STATE_PAUSED);
    CHECK(h.pauses == 1);
    CHECK(dm_app_lifecycle_pause(lc) == 1);       /* duplicate */
    CHECK(h.pauses == 1);
    CHECK(dm_app_lifecycle_resume(lc) == 1);
    CHECK(dm_app_lifecycle_state(lc) == DM_APP_STATE_OWNED_DIAGNOSTIC);
    CHECK(h.resumes == 1);
    CHECK(dm_app_lifecycle_resume(lc) == 1);      /* duplicate */
    CHECK(h.resumes == 1);
    CHECK(dm_app_lifecycle_stop(lc) == 1);
    dm_app_lifecycle_destroy(lc);
}

static void surface_contract(void) {
    fake_host h; reset_host(&h);
    dm_app_host_ops ops = ops_for(&h);
    dm_app_lifecycle *lc = dm_app_lifecycle_create(&ops, observe, &h);
    int one = 0, two = 0, stale = 0;
    CHECK(lc != NULL);
    CHECK(dm_app_lifecycle_surface_created(lc, &one) == 0); /* not active */
    CHECK(dm_app_lifecycle_start(lc) == 1);

    CHECK(dm_app_lifecycle_surface_created(lc, &one) == 1);
    CHECK(h.acquires == 1 && h.acquired[0] == &one);
    CHECK(dm_app_lifecycle_surface(lc) == &one);
    CHECK(dm_app_lifecycle_surface_created(lc, &one) == 1); /* duplicate */
    CHECK(h.acquires == 1);

    CHECK(dm_app_lifecycle_surface_created(lc, &two) == 1); /* replacement */
    CHECK(h.releases == 1 && h.released[0] == &one);
    CHECK(h.acquires == 2 && h.acquired[1] == &two);
    CHECK(dm_app_lifecycle_surface(lc) == &two);
    CHECK(dm_app_lifecycle_surface_destroyed(lc, &one) == 0); /* stale */
    CHECK(h.releases == 1);
    CHECK(dm_app_lifecycle_surface_destroyed(lc, &stale) == 0);
    CHECK(dm_app_lifecycle_surface_destroyed(lc, &two) == 1);
    CHECK(h.releases == 2 && h.released[1] == &two);
    CHECK(dm_app_lifecycle_surface(lc) == NULL);
    CHECK(dm_app_lifecycle_stop(lc) == 1);
    CHECK(h.releases == 2);
    dm_app_lifecycle_destroy(lc);
}

static void load_failure(void) {
    fake_host h; reset_host(&h);
    h.load_result = 1;
    dm_app_host_ops ops = ops_for(&h);
    dm_app_lifecycle *lc = dm_app_lifecycle_create(&ops, observe, &h);
    CHECK(lc != NULL);
    CHECK(dm_app_lifecycle_start(lc) == 0);
    CHECK(dm_app_lifecycle_state(lc) == DM_APP_STATE_FAILED);
    CHECK(h.loads == 1 && h.stops == 1);
    CHECK(dm_app_lifecycle_stop(lc) == 1);
    CHECK(dm_app_lifecycle_state(lc) == DM_APP_STATE_STOPPED);
    CHECK(h.stops == 1);                           /* no second cleanup */
    h.load_result = 0;
    CHECK(dm_app_lifecycle_start(lc) == 1);
    CHECK(dm_app_lifecycle_stop(lc) == 1);
    dm_app_lifecycle_destroy(lc);
}

static void stop_with_pending_work(void) {
    fake_host h; reset_host(&h);
    dm_app_host_ops ops = ops_for(&h);
    dm_app_lifecycle *lc = dm_app_lifecycle_create(&ops, observe, &h);
    int window = 0;
    CHECK(lc != NULL);
    CHECK(dm_app_lifecycle_start(lc) == 1);
    CHECK(dm_app_lifecycle_surface_created(lc, &window) == 1);
    CHECK(dm_app_lifecycle_work_begin(lc) == 1);
    CHECK(dm_app_lifecycle_pending_work(lc) == 1);

    CHECK(dm_app_lifecycle_stop(lc) == 1);
    CHECK(dm_app_lifecycle_state(lc) == DM_APP_STATE_STOPPING);
    CHECK(h.stops == 0 && h.releases == 0);        /* wait for pending work */
    CHECK(dm_app_lifecycle_surface(lc) == &window);
    CHECK(dm_app_lifecycle_stop(lc) == 1);
    CHECK(h.stops == 0);
    CHECK(dm_app_lifecycle_work_begin(lc) == 0);   /* no new work while stopping */
    CHECK(dm_app_lifecycle_work_end(lc) == 1);
    CHECK(dm_app_lifecycle_state(lc) == DM_APP_STATE_STOPPED);
    CHECK(dm_app_lifecycle_pending_work(lc) == 0);
    CHECK(h.releases == 1 && h.stops == 1);
    CHECK(dm_app_lifecycle_work_end(lc) == 0);
    CHECK(dm_app_lifecycle_stop(lc) == 1);
    CHECK(h.stops == 1);
    dm_app_lifecycle_destroy(lc);
}

static void error_contracts(void) {
    fake_host h; reset_host(&h);
    dm_app_host_ops ops = ops_for(&h);
    dm_app_lifecycle *lc = dm_app_lifecycle_create(&ops, observe, &h);

    CHECK(dm_app_lifecycle_stop(lc) == 1);         /* never-started stop */
    CHECK(dm_app_lifecycle_state(lc) == DM_APP_STATE_STOPPED);
    CHECK(h.stops == 0);
    CHECK(dm_app_lifecycle_start(lc) == 1);
    h.pause_result = 1;
    CHECK(dm_app_lifecycle_pause(lc) == 0);
    CHECK(dm_app_lifecycle_state(lc) == DM_APP_STATE_FAILED);
    CHECK(h.stops == 1);
    CHECK(dm_app_lifecycle_stop(lc) == 1);
    CHECK(h.stops == 1);
    dm_app_lifecycle_destroy(lc);

    reset_host(&h);
    ops = ops_for(&h);
    lc = dm_app_lifecycle_create(&ops, observe, &h);
    CHECK(dm_app_lifecycle_start(lc) == 1);
    h.acquire_result = 1;
    int window = 0;
    CHECK(dm_app_lifecycle_surface_created(lc, &window) == 0);
    CHECK(dm_app_lifecycle_state(lc) == DM_APP_STATE_FAILED);
    CHECK(dm_app_lifecycle_surface(lc) == NULL);
    CHECK(h.stops == 1);
    dm_app_lifecycle_destroy(lc);

    reset_host(&h);
    ops = ops_for(&h);
    lc = dm_app_lifecycle_create(&ops, observe, &h);
    CHECK(dm_app_lifecycle_start(lc) == 1);
    h.stop_result = 1;
    CHECK(dm_app_lifecycle_stop(lc) == 1);
    CHECK(dm_app_lifecycle_state(lc) == DM_APP_STATE_FAILED);
    CHECK(h.stops == 1);
    CHECK(dm_app_lifecycle_stop(lc) == 1);
    CHECK(dm_app_lifecycle_state(lc) == DM_APP_STATE_FAILED); /* no fake STOPPED */
    CHECK(h.stops == 1);
    CHECK(dm_app_lifecycle_destroy(lc) == 1);
}

static void failure_drains_pending_work(void) {
    fake_host h; reset_host(&h);
    dm_app_host_ops ops = ops_for(&h);
    dm_app_lifecycle *lc = dm_app_lifecycle_create(&ops, observe, &h);
    int window = 0;
    CHECK(lc != NULL);
    CHECK(dm_app_lifecycle_start(lc) == 1);
    CHECK(dm_app_lifecycle_surface_created(lc, &window) == 1);
    CHECK(dm_app_lifecycle_work_begin(lc) == 1);
    h.pause_result = 1;
    CHECK(dm_app_lifecycle_pause(lc) == 0);
    CHECK(dm_app_lifecycle_state(lc) == DM_APP_STATE_FAILED);
    CHECK(dm_app_lifecycle_pending_work(lc) == 1);
    CHECK(h.releases == 0 && h.stops == 0);       /* drain before cleanup */
    CHECK(dm_app_lifecycle_surface(lc) == &window);
    CHECK(dm_app_lifecycle_destroy(lc) == 0);     /* busy destroy must not free */
    CHECK(dm_app_lifecycle_state(lc) == DM_APP_STATE_FAILED);
    CHECK(dm_app_lifecycle_surface(lc) == &window);

    CHECK(dm_app_lifecycle_work_end(lc) == 1);
    CHECK(dm_app_lifecycle_pending_work(lc) == 0);
    CHECK(dm_app_lifecycle_state(lc) == DM_APP_STATE_FAILED);
    CHECK(h.releases == 1 && h.stops == 1);
    CHECK(dm_app_lifecycle_stop(lc) == 1);
    CHECK(dm_app_lifecycle_state(lc) == DM_APP_STATE_STOPPED);
    CHECK(h.releases == 1 && h.stops == 1);
    CHECK(dm_app_lifecycle_start(lc) == 1);
    CHECK(dm_app_lifecycle_state(lc) == DM_APP_STATE_OWNED_DIAGNOSTIC);
    CHECK(h.loads == 2);
    CHECK(dm_app_lifecycle_stop(lc) == 1);
    CHECK(dm_app_lifecycle_destroy(lc) == 1);
}

static void resume_failure_drains_pending_work(void) {
    fake_host h; reset_host(&h);
    dm_app_host_ops ops = ops_for(&h);
    dm_app_lifecycle *lc = dm_app_lifecycle_create(&ops, observe, &h);
    CHECK(lc != NULL);
    CHECK(dm_app_lifecycle_start(lc) == 1);
    CHECK(dm_app_lifecycle_work_begin(lc) == 1);
    CHECK(dm_app_lifecycle_pause(lc) == 1);
    h.resume_result = 1;
    CHECK(dm_app_lifecycle_resume(lc) == 0);
    CHECK(dm_app_lifecycle_state(lc) == DM_APP_STATE_FAILED);
    CHECK(dm_app_lifecycle_pending_work(lc) == 1);
    CHECK(h.stops == 0);
    CHECK(dm_app_lifecycle_work_end(lc) == 1);
    CHECK(dm_app_lifecycle_pending_work(lc) == 0);
    CHECK(h.stops == 1);
    CHECK(dm_app_lifecycle_stop(lc) == 1);
    CHECK(dm_app_lifecycle_state(lc) == DM_APP_STATE_STOPPED);
    CHECK(dm_app_lifecycle_destroy(lc) == 1);
}

static void acquire_failure_drains_pending_work(void) {
    fake_host h; reset_host(&h);
    dm_app_host_ops ops = ops_for(&h);
    dm_app_lifecycle *lc = dm_app_lifecycle_create(&ops, observe, &h);
    int one = 0, two = 0;
    CHECK(lc != NULL);
    CHECK(dm_app_lifecycle_start(lc) == 1);
    CHECK(dm_app_lifecycle_surface_created(lc, &one) == 1);
    CHECK(dm_app_lifecycle_work_begin(lc) == 1);
    h.acquire_fail_at = 2;
    CHECK(dm_app_lifecycle_surface_created(lc, &two) == 0);
    CHECK(dm_app_lifecycle_state(lc) == DM_APP_STATE_FAILED);
    CHECK(dm_app_lifecycle_pending_work(lc) == 1);
    CHECK(dm_app_lifecycle_surface(lc) == &one);  /* old reference retained */
    CHECK(h.releases == 0 && h.stops == 0);       /* no early release */
    CHECK(dm_app_lifecycle_destroy(lc) == 0);
    CHECK(dm_app_lifecycle_work_end(lc) == 1);
    CHECK(dm_app_lifecycle_pending_work(lc) == 0);
    CHECK(dm_app_lifecycle_surface(lc) == NULL);
    CHECK(h.releases == 1 && h.stops == 1);
    CHECK(dm_app_lifecycle_stop(lc) == 1);
    CHECK(dm_app_lifecycle_destroy(lc) == 1);
}

static void replacement_retains_surface_until_drain(void) {
    fake_host h; reset_host(&h);
    dm_app_host_ops ops = ops_for(&h);
    dm_app_lifecycle *lc = dm_app_lifecycle_create(&ops, observe, &h);
    int one = 0, two = 0, three = 0;
    CHECK(lc != NULL);
    CHECK(dm_app_lifecycle_start(lc) == 1);
    CHECK(dm_app_lifecycle_surface_created(lc, &one) == 1);
    CHECK(dm_app_lifecycle_work_begin(lc) == 1);
    CHECK(dm_app_lifecycle_surface_created(lc, &two) == 1);
    CHECK(h.acquires == 2);
    CHECK(dm_app_lifecycle_surface_created(lc, &three) == 0); /* retired slot busy */
    CHECK(h.acquires == 2);
    CHECK(h.releases == 0);                       /* A retained for pending work */
    CHECK(dm_app_lifecycle_surface(lc) == &two);
    CHECK(dm_app_lifecycle_surface_destroyed(lc, &one) == 1); /* old callback */
    CHECK(h.releases == 0);
    CHECK(dm_app_lifecycle_surface_destroyed(lc, &one) == 1); /* duplicate */
    CHECK(h.releases == 0);
    CHECK(dm_app_lifecycle_work_end(lc) == 1);
    CHECK(h.releases == 1 && h.released[0] == &one);
    CHECK(dm_app_lifecycle_surface(lc) == &two);
    CHECK(dm_app_lifecycle_surface_destroyed(lc, &one) == 0); /* stale after drain */
    CHECK(h.releases == 1);
    CHECK(dm_app_lifecycle_surface_destroyed(lc, &two) == 1);
    CHECK(h.releases == 2 && h.released[1] == &two);
    CHECK(dm_app_lifecycle_stop(lc) == 1);
    CHECK(dm_app_lifecycle_destroy(lc) == 1);
}

static void replacement_stop_releases_both(void) {
    fake_host h; reset_host(&h);
    dm_app_host_ops ops = ops_for(&h);
    dm_app_lifecycle *lc = dm_app_lifecycle_create(&ops, observe, &h);
    int one = 0, two = 0;
    CHECK(lc != NULL);
    CHECK(dm_app_lifecycle_start(lc) == 1);
    CHECK(dm_app_lifecycle_surface_created(lc, &one) == 1);
    CHECK(dm_app_lifecycle_work_begin(lc) == 1);
    CHECK(dm_app_lifecycle_surface_created(lc, &two) == 1);
    CHECK(dm_app_lifecycle_stop(lc) == 1);
    CHECK(dm_app_lifecycle_state(lc) == DM_APP_STATE_STOPPING);
    CHECK(h.releases == 0 && h.stops == 0);
    CHECK(dm_app_lifecycle_work_end(lc) == 1);
    CHECK(dm_app_lifecycle_state(lc) == DM_APP_STATE_STOPPED);
    CHECK(h.releases == 2 && h.stops == 1);
    CHECK(h.released[0] == &one && h.released[1] == &two);
    CHECK(dm_app_lifecycle_stop(lc) == 1);
    CHECK(h.stops == 1);
    CHECK(dm_app_lifecycle_destroy(lc) == 1);
}

static void matching_destroy_retains_until_drain(void) {
    fake_host h; reset_host(&h);
    dm_app_host_ops ops = ops_for(&h);
    dm_app_lifecycle *lc = dm_app_lifecycle_create(&ops, observe, &h);
    int one = 0;
    CHECK(lc != NULL);
    CHECK(dm_app_lifecycle_start(lc) == 1);
    CHECK(dm_app_lifecycle_surface_created(lc, &one) == 1);
    CHECK(dm_app_lifecycle_work_begin(lc) == 1);
    CHECK(dm_app_lifecycle_surface_destroyed(lc, &one) == 1);
    CHECK(dm_app_lifecycle_surface(lc) == &one);  /* still owned */
    CHECK(h.releases == 0);
    CHECK(dm_app_lifecycle_surface_destroyed(lc, &one) == 1); /* duplicate */
    CHECK(h.releases == 0);
    CHECK(dm_app_lifecycle_work_end(lc) == 1);
    CHECK(dm_app_lifecycle_surface(lc) == NULL);
    CHECK(h.releases == 1 && h.released[0] == &one);
    CHECK(dm_app_lifecycle_surface_destroyed(lc, &one) == 0);
    CHECK(dm_app_lifecycle_stop(lc) == 1);
    CHECK(h.releases == 1);
    CHECK(dm_app_lifecycle_destroy(lc) == 1);
}

static void busy_destroy_contract(void) {
    fake_host h; reset_host(&h);
    dm_app_host_ops ops = ops_for(&h);
    dm_app_lifecycle *lc = dm_app_lifecycle_create(&ops, observe, &h);
    int window = 0;
    CHECK(lc != NULL);
    CHECK(dm_app_lifecycle_start(lc) == 1);
    CHECK(dm_app_lifecycle_surface_created(lc, &window) == 1);
    CHECK(dm_app_lifecycle_destroy(lc) == 0);     /* active: stop first */
    CHECK(dm_app_lifecycle_surface(lc) == &window);
    CHECK(dm_app_lifecycle_work_begin(lc) == 1);
    CHECK(dm_app_lifecycle_stop(lc) == 1);
    CHECK(dm_app_lifecycle_state(lc) == DM_APP_STATE_STOPPING);
    CHECK(dm_app_lifecycle_destroy(lc) == 0);     /* pending: defer */
    CHECK(dm_app_lifecycle_surface(lc) == &window);
    CHECK(h.releases == 0 && h.stops == 0);
    CHECK(dm_app_lifecycle_work_end(lc) == 1);
    CHECK(dm_app_lifecycle_state(lc) == DM_APP_STATE_STOPPED);
    CHECK(h.releases == 1 && h.stops == 1);
    CHECK(dm_app_lifecycle_destroy(lc) == 1);
}

static void failure_acknowledgement_restart(void) {
    fake_host h; reset_host(&h);
    dm_app_host_ops ops = ops_for(&h);
    dm_app_lifecycle *lc = dm_app_lifecycle_create(&ops, observe, &h);
    CHECK(lc != NULL);
    CHECK(dm_app_lifecycle_start(lc) == 1);
    CHECK(dm_app_lifecycle_work_begin(lc) == 1);
    h.pause_result = 1;
    CHECK(dm_app_lifecycle_pause(lc) == 0);
    CHECK(dm_app_lifecycle_state(lc) == DM_APP_STATE_FAILED);
    CHECK(dm_app_lifecycle_stop(lc) == 1);
    CHECK(dm_app_lifecycle_state(lc) == DM_APP_STATE_FAILED); /* preserve failure */
    CHECK(dm_app_lifecycle_pending_work(lc) == 1);
    CHECK(h.stops == 0);
    CHECK(dm_app_lifecycle_work_end(lc) == 1);
    CHECK(dm_app_lifecycle_state(lc) == DM_APP_STATE_STOPPED);
    CHECK(dm_app_lifecycle_pending_work(lc) == 0);
    CHECK(h.stops == 1);
    CHECK(dm_app_lifecycle_start(lc) == 1);
    CHECK(h.loads == 2);
    CHECK(dm_app_lifecycle_stop(lc) == 1);
    CHECK(dm_app_lifecycle_destroy(lc) == 1);
}

static void invalid_host_ops(void) {
    fake_host h; reset_host(&h);
    dm_app_host_ops ops = ops_for(&h);
    ops.load = NULL;
    CHECK(dm_app_lifecycle_create(&ops, NULL, NULL) == NULL);
    ops = ops_for(&h);
    ops.stop = NULL;
    CHECK(dm_app_lifecycle_create(&ops, NULL, NULL) == NULL);
    ops = ops_for(&h);
    ops.surface_release = NULL;
    CHECK(dm_app_lifecycle_create(&ops, NULL, NULL) == NULL);
}

int main(void) {
    start_contract();
    pause_resume_contract();
    surface_contract();
    load_failure();
    stop_with_pending_work();
    failure_drains_pending_work();
    resume_failure_drains_pending_work();
    acquire_failure_drains_pending_work();
    replacement_retains_surface_until_drain();
    replacement_stop_releases_both();
    matching_destroy_retains_until_drain();
    busy_destroy_contract();
    failure_acknowledgement_restart();
    error_contracts();
    invalid_host_ops();
    if (failures) {
        fprintf(stderr, "FAIL %d lifecycle checks\n", failures);
        return 1;
    }
    puts("PASS app lifecycle: start/load/diagnostic/pause/resume/surface ownership/stop pending/cleanup");
    return 0;
}
