// #200 NativeActivity 音频钩子：唯一的 audio_session 调用者；只处理显式键盘诊断请求与序列化 timer。
// 构造/恢复不产生播放意图；P（DOWN、键盘源、非 repeat）在 App active/resumed/focused 时请求播放并生成自有
// 有界合成 PCM，S 停止。所有调用来自主 looper；AAudio 回调不经过这里。
// #200 NativeActivity audio hooks: the single audio_session caller; handles only explicit keyboard diagnostic requests
// and the serialized timer. Construction/resume never create play intent; P (DOWN, keyboard source, non-repeat)
// requests playback with owned bounded synthetic PCM while the App is active/resumed/focused, S stops. All calls run
// on the main looper; the AAudio callback never enters here.
#pragma once

#include <jni.h>
#include <stdint.h>

#include "audio_session.h"

typedef struct native_audio native_audio;
typedef int64_t (*native_audio_clock)(void *user);

// 48000Hz 诊断 PCM 的有界 backlog：约 85ms，足够吸收正常 timer 抖动，又不会无限积压。
// Bounded 48000Hz diagnostic backlog (~85ms): absorbs normal timer jitter without unbounded growth.
#define NATIVE_AUDIO_RATE DM_AUDIO_RATE
#define NATIVE_AUDIO_MAX_PENDING_FRAMES 4096
#define NATIVE_AUDIO_MAX_CATCHUP_NS ((NATIVE_AUDIO_MAX_PENDING_FRAMES * 1000000000ULL) / NATIVE_AUDIO_RATE + 1u)
// 首次显式播放时使用的诊断队列容量；不修改全局 session 默认。
#define NATIVE_AUDIO_QUEUE_FRAMES 4096

// Stores the VM/global Activity reference and options only; no focus request and no stream. Returns 0 on success.
int native_audio_create(JNIEnv *env, jobject activity, const dm_audio_session_options *options, native_audio **out);

// Returns 1 when the key is a claimed diagnostic control (P/S). keycode 44 = P, 47 = S; action 0 = DOWN.
int native_audio_key(native_audio *audio, JNIEnv *env, int32_t keycode, int32_t action, int32_t repeat,
                     int keyboard_source);

void native_audio_set_focus(native_audio *audio, int focused);
int native_audio_resume(native_audio *audio);
int native_audio_pause(native_audio *audio);

// Serialized timer work: drain focus events and, only while actually PLAYING, write the next bounded PCM block.
// Returns frames written (0 when idle/stopped) or a negative dm_audio_session error.
int native_audio_tick(native_audio *audio, JNIEnv *env);

// Stops producing, then retries session cleanup. Only succeeds (and frees the helper) when both the JNI focus owner
// and AAudio handle are gone; otherwise the helper is retained for a later control retry.
int native_audio_destroy(native_audio **audio_ref, JNIEnv *env);

// Diagnostics.
int native_audio_output_state(const native_audio *audio);
int native_audio_has_session(const native_audio *audio);
int native_audio_last_error(const native_audio *audio);

// 自有确定性 PCM 参考（生产者与测试共用）。
// Deterministic owned PCM reference used by the producer and the focused tests.
void native_audio_reference_pcm(uint64_t start_phase, int16_t *out, int frames);

// 仅供受控测试注入单调时钟；生产为 NULL（CLOCK_MONOTONIC）。
// Controlled tests inject a monotonic clock; production uses NULL (CLOCK_MONOTONIC).
void native_audio_set_clock(native_audio *audio, native_audio_clock clock, void *user);

// 实际写入 session 的帧数、尚未被接受的已生成帧数（诊断/测试）。
uint64_t native_audio_written_frames(const native_audio *audio);
uint32_t native_audio_pending_frames(const native_audio *audio);
