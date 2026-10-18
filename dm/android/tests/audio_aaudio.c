// #149 actual dm_audio production module with only AAudio boundaries stubbed.
// Covers sample order, partial/overflow/underrun, lifecycle failures, disconnect and callback retirement.
#include "../audio/dm_aaudio.h"
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)

struct dm_aaudio_builder {
    int32_t format, channels, rate, direction;
    dm_audio_data_callback data;
    dm_audio_error_callback error;
    void *user;
};
struct dm_aaudio_stream {
    int state;
    int32_t format, channels, rate;
    dm_audio_data_callback data;
    void *user;
    dm_audio_error_callback error;
};
static dm_audio_data_callback g_cb;
static void *g_cb_user;
static dm_audio_error_callback g_err;
static void *g_err_user;
static int g_open_fail, g_mismatch, g_start_fail, g_pause_fail, g_stop_fail, g_close_fail, g_wait_fail;
static int g_close_calls, g_delete_builder_calls, g_stop_calls;

typedef struct {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int entered, release, self_destroy;
    int self_result, destroy_result, callback_result;
    dm_audio *audio;
    int16_t buf[4];
    _Atomic int callback_done;
} hook_ctx;
static hook_ctx *g_hook;
static int g_close_before_callback_done;
static void test_hook(void *p) {
    hook_ctx *h = p;
    pthread_mutex_lock(&h->mu);
    h->entered = 1;
    pthread_cond_broadcast(&h->cv);
    if (h->self_destroy) {
        h->self_result = dm_audio_destroy(h->audio);
        pthread_mutex_unlock(&h->mu);
        return;
    }
    while (!h->release) pthread_cond_wait(&h->cv, &h->mu);
    pthread_mutex_unlock(&h->mu);
}
static void *callback_thread(void *p) {
    hook_ctx *h = p;
    h->callback_result = g_cb(NULL, h->audio, h->buf, 1);
    atomic_store_explicit(&h->callback_done, 1, memory_order_release);
    return NULL;
}
static void *owner_thread(void *p) {
    hook_ctx *h = p;
    h->destroy_result = dm_audio_destroy(h->audio);
    return NULL;
}

static dm_audio_result f_create_builder(dm_aaudio_builder **b) { *b = calloc(1, sizeof **b); return *b ? 0 : -12; }
static dm_audio_result f_delete_builder(dm_aaudio_builder *b) { g_delete_builder_calls++; free(b); return 0; }
static void f_set_format(dm_aaudio_builder *b, int32_t v) { b->format = v; }
static void f_set_channels(dm_aaudio_builder *b, int32_t v) { b->channels = v; }
static void f_set_rate(dm_aaudio_builder *b, int32_t v) { b->rate = v; }
static void f_set_direction(dm_aaudio_builder *b, int32_t v) { b->direction = v; }
static void f_set_data(dm_aaudio_builder *b, dm_audio_data_callback cb, void *u) { b->data = cb; b->user = u; }
static void f_set_error(dm_aaudio_builder *b, dm_audio_error_callback cb, void *u) { b->error = cb; (void)u; }
static dm_audio_result f_open(dm_aaudio_builder *b, dm_aaudio_stream **s) {
    if (g_open_fail) return -5;
    dm_aaudio_stream *x = calloc(1, sizeof *x);
    if (!x) return -12;
    x->state = 2; x->format = b->format; x->channels = b->channels; x->rate = b->rate;
    x->data = b->data; x->user = b->user; x->error = b->error;
    if (g_mismatch) x->rate = 44100;
    *s = x; g_cb = x->data; g_cb_user = x->user; g_err = x->error; g_err_user = b->user; return 0;
}
static dm_audio_result f_close(dm_aaudio_stream *s) {
    g_close_calls++;
    if (g_hook && !atomic_load_explicit(&g_hook->callback_done, memory_order_acquire))
        g_close_before_callback_done = 1;
    if (g_close_fail) return -5;
    free(s);
    return 0;
}
static dm_audio_result f_start(dm_aaudio_stream *s) { if (g_start_fail) return -5; s->state = 4; return 0; }
static dm_audio_result f_pause(dm_aaudio_stream *s) { if (g_pause_fail) return -5; s->state = 6; return 0; }
static dm_audio_result f_stop(dm_aaudio_stream *s) { g_stop_calls++; if (g_stop_fail) return -5; s->state = g_wait_fail ? 9 : 10; return 0; }
static dm_audio_result f_get_state(dm_aaudio_stream *s) { return s->state; }
static dm_audio_result f_wait(dm_aaudio_stream *s, int32_t cur, int32_t *next, int64_t ns) { (void)s; (void)cur; (void)ns; if (g_wait_fail) return -5; *next = 10; return 0; }
static dm_audio_result f_get_format(dm_aaudio_stream *s) { return s->format; }
static dm_audio_result f_get_channels(dm_aaudio_stream *s) { return s->channels; }
static dm_audio_result f_get_rate(dm_aaudio_stream *s) { return s->rate; }
static dm_audio_result f_get_buffer(dm_aaudio_stream *s) { (void)s; return 0; }
static dm_audio_result f_get_xruns(dm_aaudio_stream *s) { (void)s; return 0; }

