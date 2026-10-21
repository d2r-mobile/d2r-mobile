// #163 host tests: real dm_af_policy + real dm_audio, only the narrow AAudio boundary replaced.
// Verifies actual PCM output at unity/duck/mute, queued-before-gain-change frames, intent/generation/lifecycle and
// handle ownership for create -EAGAIN, failed start and failed close.
#include "../app/audio_output.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    dm_audio_data_callback data;
    dm_audio_error_callback error;
    void *user;
};

static int g_start_fail, g_pause_fail, g_stop_fail, g_close_fail_once, g_mismatch, g_mismatch_close_fails;
static int g_close_calls, g_start_calls, g_pause_calls, g_stop_calls, g_open_calls;
static struct dm_aaudio_stream *g_stream;

static dm_audio_result sb_create_builder(dm_aaudio_builder **b) {
    *b = calloc(1, sizeof **b);
    return *b ? DM_AUDIO_OK : -1;
}
static dm_audio_result sb_delete_builder(dm_aaudio_builder *b) { free(b); return DM_AUDIO_OK; }
static void sb_set_format(dm_aaudio_builder *b, int32_t v) { b->format = v; }
static void sb_set_channels(dm_aaudio_builder *b, int32_t v) { b->channels = v; }
static void sb_set_rate(dm_aaudio_builder *b, int32_t v) { b->rate = v; }
static void sb_set_direction(dm_aaudio_builder *b, int32_t v) { b->direction = v; }
static void sb_set_data(dm_aaudio_builder *b, dm_audio_data_callback cb, void *user) { b->data = cb; b->user = user; }
static void sb_set_error(dm_aaudio_builder *b, dm_audio_error_callback cb, void *user) { b->error = cb; b->user = user; }
static dm_audio_result sb_open(dm_aaudio_builder *b, dm_aaudio_stream **out) {
    struct dm_aaudio_stream *s = calloc(1, sizeof *s);
    if (!s) return -1;
    s->state = DM_AUDIO_STATE_STOPPED;
    s->data = b->data;
    s->error = b->error;
    s->user = b->user;
    g_stream = s;
    g_open_calls++;
    *out = s;
    return DM_AUDIO_OK;
}
static dm_audio_result sb_close(dm_aaudio_stream *s) {
    g_close_calls++;
    if (g_mismatch_close_fails > 0) { g_mismatch_close_fails--; return -1; }
    if (g_close_fail_once && g_close_calls == 1) return -1;
    free(s);
    if (g_stream == s) g_stream = NULL;
    return DM_AUDIO_OK;
}
static dm_audio_result sb_start(dm_aaudio_stream *s) {
    g_start_calls++;
    if (g_start_fail) return -1;
    s->state = 2;   // STARTED
    return DM_AUDIO_OK;
}
static dm_audio_result sb_pause(dm_aaudio_stream *s) {
    g_pause_calls++;
    if (g_pause_fail) return -1;
    s->state = 3;   // PAUSED
    return DM_AUDIO_OK;
}
static dm_audio_result sb_stop(dm_aaudio_stream *s) {
    g_stop_calls++;
    if (g_stop_fail) return -1;
    s->state = DM_AUDIO_STATE_STOPPED;
    return DM_AUDIO_OK;
}
static dm_audio_result sb_get_state(dm_aaudio_stream *s) { return s->state; }
static dm_audio_result sb_wait(dm_aaudio_stream *s, int32_t current, int32_t *next, int64_t timeout) {
    (void)s; (void)current; (void)timeout;
    *next = DM_AUDIO_STATE_STOPPED;
    return DM_AUDIO_OK;
}
static dm_audio_result sb_get_format(dm_aaudio_stream *s) { (void)s; return g_mismatch ? 99 : DM_AUDIO_FORMAT_PCM16; }
static dm_audio_result sb_get_channels(dm_aaudio_stream *s) { (void)s; return DM_AUDIO_CHANNELS; }
static dm_audio_result sb_get_rate(dm_aaudio_stream *s) { (void)s; return DM_AUDIO_RATE; }
static dm_audio_result sb_get_buffer(dm_aaudio_stream *s) { (void)s; return 64; }
static dm_audio_result sb_get_xruns(dm_aaudio_stream *s) { (void)s; return 0; }

static const dm_audio_api k_api = {
    sb_create_builder, sb_delete_builder, sb_set_format, sb_set_channels, sb_set_rate, sb_set_direction,
    sb_set_data, sb_set_error, sb_open, sb_close, sb_start, sb_pause, sb_stop, sb_get_state, sb_wait,
    sb_get_format, sb_get_channels, sb_get_rate, sb_get_buffer, sb_get_xruns,
};

