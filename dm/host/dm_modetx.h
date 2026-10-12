// 手柄 / 触屏模式切换的事务（纯状态机，不碰游戏内存；dm_padmode.c 用，tests/t_modetx.c 测）。
// 一次切换 = 冻结输入 → 旧一侧各登记项收手 → 改布局前缀、对游戏报另一个运行模式（游戏整套重建界面）→ 回读到重建完成 → 报回原运行模式（再重建一遍）→
// 回读到重建完成 → 新一侧各登记项就位 → 放开输入。「重建完成」不数帧：调用方给一个回读信号计数 tips（游戏来读了运行模式、界面根下的面板换了一批，各记一次），
// 计数动过、且连续 SETTLE 帧不再动算完成；TIMEOUT 帧还没等到记超时照样往下走（不能把输入一直冻着），调用方记红。
// 事务进行中来的新请求不打断：调用方只留最后一个想要的模式，事务完成后回到空闲再比一次。
// Transaction for switching between controller / touch modes (pure state machine, does not touch game memory; used by dm_padmode.c, tested by tests/t_modetx.c).
// One switch = freeze input → registered items of the old side stand down → change the layout prefix and report the other operation mode to the game (the game rebuilds the whole UI) → read back that the rebuild is complete → report the original operation mode again (rebuild once more) →
// read back that the rebuild is complete → registered items of the new side take their places → release input. "Rebuild complete" does not count frames: the caller supplies a readback signal count tips (the game came to read the operation mode, the set of panels under the UI root was replaced; each counts once),
// and it is complete when the count has moved and then stays unchanged for SETTLE consecutive frames; if that has not happened after TIMEOUT frames a timeout is recorded and it proceeds anyway (input cannot be kept frozen forever), and the caller reports a failure.
// New requests arriving during a transaction do not interrupt it: the caller keeps only the last wanted mode and compares once more after the transaction completes and returns to idle.
#ifndef DM_MODETX_H
#define DM_MODETX_H
#include <stdint.h>
enum { DM_MODETX_SETTLE = 4, DM_MODETX_TIMEOUT = 240 };
enum { DM_TX_IDLE, DM_TX_REBUILD1, DM_TX_REBUILD2 };
enum { DM_TXA_NONE, DM_TXA_BEGIN, DM_TXA_REPORT_BACK, DM_TXA_DONE };   // 这一帧调用方该做的事 | What the caller should do this frame
typedef struct { int st, timeouts; uint64_t at, moved_at; uint32_t tips; } dm_modetx;
static inline int dm_modetx_step(dm_modetx *t, uint64_t frame, int want, int cur, uint32_t tips) {
    if (t->st == DM_TX_IDLE) {
        if (want < 0 || want == cur) return DM_TXA_NONE;
        t->st = DM_TX_REBUILD1; t->at = frame; t->moved_at = 0; t->tips = tips; t->timeouts = 0; return DM_TXA_BEGIN; }
    if (tips != t->tips) { t->tips = tips; t->moved_at = frame; }
    int done = t->moved_at && frame - t->moved_at >= DM_MODETX_SETTLE, late = frame - t->at >= DM_MODETX_TIMEOUT;
    if (!done && !late) return DM_TXA_NONE;
    if (!done) t->timeouts++;
    t->at = frame; t->moved_at = 0;
    if (t->st == DM_TX_REBUILD1) { t->st = DM_TX_REBUILD2; return DM_TXA_REPORT_BACK; }
    t->st = DM_TX_IDLE; return DM_TXA_DONE;
}
// 输入闸：冻结期间交给游戏的键全 0，并记下这期间按着过的键；放开后这些键要先松开过一次才重新交给游戏
// （触发切换的那一下只做切换：按 A 把界面切成手柄版，这一下 A 不能再落到刚建好的界面上）。返回 1 = 摇杆也清零
// Input gate: during the freeze all keys handed to the game are 0, and the keys held at any point during it are recorded; after release these keys must be let go once before they are handed to the game again
// (the press that triggers the switch only does the switch: pressing A switches the UI to the controller version, and that A must not land on the freshly built UI). Returns 1 = the sticks are zeroed too
typedef struct { uint64_t hold; } dm_modegate;
static inline int dm_modegate_apply(dm_modegate *g, int frozen, uint64_t *buttons) {
    if (frozen) { g->hold |= *buttons; *buttons = 0; return 1; }
    g->hold &= *buttons; *buttons &= ~g->hold; return 0;
}
#endif
