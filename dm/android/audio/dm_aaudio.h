// 有界自有 AAudio PCM16 stereo 48kHz 输出；不含 guest 音频语义、App focus 或 Android 设备验收声明。
// Bounded owned AAudio PCM16 stereo 48kHz output; no guest audio semantics, App focus or Android device acceptance claims.
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __ANDROID__
#include <aaudio/AAudio.h>
typedef AAudioStream dm_aaudio_stream;
typedef AAudioStreamBuilder dm_aaudio_builder;
typedef aaudio_result_t dm_audio_result;
typedef aaudio_data_callback_result_t dm_audio_callback_result;
typedef AAudioStream_dataCallback dm_audio_data_callback;
typedef AAudioStream_errorCallback dm_audio_error_callback;
#define DM_AUDIO_FORMAT_PCM16 AAUDIO_FORMAT_PCM_I16
#define DM_AUDIO_OK AAUDIO_OK
#define DM_AUDIO_STATE_STOPPED AAUDIO_STREAM_STATE_STOPPED
#define DM_AUDIO_CALLBACK_CONTINUE AAUDIO_CALLBACK_RESULT_CONTINUE
#define DM_AUDIO_CALLBACK_STOP AAUDIO_CALLBACK_RESULT_STOP
#else
typedef struct dm_aaudio_stream dm_aaudio_stream;
typedef struct dm_aaudio_builder dm_aaudio_builder;
typedef int32_t dm_audio_result;
typedef int32_t dm_audio_callback_result;
typedef dm_audio_callback_result (*dm_audio_data_callback)(dm_aaudio_stream *, void *, void *, int32_t);
typedef void (*dm_audio_error_callback)(dm_aaudio_stream *, void *, dm_audio_result);
#define DM_AUDIO_FORMAT_PCM16 2
#define DM_AUDIO_OK 0
#define DM_AUDIO_STATE_STOPPED 10
#define DM_AUDIO_CALLBACK_CONTINUE 0
#define DM_AUDIO_CALLBACK_STOP 1
#endif

#define DM_AUDIO_CHANNELS 2
#define DM_AUDIO_RATE 48000
#define DM_AUDIO_FRAME_BYTES (DM_AUDIO_CHANNELS * 2)

// 窄 AAudio 边界：生产默认表指向真实 AAudio；宿主测试只替换这些入口。回调类型与 AAudio 原文一致。
// Narrow AAudio boundary: production default table points at real AAudio; host tests replace only these entries.
typedef struct {
    dm_audio_result (*create_builder)(dm_aaudio_builder **builder);
    dm_audio_result (*delete_builder)(dm_aaudio_builder *builder);
    void (*set_format)(dm_aaudio_builder *, int32_t);
    void (*set_channel_count)(dm_aaudio_builder *, int32_t);
    void (*set_sample_rate)(dm_aaudio_builder *, int32_t);
    void (*set_direction)(dm_aaudio_builder *, int32_t);
    void (*set_data_callback)(dm_aaudio_builder *, dm_audio_data_callback, void *);
    void (*set_error_callback)(dm_aaudio_builder *, dm_audio_error_callback, void *);
    dm_audio_result (*open)(dm_aaudio_builder *, dm_aaudio_stream **);
    dm_audio_result (*close)(dm_aaudio_stream *);
    dm_audio_result (*request_start)(dm_aaudio_stream *);
    dm_audio_result (*request_pause)(dm_aaudio_stream *);
    dm_audio_result (*request_stop)(dm_aaudio_stream *);
    dm_audio_result (*get_state)(dm_aaudio_stream *);
    dm_audio_result (*wait_for_state_change)(dm_aaudio_stream *, int32_t current, int32_t *next, int64_t timeout_ns);
    dm_audio_result (*get_format)(dm_aaudio_stream *);
    dm_audio_result (*get_channel_count)(dm_aaudio_stream *);
    dm_audio_result (*get_sample_rate)(dm_aaudio_stream *);
    dm_audio_result (*get_buffer_size)(dm_aaudio_stream *);
    dm_audio_result (*get_xrun_count)(dm_aaudio_stream *);
} dm_audio_api;