// 与实现相同的、已文档化的量化规则，用作独立期望值。| Same documented quantisation rule as an independent expectation.
static int16_t expect_gain(int16_t sample, uint32_t q15) {
    if (q15 >= 32768u) return sample;
    int32_t v = (int32_t)sample * (int32_t)q15;
    v = (v + 16384) >> 15;
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    return (int16_t)v;
}

static void reset_stub(void) {
    g_start_fail = g_pause_fail = g_stop_fail = g_close_fail_once = g_mismatch = g_mismatch_close_fails = 0;
    g_close_calls = g_start_calls = g_pause_calls = g_stop_calls = g_open_calls = 0;
    g_stream = NULL;
}

static void init_output(dm_audio_output *o, int duck_enabled, float duck_gain) {
    dm_audio_options options = DM_AUDIO_OPTIONS_INIT;
    options.queue_frames = 8;
    options.api = &k_api;
    CHECK(dm_audio_output_init(o, &options, duck_enabled, duck_gain) == 0);
}

static void play_baseline(dm_audio_output *o) {
    CHECK(dm_audio_output_handle(o, DM_AF_EV_APP_RESUME) == DM_AF_OK);
    CHECK(dm_audio_output_handle(o, DM_AF_EV_USER_PLAY) == DM_AF_OK);
    CHECK(dm_audio_output_handle(o, DM_AF_EV_FOCUS_GAIN) == DM_AF_OK);
}

// 真实 data callback，读取实际输出 PCM。| Runs the real data callback and returns the actual PCM.
static int pump(dm_audio_output *o, int16_t *out, int frames) {
    (void)o;
    if (!g_stream || !g_stream->data) return 0;
    memset(out, 0x7f, (size_t)frames * DM_AUDIO_FRAME_BYTES);
    const dm_audio_callback_result r = g_stream->data(g_stream, g_stream->user, out, frames);
    return r == DM_AUDIO_CALLBACK_CONTINUE ? 1 : 0;
}

static void test_unity_and_duck_pcm(void) {
    reset_stub();
    dm_audio_output o;
    init_output(&o, 1, 0.5f);
    play_baseline(&o);
    CHECK(dm_audio_output_has_handle(&o) == 1);
    CHECK(o.starts == 1 && g_start_calls == 1);

    static const int16_t src[6] = {1000, -1000, 32767, -32768, 3, -3};
    CHECK(dm_audio_output_write(&o, src, 3) == 3);
    int16_t out[6];
    CHECK(pump(&o, out, 3) == 1);
    for (int i = 0; i < 6; i++) CHECK(out[i] == src[i]);   // 1.0 位精确 | unity is bit-exact

    // duck 在真实回调里生效。| Ducking is applied inside the real callback.
    CHECK(dm_audio_output_handle(&o, DM_AF_EV_FOCUS_CAN_DUCK) == DM_AF_OK);
    CHECK(dm_audio_gain_q15(dm_audio_output_audio(&o)) == 16384u);
    CHECK(dm_audio_output_write(&o, src, 3) == 3);
    CHECK(pump(&o, out, 3) == 1);
    for (int i = 0; i < 6; i++) CHECK(out[i] == expect_gain(src[i], 16384u));

    // 恢复 1.0：再次位精确。| Restored unity is bit-exact again.
    CHECK(dm_audio_output_handle(&o, DM_AF_EV_FOCUS_GAIN) == DM_AF_OK);
    CHECK(dm_audio_output_write(&o, src, 3) == 3);
    CHECK(pump(&o, out, 3) == 1);
    for (int i = 0; i < 6; i++) CHECK(out[i] == src[i]);

    // 已入队但尚未回调的数据服从最新音量。| Queued-but-not-yet-delivered frames obey the latest gain.
    CHECK(dm_audio_output_write(&o, src, 3) == 3);
    CHECK(dm_audio_output_handle(&o, DM_AF_EV_FOCUS_CAN_DUCK) == DM_AF_OK);
    CHECK(pump(&o, out, 3) == 1);
    for (int i = 0; i < 6; i++) CHECK(out[i] == expect_gain(src[i], 16384u));

    CHECK(dm_audio_output_destroy(&o) == 0);
    CHECK(dm_audio_output_has_handle(&o) == 0);
}

