// #153 音频焦点与用户播放意图的串行策略：纯 C，不依赖 Android/AAudio 头文件，也不发起真实焦点请求。
// 后端只通过注入的 start/pause/stop/set_gain 操作访问；策略本身不创建线程、不启动音频。
// #153 serialized audio-focus and desired-play policy: plain C with no Android/AAudio headers and no real focus
// request. The backend is reached only through injected start/pause/stop/set_gain operations; the policy creates no
// thread and starts no audio.
//
// Separation of concerns:
//   desired_play  - the user's playback intent (USER_PLAY/USER_STOP), never overwritten by focus or lifecycle
//   focus         - the focus state reported by the platform (granted/loss/transient/duck-capable)
//   app_active    - App foreground/resume state
//   route_ok      - whether the output route is currently usable
//   backend_*     - what the injected backend actually accepted (never assumed)
// A later platform event cannot silently restart playback that the user stopped or that a permanent focus loss
// revoked: permanent loss sets requires_user_rearm and only a new USER_PLAY clears it (documented Android-specific
// choice; Android's guidance is that a well-behaved app pauses/ducks on loss and abandons focus when playback stops).
//
// All calls are serialized on the App's control thread. Event handlers may be invoked from arbitrary threads only
// after being queued there: handling an event never calls into the backend from a real-time/callback context.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DM_AF_FOCUS_NONE = 0,       // 无焦点（永久丢失或尚未请求） | no focus (permanent loss or not yet requested)
    DM_AF_FOCUS_GRANTED,        // 正常持有焦点 | focus held
    DM_AF_FOCUS_TRANSIENT,      // 暂时丢失，允许随后恢复 | transient loss, resume later
    DM_AF_FOCUS_CAN_DUCK        // 暂时丢失但允许降低音量 | transient loss that may duck
} dm_af_focus;

typedef enum {
    DM_AF_EV_FOCUS_GAIN = 0,
    DM_AF_EV_FOCUS_LOSS,             // 永久丢失 | permanent loss
    DM_AF_EV_FOCUS_LOSS_TRANSIENT,
    DM_AF_EV_FOCUS_CAN_DUCK,
    DM_AF_EV_USER_PLAY,
    DM_AF_EV_USER_STOP,
    DM_AF_EV_APP_RESUME,
    DM_AF_EV_APP_PAUSE,
    DM_AF_EV_ROUTE_DISCONNECT,       // 输出路由断开：旧流失效 | output route lost: the old stream is invalid
    DM_AF_EV_ROUTE_AVAILABLE,
    DM_AF_EV_BACKEND_ERROR           // 后端异步错误；处理时绝不回调后端 | async backend error; never calls the backend
} dm_af_event;

typedef enum {
    DM_AF_OUT_STOPPED = 0,      // 有意停止，后端不持有流 | deliberately stopped, no stream held
    DM_AF_OUT_PLAYING,
    DM_AF_OUT_PAUSED_FOCUS,     // 因焦点暂停（临时丢失/未启用 duck） | paused because of focus
    DM_AF_OUT_PAUSED_LIFECYCLE, // App 不在前台 | App not in the foreground
    DM_AF_OUT_PAUSED_ROUTE,     // 路由不可用 | route unavailable
    DM_AF_OUT_ERROR             // 后端拒绝或异步失败；不声称成功 | backend rejected or failed; no success claimed
} dm_af_output;

typedef enum {
    DM_AF_OK = 0,
    DM_AF_STALE,                // 迟到的旧 generation：不改变任何状态 | late event from an old generation: no state change
    DM_AF_BACKEND_REJECTED,     // 后端拒绝了本次转换 | the backend rejected this transition
    DM_AF_INVALID               // 参数/状态非法 | invalid argument or state
} dm_af_status;

typedef enum {
    DM_AF_OP_NONE = 0,
    DM_AF_OP_START,
    DM_AF_OP_PAUSE,
    DM_AF_OP_STOP,
    DM_AF_OP_GAIN
} dm_af_op;

// 注入后端：返回 0 表示接受，非 0 表示拒绝。generation 只用于让旧流的事件失效。
// Injected backend: 0 means accepted, non-zero means rejected. The generation only invalidates events of older streams.
typedef struct {
    void *ctx;
    int (*start)(void *ctx, uint32_t generation);
    int (*pause)(void *ctx);
    int (*stop)(void *ctx);
    int (*set_gain)(void *ctx, float gain);
} dm_af_backend;

typedef struct {
    dm_af_backend backend;
    int duck_enabled;           // 默认关闭：不在显式启用时绝不 duck | off by default: never duck unless enabled
    float duck_gain;            // (0, 1] 内的有限值 | finite value in (0, 1]

    int desired_play;           // 用户播放意图，与焦点/生命周期独立 | user intent, independent of focus/lifecycle
    int app_active;
    int route_ok;
    int requires_user_rearm;    // 永久丢失后必须重新获得用户播放意图 | permanent loss requires fresh user intent
    dm_af_focus focus;

    dm_af_output output;
    // 所有权与运行状态分开：backend_owned = 后端仍持有流资源（可能已暂停/已失效），必须由一次成功的 stop 退休；
    // backend_running = 此刻确实在出音。失败与异步错误都不转移所有权，generation 只决定回调是否仍然有效。
    // Ownership is separate from running: backend_owned means the backend still holds the stream resource (paused or
    // failed) and only a successful stop retires it; backend_running means audio is actually being produced now.
    // Failures and async errors never transfer ownership; the generation only decides whether a callback is valid.
    int backend_owned;
    int backend_running;
    int backend_paused;         // 持有的流处于"我们暂停过、可恢复"的状态 | owned stream paused by us and resumable
    int error_latched;          // 后端失败后锁存，直到新的显式指令 | latched after a backend failure until an explicit event
    int gain_dirty;            // 增益未被后端证实（失败后重试必须重新下发） | gain not proven by the backend, retry must re-apply
    float gain;
    uint32_t generation;        // 当前可接受的流代次 | generation of the currently acceptable stream

    uint32_t starts, pauses, stops, gain_applied;
    uint32_t stale_events, backend_failures, transitions;
    dm_af_op last_failed_op;
} dm_af_policy;

// duck_gain 必须在 (0, 1] 内且有限；duck_enabled=0 时该值仍会被校验但不会使用。
// duck_gain must be finite and inside (0, 1]; it is validated even when ducking is disabled.
int dm_af_init(dm_af_policy *policy, const dm_af_backend *backend, int duck_enabled, float duck_gain);
// 运行期修改 duck 配置；非法参数返回 -1 且不改变现有配置。| Runtime duck configuration; invalid input returns -1.
int dm_af_set_duck(dm_af_policy *policy, int enabled, float gain);

// 串行事件处理。重复事件是幂等的：已在目标状态时不会再次调用后端。
// Serialized event handling. Duplicate events are idempotent: no backend call when already in the target state.
dm_af_status dm_af_handle(dm_af_policy *policy, dm_af_event event);
// 带代次的事件（异步错误/迟到回调）：代次不匹配时返回 STALE 且不改变状态。
// Generation-carrying events (async errors/late callbacks): a mismatch returns STALE and changes nothing.
dm_af_status dm_af_handle_generation(dm_af_policy *policy, uint32_t generation, dm_af_event event);

uint32_t dm_af_generation(const dm_af_policy *policy);
// 当前是否应输出声音（意图 + 焦点 + 前台 + 路由）。| Whether sound should currently be produced.
int dm_af_should_play(const dm_af_policy *policy);

#ifdef __cplusplus
}
#endif
