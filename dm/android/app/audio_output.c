// #163 音频输出 owner 实现：策略决定"该不该播/多大声"，真实 dm_audio 负责流与 PCM；句柄始终由本 owner 保留。
// #163 owner implementation: the policy decides whether/how loud, the real dm_audio owns the stream and PCM; handles
// always stay with this owner.
#include "audio_output.h"

#include <errno.h>
#include <string.h>

static int retire_cleanup_handle(dm_audio_output *output) {
    if (!output->audio) return 0;
    const int result = dm_audio_destroy(output->audio);
    if (result != 0) {                    // 关闭失败/未证明：保留句柄，等下一次串行重试
        output->retry_pending = 1;
        output->retire_failures++;
        return result;
    }
    output->audio = NULL;
    output->cleanup_only = 0;
    output->started = 0;
    output->retry_pending = 0;
    return 0;
}

// 真实 error callback 已把流标记为 disconnected：坏流不可复用，也不能靠 stop 假装恢复。
// 真实状态只来自 dm_audio 的 stats，不在此清除 disconnected。| The real error callback marked the stream
// disconnected; it must not be reused or "cleared", only retired on the control thread.
static int stream_unusable(const dm_audio_output *output) {
    if (!output->audio) return 0;
    dm_audio_stats stats;
    dm_audio_get_stats(output->audio, &stats);
    return stats.disconnected != 0 || stats.closing != 0;
}

static int retire_unusable_stream(dm_audio_output *output) {
    output->started = 0;
    const int result = retire_cleanup_handle(output);
    if (result == 0) output->replace_pending = 1;   // 下一次成功 open 即为替换流 | the next open replaces it
    return result;
}

static int backend_start(void *ctx, uint32_t generation) {
    dm_audio_output *output = ctx;
    output->generation = generation;

    // 断流的真实句柄必须先退休（安全 stop/close/重试），再用新 open 恢复；复用它只会再次失败。
    // A disconnected real handle must be retired first (safe stop/close/retry) and replaced by a new open.
    if (output->audio && stream_unusable(output)) {
        if (retire_unusable_stream(output) != 0) { output->start_failures++; return -1; }
        output->replacements++;
    }

    if (!output->audio) {
        // 先退休上一条清理句柄，成功后才允许创建新流；失败则如实返回，不丢弃句柄。
        // Retire a previous cleanup handle first; only then create. A failure is reported and nothing is dropped.
        if (output->retry_pending && retire_cleanup_handle(output) != 0) { output->start_failures++; return -1; }
        dm_audio *audio = NULL;
        const int created = dm_audio_create(&output->audio_options, &audio);
        if (created == -EAGAIN && audio) {
            // 协商回滚 close 失败：句柄归本 owner；不能 start，等串行 destroy 重试。
            // Rollback close failed: the handle belongs to this owner, cannot be started, retired by serialized destroy.
            output->audio = audio;
            output->cleanup_only = 1;
            output->create_cleanups++;
            output->start_failures++;
            return -1;
        }
        if (created != 0 || !audio) { output->start_failures++; return -1; }
        output->audio = audio;
        output->cleanup_only = 0;
        output->opens++;
        if (output->replace_pending) { output->replacements++; output->replace_pending = 0; }
    }
    if (output->cleanup_only) { output->start_failures++; return -1; }

    // start 失败时句柄仍保留在本 owner（可能已 open 但未运行），后续 destroy 会退休它。
    // On a failed start the handle stays with this owner (open but not running) and destroy retires it later.
    if (dm_audio_start(output->audio) != 0) { output->start_failures++; return -1; }
    output->started = 1;
    output->starts++;
    return 0;
}

static int backend_pause(void *ctx) {
    dm_audio_output *output = ctx;
    if (!output->audio || output->cleanup_only) return -1;
    if (stream_unusable(output)) {
        // 已断流的句柄不能 pause；安全退休等价于暂停，失败则保留句柄由调用方重试。
        // A disconnected handle cannot be paused; retiring it is the honest equivalent, with retention on failure.
        if (retire_unusable_stream(output) != 0) return -1;
        output->pauses++;
        return 0;
    }
    if (dm_audio_pause(output->audio) != 0) return -1;
    output->started = 0;
    output->pauses++;
    return 0;
}

static int backend_stop(void *ctx) {
    dm_audio_output *output = ctx;
    if (!output->audio || output->cleanup_only) return -1;
    if (stream_unusable(output)) {
        // stop 请求本身可能失败；destroy 仍会 close 并保留失败句柄供串行重试，绝不声明坏流已复活。
        // The stop request itself may fail; destroy still closes and retains failures for a serialized retry.
        if (retire_unusable_stream(output) != 0) return -1;
        output->stops++;
        return 0;
    }
    if (dm_audio_stop(output->audio) != 0) return -1;
    output->started = 0;
    output->stops++;
    return 0;
}

static int backend_gain(void *ctx, float gain) {
    dm_audio_output *output = ctx;
    if (!output->audio || output->cleanup_only) return -1;
    if (dm_audio_set_gain(output->audio, gain) != 0) return -1;
    output->gains++;
    return 0;
}

int dm_audio_output_init(dm_audio_output *output, const dm_audio_options *audio_options, int duck_enabled,
                         float duck_gain) {
    if (!output || !audio_options) return -1;
    memset(output, 0, sizeof *output);
    output->audio_options = *audio_options;
    const dm_af_backend backend = {.ctx = output, .start = backend_start, .pause = backend_pause,
                                   .stop = backend_stop, .set_gain = backend_gain};
    if (dm_af_init(&output->policy, &backend, duck_enabled, duck_gain) != 0) return -1;
    output->last_status = DM_AF_OK;
    return 0;
}

dm_af_status dm_audio_output_handle(dm_audio_output *output, dm_af_event event) {
    if (!output) return DM_AF_INVALID;
    output->last_status = (int)dm_af_handle(&output->policy, event);
    return (dm_af_status)output->last_status;
}

dm_af_status dm_audio_output_handle_generation(dm_audio_output *output, uint32_t generation, dm_af_event event) {
    if (!output) return DM_AF_INVALID;
    const dm_af_status status = dm_af_handle_generation(&output->policy, generation, event);
    if (status == DM_AF_STALE) output->stale_events++;
    output->last_status = (int)status;
    return status;
}

uint32_t dm_audio_output_write(dm_audio_output *output, const int16_t *samples, uint32_t frames) {
    if (!output || !output->audio || output->cleanup_only) return 0;
    return dm_audio_write(output->audio, samples, frames);
}

int dm_audio_output_destroy(dm_audio_output *output) {
    if (!output) return -EINVAL;
    if (!output->audio) return 0;
    const int result = dm_audio_destroy(output->audio);
    if (result != 0) {
        // 失败/未证明：保留句柄，调用方可在控制线程串行重试；绝不再 free 一次。
        // Failure/unproven: keep the handle for a serialized retry; never free it twice.
        output->destroy_pending = 1;
        output->destroy_retries++;
        return result;
    }
    output->audio = NULL;
    output->cleanup_only = 0;
    output->started = 0;
    output->destroy_pending = 0;
    return 0;
}

int dm_audio_output_has_handle(const dm_audio_output *output) { return output && output->audio != NULL; }
dm_audio *dm_audio_output_audio(const dm_audio_output *output) { return output ? output->audio : NULL; }
