// #153 宿主测试：驱动真实策略控制器，覆盖代次、用户意图、生命周期、duck 与后端失败组合。
// 后端是注入的假实现，会记录每次调用并可注入失败；策略必须诚实反映后端是否接受。
// #153 host tests: drive the real policy controller across generation, user intent, lifecycle, ducking and backend
// failures. The backend is an injected fake that records every call and can fail; the policy must report honestly.
#include "../app/audio_focus.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int checks, failures;
#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

typedef struct {
    int calls, starts, pauses, stops, gain_calls;
    int held;                   // 后端是否仍持有流资源 | whether the backend still holds the stream resource
    float gain;                 // 后端此刻真实的音量 | gain actually held by the backend
    uint32_t last_start_gen;
    float last_gain;
    int fail_start, fail_pause, fail_stop, fail_gain;
} fake_backend;

static int fb_start(void *ctx, uint32_t generation) {
    fake_backend *f = ctx; f->calls++; f->starts++; f->last_start_gen = generation;
    if (f->fail_start) return 1;
    if (!f->held) f->gain = 1.0f;   // 新流从后端默认音量开始，不继承旧流的增益 | a new stream starts at backend default
    f->held = 1;
    return 0;
}
static int fb_pause(void *ctx) { fake_backend *f = ctx; f->calls++; f->pauses++; return f->fail_pause; }
static int fb_stop(void *ctx) {
    fake_backend *f = ctx; f->calls++; f->stops++;
    if (f->fail_stop) return 1;      // 清理失败：资源仍归后端所有 | failed cleanup: the backend still owns the resource
    f->held = 0;
    return 0;
}
static int fb_gain(void *ctx, float gain) {
    fake_backend *f = ctx; f->calls++; f->gain_calls++;
    if (f->fail_gain) return 1;
    f->last_gain = gain;
    f->gain = gain;
    return 0;
}

static void init_policy(dm_af_policy *p, fake_backend *f, int duck_enabled, float duck_gain) {
    memset(p, 0, sizeof *p);
    memset(f, 0, sizeof *f);
    dm_af_backend backend = {.ctx = f, .start = fb_start, .pause = fb_pause, .stop = fb_stop, .set_gain = fb_gain};
    CHECK(dm_af_init(p, &backend, duck_enabled, duck_gain) == 0);
}

// 走到"前台 + 用户播放 + 焦点已获得 + 正在播放"的基线状态。
// Reach the baseline: foreground, user intent, focus granted, actually playing.
static void play_baseline(dm_af_policy *p, fake_backend *f) {
    CHECK(dm_af_handle(p, DM_AF_EV_APP_RESUME) == DM_AF_OK);
    CHECK(dm_af_handle(p, DM_AF_EV_USER_PLAY) == DM_AF_OK);
    CHECK(p->output == DM_AF_OUT_PAUSED_FOCUS);     // 还没有焦点：等待而不是停止 | no focus yet: waiting, not stopped
    CHECK(f->starts == 0);
    CHECK(dm_af_handle(p, DM_AF_EV_FOCUS_GAIN) == DM_AF_OK);
    CHECK(p->output == DM_AF_OUT_PLAYING);
    CHECK(f->starts == 1);
    CHECK(p->generation == f->last_start_gen);
}

static void test_init_and_duck_bounds(void) {
    dm_af_policy p;
    fake_backend f;
    init_policy(&p, &f, 1, 1.0f);
    CHECK(p.duck_enabled == 1);
    CHECK(p.output == DM_AF_OUT_STOPPED);
    CHECK(p.app_active == 0);
    CHECK(dm_af_set_duck(&p, 1, 1.0f) == 0);
    CHECK(dm_af_set_duck(&p, 1, 0.0f) == -1);        // 0 不是有界增益 | 0 is not a bounded gain
    CHECK(dm_af_set_duck(&p, 1, 1.5f) == -1);
    CHECK(dm_af_set_duck(&p, 1, NAN) == -1);
    CHECK(p.duck_gain == 1.0f);                      // 非法值不改变配置 | invalid values change nothing

    dm_af_backend missing = {0};
    CHECK(dm_af_init(&p, &missing, 0, 0.5f) == -1);  // 缺少注入操作就拒绝 | refuse without injected operations
    dm_af_backend backend = {.ctx = &f, .start = fb_start, .pause = fb_pause, .stop = fb_stop, .set_gain = fb_gain};
    CHECK(dm_af_init(&p, &backend, 0, 0.25f) == 0);
    CHECK(p.duck_enabled == 0);
}

