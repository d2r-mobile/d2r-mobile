// #200 host JVM 入口 + 窄 AAudio fixture：驱动真实 native_audio 钩子 → audio_session → dm_afj/audio_output/policy。
// #200 host JVM entry plus narrow AAudio fixture driving the actual native_audio hooks into the real accepted stack.
#include "audio_focus.h"
#include "native_audio.h"

#include <stdio.h>
#include <stdlib.h>

static native_audio *g_audio;

struct dm_aaudio_builder {
    int32_t format, channels, rate, direction;
    dm_audio_data_callback data;
    dm_audio_error_callback error;
    void *user;
};
struct dm_aaudio_stream {
    int state;
};

static int g_open_calls, g_start_calls, g_close_calls;
static int g_close_fail_count;
static struct dm_aaudio_stream *g_stream;

static dm_audio_result fx_create_builder(dm_aaudio_builder **b) {
    *b = calloc(1, sizeof **b);
    return *b ? DM_AUDIO_OK : -1;
}
static dm_audio_result fx_delete_builder(dm_aaudio_builder *b) { free(b); return DM_AUDIO_OK; }
static void fx_set_format(dm_aaudio_builder *b, int32_t v) { b->format = v; }
static void fx_set_channels(dm_aaudio_builder *b, int32_t v) { b->channels = v; }
static void fx_set_rate(dm_aaudio_builder *b, int32_t v) { b->rate = v; }
static void fx_set_direction(dm_aaudio_builder *b, int32_t v) { b->direction = v; }
static void fx_set_data(dm_aaudio_builder *b, dm_audio_data_callback cb, void *user) { b->data = cb; b->user = user; }
static void fx_set_error(dm_aaudio_builder *b, dm_audio_error_callback cb, void *user) { b->error = cb; b->user = user; }
static dm_audio_result fx_open(dm_aaudio_builder *b, dm_aaudio_stream **out) {
    (void)b;
    struct dm_aaudio_stream *s = calloc(1, sizeof *s);
    if (!s) return -1;
    g_stream = s;
    g_open_calls++;
    *out = s;
    return DM_AUDIO_OK;
}
static dm_audio_result fx_close(dm_aaudio_stream *s) {
    g_close_calls++;
    if (g_close_fail_count > 0) { g_close_fail_count--; return -1; }
    free(s);
    if (g_stream == s) g_stream = NULL;
    return DM_AUDIO_OK;
}
static dm_audio_result fx_start(dm_aaudio_stream *s) { g_start_calls++; s->state = 2; return DM_AUDIO_OK; }
static dm_audio_result fx_pause(dm_aaudio_stream *s) { s->state = 3; return DM_AUDIO_OK; }
static dm_audio_result fx_stop(dm_aaudio_stream *s) { s->state = 0; return DM_AUDIO_OK; }
static dm_audio_result fx_state(dm_aaudio_stream *s) { return s->state; }
static dm_audio_result fx_wait(dm_aaudio_stream *s, int32_t current, int32_t *next, int64_t timeout) {
    (void)s; (void)current; (void)timeout;
    *next = 0;
    return DM_AUDIO_OK;
}
static dm_audio_result fx_format(dm_aaudio_stream *s) { (void)s; return DM_AUDIO_FORMAT_PCM16; }
static dm_audio_result fx_channels(dm_aaudio_stream *s) { (void)s; return DM_AUDIO_CHANNELS; }
static dm_audio_result fx_rate(dm_aaudio_stream *s) { (void)s; return DM_AUDIO_RATE; }
static dm_audio_result fx_buffer(dm_aaudio_stream *s) { (void)s; return 64; }
static dm_audio_result fx_xruns(dm_aaudio_stream *s) { (void)s; return 0; }

static const dm_audio_api k_fixture = {
    fx_create_builder, fx_delete_builder, fx_set_format, fx_set_channels, fx_set_rate, fx_set_direction,
    fx_set_data, fx_set_error, fx_open, fx_close, fx_start, fx_pause, fx_stop, fx_state, fx_wait,
    fx_format, fx_channels, fx_rate, fx_buffer, fx_xruns,
};

