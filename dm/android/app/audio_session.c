// #195 实现：见 audio_session.h 的契约。| Implementation of the contract in audio_session.h.
#include "audio_session.h"

#include <stdlib.h>

struct dm_audio_session {
    dm_afj_owner *focus;
    dm_audio_output output;
    uint32_t focus_generation;
    int closed;
    int accepts_delayed_focus;
    int last_error;
    uint32_t stale_events;
    uint32_t overflow_events;
};

static int map_afj_error(int rc) {
    switch (rc) {
        case DM_AFJ_OK: return DM_AS_OK;
        case DM_AFJ_E_THREAD: return DM_AS_E_THREAD;
        case DM_AFJ_E_INVALID: return DM_AS_E_INVALID;
        case DM_AFJ_E_OVERFLOW: return DM_AS_E_OVERFLOW;
        default: return DM_AS_E_JNI;
    }
}

static int set_error(dm_audio_session *session, int error) {
    session->last_error = error;
    return error;
}

static int handle_event(dm_audio_session *session, dm_af_event event) {
    const dm_af_status status = dm_audio_output_handle(&session->output, event);
    if (status == DM_AF_BACKEND_REJECTED) return set_error(session, DM_AS_E_OUTPUT);
    if (status == DM_AF_INVALID) return set_error(session, DM_AS_E_STATE);
    return DM_AS_OK;
}

int dm_audio_session_create(JNIEnv *env, jobject activity_context, const dm_audio_session_options *options,
                            dm_audio_session **out) {
    if (!env || !activity_context || !options || !out) return DM_AS_E_INVALID;
    *out = NULL;
    if (options->queue_frames < 2) return DM_AS_E_INVALID;

    dm_audio_session *session = calloc(1, sizeof *session);
    if (!session) return DM_AS_E_JNI;
    dm_audio_options audio_options = DM_AUDIO_OPTIONS_INIT;
    audio_options.queue_frames = options->queue_frames;
    audio_options.api = options->audio_api;
    if (dm_audio_output_init(&session->output, &audio_options, options->duck_enabled, options->duck_gain) != 0) {
        free(session);
        return DM_AS_E_INVALID;
    }

    const int created = dm_afj_create(env, activity_context, &session->focus);
    if (created != DM_AFJ_OK || !session->focus) {
        const int error = map_afj_error(created);
        free(session);
        return error;
    }
    session->focus_generation = 0;
    session->accepts_delayed_focus = options->accepts_delayed_focus != 0;
    session->last_error = DM_AS_OK;
    *out = session;
    return DM_AS_OK;
}

int dm_audio_session_play(dm_audio_session *session, int *platform_result) {
    if (!session || !platform_result) return DM_AS_E_INVALID;
    if (session->closed) return set_error(session, DM_AS_E_STATE);
    *platform_result = 0;

    const uint32_t previous_generation = session->focus_generation;
    const uint32_t generation = previous_generation + 1;
    int platform = 0;
    const int requested = dm_afj_request(session->focus, (int)generation,
                                         session->accepts_delayed_focus, &platform);
    if (requested != DM_AFJ_OK) {
        if (requested == DM_AFJ_E_THREAD) {
            // 证实的 off-main 拒绝：平台无任何副作用，保持现有意图/焦点/代次不变。
            // Proven off-main rejection: no platform side effect, so keep intent/focus/generation unchanged.
            return set_error(session, map_afj_error(requested));
        }
        // 平台/JNI 失败可能发生在旧请求已 abandon 之后：可见焦点不确定，必须作废旧代次并停到安全状态。
        // A platform/JNI failure may happen after the old request was abandoned: focus is uncertain, so invalidate
        // the generation and park output fail-closed instead of leaving unfocused playback running.
        session->focus_generation++;
        (void)handle_event(session, DM_AF_EV_FOCUS_LOSS);
        return set_error(session, map_afj_error(requested));
    }
    session->focus_generation = generation;
    *platform_result = platform;

    // 先处理已知的平台结果，再提交新的用户意图：避免 USER_PLAY 读取旧 GRANTED 而瞬态启动。
    // Apply the known platform result before the new intent so USER_PLAY can never consult stale GRANTED.
    int status;
    if (platform == 1) {
        status = handle_event(session, DM_AF_EV_FOCUS_GAIN);
        if (status != DM_AS_OK) return status;
    } else if (platform == 2) {
        status = handle_event(session, DM_AF_EV_FOCUS_LOSS_TRANSIENT);
        if (status != DM_AS_OK) return status;
    } else {
        status = handle_event(session, DM_AF_EV_FOCUS_LOSS);
        if (status != DM_AS_OK) return status;
    }
    return handle_event(session, DM_AF_EV_USER_PLAY);
}

int dm_audio_session_resume(dm_audio_session *session) {
    if (!session) return DM_AS_E_INVALID;
    if (session->closed) return set_error(session, DM_AS_E_STATE);
    return handle_event(session, DM_AF_EV_APP_RESUME);
}

