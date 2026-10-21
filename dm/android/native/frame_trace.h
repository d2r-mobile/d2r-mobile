#pragma once

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// 默认关闭的单帧生产路径记录：只在 property token 变化后，从下一个出图边界起记录恰好一帧，
// 到下一个边界输出结束回执；记录条数有硬上限，超出只计数并在回执里标 truncated。
// 不含地址、图像字节或资源名；只记稳定序号、格式、尺寸、实际 load/stencil 操作与 shader 键。
// A default-off one-frame production-path trace: only after the property token changes, record exactly
// one frame starting at the next present boundary and emit an end receipt at the following boundary;
// records are hard-capped, overflow is only counted and the receipt says truncated.
// No addresses, image bytes or resource names; only stable serials, formats, extents, actual
// load/stencil operations and shader keys.
#define FRAME_TRACE_TOKEN_MAX 96u
#define FRAME_TRACE_RECORD_CAP 1536u

enum { FRAME_TRACE_EVENT_END = 1, FRAME_TRACE_EVENT_BEGIN = 2 };

typedef struct {
    char token[FRAME_TRACE_TOKEN_MAX];   // 最后一次消费的 token | last consumed token
    int armed, active;                   // armed：等下一个边界；active：正在记录本帧 | armed: waiting for the next boundary; active: recording this frame
    uint64_t frame;                      // 被记录帧的序号（= 开始边界 ticket + 1） | recorded frame number (= begin boundary ticket + 1)
    unsigned skip, seen, records, dropped, captures;
} frame_trace_state;

// token 形如 <id> 或 <id>:<skip>；skip 跳过本帧前 skip 条（仍计入 seen），用于分页取被截断的帧。
// The token is <id> or <id>:<skip>; skip drops the first skip records of the frame (still counted in seen),
// to page through a truncated frame.
static inline unsigned frame_trace_skip(const char *token) {
    const char *colon = strrchr(token, ':');
    if (!colon || colon[1] < '0' || colon[1] > '9') return 0;
    char *end = NULL;
    errno = 0;
    unsigned long value = strtoul(colon + 1, &end, 10);
    if (errno || !end || *end || value > 1000000ul) return 0;
    return (unsigned)value;
}

// 报告点调用：新的非空 token 才武装；相同 token 不重开；空 token 撤销等待并允许以后重用同一 token。
// 过长 token 拒绝且不改变状态。正在记录的帧不受影响，仍在边界输出回执。
// Called at the report point: only a new non-empty token arms; the same token never re-arms; an empty token
// cancels a pending arm and allows the same token to be reused later. Overlong tokens are rejected without
// changing state. A frame being recorded is unaffected and still emits its receipt at the boundary.
static inline int frame_trace_tick(frame_trace_state *state, const char *token) {
    if (!token) token = "";
    if (!strcmp(token, state->token)) return 0;
    size_t length = strlen(token);
    if (length >= sizeof state->token) return 0;
    memcpy(state->token, token, length + 1);
    if (!length) { state->armed = 0; return 0; }
    state->armed = 1;
    state->skip = frame_trace_skip(token);
    return 1;
}

// 每个出图边界调用（ticket = 刚完成的帧）：先结束正在记录的帧，再让已武装的请求开始下一帧。
// Called at every present boundary (ticket = the frame just completed): first end the frame being recorded,
// then let an armed request start the next frame.
static inline unsigned frame_trace_boundary(frame_trace_state *state, uint64_t ticket) {
    unsigned events = 0;
    if (state->active) { state->active = 0; events |= FRAME_TRACE_EVENT_END; }
    if (state->armed) {
        state->armed = 0;
        state->active = 1;
        state->frame = ticket + 1;
        state->seen = state->records = state->dropped = 0;
        state->captures++;
        events |= FRAME_TRACE_EVENT_BEGIN;
    }
    return events;
}

// 返回 1 才允许输出一条记录；关闭时只有一次分支。
// A record may be emitted only on 1; when off this is a single branch.
static inline int frame_trace_reserve(frame_trace_state *state) {
    if (!state->active) return 0;
    if (state->seen++ < state->skip) return 0;
    if (state->records >= FRAME_TRACE_RECORD_CAP) { state->dropped++; return 0; }
    state->records++;
    return 1;
}

static inline const char *frame_trace_load_name(unsigned load) {
    switch (load) { case 0: return "load"; case 1: return "clear"; case 2: return "dont_care"; default: return "other"; }
}