static void test_focus_gain_and_duplicates(void) {
    dm_af_policy p; fake_backend f;
    init_policy(&p, &f, 0, 0.2f);
    play_baseline(&p, &f);
    // 重复的 FOCUS_GAIN 不得再次 start。| A duplicate FOCUS_GAIN must not start again.
    CHECK(dm_af_handle(&p, DM_AF_EV_FOCUS_GAIN) == DM_AF_OK);
    CHECK(f.starts == 1);
    // 重复的 USER_PLAY 也不产生新的后端调用。| A duplicate USER_PLAY produces no backend call either.
    CHECK(dm_af_handle(&p, DM_AF_EV_USER_PLAY) == DM_AF_OK);
    CHECK(f.starts == 1);
    CHECK(p.output == DM_AF_OUT_PLAYING);
}

static void test_transient_loss_and_gain(void) {
    dm_af_policy p; fake_backend f;
    init_policy(&p, &f, 0, 0.2f);
    play_baseline(&p, &f);
    uint32_t generation_before = p.generation;

    CHECK(dm_af_handle(&p, DM_AF_EV_FOCUS_LOSS_TRANSIENT) == DM_AF_OK);
    CHECK(p.output == DM_AF_OUT_PAUSED_FOCUS);
    CHECK(f.pauses == 1);
    CHECK(f.stops == 0);
    CHECK(p.desired_play == 1);                     // 意图保留 | intent is kept
    // 重复的暂时丢失：不再 pause。| Duplicate transient loss: no second pause.
    CHECK(dm_af_handle(&p, DM_AF_EV_FOCUS_LOSS_TRANSIENT) == DM_AF_OK);
    CHECK(f.pauses == 1);
    CHECK(f.starts == 1);

    // 恢复：如果意图与前台允许，则重新 start 并拿到新代次。
    // Recovery: with intent and foreground permitting, playback starts again with a new generation.
    CHECK(dm_af_handle(&p, DM_AF_EV_FOCUS_GAIN) == DM_AF_OK);
    CHECK(p.output == DM_AF_OUT_PLAYING);
    CHECK(f.starts == 2);
    CHECK(p.generation != generation_before);
    CHECK(p.generation == f.last_start_gen);
    // 上一个代次的迟到事件必须 STALE 且无副作用。
    // A late event from the previous generation must be STALE with no side effect.
    int calls_before = f.calls;
    CHECK(dm_af_handle_generation(&p, generation_before, DM_AF_EV_BACKEND_ERROR) == DM_AF_STALE);
    CHECK(p.stale_events == 1);
    CHECK(f.calls == calls_before);
    CHECK(p.output == DM_AF_OUT_PLAYING);
}

static void test_pause_while_focus_absent_then_gain(void) {
    dm_af_policy p; fake_backend f;
    init_policy(&p, &f, 0, 0.2f);
    play_baseline(&p, &f);
    CHECK(dm_af_handle(&p, DM_AF_EV_FOCUS_LOSS_TRANSIENT) == DM_AF_OK);
    CHECK(dm_af_handle(&p, DM_AF_EV_APP_PAUSE) == DM_AF_OK);
    CHECK(p.output == DM_AF_OUT_PAUSED_LIFECYCLE);
    int starts_before = f.starts;
    // 无焦点时按 Home，焦点回来也不能起播。| Focus returns while backgrounded: no start.
    CHECK(dm_af_handle(&p, DM_AF_EV_FOCUS_GAIN) == DM_AF_OK);
    CHECK(f.starts == starts_before);
    CHECK(p.output == DM_AF_OUT_PAUSED_LIFECYCLE);
    // 回到前台且焦点已获得：恢复一次。| Returning to the foreground with focus: one resume.
    CHECK(dm_af_handle(&p, DM_AF_EV_APP_RESUME) == DM_AF_OK);
    CHECK(f.starts == starts_before + 1);
    CHECK(p.output == DM_AF_OUT_PLAYING);
}

