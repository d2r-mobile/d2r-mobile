// 单帧记录状态机：默认关闭、一次性、重新武装、帧边界回执与上限截断。
// One-frame trace state machine: default off, one-shot, re-arm, frame-boundary receipt and cap truncation.
#include "frame_trace.h"

#include <stdio.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

// 模拟一帧里 n 次记录请求，返回实际允许输出的条数。
// Simulate n record requests within one frame and return how many were allowed to emit.
static unsigned frame(frame_trace_state *s, unsigned n) {
    unsigned emitted = 0;
    for (unsigned i = 0; i < n; i++) emitted += (unsigned)frame_trace_reserve(s);
    return emitted;
}

int main(void) {
    frame_trace_state s = {0};
    // 默认关闭：没有 token 时任何帧都不输出，边界无事件。
    // Off by default: with no token nothing is emitted and boundaries produce no events.
    CHECK(!frame_trace_tick(&s, ""));
    CHECK(!frame_trace_tick(&s, NULL));
    CHECK(frame_trace_boundary(&s, 59) == 0);
    CHECK(frame(&s, 100) == 0);
    CHECK(s.records == 0 && s.captures == 0);

    // 武装后当前帧剩余部分不记录；下一个边界开始，正好一帧。
    // After arming, the rest of the current frame is not recorded; recording starts at the next boundary, exactly one frame.
    CHECK(frame_trace_tick(&s, "a1"));
    CHECK(frame(&s, 5) == 0);
    CHECK(frame_trace_boundary(&s, 60) == FRAME_TRACE_EVENT_BEGIN);
    CHECK(s.frame == 61 && s.active);
    CHECK(frame(&s, 7) == 7);
    CHECK(frame_trace_boundary(&s, 61) == FRAME_TRACE_EVENT_END);
    CHECK(s.records == 7 && s.dropped == 0 && !s.active);
    CHECK(frame(&s, 9) == 0);
    CHECK(frame_trace_boundary(&s, 62) == 0);

    // 同一 token 不重开；空 token 后同一 token 可再次使用。
    // The same token never re-arms; after an empty token the same token can be used again.
    CHECK(!frame_trace_tick(&s, "a1"));
    CHECK(frame_trace_boundary(&s, 120) == 0);
    CHECK(!frame_trace_tick(&s, ""));
    CHECK(frame_trace_tick(&s, "a1"));
    CHECK(frame_trace_boundary(&s, 180) == FRAME_TRACE_EVENT_BEGIN && s.captures == 2);

    // 记录期间换新 token：本帧照常结束出回执，同一边界紧接着开始新的一帧。
    // A new token while recording: this frame still ends with a receipt and the next frame begins at the same boundary.
    CHECK(frame(&s, 3) == 3);
    CHECK(frame_trace_tick(&s, "a2"));
    CHECK(frame_trace_boundary(&s, 181) == (FRAME_TRACE_EVENT_END | FRAME_TRACE_EVENT_BEGIN));
    CHECK(s.frame == 182 && s.records == 0 && s.captures == 3);
    CHECK(frame_trace_boundary(&s, 182) == FRAME_TRACE_EVENT_END);

    // 空 token 撤销尚未开始的武装。
    // An empty token cancels an arm that has not begun.
    CHECK(frame_trace_tick(&s, "a3"));
    CHECK(!frame_trace_tick(&s, ""));
    CHECK(frame_trace_boundary(&s, 240) == 0 && !s.active);

    // 上限：超过 cap 的只计 dropped，回执可判 truncated。
    // Cap: requests beyond the cap only count as dropped, so the receipt can report truncated.
    CHECK(frame_trace_tick(&s, "cap"));
    CHECK(frame_trace_boundary(&s, 300) == FRAME_TRACE_EVENT_BEGIN);
    CHECK(frame(&s, FRAME_TRACE_RECORD_CAP + 10) == FRAME_TRACE_RECORD_CAP);
    CHECK(s.records == FRAME_TRACE_RECORD_CAP && s.dropped == 10);
    CHECK(frame_trace_boundary(&s, 301) == FRAME_TRACE_EVENT_END);

    // 分页：<id>:<skip> 跳过前 skip 条，仍计入 seen，且上限只算实际输出。
    // Paging: <id>:<skip> skips the first skip requests, still counted in seen, and the cap counts only emitted ones.
    CHECK(frame_trace_tick(&s, "p:1536"));
    CHECK(s.skip == 1536);
    CHECK(frame_trace_boundary(&s, 360) == FRAME_TRACE_EVENT_BEGIN);
    CHECK(frame(&s, 1536 + 20) == 20);
    CHECK(s.seen == 1556 && s.records == 20 && s.dropped == 0);
    CHECK(frame_trace_boundary(&s, 361) == FRAME_TRACE_EVENT_END);
    CHECK(frame_trace_tick(&s, "p:x") && s.skip == 0);
    CHECK(frame_trace_tick(&s, "q:99999999") && s.skip == 0);
    CHECK(!frame_trace_tick(&s, ""));
    CHECK(frame_trace_boundary(&s, 420) == 0);

    // 过长 token 拒绝，不改变状态。
    // Overlong tokens are rejected without changing state.
    char long_token[FRAME_TRACE_TOKEN_MAX + 4];
    memset(long_token, 'x', sizeof long_token - 1);
    long_token[sizeof long_token - 1] = 0;
    CHECK(!frame_trace_tick(&s, long_token));
    CHECK(!s.armed && s.token[0] == 0);

    CHECK(!strcmp(frame_trace_load_name(0), "load") && !strcmp(frame_trace_load_name(2), "dont_care"));
    puts("frame_trace_test OK");
    return 0;
}
