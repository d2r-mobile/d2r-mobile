// 自有 AAudio 输出：真实 open/配置校验/start/pause/stop/close，固定 PCM16 stereo 48k，单生产者有界队列。
// Owned AAudio output: real open/config validation/start/pause/stop/close, fixed PCM16 stereo 48k, bounded single-producer queue.
#include "dm_aaudio.h"
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

struct dm_audio {
    const dm_audio_api *api;
    dm_aaudio_stream *stream;
    int16_t *ring;
    uint32_t capacity, mask;
    _Atomic uint32_t head, tail;
    _Atomic uint64_t written, dropped, underrun_frames, underrun_events, callbacks;
    _Atomic int closing, disconnected, callback_active;
    _Atomic uintptr_t callback_thread;
    _Atomic int teardown; // ACTIVE / DESTROYING / CLOSE_FAILED / CLOSED
    int stop_ok, wait_ok;
    pthread_mutex_t lifecycle;
    int started;
#ifdef DM_AUDIO_TEST
    dm_audio_test_callback_hook test_hook;
    void *test_hook_user;
#endif
};

enum {
    DM_TEARDOWN_ACTIVE = 0,
    DM_TEARDOWN_DESTROYING,
    DM_TEARDOWN_CLOSE_FAILED,
    DM_TEARDOWN_CLOSED,
};

#ifdef __ANDROID__
static const dm_audio_api g_real_api = {
    .create_builder = AAudio_createStreamBuilder,
    .delete_builder = AAudioStreamBuilder_delete,
    .set_format = AAudioStreamBuilder_setFormat,
    .set_channel_count = AAudioStreamBuilder_setChannelCount,
    .set_sample_rate = AAudioStreamBuilder_setSampleRate,
    .set_direction = AAudioStreamBuilder_setDirection,
    .set_data_callback = AAudioStreamBuilder_setDataCallback,
    .set_error_callback = AAudioStreamBuilder_setErrorCallback,
    .open = AAudioStreamBuilder_openStream,
    .close = AAudioStream_close,
    .request_start = AAudioStream_requestStart,
    .request_pause = AAudioStream_requestPause,
    .request_stop = AAudioStream_requestStop,
    .get_state = AAudioStream_getState,
    .wait_for_state_change = AAudioStream_waitForStateChange,
    .get_format = AAudioStream_getFormat,
    .get_channel_count = AAudioStream_getChannelCount,
    .get_sample_rate = AAudioStream_getSampleRate,
    .get_buffer_size = AAudioStream_getBufferSizeInFrames,
    .get_xrun_count = AAudioStream_getXRunCount,
};
const dm_audio_api *dm_audio_default_api(void) { return &g_real_api; }
#else
const dm_audio_api *dm_audio_default_api(void) { return NULL; }
#endif

static uint32_t round_pow2(uint32_t n) {
    uint32_t p = 2;
    while (p < n && p < (1u << 20)) p <<= 1;
    return p;
}

static void dm_audio_free(dm_audio *a);

