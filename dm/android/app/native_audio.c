// #200 实现见 native_audio.h。| Implementation of native_audio.h.
#include "native_audio.h"

#include <stdlib.h>
#include <string.h>

struct native_audio {
    JavaVM *vm;
    jobject activity;          // global reference; released only after the session is fully gone
    dm_audio_session_options options;
    dm_audio_session *session; // lazy: created by the first explicit key request
    int active, resumed, focused;
    int closed;
    int last_error;
    uint64_t pcm_phase;
};

static uint64_t g_pcm_phase_seed;

static int set_error(native_audio *audio, int error) {
    if (audio) audio->last_error = error;
    return error;
}

// 可选诊断路径上的分配失败不得把 pending exception 留给调用者。
// Allocation failure on this optional diagnostic path must not leave a pending exception for the caller.
static void clear_pending(JNIEnv *env) {
    if (env && (*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
}

void native_audio_reference_pcm(uint64_t start_phase, int16_t *out, int frames) {
    for (int i = 0; i < frames; i++) {
        const uint64_t frame = start_phase + (uint64_t)i;
        for (int ch = 0; ch < DM_AUDIO_CHANNELS; ch++) {
            const int32_t value = (int32_t)((frame * 37u + (uint64_t)ch * 11u) & 0x1fffu) - 4096;
            out[i * DM_AUDIO_CHANNELS + ch] = (int16_t)value;
        }
    }
}

int native_audio_create(JNIEnv *env, jobject activity, const dm_audio_session_options *options, native_audio **out) {
    if (!env || !activity || !out) return -1;
    *out = NULL;
    native_audio *audio = calloc(1, sizeof *audio);
    if (!audio) return -1;
    if ((*env)->GetJavaVM(env, &audio->vm) != 0 || !audio->vm) {
        free(audio);
        return -1;
    }
    audio->activity = (*env)->NewGlobalRef(env, activity);
    if (!audio->activity) {
        clear_pending(env);
        free(audio);
        return -1;
    }
    if (options) audio->options = *options;
    else audio->options = (dm_audio_session_options)DM_AUDIO_SESSION_OPTIONS_INIT;
    audio->pcm_phase = __atomic_add_fetch(&g_pcm_phase_seed, 1, __ATOMIC_RELAXED);
    *out = audio;
    return 0;
}

static int ensure_session(native_audio *audio, JNIEnv *env) {
    if (audio->session) return DM_AS_OK;
    if (!env) return set_error(audio, DM_AS_E_JNI);
    jobject activity = (*env)->NewLocalRef(env, audio->activity);
    if (!activity) { clear_pending(env); return set_error(audio, DM_AS_E_JNI); }
    dm_audio_session *session = NULL;
    const int rc = dm_audio_session_create(env, activity, &audio->options, &session);
    (*env)->DeleteLocalRef(env, activity);
    if (rc != DM_AS_OK || !session) return set_error(audio, rc != DM_AS_OK ? rc : DM_AS_E_JNI);
    audio->session = session;
    if (audio->resumed) (void)dm_audio_session_resume(session);
    return DM_AS_OK;
}

int native_audio_key(native_audio *audio, JNIEnv *env, int32_t keycode, int32_t action, int32_t repeat,
                     int keyboard_source) {
    if (!audio || audio->closed) return 0;
    if (!keyboard_source || repeat || action != 0) return 0;   // 只认键盘源、非 repeat 的 DOWN
    if (keycode != 44 && keycode != 47) return 0;              // P / S

    if (keycode == 47) {
        if (audio->session) {
            const int rc = dm_audio_session_stop(audio->session);
            if (rc != DM_AS_OK) set_error(audio, rc);
        }
        return 1;
    }
    if (!audio->active || !audio->resumed || !audio->focused) return 1;   // 显式但不满足 App 状态：不请求
    const int ensured = ensure_session(audio, env);
    if (ensured != DM_AS_OK) { set_error(audio, ensured); return 1; }
    int platform = 0;
    const int rc = dm_audio_session_play(audio->session, &platform);
    if (rc != DM_AS_OK) set_error(audio, rc);
    return 1;
}

void native_audio_set_focus(native_audio *audio, int focused) {
    if (!audio) return;
    audio->focused = focused ? 1 : 0;
    // 失去窗口焦点是明确的诊断暂停：停输出并放弃本次意图，恢复焦点不自动播放。
    // Window-focus loss is an explicit diagnostic pause: stop output and drop the intent; regaining focus never plays.
    if (!audio->focused && audio->session) (void)dm_audio_session_stop(audio->session);
}

int native_audio_resume(native_audio *audio) {
    if (!audio || audio->closed) return DM_AS_E_INVALID;
    audio->active = 1;
    audio->resumed = 1;
    return audio->session ? dm_audio_session_resume(audio->session) : DM_AS_OK;
}

int native_audio_pause(native_audio *audio) {
    if (!audio || audio->closed) return DM_AS_E_INVALID;
    audio->active = 0;
    audio->resumed = 0;
    // 暂停必须停生产者并放弃播放意图；resume 不会重放旧请求，只有新的显式 P 才恢复。
    // Pause stops the producer and drops play intent; resume never replays the old request, only a new explicit P.
    return audio->session ? dm_audio_session_stop(audio->session) : DM_AS_OK;
}

int native_audio_tick(native_audio *audio, JNIEnv *env) {
    (void)env;
    if (!audio || audio->closed || !audio->session) return 0;
    const int drained = dm_audio_session_drain(audio->session);
    if (drained == DM_AS_E_OVERFLOW) return set_error(audio, drained);
    if (drained != DM_AS_OK) return set_error(audio, drained);
    if (dm_audio_session_output_state(audio->session) != DM_AF_OUT_PLAYING) return 0;
    int16_t pcm[NATIVE_AUDIO_BLOCK_FRAMES * DM_AUDIO_CHANNELS];
    native_audio_reference_pcm(audio->pcm_phase, pcm, NATIVE_AUDIO_BLOCK_FRAMES);
    const uint32_t written = dm_audio_session_write(audio->session, pcm, NATIVE_AUDIO_BLOCK_FRAMES);
    if (written > 0) audio->pcm_phase += NATIVE_AUDIO_BLOCK_FRAMES;
    return (int)written;
}

int native_audio_destroy(native_audio **audio_ref, JNIEnv *env) {
    if (!audio_ref) return -1;
    native_audio *audio = *audio_ref;
    if (!audio) return 0;
    audio->closed = 1;
    int rc = DM_AS_OK;
    if (audio->session) {
        // 有界串行重试：dm_audio 的 close 成功/stop-wait 未证明时需下一次控制重试才真正释放。
        // Bounded serialized retry: after a successful close with unproven stop/wait, dm_audio completes release on
        // the next control attempt. Each attempt stays on the serialized caller and never runs in a callback.
        for (int attempt = 0; attempt < 2; attempt++) {
            rc = dm_audio_session_destroy(&audio->session);
            if (rc == DM_AS_OK) break;
        }
        if (rc != DM_AS_OK) return set_error(audio, rc);   // session 保留（含 JNI/AAudio handle）
    }
    if (audio->activity && env) {
        (*env)->DeleteGlobalRef(env, audio->activity);
        audio->activity = NULL;
    }
    if (audio->activity) {
        // 没有 JNIEnv 就绝不可能证明全局引用已释放；保留 helper，不虚报清理成功。
        // Without a JNIEnv the global reference cannot be proven released; retain the helper instead of claiming success.
        return set_error(audio, DM_AS_E_JNI);
    }
    free(audio);
    *audio_ref = NULL;
    return 0;
}

int native_audio_output_state(const native_audio *audio) {
    return audio && audio->session ? dm_audio_session_output_state(audio->session) : DM_AF_OUT_STOPPED;
}

int native_audio_has_session(const native_audio *audio) { return audio && audio->session != NULL; }

int native_audio_last_error(const native_audio *audio) { return audio ? audio->last_error : DM_AS_E_INVALID; }