static void test_mute_and_invalid_gain(void) {
    reset_stub();
    dm_audio_output o;
    init_output(&o, 0, 0.5f);
    play_baseline(&o);
    dm_audio *audio = dm_audio_output_audio(&o);
    CHECK(audio != NULL);

    static const int16_t src[4] = {1234, -4321, 32767, -32768};
    CHECK(dm_audio_output_write(&o, src, 2) == 2);
    CHECK(dm_audio_set_gain(audio, 0.0f) == 0);       // 有意静音，不是 underrun | intentional mute, not underrun
    int16_t out[4];
    CHECK(pump(&o, out, 2) == 1);
    for (int i = 0; i < 4; i++) CHECK(out[i] == 0);
    dm_audio_stats stats;
    dm_audio_get_stats(audio, &stats);
    CHECK(stats.underrun_frames == 0);
    CHECK(stats.underrun_events == 0);

    // 非法值不改变现有音量。| Invalid values leave the current gain unchanged.
    CHECK(dm_audio_set_gain(audio, 1.5f) == -EINVAL);
    CHECK(dm_audio_set_gain(audio, -0.1f) == -EINVAL);
    CHECK(dm_audio_set_gain(audio, NAN) == -EINVAL);
    CHECK(dm_audio_gain_q15(audio) == 0u);
    CHECK(dm_audio_output_destroy(&o) == 0);
}

static void test_intent_generation_and_lifecycle(void) {
    reset_stub();
    dm_audio_output o;
    init_output(&o, 0, 0.5f);
    play_baseline(&o);
    const uint32_t generation = o.generation;

    // App 暂停/恢复落到真实模块。| App pause/resume reach the real module.
    CHECK(dm_audio_output_handle(&o, DM_AF_EV_APP_PAUSE) == DM_AF_OK);
    CHECK(g_pause_calls == 1);
    CHECK(dm_audio_output_handle(&o, DM_AF_EV_APP_RESUME) == DM_AF_OK);
    CHECK(g_start_calls == 2);

    // 迟到旧代次错误被拒绝，且不触碰音频。| A late error from an old generation is rejected without touching audio.
    CHECK(dm_audio_output_handle_generation(&o, generation, DM_AF_EV_BACKEND_ERROR) == DM_AF_STALE);
    CHECK(o.stale_events == 1);
    CHECK(dm_audio_output_has_handle(&o) == 1);

    // USER_STOP 后迟到的 gain 不起播。| A late gain after USER_STOP does not restart.
    CHECK(dm_audio_output_handle(&o, DM_AF_EV_USER_STOP) == DM_AF_OK);
    const uint32_t starts = o.starts;
    CHECK(dm_audio_output_handle(&o, DM_AF_EV_FOCUS_GAIN) == DM_AF_OK);
    CHECK(o.starts == starts);
    CHECK(g_start_calls == 2);
    // 句柄仍由 owner 持有（策略停止 ≠ 释放句柄），最终由 destroy 退休。
    // The handle is still owned here (policy stop is not a release) and destroy retires it.
    CHECK(dm_audio_output_has_handle(&o) == 1);
    CHECK(dm_audio_output_handle_generation(&o, o.policy.generation, DM_AF_EV_BACKEND_ERROR) == DM_AF_OK);
    CHECK(dm_audio_output_destroy(&o) == 0);
    CHECK(g_close_calls >= 1);
}

static void test_failed_start_retains_handle(void) {
    reset_stub();
    dm_audio_output o;
    init_output(&o, 0, 0.5f);
    CHECK(dm_audio_output_handle(&o, DM_AF_EV_APP_RESUME) == DM_AF_OK);
    CHECK(dm_audio_output_handle(&o, DM_AF_EV_USER_PLAY) == DM_AF_OK);
    g_start_fail = 1;
    CHECK(dm_audio_output_handle(&o, DM_AF_EV_FOCUS_GAIN) == DM_AF_BACKEND_REJECTED);
    CHECK(o.policy.output == DM_AF_OUT_ERROR);
    CHECK(dm_audio_output_has_handle(&o) == 1);        // start 失败仍保留已打开流 | open stream retained
    CHECK(o.start_failures == 1);
    g_start_fail = 0;
    CHECK(dm_audio_output_destroy(&o) == 0);           // 最终可退休 | eventually retired
    CHECK(dm_audio_output_has_handle(&o) == 0);
    CHECK(g_close_calls >= 1);
}