static void test_user_stop_before_late_gain(void) {
    dm_af_policy p; fake_backend f;
    init_policy(&p, &f, 0, 0.2f);
    play_baseline(&p, &f);
    CHECK(dm_af_handle(&p, DM_AF_EV_FOCUS_LOSS_TRANSIENT) == DM_AF_OK);
    CHECK(dm_af_handle(&p, DM_AF_EV_USER_STOP) == DM_AF_OK);
    CHECK(p.output == DM_AF_OUT_STOPPED);
    CHECK(f.stops == 1);
    CHECK(p.desired_play == 0);
    uint32_t generation_after_stop = p.generation;
    int calls_before = f.calls;
    // 用户已停止后迟到的 gain 不得起播，也不得调用后端。| A late gain after user stop must not start or call the backend.
    CHECK(dm_af_handle(&p, DM_AF_EV_FOCUS_GAIN) == DM_AF_OK);
    CHECK(p.output == DM_AF_OUT_STOPPED);
    CHECK(f.calls == calls_before);
    CHECK(dm_af_handle_generation(&p, generation_after_stop, DM_AF_EV_FOCUS_GAIN) == DM_AF_OK);
    CHECK(f.calls == calls_before);
    CHECK(p.output == DM_AF_OUT_STOPPED);
}

static void test_permanent_loss_requires_user_rearm(void) {
    dm_af_policy p; fake_backend f;
    init_policy(&p, &f, 0, 0.2f);
    play_baseline(&p, &f);
    CHECK(dm_af_handle(&p, DM_AF_EV_FOCUS_LOSS) == DM_AF_OK);
    CHECK(p.output == DM_AF_OUT_STOPPED);
    CHECK(f.stops == 1);
    CHECK(p.requires_user_rearm == 1);
    CHECK(p.desired_play == 1);                      // 意图本身没被抹掉 | the intent itself is not erased
    int starts_before = f.starts;
    // 永久丢失后的 gain 不得静默恢复。| Gain after a permanent loss must not silently resume.
    CHECK(dm_af_handle(&p, DM_AF_EV_FOCUS_GAIN) == DM_AF_OK);
    CHECK(f.starts == starts_before);
    CHECK(p.output == DM_AF_OUT_STOPPED);
    // 只有新的用户播放意图才能重新武装。| Only a fresh user play re-arms playback.
    CHECK(dm_af_handle(&p, DM_AF_EV_USER_PLAY) == DM_AF_OK);
    CHECK(f.starts == starts_before + 1);
    CHECK(p.output == DM_AF_OUT_PLAYING);
    CHECK(p.requires_user_rearm == 0);
}

static void test_duck_policy(void) {
    dm_af_policy p; fake_backend f;
    // 默认不 duck：CAN_DUCK 按暂停处理。| Ducking disabled: CAN_DUCK pauses.
    init_policy(&p, &f, 0, 0.2f);
    play_baseline(&p, &f);
    CHECK(dm_af_handle(&p, DM_AF_EV_FOCUS_CAN_DUCK) == DM_AF_OK);
    CHECK(p.output == DM_AF_OUT_PAUSED_FOCUS);
    CHECK(f.pauses == 1);
    // 新流建立后必须下发一次目标音量（1.0），即使与缓存值相同。| A new stream receives its target gain once.
    CHECK(f.gain_calls == 1);
    CHECK(fabsf(f.gain - 1.0f) < 1e-6f);
    CHECK(dm_af_handle(&p, DM_AF_EV_FOCUS_GAIN) == DM_AF_OK);
    CHECK(p.output == DM_AF_OUT_PLAYING);

    // 显式启用 duck：有界增益、不暂停、gain 恢复。| Ducking enabled: bounded gain, no pause, gain restored.
    dm_af_policy d; fake_backend g;
    init_policy(&d, &g, 1, 0.25f);
    play_baseline(&d, &g);
    CHECK(dm_af_handle(&d, DM_AF_EV_FOCUS_CAN_DUCK) == DM_AF_OK);
    CHECK(d.output == DM_AF_OUT_PLAYING);
    CHECK(g.pauses == 0);
    CHECK(g.gain_calls == 2);                        // 新流的 1.0 + duck 的 0.25 | new-stream 1.0 plus duck 0.25
    CHECK(fabsf(g.last_gain - 0.25f) < 1e-6f);
    CHECK(fabsf(d.gain - 0.25f) < 1e-6f);
    CHECK(dm_af_handle(&d, DM_AF_EV_FOCUS_GAIN) == DM_AF_OK);
    CHECK(fabsf(d.gain - 1.0f) < 1e-6f);
    CHECK(fabsf(g.last_gain - 1.0f) < 1e-6f);
    // duck 期间的重复事件不再改增益。| Duplicate duck events do not change the gain again.
    int gains = g.gain_calls;
    CHECK(dm_af_handle(&d, DM_AF_EV_FOCUS_CAN_DUCK) == DM_AF_OK);
    CHECK(g.gain_calls == gains + 1);                // 先恢复 1.0 再 duck | restored to 1.0 then ducked again
    CHECK(fabsf(d.gain - 0.25f) < 1e-6f);
}

