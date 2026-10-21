// #153 实现：串行、可注入、诚实上报的音频焦点/播放意图策略。不发起真实焦点请求，也不启动音频。
// #153 implementation: serialized, injectable, honestly reported audio-focus/desired-play policy. It performs no
// real focus request and starts no audio.
#include "audio_focus.h"

#include <math.h>
#include <string.h>

#define DM_AF_GAIN_EPSILON 0.0001f

static int valid_gain(float gain) {
    return isfinite(gain) && gain > 0.0f && gain <= 1.0f;
}

int dm_af_init(dm_af_policy *policy, const dm_af_backend *backend, int duck_enabled, float duck_gain) {
    if (!policy || !backend) return -1;
    if (!backend->start || !backend->pause || !backend->stop || !backend->set_gain) return -1;
    // duck 增益必须有界（>0 且 <=1），否则拒绝配置而不是猜测一个值。
    // The duck gain must be bounded (>0 and <=1); an invalid value is refused rather than guessed.
    if (!valid_gain(duck_gain)) return -1;
    memset(policy, 0, sizeof *policy);
    policy->backend = *backend;
    policy->duck_enabled = duck_enabled ? 1 : 0;
    policy->duck_gain = duck_gain;
    policy->focus = DM_AF_FOCUS_NONE;
    policy->output = DM_AF_OUT_STOPPED;
    policy->gain = 1.0f;
    policy->route_ok = 1;
    policy->app_active = 0;   // 前台状态未知时保守地不播放 | conservatively no playback until APP_RESUME is observed
    policy->generation = 1;
    return 0;
}

int dm_af_set_duck(dm_af_policy *policy, int enabled, float gain) {
    if (!policy || !valid_gain(gain)) return -1;
    policy->duck_enabled = enabled ? 1 : 0;
    policy->duck_gain = gain;
    return 0;
}

uint32_t dm_af_generation(const dm_af_policy *policy) {
    return policy ? policy->generation : 0;
}

// 目标输出只由意图/前台/路由/焦点决定；后端状态不参与判断，避免把"后端还在跑"当成许可。
// The target output depends only on intent/foreground/route/focus; backend state never grants permission.
static dm_af_output target_output(const dm_af_policy *policy) {
    if (!policy->desired_play) return DM_AF_OUT_STOPPED;
    if (policy->requires_user_rearm) return DM_AF_OUT_STOPPED;
    if (!policy->app_active) return DM_AF_OUT_PAUSED_LIFECYCLE;
    if (!policy->route_ok) return DM_AF_OUT_PAUSED_ROUTE;
    return DM_AF_OUT_PAUSED_FOCUS;
}

static int wants_play(const dm_af_policy *policy) {
    if (!policy->desired_play || policy->requires_user_rearm) return 0;
    if (!policy->app_active || !policy->route_ok) return 0;
    if (policy->focus == DM_AF_FOCUS_GRANTED) return 1;
    // duck 只在该策略被显式启用时算作"允许播放"；否则按暂停处理而不假装降音量。
    // Ducking counts as permission only when explicitly enabled; otherwise the policy pauses instead of pretending.
    if (policy->focus == DM_AF_FOCUS_CAN_DUCK && policy->duck_enabled) return 1;
    return 0;
}

int dm_af_should_play(const dm_af_policy *policy) {
    if (!policy) return 0;
    if (policy->output == DM_AF_OUT_ERROR) return 0;
    return wants_play(policy);
}

static void settle(dm_af_policy *policy, dm_af_output output) {
    if (policy->output != output) {
        policy->output = output;
        policy->transitions++;
    }
}