static dm_audio_callback_result data_callback(dm_aaudio_stream *stream, void *user, void *data, int32_t num_frames) {
    (void)stream;
    dm_audio *a = user;
    if (!a || num_frames <= 0 || atomic_load_explicit(&a->closing, memory_order_acquire))
        return DM_AUDIO_CALLBACK_STOP;
    atomic_store_explicit(&a->callback_thread, (uintptr_t)pthread_self(), memory_order_relaxed);
    atomic_fetch_add_explicit(&a->callback_active, 1, memory_order_acq_rel);
    if (atomic_load_explicit(&a->closing, memory_order_acquire)) {
        atomic_store_explicit(&a->callback_thread, 0, memory_order_relaxed);
        atomic_fetch_sub_explicit(&a->callback_active, 1, memory_order_acq_rel);
        return DM_AUDIO_CALLBACK_STOP;
    }
    atomic_fetch_add_explicit(&a->callbacks, 1, memory_order_relaxed);
#ifdef DM_AUDIO_TEST
    if (a->test_hook) a->test_hook(a->test_hook_user);
#endif
    uint32_t frames = (uint32_t)num_frames;
    uint32_t tail = atomic_load_explicit(&a->tail, memory_order_relaxed);
    uint32_t head = atomic_load_explicit(&a->head, memory_order_acquire);
    uint32_t available = head - tail;
    uint32_t take = available < frames ? available : frames;
    int16_t *out = data;
    for (uint32_t i = 0; i < take * DM_AUDIO_CHANNELS; i++) out[i] = a->ring[(tail * DM_AUDIO_CHANNELS + i) & (a->capacity * DM_AUDIO_CHANNELS - 1)];
    if (take < frames) {
        memset(out + take * DM_AUDIO_CHANNELS, 0, (size_t)(frames - take) * DM_AUDIO_FRAME_BYTES);
        atomic_fetch_add_explicit(&a->underrun_frames, frames - take, memory_order_relaxed);
        atomic_fetch_add_explicit(&a->underrun_events, 1, memory_order_relaxed);
    }
    atomic_store_explicit(&a->tail, tail + take, memory_order_release);
    atomic_store_explicit(&a->callback_thread, 0, memory_order_relaxed);
    atomic_fetch_sub_explicit(&a->callback_active, 1, memory_order_acq_rel);
    return DM_AUDIO_CALLBACK_CONTINUE;
}

static void error_callback(dm_aaudio_stream *stream, void *user, dm_audio_result error) {
    (void)stream; (void)error;
    dm_audio *a = user;
    if (a) atomic_store_explicit(&a->disconnected, 1, memory_order_release);
}

int dm_audio_create(const dm_audio_options *options, dm_audio **out) {
    if (!out) return -EINVAL;
    *out = NULL;
    if (!options || options->struct_size != sizeof *options || options->queue_frames < 2) return -EINVAL;
    const dm_audio_api *api = options->api ? options->api : dm_audio_default_api();
    if (!api) return -ENOTSUP;
    if (!api->create_builder || !api->delete_builder || !api->open || !api->close ||
        !api->set_format || !api->set_channel_count || !api->set_sample_rate ||
        !api->set_data_callback || !api->set_error_callback ||
        !api->request_start || !api->request_pause || !api->request_stop || !api->get_format ||
        !api->get_channel_count || !api->get_sample_rate || !api->get_state || !api->wait_for_state_change)
        return -EINVAL;
    if (options->queue_frames > (1u << 20)) return -EINVAL;
    dm_audio *a = calloc(1, sizeof *a);
    if (!a) return -ENOMEM;
    a->api = api;
    a->capacity = round_pow2(options->queue_frames);
    a->mask = a->capacity - 1;
    a->ring = calloc((size_t)a->capacity * DM_AUDIO_CHANNELS, sizeof *a->ring);
    if (!a->ring) { free(a); return -ENOMEM; }
    if (pthread_mutex_init(&a->lifecycle, NULL) != 0) { free(a->ring); free(a); return -ENOMEM; }
    dm_aaudio_builder *builder = NULL;
    dm_audio_result result = api->create_builder(&builder);
    if (result != DM_AUDIO_OK || !builder) { pthread_mutex_destroy(&a->lifecycle); free(a->ring); free(a); return -EIO; }
    api->set_format(builder, DM_AUDIO_FORMAT_PCM16);
    api->set_channel_count(builder, DM_AUDIO_CHANNELS);
    api->set_sample_rate(builder, DM_AUDIO_RATE);
    if (api->set_direction) api->set_direction(builder, 0); // AAUDIO_DIRECTION_OUTPUT
    if (api->set_data_callback) api->set_data_callback(builder, data_callback, a);
    if (api->set_error_callback) api->set_error_callback(builder, error_callback, a);
    result = api->open(builder, &a->stream);
    (void)api->delete_builder(builder);
    if (result != DM_AUDIO_OK || !a->stream) {
        pthread_mutex_destroy(&a->lifecycle); free(a->ring); free(a); return -EIO;
    }
    if (api->get_format(a->stream) != DM_AUDIO_FORMAT_PCM16 ||
        api->get_channel_count(a->stream) != DM_AUDIO_CHANNELS ||
        api->get_sample_rate(a->stream) != DM_AUDIO_RATE) {
        dm_audio_result close = api->close(a->stream);
        if (close == DM_AUDIO_OK) {
            a->stream = NULL;
            dm_audio_free(a);
            return -EPROTO;
        }
        // 回滚 close 失败：stream 仍注册 a 为 callback userdata，不能释放 a。
        // 调用者通过返回的 cleanup handle 继续 retry；见头文件 -EAGAIN 契约。
        // Rollback close failed: the stream still holds a as callback userdata, so a cannot be freed.
        // The caller owns the returned cleanup handle and retries destroy; see the -EAGAIN header contract.
        atomic_store_explicit(&a->closing, 1, memory_order_release);
        a->stop_ok = 1; // opened but never started; rollback cleanup only needs close retry
        a->wait_ok = 1;
        atomic_store_explicit(&a->teardown, DM_TEARDOWN_CLOSE_FAILED, memory_order_release);
        *out = a;
        return -EAGAIN;
    }
    *out = a;
    return 0;
}