static void test_route_disconnect(void) {
    dm_af_policy p; fake_backend f;
    init_policy(&p, &f, 0, 0.2f);
    play_baseline(&p, &f);
    uint32_t generation_before = p.generation;
    CHECK(dm_af_handle(&p, DM_AF_EV_ROUTE_DISCONNECT) == DM_AF_OK);
    CHECK(p.route_ok == 0);
    CHECK(p.output == DM_AF_OUT_PAUSED_ROUTE);
    CHECK(f.stops == 1);                             // 旧流失效，必须关闭 | the old stream is invalid and must be closed
    CHECK(p.generation != generation_before);
    int starts_before = f.starts;
    CHECK(dm_af_handle(&p, DM_AF_EV_FOCUS_GAIN) == DM_AF_OK);
    CHECK(f.starts == starts_before);                // 路由不在，不能起播 | no route, no start
    CHECK(dm_af_handle(&p, DM_AF_EV_ROUTE_AVAILABLE) == DM_AF_OK);
    CHECK(f.starts == starts_before + 1);
    CHECK(p.output == DM_AF_OUT_PLAYING);
}

static void test_backend_failures(void) {
    // start 失败：如实报错、不声称成功，且重复焦点事件不自动重试。
    // A failing start reports the error, claims no success, and duplicate focus events do not retry.
    dm_af_policy p; fake_backend f;
    init_policy(&p, &f, 0, 0.2f);
    CHECK(dm_af_handle(&p, DM_AF_EV_APP_RESUME) == DM_AF_OK);
    CHECK(dm_af_handle(&p, DM_AF_EV_USER_PLAY) == DM_AF_OK);
    f.fail_start = 1;
    CHECK(dm_af_handle(&p, DM_AF_EV_FOCUS_GAIN) == DM_AF_BACKEND_REJECTED);
    CHECK(p.output == DM_AF_OUT_ERROR);
    CHECK(p.backend_failures == 1);
    CHECK(p.last_failed_op == DM_AF_OP_START);
    CHECK(p.backend_running == 0);
    CHECK(dm_af_should_play(&p) == 0);
    int calls_before = f.calls;
    CHECK(dm_af_handle(&p, DM_AF_EV_FOCUS_GAIN) == DM_AF_OK);
    CHECK(f.calls == calls_before);                  // 锁存：没有自动重试 | latched: no automatic retry
    // 显式 USER_PLAY 才重试，失败仍如实上报。| An explicit USER_PLAY retries; another failure is reported again.
    CHECK(dm_af_handle(&p, DM_AF_EV_USER_PLAY) == DM_AF_BACKEND_REJECTED);
    CHECK(p.backend_failures == 2);
    f.fail_start = 0;
    CHECK(dm_af_handle(&p, DM_AF_EV_USER_PLAY) == DM_AF_OK);
    CHECK(p.output == DM_AF_OUT_PLAYING);

    // pause 失败：不得假装已暂停。| A failing pause must not pretend playback is paused.
    dm_af_policy q; fake_backend g;
    init_policy(&q, &g, 0, 0.2f);
    play_baseline(&q, &g);
    g.fail_pause = 1;
    CHECK(dm_af_handle(&q, DM_AF_EV_FOCUS_LOSS_TRANSIENT) == DM_AF_BACKEND_REJECTED);
    CHECK(q.output == DM_AF_OUT_ERROR);
    CHECK(q.last_failed_op == DM_AF_OP_PAUSE);
    CHECK(q.backend_running == 1);                   // 后端状态未知/未变，不假设成功 | backend state unchanged, not assumed

    // gain 失败：duck 不算成功。| A failing gain means ducking did not succeed.
    dm_af_policy d; fake_backend h;
    init_policy(&d, &h, 1, 0.3f);
    play_baseline(&d, &h);
    h.fail_gain = 1;
    CHECK(dm_af_handle(&d, DM_AF_EV_FOCUS_CAN_DUCK) == DM_AF_BACKEND_REJECTED);
    CHECK(d.last_failed_op == DM_AF_OP_GAIN);
    CHECK(d.output == DM_AF_OUT_ERROR);
}