// 后端失败：不改变对后端实际状态的假设（除非是 start 从未成功），使旧代次失效，并锁存到下一次显式指令。
// Backend failure: keep the assumption about the real backend state unless start never succeeded, invalidate the
// old generation, and latch until the next explicit instruction.
static dm_af_status op_failed(dm_af_policy *policy, dm_af_op op) {
    policy->backend_failures++;
    policy->last_failed_op = op;
    policy->error_latched = 1;
    policy->output = DM_AF_OUT_ERROR;
    policy->generation++;
    policy->transitions++;
    if (op == DM_AF_OP_GAIN) policy->gain_dirty = 1;   // 增益未获证实，重试时必须重新下发 | gain unproven, retry re-applies
    // 所有权不因失败而消失：已持有的流仍要由一次成功的 stop 退休；失败的首次 start 不会凭空产生所有权。
    // Ownership does not disappear on failure: a retained stream still needs one successful stop, while a failed
    // initial start never invents ownership.
    return DM_AF_BACKEND_REJECTED;
}

// 一次状态收敛：只在真正需要时调用后端；已处于目标状态时不产生任何后端调用。
// One convergence pass: the backend is called only when a real transition is needed.
static dm_af_status apply_policy(dm_af_policy *policy) {
    if (policy->error_latched) return DM_AF_OK;   // 不自动重试；等待显式指令 | no auto retry; wait for an explicit event

    if (!wants_play(policy)) {
        if (policy->backend_owned) {
            const int must_stop = !policy->desired_play || policy->requires_user_rearm || !policy->route_ok;
            if (must_stop) {
                if (policy->backend.stop(policy->backend.ctx)) return op_failed(policy, DM_AF_OP_STOP);
                policy->backend_owned = 0;
                policy->backend_running = 0;
                policy->backend_paused = 0;
                policy->stops++;
                policy->generation++;   // 旧流的所有迟到回调失效 | every late callback of the old stream is stale
            } else if (policy->backend_running) {
                if (policy->backend.pause(policy->backend.ctx)) return op_failed(policy, DM_AF_OP_PAUSE);
                policy->backend_running = 0;
                policy->backend_paused = 1;
                policy->pauses++;
            }
        }
        settle(policy, target_output(policy));
        return DM_AF_OK;
    }

    int started = 0;
    if (!policy->backend_owned || !policy->backend_running) {
        // 持有一个已失效（非我们暂停）的旧流时，先退休它，再开新流；退休失败则保持错误且仍然持有。
        // Retire a retained but dead stream (not one we paused) before starting a replacement; a failed retirement
        // keeps the error and the ownership.
        if (policy->backend_owned && !policy->backend_paused) {
            if (policy->backend.stop(policy->backend.ctx)) return op_failed(policy, DM_AF_OP_STOP);
            policy->backend_owned = 0;
            policy->stops++;
            policy->generation++;
        }
        // 新流或恢复：每次 start 都拿新的代次，旧代次的事件随后一律 STALE。
        // Fresh stream or resume: every start takes a new generation, so events of older streams become STALE.
        const uint32_t generation = policy->generation + 1;
        if (policy->backend.start(policy->backend.ctx, generation)) return op_failed(policy, DM_AF_OP_START);
        policy->generation = generation;
        policy->backend_owned = 1;
        policy->backend_running = 1;
        policy->backend_paused = 0;
        policy->starts++;
        started = 1;   // 新流不继承旧流的增益：本趟必须下发音量目标 | a new stream does not inherit the old gain
    }

    const float target_gain = (policy->focus == DM_AF_FOCUS_CAN_DUCK && policy->duck_enabled) ? policy->duck_gain : 1.0f;
    // started 守卫：即使目标增益与旧缓存值相同，新流也必须真正收到 set_gain；未启动的重复焦点事件仍保持幂等。
    // started guard: even when the target equals the old cached value, a new stream really receives set_gain, while
    // duplicate focus events without a start stay idempotent. A failed gain marks the value unproven so a retry re-applies.
    if (started || policy->gain_dirty || fabsf(policy->gain - target_gain) > DM_AF_GAIN_EPSILON) {
        if (policy->backend.set_gain(policy->backend.ctx, target_gain)) return op_failed(policy, DM_AF_OP_GAIN);
        policy->gain = target_gain;
        policy->gain_dirty = 0;
        policy->gain_applied++;
    }
    settle(policy, DM_AF_OUT_PLAYING);
    return DM_AF_OK;
}