static const dm_audio_api api = {
    f_create_builder, f_delete_builder, f_set_format, f_set_channels, f_set_rate, f_set_direction,
    f_set_data, f_set_error, f_open, f_close, f_start, f_pause, f_stop, f_get_state, f_wait,
    f_get_format, f_get_channels, f_get_rate, f_get_buffer, f_get_xruns,
};
static dm_audio_options options(uint32_t frames) { dm_audio_options o = DM_AUDIO_OPTIONS_INIT; o.queue_frames = frames; o.api = &api; return o; }
static void reset(void) {
    g_open_fail = g_mismatch = g_start_fail = g_pause_fail = g_stop_fail = g_close_fail = g_wait_fail = 0;
    g_close_calls = g_delete_builder_calls = g_stop_calls = 0;
    g_hook = NULL; g_close_before_callback_done = 0;
}

int main(void) {
    reset(); dm_audio *a = NULL;
    CHECK(dm_audio_create(&(dm_audio_options){0}, &a) == -EINVAL);
    dm_audio_options bad = options(4); bad.struct_size = 0;
    CHECK(dm_audio_create(&bad, &a) == -EINVAL);
    dm_audio_options noapi = options(4); noapi.api = NULL;
    CHECK(dm_audio_create(&noapi, &a) == -ENOTSUP);

    reset(); CHECK(dm_audio_create(&(dm_audio_options)DM_AUDIO_OPTIONS_INIT, &a) == -EINVAL);

    // Every unconditionally used setter must be validated before any call/alloc.
    for (int missing = 0; missing < 3; missing++) {
        dm_audio_api incomplete = api;
        if (missing == 0) incomplete.set_format = NULL;
        if (missing == 1) incomplete.set_channel_count = NULL;
        if (missing == 2) incomplete.set_sample_rate = NULL;
        dm_audio_options miss = options(4); miss.api = &incomplete; a = (dm_audio *)0x1;
        CHECK(dm_audio_create(&miss, &a) == -EINVAL && a == NULL);
    }

    reset(); dm_audio_options o = options(4);
    CHECK(dm_audio_create(&o, &a) == 0 && a);
    CHECK(g_cb != NULL && g_cb_user == a && g_delete_builder_calls == 1);
    CHECK(dm_audio_start(a) == 0);
    CHECK(dm_audio_pause(a) == 0);
    CHECK(dm_audio_stop(a) == 0);

    // Sample order and multiple chunks.
    int16_t frames[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    CHECK(dm_audio_write(a, frames, 2) == 2);
    CHECK(dm_audio_write(a, frames + 4, 2) == 2);
    int16_t out[8] = {0};
    CHECK(g_cb(NULL, g_cb_user, out, 4) == DM_AUDIO_CALLBACK_CONTINUE);
    for (int i = 0; i < 8; i++) CHECK(out[i] == (int16_t)(i + 1));
    dm_audio_stats st; dm_audio_get_stats(a, &st);
    CHECK(st.frames_written == 4 && st.frames_dropped == 0 && st.underrun_frames == 0);
    CHECK(dm_audio_destroy(a) == 0);

    // Overflow preserves the bounded prefix and counts dropped frames.
    reset(); o = options(4); a = NULL; CHECK(dm_audio_create(&o, &a) == 0);
    int16_t big[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    CHECK(dm_audio_write(a, big, 6) == 4);
    memset(out, 0, sizeof out); CHECK(g_cb(NULL, g_cb_user, out, 4) == 0);
    for (int i = 0; i < 8; i++) CHECK(out[i] == big[i]);
    dm_audio_get_stats(a, &st); CHECK(st.frames_written == 4 && st.frames_dropped == 2);
    CHECK(dm_audio_destroy(a) == 0);

    // Partial callback: available frames then explicit silence + underrun count.
    reset(); o = options(4); a = NULL; CHECK(dm_audio_create(&o, &a) == 0);
    CHECK(dm_audio_write(a, frames, 1) == 1);
    memset(out, 0xff, sizeof out);
    CHECK(g_cb(NULL, g_cb_user, out, 3) == 0);
    CHECK(out[0] == 1 && out[1] == 2 && out[2] == 0 && out[3] == 0 && out[4] == 0 && out[5] == 0);
    dm_audio_get_stats(a, &st); CHECK(st.underrun_frames == 2 && st.underrun_events == 1);
    CHECK(dm_audio_destroy(a) == 0);

    // Format mismatch is rejected after open and the stream is closed.
    reset(); g_mismatch = 1; o = options(4); a = NULL;
    CHECK(dm_audio_create(&o, &a) == -EPROTO && a == NULL && g_close_calls == 1);
    reset(); g_open_fail = 1; a = NULL; CHECK(dm_audio_create(&o, &a) == -EIO && a == NULL);

    // Mismatch + failed rollback close: callback userdata must stay alive; handle retries to final free.
    reset(); g_mismatch = 1; g_close_fail = 1; o = options(4); a = (dm_audio *)0x1;
    CHECK(dm_audio_create(&o, &a) == -EAGAIN && a != NULL && g_close_calls == 1);
    CHECK(g_err != NULL && g_err_user == a);
    g_err(NULL, g_err_user, -3); // earlier bug: UAF here after create freed a
    dm_audio_stats mismatch_stats; dm_audio_get_stats(a, &mismatch_stats);
    CHECK(mismatch_stats.disconnected == 1 && mismatch_stats.closing == 1);
    CHECK(dm_audio_write(a, frames, 1) == 0);
    g_close_fail = 0;
    CHECK(dm_audio_destroy(a) == 0 && g_close_calls == 2);

    // Repeated rollback close failures retain the handle; recovery makes one final close and free.
    reset(); g_mismatch = 1; g_close_fail = 1; o = options(4); a = NULL;
    CHECK(dm_audio_create(&o, &a) == -EAGAIN && a != NULL && g_close_calls == 1);
    CHECK(dm_audio_destroy(a) == -EIO && g_close_calls == 2);
    if (g_err) g_err(NULL, g_err_user, -3);
    CHECK(dm_audio_destroy(a) == -EIO && g_close_calls == 3);
    g_close_fail = 0;
    CHECK(dm_audio_destroy(a) == 0 && g_close_calls == 4);

    // Lifecycle and disconnect/error paths.
    reset(); g_start_fail = 1; o = options(4); a = NULL;
    CHECK(dm_audio_create(&o, &a) == 0 && dm_audio_start(a) == -EIO);
    CHECK(dm_audio_destroy(a) == 0);
    reset(); g_pause_fail = g_stop_fail = 1; o = options(4); a = NULL;
    CHECK(dm_audio_create(&o, &a) == 0);
    CHECK(dm_audio_pause(a) == -EIO && dm_audio_stop(a) == -EIO);
    g_pause_fail = g_stop_fail = 0;
    CHECK(dm_audio_destroy(a) == 0);
    reset(); o = options(4); a = NULL; CHECK(dm_audio_create(&o, &a) == 0);
    dm_audio_stats before; dm_audio_get_stats(a, &before);
    (void)before;
    if (g_err) g_err(NULL, g_err_user, -3);
    dm_audio_get_stats(a, &before); CHECK(before.disconnected == 1);
    CHECK(dm_audio_write(a, frames, 1) == 0);
    CHECK(dm_audio_start(a) == -ENOTCONN);
    CHECK(dm_audio_destroy(a) == 0);

    // A close that rejects once retains ownership; a serialized retry must close again and free.
    reset(); o = options(4); a = NULL; CHECK(dm_audio_create(&o, &a) == 0);
    g_close_fail = 1;
    CHECK(dm_audio_destroy(a) == -EIO);
    CHECK(g_close_calls == 1);
    g_close_fail = 0;
    CHECK(dm_audio_destroy(a) == 0);
    CHECK(g_close_calls == 2);

    // Stop failure + successful close: report failure once, then final-release without double close.
    reset(); g_stop_fail = 1; o = options(4); a = NULL; CHECK(dm_audio_create(&o, &a) == 0);
    CHECK(dm_audio_destroy(a) == -EIO);
    CHECK(g_close_calls == 1);
    g_stop_fail = 0;
    CHECK(dm_audio_destroy(a) == 0);
    CHECK(g_close_calls == 1);

    // Wait failure + successful close: same truthful retained CLOSED state, no NULL close.
    reset(); g_wait_fail = 1; o = options(4); a = NULL; CHECK(dm_audio_create(&o, &a) == 0);
    CHECK(dm_audio_destroy(a) == -EIO);
    CHECK(g_close_calls == 1);
    g_wait_fail = 0;
    CHECK(dm_audio_destroy(a) == 0);
    CHECK(g_close_calls == 1);

    // Callback returns before destroy in this synchronous case.
    reset(); g_cb = NULL; o = options(4); a = NULL; CHECK(dm_audio_create(&o, &a) == 0);
    CHECK(g_cb != NULL);
    g_cb(NULL, a, out, 1); // no data -> underrun; still callback context ended
    CHECK(dm_audio_destroy(a) == 0);

    // Actual production callback thread: owner destroy must wait for the active callback, then close/free.
    reset(); o = options(4); a = NULL; CHECK(dm_audio_create(&o, &a) == 0);
    hook_ctx h = {0};
    pthread_mutex_init(&h.mu, NULL); pthread_cond_init(&h.cv, NULL); h.audio = a;
    dm_audio_test_set_callback_hook(a, test_hook, &h);
    g_hook = &h;
    pthread_t cb, owner;
    CHECK(pthread_create(&cb, NULL, callback_thread, &h) == 0);
    pthread_mutex_lock(&h.mu);
    while (!h.entered) pthread_cond_wait(&h.cv, &h.mu);
    pthread_mutex_unlock(&h.mu);
    CHECK(pthread_create(&owner, NULL, owner_thread, &h) == 0);
    usleep(100000); // owner is inside destroy; callback is active and blocked
    pthread_mutex_lock(&h.mu); h.release = 1; pthread_cond_broadcast(&h.cv); pthread_mutex_unlock(&h.mu);
    pthread_join(owner, NULL); pthread_join(cb, NULL);
    CHECK(h.destroy_result == 0 && h.callback_result == DM_AUDIO_CALLBACK_CONTINUE);
    CHECK(g_close_before_callback_done == 0);
    pthread_cond_destroy(&h.cv); pthread_mutex_destroy(&h.mu);

    // Callback thread cannot destroy itself; owner later retires it normally.
    reset(); o = options(4); a = NULL; CHECK(dm_audio_create(&o, &a) == 0);
    memset(&h, 0, sizeof h);
    pthread_mutex_init(&h.mu, NULL); pthread_cond_init(&h.cv, NULL); h.audio = a; h.self_destroy = 1;
    dm_audio_test_set_callback_hook(a, test_hook, &h);
    CHECK(pthread_create(&cb, NULL, callback_thread, &h) == 0);
    pthread_join(cb, NULL);
    CHECK(h.self_result == -EBUSY && h.callback_result == DM_AUDIO_CALLBACK_CONTINUE);
    pthread_cond_destroy(&h.cv); pthread_mutex_destroy(&h.mu);
    CHECK(dm_audio_destroy(a) == 0);

    printf("audio_aaudio: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