typedef struct {
    uint32_t struct_size;        // sizeof(dm_audio_options)
    uint32_t queue_frames;       // 固定环形缓冲帧数；不足 2 帧拒绝 | fixed ring frames, reject <2
    const dm_audio_api *api;     // NULL = Android real AAudio; host NULL = unsupported
} dm_audio_options;
#define DM_AUDIO_OPTIONS_INIT { sizeof(dm_audio_options), 0, NULL }

typedef struct {
    uint64_t frames_written;     // 成功复制进队列的帧 | frames copied into queue
    uint64_t frames_dropped;     // 队列满时丢弃的帧 | frames dropped on full queue
    uint64_t underrun_frames;    // 回调填静音的帧；不是已播放数据 | silence-filled frames, not played data
    uint64_t underrun_events;
    uint64_t callbacks;
    uint64_t xruns;              // 驱动报告值，仅诊断，不与 underrun 混同 | driver-reported, diagnostic only
    uint32_t gain_q15;           // 当前音量（Q15，0..32768） | current gain in Q15
    int disconnected;
    int closing;
} dm_audio_stats;

typedef struct dm_audio dm_audio;

// 创建并立即 open/验证协商格式。普通失败 out=NULL 且已回滚。唯一例外：协商失败后的回滚 close 失败，
// 返回 -EAGAIN 且 *out 为 cleanup handle；调用者拥有它，必须调用 dm_audio_destroy 直到 0。
// Creates and opens/validates negotiated format. Normal failures return out=NULL after rollback.
// Sole exception: rollback close fails after negotiation mismatch; returns -EAGAIN and *out is a cleanup handle
// owned by the caller, which must call dm_audio_destroy until it returns 0.
int dm_audio_create(const dm_audio_options *options, dm_audio **out);
int dm_audio_start(dm_audio *audio);
int dm_audio_pause(dm_audio *audio);
int dm_audio_stop(dm_audio *audio);
// 生产线程追加 interleaved PCM16；返回实际写入帧，overflow/partial 由 stats 明确记录，不伪造写入。
// Producer appends interleaved PCM16; returns frames actually written and records overflow/partial explicitly.
uint32_t dm_audio_write(dm_audio *audio, const int16_t *samples, uint32_t frames);
// 有界有限音量（0..1），在真实 data callback 输出 PCM 时生效，因此已入队但尚未回调的数据也服从最新音量。
// 1.0 位精确保留样本，0 是有意静音（不计 underrun）；非法/越界值返回 -EINVAL 且不改变现有音量。
// 回调内只做一次原子读取与整数定点乘法，不分配、不加锁、不做文件 IO；量化规则见实现（Q15，四舍五入，防溢出）。
// Bounded finite gain (0..1) applied inside the real data callback, so frames already queued but not yet delivered
// still obey the latest gain. 1.0 preserves samples bit-exactly and 0 is intentional mute (not underrun); invalid or
// out-of-range input returns -EINVAL and leaves the current gain unchanged. The callback only performs one atomic load
// and integer fixed-point multiplies: no allocation, locking or file IO. Quantisation is documented in the source.
int dm_audio_set_gain(dm_audio *audio, float gain);
uint32_t dm_audio_gain_q15(const dm_audio *audio);   // 诊断/测试用 | diagnostic/test accessor
void dm_audio_get_stats(const dm_audio *audio, dm_audio_stats *stats);
// 停止 → 等已进入回调退出 → close/free；从回调内调用返回 -EBUSY，不重入 close。
// close 失败保留 stream，串行 retry 可再次 close；close 成功但 stop/wait 未证明时保留 CLOSED，retry 只做最终释放。
// stop -> retire the active callback -> close/free; callback-context calls return -EBUSY.
// A failed close retains the stream for a serialized retry; close success with unproven stop/wait retains CLOSED and retry only final-releases.
int dm_audio_destroy(dm_audio *audio);

#ifdef DM_AUDIO_TEST
// 仅测试构建：在真实回调 active 区内注入观测/阻塞点，用于证明 owner 与回调的退休顺序。
// Test build only: inject an observation/blocking point inside the real active-callback window.
typedef void (*dm_audio_test_callback_hook)(void *user);
void dm_audio_test_set_callback_hook(dm_audio *audio, dm_audio_test_callback_hook hook, void *user);
#endif

// Android 真实 AAudio 表；非 Android 返回 NULL。测试显式传入 stub 表。
const dm_audio_api *dm_audio_default_api(void);
