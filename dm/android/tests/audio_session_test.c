// #195 host JVM 入口 + 窄 AAudio fixture：驱动真实 audio_session + dm_afj + dm_audio_output/dm_audio/policy。
// #195 host JVM entry plus narrow AAudio fixture driving the real audio_session + dm_afj + dm_audio_output stack.
#include "audio_focus.h"
#include "audio_session.h"

#include <stdlib.h>
#include <string.h>

static dm_audio_session *g_session;

struct dm_aaudio_builder {
    int32_t format, channels, rate, direction;
    dm_audio_data_callback data;
    dm_audio_error_callback error;
    void *user;
};
struct dm_aaudio_stream {
    int state;
    dm_audio_data_callback data;
    void *user;
};

static int g_open_calls, g_start_calls, g_close_calls;
static int g_start_fail, g_close_fail_once;
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
    struct dm_aaudio_stream *s = calloc(1, sizeof *s);
    if (!s) return -1;
    s->state = DM_AUDIO_STATE_STOPPED;
    s->data = b->data;
    s->user = b->user;
    g_stream = s;
    g_open_calls++;
    *out = s;
    return DM_AUDIO_OK;
}
static dm_audio_result fx_close(dm_aaudio_stream *s) {
    g_close_calls++;
    if (g_close_fail_once && g_close_calls == 1) return -1;
    free(s);
    if (g_stream == s) g_stream = NULL;
    return DM_AUDIO_OK;
}
static dm_audio_result fx_start(dm_aaudio_stream *s) {
    g_start_calls++;
    if (g_start_fail) return -1;
    s->state = 2;
    return DM_AUDIO_OK;
}
static dm_audio_result fx_pause(dm_aaudio_stream *s) { s->state = 3; return DM_AUDIO_OK; }
static dm_audio_result fx_stop(dm_aaudio_stream *s) { s->state = DM_AUDIO_STATE_STOPPED; return DM_AUDIO_OK; }
static dm_audio_result fx_state(dm_aaudio_stream *s) { return s->state; }
static dm_audio_result fx_wait(dm_aaudio_stream *s, int32_t current, int32_t *next, int64_t timeout) {
    (void)s; (void)current; (void)timeout;
    *next = DM_AUDIO_STATE_STOPPED;
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

JNIEXPORT void JNICALL Java_AudioSessionHarness_nativeFixtureReset(JNIEnv *env, jclass cls) {
    (void)env; (void)cls;
    g_open_calls = g_start_calls = g_close_calls = 0;
    g_start_fail = g_close_fail_once = 0;
    g_stream = NULL;
}
JNIEXPORT void JNICALL Java_AudioSessionHarness_nativeFixtureCloseFailOnce(JNIEnv *env, jclass cls) {
    (void)env; (void)cls;
    g_close_fail_once = 1;
}
JNIEXPORT jint JNICALL Java_AudioSessionHarness_nativeFixtureOpens(JNIEnv *env, jclass cls) {
    (void)env; (void)cls; return g_open_calls;
}
JNIEXPORT jint JNICALL Java_AudioSessionHarness_nativeFixtureStarts(JNIEnv *env, jclass cls) {
    (void)env; (void)cls; return g_start_calls;
}
JNIEXPORT jint JNICALL Java_AudioSessionHarness_nativeFixtureCloses(JNIEnv *env, jclass cls) {
    (void)env; (void)cls; return g_close_calls;
}

JNIEXPORT jlong JNICALL Java_AudioSessionHarness_nativeCreate(JNIEnv *env, jclass cls, jobject context,
                                                              jint queue_frames) {
    (void)cls;
    if (g_session) return 0;
    dm_audio_session_options options = DM_AUDIO_SESSION_OPTIONS_INIT;
    options.audio_api = &k_fixture;
    options.queue_frames = (uint32_t)(queue_frames > 1 ? queue_frames : 2);
    dm_audio_session *session = NULL;
    if (dm_audio_session_create(env, context, &options, &session) != DM_AS_OK || !session) return 0;
    g_session = session;
    return 1;
}

JNIEXPORT jint JNICALL Java_AudioSessionHarness_nativePlay(JNIEnv *env, jclass cls) {
    (void)env; (void)cls;
    if (!g_session) return DM_AS_E_INVALID;
    int platform = 0;
    const int rc = dm_audio_session_play(g_session, &platform);
    return rc == DM_AS_OK ? platform : rc;
}

JNIEXPORT jint JNICALL Java_AudioSessionHarness_nativeResume(JNIEnv *env, jclass cls) {
    (void)env; (void)cls; return g_session ? dm_audio_session_resume(g_session) : DM_AS_E_INVALID;
}
JNIEXPORT jint JNICALL Java_AudioSessionHarness_nativePause(JNIEnv *env, jclass cls) {
    (void)env; (void)cls; return g_session ? dm_audio_session_pause(g_session) : DM_AS_E_INVALID;
}
JNIEXPORT jint JNICALL Java_AudioSessionHarness_nativeStop(JNIEnv *env, jclass cls) {
    (void)env; (void)cls; return g_session ? dm_audio_session_stop(g_session) : DM_AS_E_INVALID;
}
JNIEXPORT jint JNICALL Java_AudioSessionHarness_nativeDrain(JNIEnv *env, jclass cls) {
    (void)env; (void)cls; return g_session ? dm_audio_session_drain(g_session) : DM_AS_E_INVALID;
}

JNIEXPORT jint JNICALL Java_AudioSessionHarness_nativeWrite(JNIEnv *env, jclass cls, jshortArray samples,
                                                            jint frames) {
    (void)cls;
    if (!env || !samples || !g_session || frames <= 0) return 0;
    jsize capacity = (*env)->GetArrayLength(env, samples);
    if (capacity < frames * DM_AUDIO_CHANNELS) return 0;
    int16_t *pcm = malloc((size_t)frames * DM_AUDIO_FRAME_BYTES);
    if (!pcm) return 0;
    (*env)->GetShortArrayRegion(env, samples, 0, (jsize)(frames * DM_AUDIO_CHANNELS), (jshort *)pcm);
    const uint32_t written = dm_audio_session_write(g_session, pcm, (uint32_t)frames);
    free(pcm);
    return (jint)written;
}

JNIEXPORT jint JNICALL Java_AudioSessionHarness_nativePump(JNIEnv *env, jclass cls, jshortArray out, jint frames) {
    (void)cls;
    if (!env || !out || !g_stream || !g_stream->data || frames <= 0) return 0;
    jsize capacity = (*env)->GetArrayLength(env, out);
    if (capacity < frames * DM_AUDIO_CHANNELS) return 0;
    int16_t *pcm = calloc((size_t)frames * DM_AUDIO_CHANNELS, sizeof *pcm);
    if (!pcm) return 0;
    const dm_audio_callback_result result = g_stream->data(g_stream, g_stream->user, pcm, frames);
    (*env)->SetShortArrayRegion(env, out, 0, (jsize)(frames * DM_AUDIO_CHANNELS), (jshort *)pcm);
    free(pcm);
    return result == DM_AUDIO_CALLBACK_CONTINUE ? 1 : 0;
}

JNIEXPORT jint JNICALL Java_AudioSessionHarness_nativeDestroy(JNIEnv *env, jclass cls) {
    (void)env; (void)cls;
    if (!g_session) return DM_AS_E_INVALID;
    const int rc = dm_audio_session_destroy(&g_session);
    return rc;
}

JNIEXPORT jint JNICALL Java_AudioSessionHarness_nativeState(JNIEnv *env, jclass cls) {
    (void)env; (void)cls; return g_session ? dm_audio_session_output_state(g_session) : DM_AF_OUT_STOPPED;
}
JNIEXPORT jint JNICALL Java_AudioSessionHarness_nativeFocusGeneration(JNIEnv *env, jclass cls) {
    (void)env; (void)cls; return g_session ? dm_audio_session_focus_generation(g_session) : 0;
}
JNIEXPORT jint JNICALL Java_AudioSessionHarness_nativeLastError(JNIEnv *env, jclass cls) {
    (void)env; (void)cls; return g_session ? dm_audio_session_last_error(g_session) : DM_AS_E_INVALID;
}
JNIEXPORT jint JNICALL Java_AudioSessionHarness_nativeHasFocus(JNIEnv *env, jclass cls) {
    (void)env; (void)cls; return g_session ? dm_audio_session_has_focus_owner(g_session) : 0;
}
JNIEXPORT jint JNICALL Java_AudioSessionHarness_nativeHasAudio(JNIEnv *env, jclass cls) {
    (void)env; (void)cls; return g_session ? dm_audio_session_has_audio_handle(g_session) : 0;
}
JNIEXPORT jint JNICALL Java_AudioSessionHarness_nativeClosed(JNIEnv *env, jclass cls) {
    (void)env; (void)cls; return g_session ? dm_audio_session_is_closed(g_session) : 1;
}