static int event_clears_latch(dm_af_event event) {
    // 只有新的显式指令才解除失败锁存；重复的焦点回调不会变成自动重试。
    // Only a fresh explicit instruction clears the failure latch; repeated focus callbacks never become a retry.
    return event == DM_AF_EV_USER_PLAY || event == DM_AF_EV_USER_STOP || event == DM_AF_EV_APP_RESUME ||
           event == DM_AF_EV_ROUTE_AVAILABLE;
}

dm_af_status dm_af_handle(dm_af_policy *policy, dm_af_event event) {
    if (!policy) return DM_AF_INVALID;
    if (!policy->backend.start || !policy->backend.pause || !policy->backend.stop || !policy->backend.set_gain)
        return DM_AF_INVALID;
    if (event_clears_latch(event)) policy->error_latched = 0;

    switch (event) {
    case DM_AF_EV_FOCUS_GAIN:
        policy->focus = DM_AF_FOCUS_GRANTED;
        // 焦点回归不是新的用户指令：后端失败后仍保持锁存，只有显式 USER_PLAY/APP_RESUME/ROUTE_AVAILABLE 重试。
        // Focus returning is not a new user instruction: a backend failure stays latched until an explicit
        // USER_PLAY/APP_RESUME/ROUTE_AVAILABLE.
        break;
    case DM_AF_EV_FOCUS_LOSS:
        policy->focus = DM_AF_FOCUS_NONE;
        // 永久丢失：意图保留，但绝不静默自动恢复；必须等新的 USER_PLAY。
        // Permanent loss: intent is kept but playback never silently auto-resumes; a fresh USER_PLAY is required.
        policy->requires_user_rearm = 1;
        break;
    case DM_AF_EV_FOCUS_LOSS_TRANSIENT:
        policy->focus = DM_AF_FOCUS_TRANSIENT;
        break;
    case DM_AF_EV_FOCUS_CAN_DUCK:
        policy->focus = DM_AF_FOCUS_CAN_DUCK;
        break;
    case DM_AF_EV_USER_PLAY:
        policy->desired_play = 1;
        policy->requires_user_rearm = 0;
        break;
    case DM_AF_EV_USER_STOP:
        policy->desired_play = 0;
        break;
    case DM_AF_EV_APP_RESUME:
        policy->app_active = 1;
        break;
    case DM_AF_EV_APP_PAUSE:
        policy->app_active = 0;
        break;
    case DM_AF_EV_ROUTE_DISCONNECT:
        policy->route_ok = 0;
        break;
    case DM_AF_EV_ROUTE_AVAILABLE:
        policy->route_ok = 1;
        break;
    case DM_AF_EV_BACKEND_ERROR:
        // 异步错误在回调线程上到达：只更新状态，绝不回调后端。流资源仍归后端所有，留给后续串行的 stop/替换退休。
        // An async error arrives on a callback thread: only state is updated, the backend is never called. The stream
        // resource stays owned by the backend and is retired by a later serialized stop/replacement.
        policy->backend_failures++;
        policy->backend_running = 0;
        policy->backend_paused = 0;
        policy->error_latched = 1;
        policy->generation++;
        policy->output = DM_AF_OUT_ERROR;
        policy->transitions++;
        return DM_AF_OK;
    default:
        return DM_AF_INVALID;
    }
    return apply_policy(policy);
}

dm_af_status dm_af_handle_generation(dm_af_policy *policy, uint32_t generation, dm_af_event event) {
    if (!policy) return DM_AF_INVALID;
    if (generation != policy->generation) {
        policy->stale_events++;   // 迟到的旧事件：不改变状态，也不调用后端 | late old event: no state change, no backend call
        return DM_AF_STALE;
    }
    return dm_af_handle(policy, event);
}