static void test_async_error_never_calls_backend(void) {
    dm_af_policy p; fake_backend f;
    init_policy(&p, &f, 0, 0.2f);
    play_baseline(&p, &f);
    uint32_t generation = p.generation;
    int calls_before = f.calls;
    CHECK(dm_af_handle_generation(&p, generation, DM_AF_EV_BACKEND_ERROR) == DM_AF_OK);
    CHECK(f.calls == calls_before);                  // 回调线程绝不进入后端 | the callback thread never enters the backend
    CHECK(p.output == DM_AF_OUT_ERROR);
    CHECK(p.backend_failures == 1);
    CHECK(p.generation != generation);
    CHECK(dm_af_handle_generation(&p, generation, DM_AF_EV_FOCUS_GAIN) == DM_AF_STALE);
    CHECK(p.stale_events == 1);
    // 显式恢复后可以重新开始。| An explicit resume may start again afterwards.
    CHECK(dm_af_handle(&p, DM_AF_EV_APP_RESUME) == DM_AF_OK);
    CHECK(p.output == DM_AF_OUT_PLAYING);
    CHECK(f.starts == 2);
}

static void test_async_error_retains_ownership(void) {
    dm_af_policy p; fake_backend f;
    init_policy(&p, &f, 0, 0.2f);
    play_baseline(&p, &f);
    CHECK(f.held == 1);
    const uint32_t generation = p.generation;
    int calls_before = f.calls;

    // 异步错误：回调里零后端调用，但资源仍归后端所有。| Async error: zero backend calls, ownership retained.
    CHECK(dm_af_handle_generation(&p, generation, DM_AF_EV_BACKEND_ERROR) == DM_AF_OK);
    CHECK(f.calls == calls_before);
    CHECK(p.backend_owned == 1);
    CHECK(p.backend_running == 0);
    CHECK(f.held == 1);

    // 之后的串行 USER_STOP 必须恰好退休一次。| A later serialized USER_STOP retires it exactly once.
    CHECK(dm_af_handle(&p, DM_AF_EV_USER_STOP) == DM_AF_OK);
    CHECK(f.stops == 1);
    CHECK(f.held == 0);
    CHECK(p.backend_owned == 0);
    CHECK(p.output == DM_AF_OUT_STOPPED);
    CHECK(dm_af_handle(&p, DM_AF_EV_USER_STOP) == DM_AF_OK);
    CHECK(f.stops == 1);                             // 重复停止不再调用 | duplicate stop issues no second call
}

static void test_failed_resume_retains_ownership(void) {
    dm_af_policy p; fake_backend f;
    init_policy(&p, &f, 0, 0.2f);
    play_baseline(&p, &f);
    CHECK(dm_af_handle(&p, DM_AF_EV_APP_PAUSE) == DM_AF_OK);
    CHECK(f.pauses == 1);
    CHECK(p.backend_owned == 1);
    CHECK(p.backend_paused == 1);

    // 恢复被拒：旧流仍然被持有，等待清理。| Rejected resume: the old stream is still owned and needs cleanup.
    f.fail_start = 1;
    CHECK(dm_af_handle(&p, DM_AF_EV_APP_RESUME) == DM_AF_BACKEND_REJECTED);
    CHECK(p.backend_owned == 1);
    CHECK(p.backend_running == 0);
    CHECK(f.held == 1);

    CHECK(dm_af_handle(&p, DM_AF_EV_USER_STOP) == DM_AF_OK);
    CHECK(f.stops == 1);
    CHECK(f.held == 0);
    CHECK(p.backend_owned == 0);
    CHECK(p.output == DM_AF_OUT_STOPPED);
}

