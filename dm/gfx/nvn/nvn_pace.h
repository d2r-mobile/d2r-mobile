// 出图节拍器（纯函数，Mac / iOS 共用；单测 tests/t_pace.c）。
// 语义同垂直同步：节拍点 = 上一节拍点 + 周期。早到 → 等到节拍点；迟到不足一个周期 → 立即出图、保持原相位；
// 落后一个周期以上（卡顿）→ 从当前时刻重新起拍，不连发追帧。周期 0 = 不等（节拍点跟随当前时刻）。
// Present pacer (pure function, shared by Mac / iOS; unit test tests/t_pace.c).
// Semantics as vsync: present slot = previous present slot + period. Early → wait until the present slot; late by less than one period → present immediately and keep the original phase;
// behind by one period or more (stutter) → restart the cadence from the current time, no burst of presents to catch up. Period 0 = no wait (the present slot follows the current time).
#pragma once
#include <stdint.h>

typedef struct { uint64_t target; } nvn_pacer;

// now：单调时钟（ns）；period：本帧周期（ns，0 = 不等）。返回需要等待的 ns（0 = 立即出图），并更新内部目标时刻。
// now: monotonic clock (ns); period: this frame's period (ns, 0 = no wait). Returns the ns to wait (0 = present immediately) and updates the internal target time.
static inline uint64_t nvn_pace(nvn_pacer *p, uint64_t now, uint64_t period) {
    if (!period || !p->target) { p->target = now; return 0; }
    uint64_t next = p->target + period;
    if (now <= next) { p->target = next; return next - now; }
    p->target = now - next < period ? next : now;
    return 0;
}

// 出图帧率 = 屏幕刷新率的整数分之一里不低于目标（60，省电模式 30）的最小一档：每帧在屏上停整数个刷新周期，没有交替抖动。
// 60 / 120 Hz 屏 → 60；144 → 72；165 → 82.5；100 → 100；240 → 60；省电 30：120 → 30、144 → 36。刷新率读不到（0）就按目标。
// 0.02 的余量让 119.88 / 59.94 这类 NTSC 档按 2 / 1 倍算
// Present frame rate = the smallest tier among integer fractions of the screen refresh rate that is not below the target (60, power saving mode 30): each frame stays on screen for an integer number of refresh cycles, with no alternating jitter.
// 60 / 120 Hz screen → 60; 144 → 72; 165 → 82.5; 100 → 100; 240 → 60; power saving 30: 120 → 30, 144 → 36. When the refresh rate cannot be read (0), the target is used.
// The 0.02 margin lets NTSC rates like 119.88 / 59.94 count as 2 / 1 times
static inline double nvn_pace_hz(double screen_hz, double floor_hz) {
    if (screen_hz <= 0 || floor_hz <= 0) return floor_hz;
    double k = (double)(long)(screen_hz / floor_hz + 0.02); return k >= 1 ? screen_hz / k : screen_hz;
}
