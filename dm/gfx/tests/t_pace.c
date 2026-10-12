// 节拍器单测：给定时钟序列，期望等待时长手算。周期 16.666 ms 记为 P = 16666667 ns。
// Pacer unit tests: given a clock sequence, the expected wait durations are hand-calculated. The period 16.666 ms is written as P = 16666667 ns.
#include "../nvn/nvn_pace.h"
#include <stdio.h>

int main(void) {
    const uint64_t P = 16666667, MS = 1000000;
    struct { uint64_t now, period, want; const char *why; } c[] = {
        {1000 * MS, P, 0, "首帧：起拍，不等"},
        {1005 * MS, P, 1000 * MS + P - 1005 * MS, "早到 5 ms：等到 1000+P"},
        {1000 * MS + 2 * P - 2 * MS, P, 2 * MS, "第三帧早 2 ms：等 2 ms"},
        {1000 * MS + 3 * P + 3 * MS, P, 0, "迟到 3 ms（不足一周期）：立即出图，节拍点仍为 1000+3P"},
        {1000 * MS + 4 * P - 1 * MS, P, 1 * MS, "下一帧早 1 ms：按原相位等到 1000+4P"},
        {1000 * MS + 7 * P, P, 0, "落后两个周期（卡顿）：重新起拍于当时，不连发追帧"},
        {1000 * MS + 7 * P + 4 * MS, P, P - 4 * MS, "重新起拍后的下一帧：等到新目标"},
        {1000 * MS + 9 * P, 0, 0, "周期 0（DM_PRESENT_HZ=0）：不等"},
        {1000 * MS + 9 * P + 1 * MS, 2 * P, 2 * P - 1 * MS, "切回间隔 2：节拍点 = 上帧时刻 9P + 2P"},
        {1000 * MS + 11 * P + 3 * MS, 2 * P, 2 * P - 3 * MS, "间隔 2 下一帧：节拍点 13P"},
    };
    nvn_pacer p = {0}; int fail = 0, n = sizeof c / sizeof *c;
    for (int i = 0; i < n; i++) {
        uint64_t got = nvn_pace(&p, c[i].now, c[i].period);
        int ok = got == c[i].want;
        printf("%-52s 期望等 %8.3f ms → 真结果 %8.3f ms %s\n", c[i].why, c[i].want / 1e6, got / 1e6, ok ? "✓" : "✗");
        fail += !ok;
    }
    printf("== %d 项，失败 %d ==\n", n, fail);
    return fail != 0;
}