static void test_create_eagain_cleanup_handle(void) {
    reset_stub();
    g_mismatch = 1;
    g_mismatch_close_fails = 2;   // create 回滚一次 + 首次 destroy 一次 | one rollback close plus the first destroy close
    dm_audio_output o;
    init_output(&o, 0, 0.5f);
    CHECK(dm_audio_output_handle(&o, DM_AF_EV_APP_RESUME) == DM_AF_OK);
    CHECK(dm_audio_output_handle(&o, DM_AF_EV_USER_PLAY) == DM_AF_OK);
    CHECK(dm_audio_output_handle(&o, DM_AF_EV_FOCUS_GAIN) == DM_AF_BACKEND_REJECTED);
    CHECK(o.create_cleanups == 1);
    CHECK(o.cleanup_only == 1);
    CHECK(dm_audio_output_has_handle(&o) == 1);        // 清理句柄保留 | cleanup handle retained
    // 第一次退休失败：句柄保留可重试。| The first retirement fails and the handle is retained.
    CHECK(dm_audio_output_destroy(&o) != 0);
    CHECK(dm_audio_output_has_handle(&o) == 1);
    CHECK(dm_audio_output_destroy(&o) == 0);           // 串行重试成功 | serialized retry succeeds
    CHECK(dm_audio_output_has_handle(&o) == 0);
}

static void test_destroy_close_failure_retry(void) {
    reset_stub();
    dm_audio_output o;
    init_output(&o, 0, 0.5f);
    play_baseline(&o);
    g_close_fail_once = 1;
    CHECK(dm_audio_output_destroy(&o) != 0);
    CHECK(dm_audio_output_has_handle(&o) == 1);
    CHECK(o.destroy_retries == 1);
    CHECK(dm_audio_output_destroy(&o) == 0);
    CHECK(dm_audio_output_has_handle(&o) == 0);
}

static void test_error_replacement_gain(void) {
    reset_stub();
    dm_audio_output o;
    init_output(&o, 1, 0.5f);
    play_baseline(&o);
    CHECK(dm_audio_output_handle(&o, DM_AF_EV_FOCUS_CAN_DUCK) == DM_AF_OK);
    CHECK(dm_audio_output_handle_generation(&o, o.generation, DM_AF_EV_BACKEND_ERROR) == DM_AF_OK);
    CHECK(dm_audio_output_handle(&o, DM_AF_EV_FOCUS_GAIN) == DM_AF_OK);
    CHECK(dm_audio_output_handle(&o, DM_AF_EV_USER_PLAY) == DM_AF_OK);
    CHECK(o.starts == 2);
    CHECK(dm_audio_gain_q15(dm_audio_output_audio(&o)) == 32768u);   // 新流收到普通音量 | replacement gets unity
    static const int16_t src[2] = {2000, -2000};
    CHECK(dm_audio_output_write(&o, src, 1) == 1);
    int16_t out[2];
    CHECK(pump(&o, out, 1) == 1);
    CHECK(out[0] == 2000 && out[1] == -2000);
    CHECK(dm_audio_output_destroy(&o) == 0);
}

static void test_actual_disconnect_replacement(void) {
    reset_stub();
    dm_audio_output o;
    init_output(&o, 1, 0.5f);
    play_baseline(&o);
    CHECK(dm_audio_output_handle(&o, DM_AF_EV_FOCUS_CAN_DUCK) == DM_AF_OK);
    dm_audio *broken = dm_audio_output_audio(&o);
    struct dm_aaudio_stream *old_stream = g_stream;
    CHECK(broken != NULL && old_stream != NULL);

    // 真实 AAudio error callback 断流；不用策略事件代替真实断流。| The real AAudio error callback disconnects.
    CHECK(old_stream->error != NULL);
    old_stream->error(old_stream, old_stream->user, -899);
    dm_audio_stats stats;
    dm_audio_get_stats(broken, &stats);
    CHECK(stats.disconnected == 1);

    // 迟到的策略错误只更新状态；随后显式 USER_PLAY 必须退休坏流并真正新建流恢复 PCM。
    // The policy error only updates state; explicit USER_PLAY must retire the bad stream and open a fresh one.
    CHECK(dm_audio_output_handle_generation(&o, o.policy.generation, DM_AF_EV_BACKEND_ERROR) == DM_AF_OK);
    CHECK(o.policy.output == DM_AF_OUT_ERROR);
    CHECK(dm_audio_output_handle(&o, DM_AF_EV_USER_PLAY) == DM_AF_OK);
    CHECK(o.opens == 2);
    CHECK(g_open_calls == 2);
    CHECK(g_stream != NULL && g_stream != old_stream);
    CHECK(dm_audio_output_audio(&o) != broken);
    CHECK(o.replacements == 1);

    static const int16_t src[4] = {2000, -2000, 4000, -4000};
    CHECK(dm_audio_output_write(&o, src, 2) == 2);
    int16_t out[4];
    CHECK(pump(&o, out, 2) == 1);
    for (int i = 0; i < 4; i++) CHECK(out[i] == expect_gain(src[i], 16384u));   // 新流也服从 duck
    CHECK(dm_audio_output_destroy(&o) == 0);
}

