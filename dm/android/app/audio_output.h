// #163 最小音频输出 owner：将真实 dm_af_policy 与真实 dm_audio（AAudio）组合起来，音量由真实 data callback 应用。
// 不提供 App/JNI/AudioManager 接线，不做设备播放/延迟/AV 同步声明。
// #163 minimal audio-output owner: combines the real dm_af_policy with the real dm_audio (AAudio) module; gain is
// applied inside the real data callback. No App/JNI/AudioManager wiring and no device playback/latency/AV-sync claim.
//
// Ownership contract:
//   - The owner keeps every AAudio handle independently of the policy's own "started/owned" view. A create that
//     returns -EAGAIN with a cleanup handle, a stream left open by a failed start, and a stream whose close failed are
//     all retained in this object and retired by later serialized control calls until zero handles remain.
//   - The data/error callbacks inside dm_audio never call this policy or close anything; they only touch the bounded
//     ring and the atomic gain. All policy/AAudio control happens on the caller's control thread.
//   - The caller serializes control calls (handle/destroy) on one thread and must stop external producers before
//     destroy; dm_audio_write is single-producer.
#pragma once

#include <stdint.h>

#include "../audio/dm_aaudio.h"
#include "audio_focus.h"

typedef struct {
    dm_audio_options audio_options;   // queue_frames + narrow AAudio api (NULL = Android real table)
    dm_af_policy policy;              // real focus/intent policy; its backend ops land on dm_audio
    dm_audio *audio;                  // owned AAudio handle, independent of policy flags
    int cleanup_only;                 // handle came from dm_audio_create -EAGAIN (must be destroyed, not started)
    int retry_pending;                // a previous retirement failed; retry before creating a new stream
    int replace_pending;              // a bad stream was retired; the next successful open replaces it
    int started;                      // this owner's own dm_audio start state
    int destroy_pending;              // destroy failed; the handle is retained for a serialized retry
    uint32_t generation;              // last accepted start generation (diagnostic)
    uint32_t starts, pauses, stops, gains;
    uint32_t opens, replacements, retire_failures;
    uint32_t start_failures, destroy_retries, stale_events, create_cleanups;
    int last_status;
} dm_audio_output;

// duck_enabled/duck_gain follow the accepted policy contract (gain finite, (0,1]); returns -1 on invalid input.
int dm_audio_output_init(dm_audio_output *output, const dm_audio_options *audio_options, int duck_enabled,
                         float duck_gain);

// Serialized control-thread entry points. Each may create/start/pause/stop the real audio module and set its gain.
dm_af_status dm_audio_output_handle(dm_audio_output *output, dm_af_event event);
dm_af_status dm_audio_output_handle_generation(dm_audio_output *output, uint32_t generation, dm_af_event event);

// Producer entry: appends interleaved PCM16. Never called from callbacks.
uint32_t dm_audio_output_write(dm_audio_output *output, const int16_t *samples, uint32_t frames);

// Stops/closes the real stream and frees it. Returns 0 only when no handle is retained; a failed retirement keeps the
// handle and returns the negative error, so a later serialized call can retry. Must be called on the control thread
// with producers stopped.
int dm_audio_output_destroy(dm_audio_output *output);

// Diagnostics.
int dm_audio_output_has_handle(const dm_audio_output *output);
dm_audio *dm_audio_output_audio(const dm_audio_output *output);