int dm_audio_start(dm_audio *a) {
    if (!a || atomic_load_explicit(&a->closing, memory_order_acquire)) return -EINVAL;
    if (atomic_load_explicit(&a->disconnected, memory_order_acquire)) return -ENOTCONN;
    dm_audio_result r = a->api->request_start(a->stream);
    if (r == DM_AUDIO_OK) a->started = 1;
    return r == DM_AUDIO_OK ? 0 : -EIO;
}
int dm_audio_pause(dm_audio *a) {
    if (!a || !a->stream) return -EINVAL;
    dm_audio_result r = a->api->request_pause(a->stream);
    if (r == DM_AUDIO_OK) a->started = 0;
    return r == DM_AUDIO_OK ? 0 : -EIO;
}
int dm_audio_stop(dm_audio *a) {
    if (!a || !a->stream) return -EINVAL;
    dm_audio_result r = a->api->request_stop(a->stream);
    if (r == DM_AUDIO_OK) a->started = 0;
    return r == DM_AUDIO_OK ? 0 : -EIO;
}

uint32_t dm_audio_write(dm_audio *a, const int16_t *samples, uint32_t frames) {
    if (!a || !samples || !frames) return 0;
    if (atomic_load_explicit(&a->closing, memory_order_acquire) ||
        atomic_load_explicit(&a->disconnected, memory_order_acquire)) return 0;
    uint32_t head = atomic_load_explicit(&a->head, memory_order_relaxed);
    uint32_t tail = atomic_load_explicit(&a->tail, memory_order_acquire);
    uint32_t free_frames = a->capacity - (head - tail);
    uint32_t take = frames < free_frames ? frames : free_frames;
    for (uint32_t i = 0; i < take * DM_AUDIO_CHANNELS; i++)
        a->ring[(head * DM_AUDIO_CHANNELS + i) & (a->capacity * DM_AUDIO_CHANNELS - 1)] =
            samples[i];
    atomic_store_explicit(&a->head, head + take, memory_order_release);
    atomic_fetch_add_explicit(&a->written, take, memory_order_relaxed);
    if (take < frames) atomic_fetch_add_explicit(&a->dropped, frames - take, memory_order_relaxed);
    return take;
}

void dm_audio_get_stats(const dm_audio *a, dm_audio_stats *stats) {
    if (!stats) return;
    memset(stats, 0, sizeof *stats);
    if (!a) return;
    stats->frames_written = atomic_load_explicit(&a->written, memory_order_relaxed);
    stats->frames_dropped = atomic_load_explicit(&a->dropped, memory_order_relaxed);
    stats->underrun_frames = atomic_load_explicit(&a->underrun_frames, memory_order_relaxed);
    stats->underrun_events = atomic_load_explicit(&a->underrun_events, memory_order_relaxed);
    stats->callbacks = atomic_load_explicit(&a->callbacks, memory_order_relaxed);
    stats->disconnected = atomic_load_explicit(&a->disconnected, memory_order_acquire);
    stats->closing = atomic_load_explicit(&a->closing, memory_order_acquire);
    if (a->api && a->api->get_xrun_count && a->stream) {
        dm_audio_result x = a->api->get_xrun_count(a->stream);
        if (x > 0) stats->xruns = (uint64_t)x;
    }
}

