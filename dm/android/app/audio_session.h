// #195 串行音频会话：把已验收的 JNI 平台焦点适配（dm_afj）与真实音频输出 owner（dm_audio_output）组合起来。
// 不复制任何队列/注册表/策略：焦点事件走 dm_afj 的有界环，播放/增益/流由 dm_audio_output 的 dm_af_policy 与
// 真实 dm_audio 决定。构造函数不请求焦点、不开流、不启动；所有控制调用在主线程串行，回调绝不启动音频。
// #195 serialized audio session combining the accepted JNI platform-focus adapter (dm_afj) with the real audio
// output owner (dm_audio_output). No queue/registry/policy is duplicated: focus events use dm_afj's bounded ring and
// playback/gain/streams stay inside dm_audio_output's dm_af_policy and real dm_audio. Construction requests no focus,
// opens no stream and starts nothing; control calls are serialized on the main thread and callbacks never start audio.
#pragma once

#include <jni.h>
#include <stdint.h>

#include "audio_focus_jni.h"
#include "audio_output.h"

typedef struct {
    const dm_audio_api *audio_api;   // NULL = real AAudio; host tests replace only this table
    uint32_t queue_frames;           // PCM ring frames (dm_audio requirement)
    int duck_enabled;
    float duck_gain;
    int accepts_delayed_focus;       // 1 = request the platform's delayed-focus opt-in (DELAYED is not playback)
} dm_audio_session_options;

#define DM_AUDIO_SESSION_OPTIONS_INIT { NULL, 32u, 0, 0.5f, 1 }

enum {
    DM_AS_OK = 0,
    DM_AS_E_INVALID = -1,
    DM_AS_E_JNI = -2,
    DM_AS_E_THREAD = -3,
    DM_AS_E_OVERFLOW = -4,
    DM_AS_E_OUTPUT = -5,
    DM_AS_E_CLEANUP = -6,
    DM_AS_E_STATE = -7
};

typedef struct dm_audio_session dm_audio_session;

// Create must run on the Android main thread with the Activity/Context. No focus request and no AAudio stream happen
// here. On failure every partially created owner is rolled back before returning.
int dm_audio_session_create(JNIEnv *env, jobject activity_context, const dm_audio_session_options *options,
                            dm_audio_session **out);

// Explicit user play: requests focus with a fresh session focus generation, records user intent, and dispatches
// FOCUS_GAIN only when the platform really returned GRANTED. DELAYED/FAILED never start playback; a later real GAIN
// callback (drained on this thread) may start it. *platform_result is the real 1/2/0 value.
int dm_audio_session_play(dm_audio_session *session, int *platform_result);

// App lifecycle. resume may start only when user intent + focus GRANTED permit it; pause stops output without losing
// user intent. Neither opens a stream by itself.
int dm_audio_session_resume(dm_audio_session *session);
int dm_audio_session_pause(dm_audio_session *session);

// User stop: policy stop plus platform abandon; late GAIN after this must not auto-play (fresh play is required).
int dm_audio_session_stop(dm_audio_session *session);

// Drain platform focus events from dm_afj on the serialized control thread and dispatch them to the output policy.
// Stale generations are dropped; ring overflow stops output and returns DM_AS_E_OVERFLOW (never silent LOSS).
int dm_audio_session_drain(dm_audio_session *session);

// Bounded PCM16 producer. Returns 0 after stop/destroy or when no stream is owned.
uint32_t dm_audio_session_write(dm_audio_session *session, const int16_t *samples, uint32_t frames);

// Destroy only succeeds when neither the JNI focus owner nor the AAudio handle remains; otherwise both are retained
// and the error is retryable on the same serialized thread.
int dm_audio_session_destroy(dm_audio_session **session_ref);

// Diagnostics.
int dm_audio_session_output_state(const dm_audio_session *session);   // dm_af_output
int dm_audio_session_focus_generation(const dm_audio_session *session);
int dm_audio_session_last_error(const dm_audio_session *session);
int dm_audio_session_has_focus_owner(const dm_audio_session *session);
int dm_audio_session_has_audio_handle(const dm_audio_session *session);
int dm_audio_session_is_closed(const dm_audio_session *session);