static void test_disconnect_stop_failure_retry(void) {
    reset_stub();
    dm_audio_output o;
    init_output(&o, 0, 0.5f);
    play_baseline(&o);
    struct dm_aaudio_stream *old_stream = g_stream;
    old_stream->error(old_stream, old_stream->user, -899);

    // 坏流的 stop 请求失败：仍必须安全 close/释放；首次退休返回失败、保留句柄，绝不把坏流当已恢复。
    // The bad stream's stop request fails: close/free must still happen; the first retirement fails and retains.
    g_stop_fail = 1;
    CHECK(dm_audio_output_handle_generation(&o, o.policy.generation, DM_AF_EV_BACKEND_ERROR) == DM_AF_OK);
    CHECK(dm_audio_output_handle(&o, DM_AF_EV_USER_PLAY) == DM_AF_BACKEND_REJECTED);
    CHECK(o.policy.output == DM_AF_OUT_ERROR);
    CHECK(dm_audio_output_has_handle(&o) == 1);
    CHECK(o.opens == 1);
    CHECK(o.retire_failures >= 1);

    // 串行重试完成释放并建立真正的新流。| A serialized retry completes the release and opens a fresh stream.
    CHECK(dm_audio_output_handle(&o, DM_AF_EV_USER_PLAY) == DM_AF_OK);
    CHECK(o.opens == 2);
    CHECK(dm_audio_output_audio(&o) != NULL);
    static const int16_t src[2] = {700, -700};
    CHECK(dm_audio_output_write(&o, src, 1) == 1);
    int16_t out[2];
    CHECK(pump(&o, out, 1) == 1);
    CHECK(out[0] == 700 && out[1] == -700);
    // stop 仍处失败态：destroy 如实报告 stop 未证明、close 已发生，句柄需再串行重试释放。
    // The stop still fails: destroy reports the unproven stop, close already happened, and a retry releases it.
    CHECK(dm_audio_output_destroy(&o) != 0);
    CHECK(dm_audio_output_has_handle(&o) == 1);
    CHECK(dm_audio_output_destroy(&o) == 0);
    CHECK(dm_audio_output_has_handle(&o) == 0);
}

static void test_disconnect_close_failure_retry(void) {
    reset_stub();
    dm_audio_output o;
    init_output(&o, 0, 0.5f);
    play_baseline(&o);
    struct dm_aaudio_stream *old_stream = g_stream;
    old_stream->error(old_stream, old_stream->user, -899);

    // 坏流 close 失败：句柄保留，策略如实报错；重试后 close 成功并新建流。
    // A failed close on the bad stream retains the handle and reports failure; retry closes and opens afresh.
    g_close_fail_once = 1;
    CHECK(dm_audio_output_handle_generation(&o, o.policy.generation, DM_AF_EV_BACKEND_ERROR) == DM_AF_OK);
    CHECK(dm_audio_output_handle(&o, DM_AF_EV_USER_PLAY) == DM_AF_BACKEND_REJECTED);
    CHECK(dm_audio_output_has_handle(&o) == 1);
    CHECK(o.opens == 1);
    CHECK(dm_audio_output_handle(&o, DM_AF_EV_USER_PLAY) == DM_AF_OK);
    CHECK(o.opens == 2);
    CHECK(dm_audio_output_has_handle(&o) == 1);
    static const int16_t src[2] = {900, -900};
    CHECK(dm_audio_output_write(&o, src, 1) == 1);
    int16_t out[2];
    CHECK(pump(&o, out, 1) == 1);
    CHECK(out[0] == 900 && out[1] == -900);
    CHECK(dm_audio_output_destroy(&o) == 0);
}

int main(void) {
    test_unity_and_duck_pcm();
    test_mute_and_invalid_gain();
    test_intent_generation_and_lifecycle();
    test_failed_start_retains_handle();
    test_create_eagain_cleanup_handle();
    test_destroy_close_failure_retry();
    test_error_replacement_gain();
    test_actual_disconnect_replacement();
    test_disconnect_stop_failure_retry();
    test_disconnect_close_failure_retry();
    printf("audio_output: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
