// 秘境「下一层」层号的单测（公开仓库 第 100 层限时通关后顶部条写「下次第 103 层」）：直接包含真的 dm_rift.c，不调任何原版地址。
// 要证明的事：算下一层只有宿主一处、封顶 100；顶部进度条拿到的 next 就是这个数（显示方不再自己用 tier + inc 算）。
// 期望值手写：下一层 = min(层 + 晋级量, 100)，晋级量 0（超时）时等于本层。逐格列出，不用被测公式生成。
// 不覆盖：屏上那行字选哪一句（在 iOS 壳 dm_rift_hud.m 里：next > tier → 「下次第 N 层」；晋级了但 next = tier → 「已通关最高层」；没晋级 → 「下次仍是这一层」）。
// Unit test for the rift "next level" number (after a timed clear of level 100 the top bar wrote "next: level 103"): includes the real dm_rift.c directly and calls no original address.
// What is to be proved: the next level is computed in one place in the host and capped at 100; the next the top progress bar receives is that number (the display no longer computes tier + inc itself).
// Expected values are written by hand: next level = min(level + increase, 100), equal to this level when the increase is 0 (overtime). Listed cell by cell, not generated with the formula under test.
// Not covered: which sentence the line on screen picks (in the iOS shell's dm_rift_hud.m: next > tier -> "next: level N"; advanced but next = tier -> "highest level cleared"; not advanced -> "next: this level again").
#include "../dm_rift.c"

void (*g_dm_pre_commit_hook[4])(const char *);
uint64_t dm_fs_commit_count(void) { return 0; }

int main(void) {
    static const struct { int tier, inc, want; } c[] = {
        { 1, 0, 1 }, { 1, 1, 2 }, { 1, 2, 3 }, { 1, 3, 4 },
        { 96, 3, 99 }, { 97, 0, 97 }, { 97, 1, 98 }, { 97, 2, 99 }, { 97, 3, 100 },
        { 98, 0, 98 }, { 98, 1, 99 }, { 98, 2, 100 }, { 98, 3, 100 },
        { 99, 0, 99 }, { 99, 1, 100 }, { 99, 2, 100 }, { 99, 3, 100 },
        { 100, 0, 100 }, { 100, 1, 100 }, { 100, 2, 100 }, { 100, 3, 100 },
    };
    int fails = 0, n = (int)(sizeof c / sizeof *c);
    atomic_store(&K.st, ST_DONE);
    for (int i = 0; i < n; i++) {
        K.tier = c[i].tier; K.inc = c[i].inc; dm_rift_hud_t h = {0}; int ok = dm_rift_hud(&h);
        if (!ok || next_tier() != c[i].want || h.next != c[i].want || h.next > 100 || h.tier != c[i].tier || h.inc != c[i].inc) {
            fails++; printf("  ✗ 第 %d 层 晋级 %d：下一层应为 %d，宿主 %d、顶部条 %d（取到 %d）\n", c[i].tier, c[i].inc, c[i].want, next_tier(), h.next, ok); }
    }
    // 生命基础项的可写上限（hp_base_limit）：写到这个数时，合计 = 基础 × (合计 ÷ 基础) 不能过属性上限 0x7fffff00 = 2147483392。期望值手算：
    //   没加成（合计 = 基础）→ 2147483392；+77%（1000 / 1770）→ 2147483392 × 1000 ÷ 1770 = 1213267453；+100%（256 / 512）→ 1073741696；
    //   随从实例（基础 27000 点 = 6912000，合计 47790 点 = 12234240）→ 1213267453；减益（合计小于基础）与读不到基础项（0）→ 2147483392
    // The writable limit of the life base entry (hp_base_limit): when written to this number, the total = base x (total / base) must not exceed the stat limit 0x7fffff00 = 2147483392. Expected values worked out by hand:
    //   no bonus (total = base) -> 2147483392; +77% (1000 / 1770) -> 2147483392 x 1000 / 1770 = 1213267453; +100% (256 / 512) -> 1073741696;
    //   a minion instance (base 27000 points = 6912000, total 47790 points = 12234240) -> 1213267453; a debuff (total smaller than base) and an unreadable base entry (0) -> 2147483392
    static const struct { int base, full; long long want; } hc[] = {
        { 2147483392, 2147483392, 2147483392LL }, { 1000, 1770, 1213267453LL }, { 256, 512, 1073741696LL },
        { 6912000, 12234240, 1213267453LL }, { 100, 90, 2147483392LL }, { 0, 500, 2147483392LL },
    };
    int hn = (int)(sizeof hc / sizeof *hc);
    for (int i = 0; i < hn; i++) { long long got = hp_base_limit(hc[i].base, hc[i].full), tot = hc[i].base > 0 ? got * hc[i].full / hc[i].base : got;
        if (got != hc[i].want || (hc[i].full > hc[i].base && hc[i].base > 0 && tot > 2147483392LL)) { fails++; printf("  ✗ 基础 %d 合计 %d：可写上限应为 %lld，得 %lld（写满后合计 %lld）\n", hc[i].base, hc[i].full, hc[i].want, got, tot); } }
    printf("hp_base_limit %s：%d 格，带百分比加成时写满基础项合计不过属性上限\n", fails ? "失败" : "OK", hn);
    printf("t_rift_next %s：%d 格（层 × 晋级量）下一层 = 手写期望且 ≤ 100，宿主与顶部条同值\n", fails ? "失败" : "OK", n);
    return fails != 0;
}
