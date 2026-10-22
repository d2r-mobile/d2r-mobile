#pragma once
// Minimal fake AAudio surface for the owned actual-caller regression. No device, no real stream.
#include <stdint.h>
#include <stddef.h>

typedef struct AAudioStream { int unused; } AAudioStream;
typedef struct AAudioStreamBuilder { int unused; } AAudioStreamBuilder;
typedef int32_t aaudio_result_t;
typedef int32_t aaudio_stream_state_t;
typedef int32_t aaudio_data_callback_result_t;

enum {
    AAUDIO_OK = 0,
    AAUDIO_ERROR_TIMEOUT = -895,
    AAUDIO_DIRECTION_OUTPUT = 0,
    AAUDIO_SHARING_MODE_SHARED = 0,
    AAUDIO_FORMAT_PCM_I16 = 1,
    AAUDIO_PERFORMANCE_MODE_LOW_LATENCY = 12,
    AAUDIO_USAGE_GAME = 14,
    AAUDIO_CALLBACK_RESULT_CONTINUE = 0,
    AAUDIO_STREAM_STATE_STARTED = 2,
    AAUDIO_STREAM_STATE_STOPPED = 4,
    AAUDIO_STREAM_STATE_CLOSED = 7,
    AAUDIO_STREAM_STATE_DISCONNECTED = 8,
};

typedef aaudio_data_callback_result_t (*aaudio_data_callback_t)(AAudioStream *, void *, void *, int32_t);

static struct {
    int state;
    int request_stop_result;
    int start_result;
    int close_calls;
    int open_calls;
    int start_calls;
    int wait_calls;
    int sample_rate;
    int channels;
    int format;
} fake_aaudio = {.state = AAUDIO_STREAM_STATE_STARTED, .sample_rate = 48000, .channels = 2, .format = AAUDIO_FORMAT_PCM_I16};
static AAudioStream fake_stream_object;

static inline aaudio_result_t AAudio_createStreamBuilder(AAudioStreamBuilder **out) { *out = (AAudioStreamBuilder *)&fake_stream_object; return AAUDIO_OK; }
static inline void AAudioStreamBuilder_delete(AAudioStreamBuilder *b) { (void)b; }
static inline void AAudioStreamBuilder_setDirection(AAudioStreamBuilder *b, int v) { (void)b; (void)v; }
static inline void AAudioStreamBuilder_setSharingMode(AAudioStreamBuilder *b, int v) { (void)b; (void)v; }
static inline void AAudioStreamBuilder_setFormat(AAudioStreamBuilder *b, int v) { (void)b; (void)v; }
static inline void AAudioStreamBuilder_setSampleRate(AAudioStreamBuilder *b, int v) { (void)b; (void)v; }
static inline void AAudioStreamBuilder_setChannelCount(AAudioStreamBuilder *b, int v) { (void)b; (void)v; }
static inline void AAudioStreamBuilder_setPerformanceMode(AAudioStreamBuilder *b, int v) { (void)b; (void)v; }
static inline void AAudioStreamBuilder_setUsage(AAudioStreamBuilder *b, int v) { (void)b; (void)v; }
static inline void AAudioStreamBuilder_setDataCallback(AAudioStreamBuilder *b, aaudio_data_callback_t cb, void *user) { (void)b; (void)cb; (void)user; }
static inline aaudio_result_t AAudioStreamBuilder_openStream(AAudioStreamBuilder *b, AAudioStream **out) { (void)b; ++fake_aaudio.open_calls; *out = &fake_stream_object; return AAUDIO_OK; }
static inline int AAudioStream_getSampleRate(AAudioStream *s) { (void)s; return fake_aaudio.sample_rate; }
static inline int AAudioStream_getChannelCount(AAudioStream *s) { (void)s; return fake_aaudio.channels; }
static inline int AAudioStream_getFormat(AAudioStream *s) { (void)s; return fake_aaudio.format; }
static inline int AAudioStream_getDeviceId(AAudioStream *s) { (void)s; return 1; }
static inline int AAudioStream_getFramesPerBurst(AAudioStream *s) { (void)s; return 192; }
static inline int AAudioStream_getPerformanceMode(AAudioStream *s) { (void)s; return AAUDIO_PERFORMANCE_MODE_LOW_LATENCY; }
static inline aaudio_stream_state_t AAudioStream_getState(AAudioStream *s) { (void)s; return fake_aaudio.state; }
static inline aaudio_result_t AAudioStream_requestStart(AAudioStream *s) { (void)s; ++fake_aaudio.start_calls; if (fake_aaudio.start_result) return fake_aaudio.start_result; fake_aaudio.state = AAUDIO_STREAM_STATE_STARTED; return AAUDIO_OK; }
static inline aaudio_result_t AAudioStream_requestStop(AAudioStream *s) { (void)s; if (fake_aaudio.request_stop_result) return fake_aaudio.request_stop_result; fake_aaudio.state = AAUDIO_STREAM_STATE_STOPPED; return AAUDIO_OK; }
static inline aaudio_result_t AAudioStream_waitForStateChange(AAudioStream *s, aaudio_stream_state_t current, aaudio_stream_state_t *next, int64_t timeout) {
    (void)s; (void)timeout; ++fake_aaudio.wait_calls;
    if (current == fake_aaudio.state) return AAUDIO_ERROR_TIMEOUT;
    *next = fake_aaudio.state; return AAUDIO_OK;
}
static inline void AAudioStream_close(AAudioStream *s) { (void)s; ++fake_aaudio.close_calls; fake_aaudio.state = AAUDIO_STREAM_STATE_CLOSED; }
static inline const char *AAudio_convertResultToText(aaudio_result_t r) { (void)r; return "fake"; }
