// #222 owned actual-caller regression: drives hle_audio.c's real suspend/Stop/Start/Close paths
// against a fake AAudio stream. Failure injection is in the stream results, not a mirror state machine.
#include "fake-aaudio/aaudio/AAudio.h"
#include "../../host/hle_audio.c"
#include <stdio.h>

static aout *g_target;
static int objmap_deleted;
static uint64_t fake_now;
uint64_t now_ns(void) { return ++fake_now; }
void *objmap_get(const void *guest) { (void)guest; return g_target; }
void objmap_put(const void *guest, void *host) { (void)guest; (void)host; }
void *objmap_del(const void *guest) { (void)guest; ++objmap_deleted; return g_target; }
void hle_signal_system_event(void *event) { (void)event; }
void hle_bind_system_event(void *event, int autoclear) { (void)event; (void)autoclear; }

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

static void reset(aout *a, int active) {
    a->au = &fake_stream_object; a->state = STATE_STARTED;
    atomic_store(&a->au_active, active); atomic_store(&a->null_run, 0);
    fake_aaudio.state = AAUDIO_STREAM_STATE_STARTED;
    fake_aaudio.request_stop_result = 0; fake_aaudio.start_result = 0;
}

int main(void) {
    static aout a;
    int guest;
    pthread_mutex_init(&a.m, NULL);
    g_target = &a; g_aos[0] = &a; a.ch = 2;
    reset(&a, 1);

    // 1) suspend stop fails: callback remains sole consumer, no sink.
    fake_aaudio.request_stop_result = -1;
    dm_audio_suspend(1);
    CHECK(atomic_load(&a.au_active) == 1);
    CHECK(atomic_load(&a.null_run) == 0);

    // 2) Stop retries and fails: still no sink.
    h_au_Stop(&guest);
    CHECK(atomic_load(&a.au_active) == 1);
    CHECK(atomic_load(&a.null_run) == 0);

    // 3) Start while the old callback is still live must not spawn a sink (the round-1 bug).
    h_au_Start(&guest);
    CHECK(atomic_load(&a.au_active) == 1);
    CHECK(atomic_load(&a.null_run) == 0);

    // 4) Close with unconfirmed stop retains object/buffers and never frees storage.
    h_au_Close(&guest);
    CHECK(objmap_deleted == 0);
    CHECK(fake_aaudio.close_calls == 0);

    // 5) Confirmed stop transfers ownership exactly once.
    fake_aaudio.request_stop_result = 0;
    fake_aaudio.state = AAUDIO_STREAM_STATE_STARTED;
    g_suspended = 0; a.state = STATE_STARTED; atomic_store(&a.au_active, 1);
    dm_audio_suspend(1);
    CHECK(atomic_load(&a.au_active) == 0);
    CHECK(atomic_load(&a.null_run) == 1);
    h_au_Stop(&guest);
    CHECK(atomic_load(&a.null_run) == 0);

    // 6) Resume/start error: close the stream first, then the sink owns consumption.
    reset(&a, 0);
    fake_aaudio.request_stop_result = 0; fake_aaudio.start_result = -1;
    g_suspended = 0; a.state = STATE_STOPPED;
    h_au_Start(&guest);
    CHECK(a.au == NULL);
    CHECK(atomic_load(&a.au_active) == 0);
    CHECK(atomic_load(&a.null_run) == 1);
    h_au_Stop(&guest);
    CHECK(atomic_load(&a.null_run) == 0);

    // 7) A transient start failure must not permanently silence output: the next resume reopens a fresh stream.
    reset(&a, 0);
    fake_aaudio.start_result = -1; fake_aaudio.open_calls = 0; fake_aaudio.close_calls = 0;
    g_suspended = 0; a.state = STATE_STOPPED;
    h_au_Start(&guest);
    CHECK(a.au == NULL && atomic_load(&a.null_run) == 1);
    fake_aaudio.start_result = 0;
    dm_audio_suspend(1);
    dm_audio_suspend(0);
    CHECK(a.au != NULL && atomic_load(&a.au_active) == 1 && atomic_load(&a.null_run) == 0);
    CHECK(fake_aaudio.open_calls >= 1);
    h_au_Stop(&guest);

    // 8) A route/disconnect state must close the stale stream and open a fresh one before start.
    reset(&a, 0);
    fake_aaudio.open_calls = 0; fake_aaudio.close_calls = 0;
    g_suspended = 0; a.state = STATE_STOPPED; fake_aaudio.state = AAUDIO_STREAM_STATE_DISCONNECTED;
    h_au_Start(&guest);
    CHECK(a.au != NULL && atomic_load(&a.au_active) == 1 && atomic_load(&a.null_run) == 0);
    CHECK(fake_aaudio.close_calls >= 1 && fake_aaudio.open_calls >= 1);
    h_au_Stop(&guest);

    // 9) Disconnect while playing: suspend hands off to the sink; resume replaces the stale handle.
    reset(&a, 0);
    fake_aaudio.open_calls = 0; fake_aaudio.close_calls = 0;
    g_suspended = 0; a.state = STATE_STARTED; fake_aaudio.state = AAUDIO_STREAM_STATE_DISCONNECTED;
    dm_audio_suspend(1);
    CHECK(atomic_load(&a.null_run) == 1 && atomic_load(&a.au_active) == 0);
    dm_audio_suspend(0);
    CHECK(a.au != NULL && atomic_load(&a.au_active) == 1 && atomic_load(&a.null_run) == 0);
    CHECK(fake_aaudio.close_calls >= 1 && fake_aaudio.open_calls >= 1);
    h_au_Stop(&guest);

    puts("PASS: actual suspend/Stop/Start/Close keep one consumer across failed and confirmed AAudio stops; reopen after transient start failure and disconnect");
    return 0;
}