static void dm_audio_free(dm_audio *a) {
    pthread_mutex_destroy(&a->lifecycle);
    free(a->ring);
    free(a);
}

int dm_audio_destroy(dm_audio *a) {
    if (!a) return 0;
    pthread_mutex_lock(&a->lifecycle);
    if (atomic_load_explicit(&a->callback_thread, memory_order_relaxed) == (uintptr_t)pthread_self()) {
        pthread_mutex_unlock(&a->lifecycle);
        return -EBUSY;
    }
    int state = atomic_load_explicit(&a->teardown, memory_order_acquire);
    if (state == DM_TEARDOWN_DESTROYING) {
        pthread_mutex_unlock(&a->lifecycle);
        return -EBUSY;
    }
    if (state == DM_TEARDOWN_CLOSED) {
        // 上次 close 已成功，只是 stop/wait 未证明；流已关闭，重试只做最终释放。
        // Close already succeeded; retry only completes final release, never closes twice.
        atomic_store_explicit(&a->teardown, DM_TEARDOWN_DESTROYING, memory_order_release);
        pthread_mutex_unlock(&a->lifecycle);
        dm_audio_free(a);
        return 0;
    }
    if (state == DM_TEARDOWN_CLOSE_FAILED) {
        // close 失败后流仍由本对象拥有；串行重试可再次 close，不永久锁死。
        // A failed close retained the stream; a serialized retry may close it again.
        atomic_store_explicit(&a->teardown, DM_TEARDOWN_DESTROYING, memory_order_release);
        pthread_mutex_unlock(&a->lifecycle);
    } else {
        atomic_store_explicit(&a->closing, 1, memory_order_release);
        atomic_store_explicit(&a->teardown, DM_TEARDOWN_DESTROYING, memory_order_release);
        pthread_mutex_unlock(&a->lifecycle);
    }

    if (!a->stop_ok) {
        dm_audio_result stop = a->api->request_stop(a->stream);
        a->stop_ok = (stop == DM_AUDIO_OK);
    }
    if (!a->wait_ok) {
        int32_t current = a->api->get_state(a->stream);
        int32_t next = current;
        dm_audio_result wait = DM_AUDIO_OK;
        if (current != DM_AUDIO_STATE_STOPPED)
            wait = a->api->wait_for_state_change(a->stream, current, &next, 100000000);
        a->wait_ok = (wait == DM_AUDIO_OK && next == DM_AUDIO_STATE_STOPPED);
    }
    // AAudio 文档：requestStop 后不再开始新数据回调；先等已进入的回调退出，再 close。
    // AAudio docs: no new data callback starts after requestStop; retire the active callback before close.
    while (atomic_load_explicit(&a->callback_active, memory_order_acquire) != 0) sched_yield();
    dm_audio_result close = a->api->close(a->stream);
    if (close == DM_AUDIO_OK) a->stream = NULL;
    // callback 的最后一次对象访问必须在 active-- 之前；此处 active==0 才是真正可释放证明。
    // The callback's last object access precedes active--; active==0 is the release proof.
    while (atomic_load_explicit(&a->callback_active, memory_order_acquire) != 0) sched_yield();
    if (close != DM_AUDIO_OK) {
        atomic_store_explicit(&a->teardown, DM_TEARDOWN_CLOSE_FAILED, memory_order_release);
        return -EIO;
    }
    if (!a->stop_ok || !a->wait_ok) {
        // close 已成功，无 NULL 解引用、无二次 close；保留到下一次串行 retry 做真实最终释放。
        // Close succeeded; retain until a serialized retry performs the real final release.
        atomic_store_explicit(&a->teardown, DM_TEARDOWN_CLOSED, memory_order_release);
        return -EIO;
    }
    dm_audio_free(a);
    return 0;
}

#ifdef DM_AUDIO_TEST
void dm_audio_test_set_callback_hook(dm_audio *a, dm_audio_test_callback_hook hook, void *user) {
    if (!a) return;
    a->test_hook = hook;
    a->test_hook_user = user;
}
#endif
