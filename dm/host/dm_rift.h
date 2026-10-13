// 秘境爬塔（dm_rift.c）
// Rift tower climb (dm_rift.c)
#pragma once
#include <stdint.h>
typedef struct { int tier; float progress; int secs_left, done, guardian, inc, next; } dm_rift_hud_t;   // next = 本层完成后下一次打第几层（封顶 100；等于 tier = 不涨：超时，或已在最高层）。显示方只读它，不自己用 tier + inc 算 | next = which level will be played after this one is completed (capped at 100; equal to tier = no increase: overtime, or already at the highest level). Display code only reads it and does not compute tier + inc itself
void dm_rift_tick(uint64_t G, uint64_t player, uint8_t *main_base);   // 服务端拍（diag.c） | Server tick (diag.c)
void dm_rift_c2s(uint64_t G, const uint8_t *pkt, uint32_t len);      // 客户端命令（点门） | Client command (door tap)
void dm_rift_client_pump(uint8_t *main_base);                         // 取手柄处（dm_world.c）：认击杀、新刷出的怪 | At the controller sampling point (dm_world.c): recognize kills and newly spawned monsters
int dm_rift_hud(dm_rift_hud_t *out);                                  // 前端每帧：秘境进行中 / 刚完成返回 1 | Front end every frame: returns 1 while a rift is in progress / just completed
int dm_rift_active(void);
void dm_rift_gamble_vals(int best, uint32_t *uniq, uint32_t *set);   // 赌博加成：最高通关层 → 地狱暗金 / 套装值（十万分之一）；best < 1 = 0（不加） | Gambling bonus: highest cleared level -> Hell unique / set values (in units of 1/100000); best < 1 = 0 (no bonus)
typedef struct { int menu, best, unlock, def; unsigned ms; int clvl, tier, done, secs_left; float progress; int inc, next; } dm_rift_menu_t;   // menu 1 = 选层页、2 = 继续 / 放弃、3 = 守护者处那扇门的确认页（next = 要进的层）；inc = 0 且 done = 超时打倒守护者（没过关） | menu 1 = level-selection page, 2 = Continue / Abandon, 3 = confirmation page of the door at the Guardian (next = the level to enter); inc = 0 and done = Guardian defeated overtime (not passed)
int dm_rift_cleared_any(void);              // 这个角色打通过任意一层秘境没有（限时内过关才算：最高通关层 ≥ 1）——技能硬点上限的解锁条件（dm_actions.c） | has this character cleared any Greater Rift level (only clears within the time limit count: highest cleared level ≥ 1): the unlock condition for the hard skill point cap (dm_actions.c)
int dm_rift_merc_unlocked(void);            // 佣兵新装备位解没解锁（秘境最高通关 ≥ 10 层） | Whether the new mercenary equipment slots are unlocked (highest rift level cleared ≥ 10)
void dm_rift_merc_locked_hint(void);        // 没解锁时的提示「通关地狱后打通秘境10层解锁」 | Hint when not unlocked: 「通关地狱后打通秘境10层解锁」(Clear Greater Rift Level 10 to unlock)
int dm_rift_tier_level(int tier, int clvl);   // 这一层的怪物等级（9 层以下 0 = 不改） | this level's monster level (below level 9, 0 = unchanged)
int dm_rift_tier_hp_x10(int tier, int clvl);  // 这一层怪物生命总倍率 ×10（相对地狱 /players 1 的原生怪） | total life multiplier of this level's monsters ×10 (relative to a native Hell /players 1 monster)
int dm_rift_menu(dm_rift_menu_t *out);      // 前端每帧：要显示的页（0 = 不显示） | Front end every frame: the page to show (0 = none)
void dm_rift_menu_close(void);              // 关闭（右上角 X） | Close (the X at the top right)
void dm_rift_menu_start(int tier);          // 选层页「开始」 | Level selection page 「开始」(Enter Rift)
void dm_rift_menu_continue(void);           // 「继续」 | 「继续」(Continue)
void dm_rift_menu_abandon(void);
void dm_rift_menu_next(void);               // 确认页「继续」：离开这一场，进下一层（或重打） | "Continue" on the confirmation page: leave this run and enter the next level (or replay)
int dm_rift_menu_pending(void); int dm_rift_menu_seq(void); int dm_rift_swept(uint32_t id);            // 「放弃」（放弃后服务端再打开选层页） | 「放弃」(Quit) (after abandoning, the server opens the level selection page again)
void dm_rift_menu_peek(dm_rift_menu_t *out);   // 没有要开的页时也取页面读数（编年史「秘境」页） | Fetch the page readout even when there is no page to open (chronicle 「秘境」(Rift) page)
void dm_rift_ui_pump(void);                     // dm_rift_ui.c：游戏主线程每帧（dm_actions.c） | dm_rift_ui.c: every frame on the game main thread (dm_actions.c)
void dm_rift_ui_on_uimsg(const char *cat, const char *act, const char *arg);   // 界面消息（diag.c） | UI messages (diag.c)
typedef struct { int best, clears, deaths, tier, unlock; unsigned ms; int fast[101]; } dm_rift_stats_t;
int dm_rift_stats(dm_rift_stats_t *out);    // 积分牌「秘境」页读数（侧文件），没有返回 0 | Kill board 「秘境」(Rift) page readout (sidecar file); returns 0 if there is none