JNIEXPORT void JNICALL Java_NativeAudioHarness_nativeFixtureReset(JNIEnv *env, jclass cls) {
    (void)env; (void)cls;
    g_open_calls = g_start_calls = g_close_calls = 0;
    g_close_fail_count = 0;
    g_stream = NULL;
}
JNIEXPORT void JNICALL Java_NativeAudioHarness_nativeFixtureCloseFailTwice(JNIEnv *env, jclass cls) {
    (void)env; (void)cls; g_close_fail_count = 2;
}
JNIEXPORT jint JNICALL Java_NativeAudioHarness_nativeFixtureOpens(JNIEnv *env, jclass cls) {
    (void)env; (void)cls; return g_open_calls;
}
JNIEXPORT jint JNICALL Java_NativeAudioHarness_nativeFixtureStarts(JNIEnv *env, jclass cls) {
    (void)env; (void)cls; return g_start_calls;
}
JNIEXPORT jint JNICALL Java_NativeAudioHarness_nativeFixtureCloses(JNIEnv *env, jclass cls) {
    (void)env; (void)cls; return g_close_calls;
}

JNIEXPORT jint JNICALL Java_NativeAudioHarness_nativeCreate(JNIEnv *env, jclass cls, jobject context,
                                                            jint queue_frames) {
    (void)cls;
    if (g_audio) return 0;
    dm_audio_session_options options = DM_AUDIO_SESSION_OPTIONS_INIT;
    options.audio_api = &k_fixture;
    options.queue_frames = (uint32_t)(queue_frames > 1 ? queue_frames : 2);
    return native_audio_create(env, context, &options, &g_audio) == 0 ? 1 : 0;
}
JNIEXPORT jint JNICALL Java_NativeAudioHarness_nativeKey(JNIEnv *env, jclass cls, jint key, jint action,
                                                         jint repeat, jint keyboard) {
    (void)cls;
    return native_audio_key(g_audio, env, key, action, repeat, keyboard);
}
JNIEXPORT jint JNICALL Java_NativeAudioHarness_nativeResume(JNIEnv *env, jclass cls) {
    (void)env; (void)cls; return native_audio_resume(g_audio);
}
JNIEXPORT jint JNICALL Java_NativeAudioHarness_nativePause(JNIEnv *env, jclass cls) {
    (void)env; (void)cls; return native_audio_pause(g_audio);
}
JNIEXPORT void JNICALL Java_NativeAudioHarness_nativeSetFocus(JNIEnv *env, jclass cls, jint focused) {
    (void)env; (void)cls; native_audio_set_focus(g_audio, focused);
}
JNIEXPORT jint JNICALL Java_NativeAudioHarness_nativeTick(JNIEnv *env, jclass cls) {
    (void)cls; return native_audio_tick(g_audio, env);
}
JNIEXPORT jint JNICALL Java_NativeAudioHarness_nativeState(JNIEnv *env, jclass cls) {
    (void)env; (void)cls; return native_audio_output_state(g_audio);
}
JNIEXPORT jint JNICALL Java_NativeAudioHarness_nativeHasSession(JNIEnv *env, jclass cls) {
    (void)env; (void)cls; return native_audio_has_session(g_audio);
}
JNIEXPORT jint JNICALL Java_NativeAudioHarness_nativeLastError(JNIEnv *env, jclass cls) {
    (void)env; (void)cls; return native_audio_last_error(g_audio);
}
JNIEXPORT jint JNICALL Java_NativeAudioHarness_nativeDestroy(JNIEnv *env, jclass cls) {
    (void)cls; return native_audio_destroy(&g_audio, env);
}
JNIEXPORT jint JNICALL Java_NativeAudioHarness_nativeDestroyNoEnv(JNIEnv *env, jclass cls) {
    (void)env; (void)cls; return native_audio_destroy(&g_audio, NULL);
}
JNIEXPORT jint JNICALL Java_NativeAudioHarness_nativeReference(JNIEnv *env, jclass cls, jlong phase,
                                                               jshortArray out, jint frames) {
    (void)cls;
    if (!out || frames <= 0 || frames > 64) return 0;
    int16_t pcm[64 * DM_AUDIO_CHANNELS];
    native_audio_reference_pcm((uint64_t)phase, pcm, frames);
    (*env)->SetShortArrayRegion(env, out, 0, frames * DM_AUDIO_CHANNELS, pcm);
    return frames;
}
