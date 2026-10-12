// 手柄 / 触屏切换事务单测（秒级）：dm_modetx.h 的状态机与输入闸，不碰游戏。期望值都从规则手算：
// 重建完成 = 提示面板重开次数动过之后连续 SETTLE 帧不动；TIMEOUT 帧没等到按超时往下走；冻结期间按着过的键放开后要松开过一次才交。
// Unit test of the controller / touch switch transaction (takes seconds): the state machine and input gate of dm_modetx.h, without touching the game. Expected values are all computed by hand from the rules:
// rebuild complete = SETTLE consecutive frames without movement after the hint panel reopen count has moved; if that has not happened within TIMEOUT frames, proceed as a timeout; keys that were held during the freeze are passed to the game only after they have been released once following the unfreeze.
#include <stdio.h>
#include "../dm_modetx.h"
static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("  ✗ "); printf(__VA_ARGS__); printf("\n"); } } while (0)
int main(void) {
    { dm_modetx t = {0}; uint64_t f = 100;   // 一次正常切换：触屏(0) → 手柄(1) | One normal switch: touch (0) → controller (1)
      CHECK(dm_modetx_step(&t, f, -1, 0, 7) == DM_TXA_NONE, "没请求不该动");
      CHECK(dm_modetx_step(&t, f, 0, 0, 7) == DM_TXA_NONE, "想要的就是当前模式不该动");
      CHECK(dm_modetx_step(&t, f, 1, 0, 7) == DM_TXA_BEGIN && t.st == DM_TX_REBUILD1, "请求另一模式应开始");
      for (int i = 1; i <= 10; i++) CHECK(dm_modetx_step(&t, f + i, 1, 0, 7) == DM_TXA_NONE, "提示面板没重开前不该往下走（第 %d 帧）", i);
      CHECK(dm_modetx_step(&t, f + 11, 1, 0, 8) == DM_TXA_NONE, "刚重开那一帧不算完成");
      CHECK(dm_modetx_step(&t, f + 13, 1, 0, 9) == DM_TXA_NONE, "又重开一次：重新计稳定");
      CHECK(dm_modetx_step(&t, f + 16, 1, 0, 9) == DM_TXA_NONE, "稳定 3 帧不够");
      CHECK(dm_modetx_step(&t, f + 17, 1, 0, 9) == DM_TXA_REPORT_BACK && t.st == DM_TX_REBUILD2, "稳定 4 帧 → 报回原运行模式");
      CHECK(dm_modetx_step(&t, f + 30, 0, 1, 9) == DM_TXA_NONE, "事务中来的反向请求不打断");
      CHECK(dm_modetx_step(&t, f + 31, 0, 1, 10) == DM_TXA_NONE, "第二次重建刚开始");
      CHECK(dm_modetx_step(&t, f + 35, 0, 1, 10) == DM_TXA_DONE && t.st == DM_TX_IDLE && !t.timeouts, "第二次重建稳定 → 完成、无超时");
      CHECK(dm_modetx_step(&t, f + 36, 0, 1, 10) == DM_TXA_BEGIN, "完成后下一帧处理排着的反向请求"); }
    { dm_modetx t = {0};   // 超时：提示面板一直没重开 | Timeout: the hint panel never reopens
      CHECK(dm_modetx_step(&t, 0, 1, 0, 0) == DM_TXA_BEGIN, "开始");
      CHECK(dm_modetx_step(&t, DM_MODETX_TIMEOUT - 1, 1, 0, 0) == DM_TXA_NONE, "差 1 帧不超时");
      CHECK(dm_modetx_step(&t, DM_MODETX_TIMEOUT, 1, 0, 0) == DM_TXA_REPORT_BACK && t.timeouts == 1, "到点按超时往下走并记数");
      CHECK(dm_modetx_step(&t, 2 * DM_MODETX_TIMEOUT - 1, 1, 0, 0) == DM_TXA_NONE, "第二段重新计时");
      CHECK(dm_modetx_step(&t, 2 * DM_MODETX_TIMEOUT, 1, 0, 0) == DM_TXA_DONE && t.timeouts == 2 && t.st == DM_TX_IDLE, "第二段也超时 → 完成、记 2 次"); }
    { dm_modegate g = {0}; uint64_t b;   // 输入闸：A=0x1 触发切换并一直按着，途中又按了 B=0x2，松开 B 后再按 | Input gate: A=0x1 triggers the switch and stays held, B=0x2 is also pressed along the way, and after releasing B it is pressed again
      b = 0x1; CHECK(dm_modegate_apply(&g, 1, &b) == 1 && b == 0, "冻结：键清零、摇杆也清");
      b = 0x3; CHECK(dm_modegate_apply(&g, 1, &b) == 1 && b == 0, "冻结中新按的键也不交");
      b = 0x3; CHECK(dm_modegate_apply(&g, 0, &b) == 0 && b == 0, "放开后还按着的 A、B 不交");
      b = 0x5; CHECK(dm_modegate_apply(&g, 0, &b) == 0 && b == 0x4, "B 松了、A 还按着：新按的 X=0x4 照常交，A 仍不交");
      b = 0x0; CHECK(dm_modegate_apply(&g, 0, &b) == 0 && b == 0, "全松开");
      b = 0x3; CHECK(dm_modegate_apply(&g, 0, &b) == 0 && b == 0x3, "松开过之后再按照常交"); }
    printf("t_modetx: %d 项，%d 失败\n", checks, fails); return fails != 0;
}
