// 秘境计时的单测（公开仓库）：直接包含真的 dm_rift.c、链接真的客体时钟 hle_os.c，不调任何原版地址。
// 要证明的事：倒计时读的是客体时钟——冻结（后台 = 原因 1、系统键盘 = 原因 2）期间 HUD 与菜单的剩余秒数一秒不少，放开后照墙钟走。
// 期望值都不靠被测实现算：开局时刻摆在 120 秒前 ⇒ 剩余 = 900 − 120 = 780；冻结 N 秒前后差 0；放开后睡 2 秒 ⇒ 少 2 秒。
// 读数取整到秒、睡眠有抖动，所以「照墙钟走」那几项允许差 1 秒；「冻结期间不走」那几项必须一秒不差。
// 不覆盖：真机锁屏、超时判负与结算（那两处在 dm_rift_tick / finish 里，要真局）。
// Unit test of the rift timer: includes the real dm_rift.c directly and links the real guest clock hle_os.c, calling no original-game addresses.
// What it proves: the countdown reads the guest clock -- during a freeze (background = reason 1, system keyboard = reason 2) the remaining seconds on the HUD and in the menu do not drop by a single second, and after release they follow the wall clock.
// No expected value is computed by the implementation under test: the start time is placed 120 seconds in the past => remaining = 900 - 120 = 780; before and after a freeze of N seconds the difference is 0; after release, sleeping 2 seconds => 2 seconds less.
// Readouts are rounded to seconds and sleep has jitter, so the "follows the wall clock" items allow a difference of 1 second; the "does not advance while frozen" items must match to the second.
// Not covered: locking the screen on a real device, the timeout verdict and settlement (those two are in dm_rift_tick / finish and need a real run).
#include "../dm_rift.c"

void (*g_dm_pre_commit_hook[5])(const char *);
uint64_t dm_fs_commit_count(void) { return 0; }

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("  ✗ "); printf(__VA_ARGS__); printf("\n"); } } while (0)
static void nap(long ms) { struct timespec d = { ms / 1000, (ms % 1000) * 1000000L }, r; while (nanosleep(&d, &r)) d = r; }
static void left(int *hud, int *menu) { dm_rift_hud_t h = {0}; dm_rift_menu_t m = {0}; if (!dm_rift_hud(&h)) h.secs_left = -9999; menu_fill(&m, 2); *hud = h.secs_left; *menu = m.secs_left; }

int main(void) {
    unsetenv("DM_DETCLOCK");
    int h0, m0, h, m;
    K.t0_ns = guest_ns() - 120ull * 1000000000ull; atomic_store(&K.st, ST_RUN);
    left(&h0, &m0); CHECK((h0 == 780 || h0 == 779) && m0 == h0, "开局 120 秒后剩余应为 780（差 1 秒内），HUD %d 菜单 %d", h0, m0);
    // 进后台：冻结 1.3 秒，前后一秒不差
    // Enter the background: frozen for 1.3 seconds, not a second of difference before and after
    guest_clock_freeze_for(1, 1); left(&h0, &m0); nap(1300); left(&h, &m);
    CHECK(h == h0 && m == m0, "后台 1.3 秒，剩余不该变：HUD %d → %d，菜单 %d → %d", h0, h, m0, m);
    // 后台期间键盘也弹着；先回前台，键盘还在 ⇒ 仍然不走
    // The keyboard is also up while in the background; return to the foreground first with the keyboard still up => still not advancing
    guest_clock_freeze_for(2, 1); guest_clock_freeze_for(1, 0); nap(1300); left(&h, &m);
    CHECK(h == h0 && m == m0, "回前台但键盘还弹着 1.3 秒，剩余不该变：HUD %d → %d，菜单 %d → %d", h0, h, m0, m);
    // 全部放开的那一刻不跳；之后照墙钟走：睡 2.2 秒 ⇒ 少 2 秒（取整抖动差 1 秒内）
    // No jump at the moment everything is released; afterwards it follows the wall clock: sleeping 2.2 seconds => 2 seconds less (rounding jitter within 1 second)
    guest_clock_freeze_for(2, 0); left(&h, &m); CHECK(h == h0 && m == m0, "放开那一刻不该跳：HUD %d → %d", h0, h);
    nap(2200); left(&h, &m); CHECK(h0 - h >= 2 && h0 - h <= 3 && m == h, "放开后 2.2 秒应少 2 秒（差 1 秒内）：HUD %d → %d，菜单 %d", h0, h, m);
    // 已结算：两处都停在结算那一刻的读数
    // Settled: both readouts stay at the value of the moment of settlement
    g_left_done = 321; atomic_store(&K.st, ST_DONE); left(&h, &m); CHECK(h == 321 && m == 321, "结算后应停在 321：HUD %d 菜单 %d", h, m);
    printf("t_rift_clock：%d 项，%d 项不通\n", checks, fails); return fails != 0;
}