int dm_audio_session_pause(dm_audio_session *session) {
    if (!session) return DM_AS_E_INVALID;
    if (session->closed) return set_error(session, DM_AS_E_STATE);
    return handle_event(session, DM_AF_EV_APP_PAUSE);
}

int dm_audio_session_stop(dm_audio_session *session) {
    if (!session) return DM_AS_E_INVALID;
    if (session->closed) return set_error(session, DM_AS_E_STATE);
    const int stopped = handle_event(session, DM_AF_EV_USER_STOP);
    if (stopped != DM_AS_OK) return stopped;
    int abandoned = 0;
    const int rc = dm_afj_abandon(session->focus, &abandoned);
    session->focus_generation++;   // 旧请求的待处理事件立即失效
    if (rc != DM_AFJ_OK) return set_error(session, map_afj_error(rc));
    return DM_AS_OK;
}

int dm_audio_session_drain(dm_audio_session *session) {
    if (!session) return DM_AS_E_INVALID;
    if (session->closed) return set_error(session, DM_AS_E_STATE);
    for (;;) {
        dm_afj_event events[16];
        int count = 0;
        const int rc = dm_afj_drain(session->focus, events, 16, &count);
        if (rc == DM_AFJ_E_OVERFLOW) {
            // fail closed：立即停在安全状态并显式上报，绝不静默丢永久 LOSS。
            session->overflow_events++;
            handle_event(session, DM_AF_EV_USER_STOP);
            return set_error(session, DM_AS_E_OVERFLOW);
        }
        if (rc != DM_AFJ_OK) return set_error(session, map_afj_error(rc));
        if (count == 0) break;
        for (int i = 0; i < count; i++) {
            if ((uint32_t)events[i].generation != session->focus_generation) {
                session->stale_events++;   // 旧请求/旧代次的迟到回调
                continue;
            }
            dm_af_event event;
            switch (events[i].event) {
                case 0: event = DM_AF_EV_FOCUS_GAIN; break;
                case 1: event = DM_AF_EV_FOCUS_LOSS; break;
                case 2: event = DM_AF_EV_FOCUS_LOSS_TRANSIENT; break;
                case 3: event = DM_AF_EV_FOCUS_CAN_DUCK; break;
                default: continue;
            }
            const int status = handle_event(session, event);
            if (status != DM_AS_OK) return status;
        }
    }
    return DM_AS_OK;
}

uint32_t dm_audio_session_write(dm_audio_session *session, const int16_t *samples, uint32_t frames) {
    if (!session || session->closed || !samples || frames == 0) return 0;
    // 只有真正在播放（含 duck 的 PLAYING）时才接受生产者；停止/暂停/错误/overflow 后不再入队。
    // Accept producer data only while actually playing (ducking stays PLAYING); stop/pause/error/overflow reject it.
    if (session->output.policy.output != DM_AF_OUT_PLAYING) return 0;
    return dm_audio_output_write(&session->output, samples, frames);
}

int dm_audio_session_destroy(dm_audio_session **session_ref) {
    if (!session_ref) return DM_AS_E_INVALID;
    dm_audio_session *session = *session_ref;
    if (!session) return DM_AS_OK;
    session->closed = 1;   // 关停后不再接受生产者/控制调用

    int focus_error = DM_AS_OK;
    if (session->focus) {
        const int rc = dm_afj_release(&session->focus);
        if (rc != DM_AFJ_OK) focus_error = map_afj_error(rc);
    }
    int audio_error = DM_AS_OK;
    if (dm_audio_output_has_handle(&session->output)) {
        const int rc = dm_audio_output_destroy(&session->output);
        if (rc != 0) audio_error = DM_AS_E_CLEANUP;
    }
    if (session->focus || dm_audio_output_has_handle(&session->output)) {
        // 两个 handle 独立保留：绝不在任一 handle 仍存在时报告成功释放。
        session->last_error = focus_error != DM_AS_OK ? focus_error
                             : (audio_error != DM_AS_OK ? audio_error : DM_AS_E_CLEANUP);
        return session->last_error;
    }
    free(session);
    *session_ref = NULL;
    return DM_AS_OK;
}

int dm_audio_session_output_state(const dm_audio_session *session) {
    return session ? (int)session->output.policy.output : DM_AF_OUT_STOPPED;
}

int dm_audio_session_focus_generation(const dm_audio_session *session) {
    return session ? (int)session->focus_generation : 0;
}

int dm_audio_session_last_error(const dm_audio_session *session) {
    return session ? session->last_error : DM_AS_E_INVALID;
}

int dm_audio_session_has_focus_owner(const dm_audio_session *session) {
    return session && session->focus != NULL;
}

int dm_audio_session_has_audio_handle(const dm_audio_session *session) {
    return session && dm_audio_output_has_handle(&session->output);
}

int dm_audio_session_is_closed(const dm_audio_session *session) {
    return session ? session->closed : 1;
}