static void test_stop_failure_is_visible_and_retryable(void) {
    dm_af_policy p; fake_backend f;
    init_policy(&p, &f, 0, 0.2f);
    play_baseline(&p, &f);
    f.fail_stop = 1;
    CHECK(dm_af_handle(&p, DM_AF_EV_USER_STOP) == DM_AF_BACKEND_REJECTED);
    CHECK(p.output == DM_AF_OUT_ERROR);
    CHECK(p.last_failed_op == DM_AF_OP_STOP);
    CHECK(p.backend_owned == 1);                     // 清理失败不谎称已释放 | a failed cleanup is not a release
    CHECK(p.stops == 0);
    CHECK(f.held == 1);

    // 重试成功后才算释放，且只成功一次。| Only a successful retry releases it, once.
    f.fail_stop = 0;
    CHECK(dm_af_handle(&p, DM_AF_EV_USER_STOP) == DM_AF_OK);
    CHECK(p.stops == 1);
    CHECK(p.backend_owned == 0);
    CHECK(f.held == 0);
    CHECK(p.output == DM_AF_OUT_STOPPED);
    CHECK(f.stops == 2);                             // 一次失败 + 一次成功调用 | one failure plus one successful call
}

static void test_play_after_error_retires_then_starts(void) {
    dm_af_policy p; fake_backend f;
    init_policy(&p, &f, 0, 0.2f);
    play_baseline(&p, &f);
    CHECK(dm_af_handle_generation(&p, p.generation, DM_AF_EV_BACKEND_ERROR) == DM_AF_OK);
    CHECK(p.backend_owned == 1);
    int starts_before = f.starts;
    // 明确的新播放意图：先退休保留的旧流，再开新流。| Explicit play: retire the retained stream, then start.
    CHECK(dm_af_handle(&p, DM_AF_EV_USER_PLAY) == DM_AF_OK);
    CHECK(f.stops == 1);
    CHECK(f.starts == starts_before + 1);
    CHECK(p.backend_owned == 1);
    CHECK(p.backend_running == 1);
    CHECK(f.held == 1);
}

static void test_failed_initial_start_owns_nothing(void) {
    dm_af_policy p; fake_backend f;
    init_policy(&p, &f, 0, 0.2f);
    CHECK(dm_af_handle(&p, DM_AF_EV_APP_RESUME) == DM_AF_OK);
    CHECK(dm_af_handle(&p, DM_AF_EV_USER_PLAY) == DM_AF_OK);
    f.fail_start = 1;
    CHECK(dm_af_handle(&p, DM_AF_EV_FOCUS_GAIN) == DM_AF_BACKEND_REJECTED);
    CHECK(p.backend_owned == 0);                     // 首次启动失败不产生所有权 | a failed initial start owns nothing

    // 没有流可清理：停止不应调用后端。| Nothing to clean up: stop must not call the backend.
    int calls_before = f.calls;
    f.fail_start = 0;
    CHECK(dm_af_handle(&p, DM_AF_EV_USER_STOP) == DM_AF_OK);
    CHECK(f.calls == calls_before);
    CHECK(p.output == DM_AF_OUT_STOPPED);
    CHECK(p.backend_owned == 0);
}

static void test_replacement_applies_duck_gain(void) {
    dm_af_policy p; fake_backend f;
    init_policy(&p, &f, 1, 0.2f);
    play_baseline(&p, &f);
    CHECK(fabsf(f.gain - 1.0f) < 1e-6f);
    CHECK(dm_af_handle(&p, DM_AF_EV_FOCUS_CAN_DUCK) == DM_AF_OK);
    CHECK(fabsf(f.gain - 0.2f) < 1e-6f);

    // 路由断开退休旧流，恢复后建立新流：新流必须重新收到 duck 音量，而不是沿用旧缓存。
    // Route disconnect retires the old stream; the replacement must receive the duck gain instead of reusing the cache.
    CHECK(dm_af_handle(&p, DM_AF_EV_ROUTE_DISCONNECT) == DM_AF_OK);
    CHECK(p.backend_owned == 0);
    CHECK(dm_af_handle(&p, DM_AF_EV_ROUTE_AVAILABLE) == DM_AF_OK);
    CHECK(f.starts == 2);
    CHECK(fabsf(f.gain - 0.2f) < 1e-6f);
    CHECK(fabsf(p.gain - 0.2f) < 1e-6f);
    CHECK(p.output == DM_AF_OUT_PLAYING);
}

