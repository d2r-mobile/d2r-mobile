// 秘境「有没有可回去的场」与新局重置的单测（公开仓库 里与玩法无关的两处）：直接包含真的 dm_rift.c，不调任何原版地址。
// 要证明的事：
//   1. 城里入口给哪一页只看「状态 + 旧场位置」，不看守护者处那扇门的单位号：打完了、位置记着、门没开出来（单位号 0）→ 仍给「继续 / 放弃」页（原先给选层页，回不去捡东西）；
//      打完了但位置没记（结算时读不到玩家位置）→ 选层页，哪怕门的单位号非 0。
//   2. 新局重置把上一局留下的 开始 / 继续 / 放弃 请求和待开的页都清掉。
// 期望值按规则手写。不覆盖：真局里「继续」落回旧场位置、开新场 / 放弃时清位置（那两处在 start_rift / dm_rift_tick 里，要真局）。
// Unit test for the rift's "is there a run to go back to" and the new-game reset: includes the real dm_rift.c directly and calls no original address.
// What is to be proved:
//   1. Which page the town entrance gives depends only on "state + the old run's position", not on the unit id of the door at the Guardian: finished, position recorded, door not placed (unit id 0) -> still the "Continue / Abandon" page (it used to give level selection, with no way back to pick things up);
//      finished but position not recorded (the player's position could not be read at completion) -> level selection, even if the door's unit id is non-zero.
//   2. The new-game reset clears the start / continue / abandon requests and the pending page left by the previous game.
// Expected values written by hand from the rules. Not covered: "Continue" landing back at the old run's position in a real game, and clearing the position when a new run starts / on abandon (those two are in start_rift / dm_rift_tick and need a real game).
#include "../dm_rift.c"

void (*g_dm_pre_commit_hook[4])(const char *);
uint64_t dm_fs_commit_count(void) { return 0; }

int main(void) {
    static const struct { int st, nd_lv; unsigned next_door; int want; const char *what; } c[] = {
        { ST_IDLE, 0, 0, MENU_SELECT, "没在打" }, { ST_IDLE, 120, 5, MENU_SELECT, "没在打（位置与门号是残留）" },
        { ST_RUN, 0, 0, MENU_CHOICE, "正在打" },
        { ST_DONE, 120, 5, MENU_CHOICE, "打完，位置在，门在" }, { ST_DONE, 120, 0, MENU_CHOICE, "打完，位置在，门没开出来" },
        { ST_DONE, 0, 5, MENU_SELECT, "打完，位置没记，门号非 0" }, { ST_DONE, 0, 0, MENU_SELECT, "打完，位置没记，门也没有" },
    };
    int fails = 0, n = (int)(sizeof c / sizeof *c);
    for (int i = 0; i < n; i++) { atomic_store(&K.st, c[i].st); L.nd_lv = c[i].nd_lv; L.next_door = c[i].next_door; dm_rift_menu_t m = {0}; dm_rift_menu_peek(&m);
        if (town_menu(c[i].st) != c[i].want || m.menu != c[i].want) { fails++; printf("  ✗ %s：应给页 %d，town_menu %d、peek %d\n", c[i].what, c[i].want, town_menu(c[i].st), m.menu); } }
    atomic_store(&M.req_start, 7); atomic_store(&M.req_cont, 1); atomic_store(&M.req_abandon, 1); atomic_store(&M.menu, MENU_CHOICE); menu_reset();
    if (atomic_load(&M.req_start) || atomic_load(&M.req_cont) || atomic_load(&M.req_abandon) || atomic_load(&M.menu) != MENU_NONE) { fails++; printf("  ✗ 新局重置后仍有残留：开始 %d 继续 %d 放弃 %d 页 %d\n", atomic_load(&M.req_start), atomic_load(&M.req_cont), atomic_load(&M.req_abandon), atomic_load(&M.menu)); }
    // 「进行中且已超时」（rift_overtime）：= 状态是进行中 ∧ 开局至今 ≥ 900 秒；顶部条与菜单拿到的是同一个量。开局时刻摆在 N 秒前（客体时钟）
    // "In progress and overtime" (rift_overtime): = state is in progress AND at least 900 seconds since the start; the top bar and the menu receive the same quantity. The start moment is placed N seconds in the past (guest clock)
    static const struct { int st; int ago; int want; } oc[] = { { ST_RUN, 100, 0 }, { ST_RUN, 899, 0 }, { ST_RUN, 901, 1 }, { ST_RUN, 5000, 1 }, { ST_DONE, 901, 0 }, { ST_IDLE, 901, 0 } };
    for (unsigned i = 0; i < sizeof oc / sizeof *oc; i++) { atomic_store(&K.st, oc[i].st); K.t0_ns = guest_ns() - (uint64_t)oc[i].ago * 1000000000ull; dm_rift_hud_t h = {0}; dm_rift_menu_t m = {0}; dm_rift_hud(&h); menu_fill(&m, 2);
        if (rift_overtime() != oc[i].want || m.overtime != oc[i].want || (oc[i].st != ST_IDLE && h.overtime != oc[i].want)) { fails++; printf("  ✗ 状态 %d 开局 %d 秒前：超时量应为 %d，得 %d（顶部条 %d、菜单 %d）\n", oc[i].st, oc[i].ago, oc[i].want, rift_overtime(), h.overtime, m.overtime); } }
    printf("rift_overtime %s：6 格\n", fails ? "失败" : "OK");
    printf("t_rift_oldrun %s：%d 格城里入口给页 + 新局重置清请求\n", fails ? "失败" : "OK", n);
    return fails != 0;
}