static void test_replacement_applies_normal_gain(void) {
    dm_af_policy p; fake_backend f;
    init_policy(&p, &f, 1, 0.2f);
    play_baseline(&p, &f);
    CHECK(dm_af_handle(&p, DM_AF_EV_FOCUS_CAN_DUCK) == DM_AF_OK);
    CHECK(fabsf(f.gain - 0.2f) < 1e-6f);

    // 异步错误后显式播放：先退休旧流，再建立新流；焦点已恢复普通，新流必须收到 1.0。
    // Explicit play after an async error retires the old stream; with normal focus the replacement must receive 1.0.
    CHECK(dm_af_handle_generation(&p, p.generation, DM_AF_EV_BACKEND_ERROR) == DM_AF_OK);
    CHECK(p.backend_owned == 1);
    CHECK(dm_af_handle(&p, DM_AF_EV_FOCUS_GAIN) == DM_AF_OK);
    CHECK(dm_af_handle(&p, DM_AF_EV_USER_PLAY) == DM_AF_OK);
    CHECK(f.stops >= 1);
    CHECK(f.starts == 2);
    CHECK(fabsf(f.gain - 1.0f) < 1e-6f);
    CHECK(fabsf(p.gain - 1.0f) < 1e-6f);
    CHECK(p.output == DM_AF_OUT_PLAYING);
}

static void test_failed_gain_on_new_stream_retry(void) {
    dm_af_policy p; fake_backend f;
    init_policy(&p, &f, 1, 0.2f);
    play_baseline(&p, &f);
    CHECK(dm_af_handle(&p, DM_AF_EV_FOCUS_CAN_DUCK) == DM_AF_OK);
    CHECK(dm_af_handle(&p, DM_AF_EV_ROUTE_DISCONNECT) == DM_AF_OK);

    // 新流建立后 set_gain 失败：如实报错、保留清理所有权，增益标记为未证实。
    // set_gain fails after the new stream started: the error is reported, cleanup ownership is kept and the gain is
    // marked unproven.
    f.fail_gain = 1;
    CHECK(dm_af_handle(&p, DM_AF_EV_ROUTE_AVAILABLE) == DM_AF_BACKEND_REJECTED);
    CHECK(p.output == DM_AF_OUT_ERROR);
    CHECK(p.last_failed_op == DM_AF_OP_GAIN);
    CHECK(p.backend_owned == 1);
    CHECK(p.gain_dirty == 1);
    CHECK(fabsf(f.gain - 1.0f) < 1e-6f);             // 后端仍是新流默认音量 | the backend still has the new stream's default

    // 显式重试重新下发；成功后状态与缓存都一致，清理所有权仍可正常退休。
    // An explicit retry re-applies the gain; afterwards state and cache agree and cleanup still works.
    f.fail_gain = 0;
    CHECK(dm_af_handle(&p, DM_AF_EV_USER_PLAY) == DM_AF_OK);
    CHECK(p.gain_dirty == 0);
    CHECK(fabsf(f.gain - 0.2f) < 1e-6f);
    CHECK(fabsf(p.gain - 0.2f) < 1e-6f);
    CHECK(p.output == DM_AF_OUT_PLAYING);
    CHECK(dm_af_handle(&p, DM_AF_EV_USER_STOP) == DM_AF_OK);
    CHECK(f.held == 0);
    CHECK(p.backend_owned == 0);
}

int main(void) {
    test_init_and_duck_bounds();
    test_focus_gain_and_duplicates();
    test_transient_loss_and_gain();
    test_pause_while_focus_absent_then_gain();
    test_user_stop_before_late_gain();
    test_permanent_loss_requires_user_rearm();
    test_duck_policy();
    test_route_disconnect();
    test_backend_failures();
    test_async_error_never_calls_backend();
    test_async_error_retains_ownership();
    test_failed_resume_retains_ownership();
    test_stop_failure_is_visible_and_retryable();
    test_play_after_error_retires_then_starts();
    test_failed_initial_start_owns_nothing();
    test_replacement_applies_duck_gain();
    test_replacement_applies_normal_gain();
    test_failed_gain_on_new_stream_retry();
    printf("audio_focus: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
