// 秘境爬塔 P1。
// 玩法：哈洛加斯一扇秘境门（物件 342 墨菲斯托大红门的外观）→ 本局里抽一张野外开阔图 + 1–2 张短迷宫串起来（本局没生成过的，图是新的），
// 怪更密；杀怪攒进度，满 100% 在身边刷守护者，杀掉 = 本层成功；15 分钟内按剩余时间涨 3 / 2 / 1 层，超时不涨；成功送一个世界之石碎片。
// 资格：地狱、当前难度已打过巴尔（同首领连战 eligible）。测试开关 DM_RIFT_TEST=1 跳过资格。
//
// 线程：服务端拍（dm_rift_tick，diag.c 读玩家状态处）管门、开局、串图、守护者、涨层；客户端（dm_rift_client_pump，取手柄处）认击杀与新刷出的怪；
// 点门走命令分派观测门（dm_rift_c2s）；前端每帧读 dm_rift_hud 画顶部进度条 + 倒计时。层数存侧文件；本次秘境随局（退出即作废）。
// 读码依据（P0，宿主实测）：
//   本局怪物区域 [G+0x1d0+8×关卡]：MonDen +0x2c4（刷怪时夹到 10000）、MonUMin +0x2c8、MonUMax +0x2c9——建局时从关卡表拷来，换关前改它对之后第一次刷怪的房间生效；
//   LvlMaze（数据表根 +0x18f8，每条 0x1c，+0 关卡号，+0xc 地狱房间数）建图时读表，改表对还没生成的关卡生效，秘境结束写回；
//   怪物等级：区域等级字段写了不影响刷怪（P0 第 2 项），所以 9 层起按 MonLvl 表（根 +0x1178，每条 0x78 = 30 个 u32：AC / TH / HP / DM / XP 各 6 列，资料片地狱 = 组内第 6 列）
//   的比例在刷出后改属性：生命 7 / 6（定点 ×256）、伤害百分比 25、防御 31，等级 12 写成目标等级；
//   设属性 0x5bc1f0(单位, 属性, 值, 层)、取属性 0x5b9290；刷怪 0x7ce630(游戏, 房间, 1, 类号, x, y, 1, 0, 0x10)、刷物件同函数类型 2；
//   门 342 的初始化往任务链表 [G+0x21d8]（下一项 +0x310）里类型 0x14 那条写 +0x21 = 1、+0x24 = 门 id（墨菲斯托任务），建前备份、建后写回；
//   换关 0x634000(游戏, 玩家, 关卡, 11)；生成物品照 0x7c0b20 拼参数调 0x656780（同 P0 DM_TEST_MKITEM）。
// Rift tower climb P1.
// Gameplay: a rift door in Harrogath (with the look of object 342, Mephisto's big red portal) -> within this run, draw one open wilderness map + 1–2 short mazes and chain them (ones not generated in this run yet, so the maps are fresh),
// with denser monsters; killing monsters builds progress, at 100% the guardian spawns next to the player, and killing it = this Rift level succeeds; within 15 minutes gain 3 / 2 / 1 Rift levels by time left, none on timeout; success gives one Worldstone Shard.
// Eligibility: Hell, Baal already defeated on the current difficulty (same as boss rush eligible). Test switch DM_RIFT_TEST=1 skips eligibility.
// Threads: the server tick (dm_rift_tick, where diag.c reads player state) handles the door, rift start, map chaining, guardian and Rift level increase; the client (dm_rift_client_pump, at the controller sampling point) recognizes kills and newly spawned monsters;
// door taps go through the command dispatch observation gate (dm_rift_c2s); the front end reads dm_rift_hud every frame to draw the top progress bar + countdown. The Rift level is stored in the sidecar file; the current rift lives with the run (void on exit).
// Code-reading basis (P0, measured on the host):
//   this run's monster regions [G+0x1d0+8×level]: MonDen +0x2c4 (clamped to 10000 when spawning), MonUMin +0x2c8, MonUMax +0x2c9 -- copied from the level table when the run is created; changing them before the level change takes effect for rooms that spawn monsters for the first time afterwards;
//   LvlMaze (data table root +0x18f8, 0x1c per entry, +0 level id, +0xc Hell room count) is read when the map is built; changing the table takes effect for levels not yet generated, and it is written back when the rift ends;
//   monster level: writing the region level field does not affect spawning (P0 item 2), so from Rift level 9 the MonLvl table (root +0x1178, 0x78 per entry = 30 u32: 6 columns each for AC / TH / HP / DM / XP, expansion Hell = 6th column in the group)
//   is used to scale stats after spawning: life 7 / 6 (fixed point ×256), damage percent 25, defense 31, and level 12 is written as the target level;
//   set stat 0x5bc1f0(unit, stat, value, layer), get stat 0x5b9290; spawn monster 0x7ce630(game, room, 1, class id, x, y, 1, 0, 0x10), spawn object is the same function with type 2;
//   the init of door 342 writes +0x21 = 1 and +0x24 = door id into the type 0x14 entry of the quest linked list [G+0x21d8] (next entry +0x310) (the Mephisto quest); back up before building and write back after;
//   level change 0x634000(game, player, level, 11); item generation builds the parameters following 0x7c0b20 and calls 0x656780 (same as P0 DM_TEST_MKITEM).
#include "dm_rift.h"
#include "dm_bossrush.h"
#include "dm_hud.h"
#include "dm_unit.h"
#include "dm_doorspot.h"
#include "dm_i18n.h"
#include "dm_cursor.h"
#include "hle.h"
#include "dm.h"
#include <stdarg.h>
#include <pthread.h>
#include <sys/stat.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { LV_TOWN = 109, RIFT_OBJ = 342, TOWN_OBJ = 298, RIFT_SECS = 900 };   // TOWN_OBJ：打倒守护者后的回城门（物件类同首领连战的门，名牌在秘境图里换成「回城」） | TOWN_OBJ: the town door after the guardian is defeated (same object class as the boss rush door; its nameplate is changed to 「回城」(Return to Town) on rift maps)
// 地图池（设计稿第三节）：野外开阔图 + 短迷宫
// Map pool (design doc section 3): open wilderness maps + short mazes
static const int k_open[] = { 3, 7, 43, 111, 112, 117 };   // 冰冷之原、塔莫高地、遥远的绿洲、冰河高原、亚瑞特高原、冰冻冻原 | Cold Plains, Tamoe Highland, Far Oasis, Frigid Highlands, Arreat Plateau, Frozen Tundra
static const int k_maze[] = { 12, 16, 52, 53, 54, 55, 56, 57, 59, 60, 122, 123, 125, 126, 127 };   // 地穴 1–2、宫殿地窖 1–3、石墓 1–2、亡者大厅 1–3、尼拉塞克神殿两层、深渊三张 | Pit 1–2, Palace Cellar 1–3, Stony Tomb 1–2, Halls of the Dead 1–3, two levels of Nihlathak's Temple, three Abyss maps
static const int k_guard[] = { 156, 211, 242, 243, 544 };   // 守护者：五位幕首领随机一位（同首领连战刷法） | Guardian: a random one of the five act bosses (spawned the same way as boss rush)

static uint8_t *B;
static _Atomic int g_merc_req;   // 这一拍收到过「给佣兵穿装备」的包（0x51 / 0x5b）：分支门 0x8d 据此区分「玩家新穿」与「读档落位」（下一拍清） | An "equip the mercenary" packet (0x51 / 0x5b) was received this tick: branch gate 0x8d uses it to tell "the player newly equipping" from "placement on load" (cleared on the next tick)
static uint64_t rd(uint64_t a) { return a ? *(volatile uint64_t *)(uintptr_t)a : 0; }
static uint32_t tick_of(uint64_t G) { return *(volatile uint32_t *)(uintptr_t)(G + 0x170); }
static uint64_t level_of(uint64_t u) { return dm_unit_level_ptr(u); }   // 逐步判空的公共读法（dm_unit.h） | Shared read with step-by-step null checks (dm_unit.h)
static int unit_level(uint64_t u) { return dm_unit_level(u); }
static uint64_t unit_room(uint64_t u) { uint64_t p = rd(u + 0x38); return !p ? 0 : *(uint32_t *)(uintptr_t)u == 2 ? rd(p) : rd(p + 0x20); }
static int unit_xy(uint64_t u, int *x, int *y) {
    uint64_t p = rd(u + 0x38); if (!p) return 0; uint32_t t = *(uint32_t *)(uintptr_t)u;
    if (t == 2) { *x = *(int32_t *)(uintptr_t)(p + 0x10); *y = *(int32_t *)(uintptr_t)(p + 0x14); } else { *x = *(uint16_t *)(uintptr_t)(p + 2); *y = *(uint16_t *)(uintptr_t)(p + 6); }
    return 1;
}
static int ground_items(uint64_t G) { int n = 0; for (int b = 0; b < 128; b++) for (uint64_t u = rd(G + 0x2e30 + 8 * b); u; u = rd(u + 0x158)) n += *(uint32_t *)(uintptr_t)u == 4 && *(uint32_t *)(uintptr_t)(u + 0xc) == 3; return n; }   // 地上的物品件数（物品表 游戏 +0x2e30，模式 3 = 在地上） | Number of items on the ground (item table game +0x2e30, mode 3 = on the ground)
static uint64_t find_unit(uint64_t G, int type, uint32_t id) { for (int b = 0; b < 128; b++) for (uint64_t u = rd(G + 0x2230 + type * 0x400 + 8 * b); u; u = rd(u + 0x158)) if (*(uint32_t *)(uintptr_t)(u + 8) == id) return u; return 0; }
static int gs(uint64_t u, int st) { return ((int (*)(uint64_t, int, int))(B + 0x5b9290))(u, st, 0); }
static void ss(uint64_t u, int st, int v) { ((void (*)(uint64_t, int, int, int))(B + 0x5bc1f0))(u, st, v, 0); }
// 属性的基础项。0x5b9290（gs）在带扩展标志的属性表上读的是合计数组（表 +0xe8：基础 + 各状态 / 光环 / 装备的加成，百分比加成已乘进去），
// 而 0x5bc1f0（ss）写的是基础数组（表 +0x30，经 0x5bbe20）、写完由 0x5bbcd0 重算合计。「读合计、乘一个数、写回」会把加成乘两遍；要改基础项就得读基础项。
// 基础数组：{键 = 属性号 << 16 | 层号, 值} 各 4 字节，个数在 +0x38（同 0x5b9290 里的查法，这里层号取 0）
// The base entry of a stat. On a stat table with the extended flag, 0x5b9290 (gs) reads the totals array (table +0xe8: base plus the bonuses of states / auras / equipment, with percentage bonuses already multiplied in),
// while 0x5bc1f0 (ss) writes the base array (table +0x30, through 0x5bbe20), after which 0x5bbcd0 recomputes the total. "Read the total, multiply by a number, write back" multiplies the bonus in twice; to change the base entry the base entry must be read.
// Base array: {key = stat id << 16 | layer, value}, 4 bytes each, count at +0x38 (the same lookup as in 0x5b9290, with layer 0 here)
static int gsb(uint64_t u, int st) { uint64_t sl = rd(u + 0x88); const uint32_t *a = sl ? (const uint32_t *)(uintptr_t)rd(sl + 0x30) : NULL; uint64_t n = sl ? rd(sl + 0x38) : 0;
    if (!a || n > 4096) return 0; for (uint64_t i = 0; i < n; i++) if (a[i * 2] == (uint32_t)st << 16) return (int)a[i * 2 + 1]; return 0; }
static uint64_t free_spot(uint64_t G, uint64_t room, int32_t pos[2]) { uint64_t out = 0; ((void (*)(uint64_t, uint64_t, int32_t *, int, int, uint64_t *, int, int))(B + 0x7160d0))(G, room, pos, 3, 0x400, &out, 4, 100); return out; }
static void level_origin(uint64_t lv, int *x0, int *y0) { *x0 = *y0 = 1 << 30; int n = 0;
    for (uint64_t r = dm_level_rooms(lv); r && n < 2000; r = rd(r + 0x48), n++) { int x = *(int32_t *)(uintptr_t)(r + 0x60) * 5, y = *(int32_t *)(uintptr_t)(r + 0x64) * 5; if (x < *x0) *x0 = x; if (y < *y0) *y0 = y; } }
static uint64_t room_at(uint64_t lv, int px, int py) { int n = 0; for (uint64_t r = dm_level_rooms(lv); r && n < 2000; r = rd(r + 0x48), n++) { int x = *(int32_t *)(uintptr_t)(r + 0x60) * 5, y = *(int32_t *)(uintptr_t)(r + 0x64) * 5, w = *(int32_t *)(uintptr_t)(r + 0x68) * 5, h = *(int32_t *)(uintptr_t)(r + 0x6c) * 5;
    if (px >= x && px < x + w && py >= y && py < y + h) return rd(r + 0x58); } return 0; }
// 同上，房间还没载入就用原版自己的房间初始化 main+0x52dcc0(数据版本 [游戏 +0x106], 关卡房间) 载入（原版按砖换关取对端房间时也是这么做的，0x559284）
// Same as above; if the room is not loaded yet, load it with the original game's own room init main+0x52dcc0(data version [game +0x106], level room) (the original does the same when fetching the room at the other end on a tile-based level change, 0x559284)
static uint64_t room_load(uint64_t G, uint64_t lv, int px, int py) { int n = 0; for (uint64_t r = dm_level_rooms(lv); r && n < 2000; r = rd(r + 0x48), n++) { int x = *(int32_t *)(uintptr_t)(r + 0x60) * 5, y = *(int32_t *)(uintptr_t)(r + 0x64) * 5, w = *(int32_t *)(uintptr_t)(r + 0x68) * 5, h = *(int32_t *)(uintptr_t)(r + 0x6c) * 5;
    if (px >= x && px < x + w && py >= y && py < y + h) { if (!rd(r + 0x58)) ((void (*)(int, uint64_t))(B + 0x52dcc0))(*(uint8_t *)(uintptr_t)(G + 0x106), r); return rd(r + 0x58); } } return 0; }
static void warp(uint64_t G, uint64_t player, int lv) { ((void (*)(uint64_t, uint64_t, int, int))(B + 0x634000))(G, player, lv, 11); }
static uint64_t tables(uint64_t G) { return rd((uint64_t)(uintptr_t)(B + 0x2d64a18 + *(uint8_t *)(uintptr_t)(G + 0x106) * 16)); }
// 提示一律给中英表的键（dm/i18n/ui_text.tsv，原版取串、跟游戏语言走）；键对应的串当格式串用，中英占位符同序由 dm_i18n.py 把关
// Messages always pass keys of the Chinese-English string table (dm/i18n/ui_text.tsv, looked up by the original game, following the game language); the
// string for a key is used as a format string, and dm_i18n.py enforces that the Chinese and English placeholders are in the same order
static void say(const char *key, ...) { char line[160], full[180]; va_list ap; va_start(ap, key); vsnprintf(line, sizeof line, dm_str(B, key), ap); va_end(ap);
    snprintf(full, sizeof full, dm_str(B, "dmRiftSayPrefix"), line); dm_game_notice(B, full); LOG("秘境：提示「%s」", full); }

// ---- 进程级状态（换局保留）
// ---- Process-level state (kept across run changes)
enum { ST_IDLE, ST_RUN, ST_DONE };
static struct {
    _Atomic int st; int tier, maps[3], nmaps, cur;                    // 本次层数、串起来的图、正在第几张 | This rift's level, the chained maps, which map we are on
    _Atomic int score, need; uint64_t t0_ns; _Atomic int timeout, guard_dead;    // 分数（客户端加）、开局时刻（客体时钟）、超时、守护者已死 | Score (added by the client), start time (guest clock), timed out, guardian dead
    uint32_t guard; int guard_cls, inc;                                 // 守护者单位 id；成功涨了几层 | Guardian unit id; how many levels were gained on success
    int players_old;                                                    // 开局前的 /players 档 | The /players setting from before the start of the run
    char who[64];                                                       // 角色名（层数侧文件） | Character name (level sidecar file)
    int resume_tier;                                                    // 图用完 → 一键重开后新局里要直接开的层（0 = 无） | Maps ran out -> the level to open directly in the new run after the one-tap restart (0 = none)
} K;
static struct { uint64_t G, player; uint32_t last_t; int eligible; uint32_t door, next_door, town_door, pend, pend_at; int door_tried, lv_prev, dead; int used[24], nused; int redir; int last_lv, last_x, last_y, nd_lv, nd_x, nd_y, tp_lv, tp_x, tp_y, resume, gift_due; uint32_t tp_until; } L;   // used = 本局秘境用过的图（已生成过的关卡同一局里不会重新生成，下次不抽）   // 本局 | used = maps used by the rift in this run (levels already generated are not regenerated in the same run, so they are not drawn next time)   // This run
// 选层页 / 继续放弃页（前端 dm_rift_menu.m 画，点按经下面的请求回到服务端拍里执行）
// Level selection page / continue-abandon page (drawn by the front end dm_rift_menu.m; taps go back through the requests below to be executed in the server tick)
enum { MENU_NONE, MENU_SELECT, MENU_CHOICE };
static struct { _Atomic int menu, req_start, req_cont, req_abandon, clvl; } M;

// 侧文件：<存档工作目录>/<角色名>/<角色名>.sw_rift（同刷怪积分牌的写法：写在工作目录、随存档提交）。一行一项，键在前：
//   tier N（下次进门打第 N 层）/ best N（通关过的最高层）/ clears N（通关次数）；best / clears 只计 15 分钟内打倒守护者的（超时通关不算过关；旧文件里的数不回溯）/ deaths N（秘境里死亡次数）/
//   ms 掩码（领过的 10 层里程碑，位 k = 第 (k+1)×10 层）/ sunder 掩码（里程碑送过的破免大板）/ fast 层 秒（该层最快用时，限时内才记）。只有「tier N」一行的旧文件照读
// Sidecar file: <save working directory>/<character name>/<character name>.sw_rift (same approach as the kill board: written in the working directory
// and committed with the save). One entry per line, key first:
//   tier N (the next entry plays level N) / best N (highest level cleared) / clears N (number of clears); best / clears count only runs where the Guardian
//   was defeated within 15 minutes (a clear after the time limit does not count as passing; numbers in old files are not revisited) / deaths N (deaths
//   in the Rift) /
//   ms mask (the 10-level milestones claimed, bit k = level (k+1)×10) / sunder mask (Sunder Charms already given by milestones) / fast level seconds
//   (fastest time for that level, recorded only within the time limit). Old files with only a "tier N" line are still read
const char *dm_save_workdir(void);
enum { RIFT_UNLOCK0 = 8 };   // 没通关过的角色也能选 1–8 层；进度按角色存（<存档>/<角色名>/<角色名>.sw_rift），不跨角色 | Characters that have never cleared can also select levels 1–8; progress is stored per character (<save>/<character name>/<character name>.sw_rift), not shared across characters
static struct { int tier, best, clears, deaths, unlock, gift, pts; unsigned ms, sunder; int fast[101]; } R;   // gift = 首次进秘境的彩虹刻面已送；pts = 通关点数已发到最高通关第几层 | gift = the Rainbow Facet for the first Rift entry has been given; pts = the highest cleared level up to which clear points have been granted
static struct { pthread_mutex_t mu; int dirty; uint64_t wrote; char who[64]; __typeof__(R) snap; } RS = { .mu = PTHREAD_MUTEX_INITIALIZER };
static int rec_path(char *o, size_t n) { const char *w = dm_save_workdir(); if (!w || !K.who[0]) return 0; snprintf(o, n, "%s/%s/%s.sw_rift", w, K.who, K.who); return 1; }
static void rec_load(void) {
    { int pend; pthread_mutex_lock(&RS.mu); pend = (RS.dirty || RS.wrote) && !strcmp(RS.who, K.who); if (pend) R = RS.snap; pthread_mutex_unlock(&RS.mu); if (pend) { LOG("秘境：进度还有一份待落盘的快照（同一进程里上一局留下的），用它，不读文件"); return; } }
    memset(&R, 0, sizeof R); R.tier = 1; R.unlock = RIFT_UNLOCK0; int saw_best = 0; char p[1200], ln[128]; if (!rec_path(p, sizeof p)) return; FILE *f = fopen(p, "r"); if (!f) return;
    while (fgets(ln, sizeof ln, f)) { int a, b; unsigned m;
        if (sscanf(ln, "tier %d", &a) == 1) R.tier = a; else if (sscanf(ln, "best %d", &a) == 1) { R.best = a; saw_best = 1; } else if (sscanf(ln, "clears %d", &a) == 1) R.clears = a;
        else if (sscanf(ln, "deaths %d", &a) == 1) R.deaths = a; else if (sscanf(ln, "unlock %d", &a) == 1) R.unlock = a; else if (sscanf(ln, "gift %d", &a) == 1) R.gift = a; else if (sscanf(ln, "pts %d", &a) == 1) R.pts = a; else if (sscanf(ln, "ms %x", &m) == 1) R.ms = m; else if (sscanf(ln, "sunder %x", &m) == 1) R.sunder = m;
        else if (sscanf(ln, "fast %d %d", &a, &b) == 2 && a >= 1 && a <= 100) R.fast[a] = b; }
    fclose(f); if (!saw_best && R.tier > 1) { R.best = R.tier - 3 < 1 ? 1 : R.tier - 3; LOG("秘境：旧侧文件没有最高通关（早期包只记下次层数 %d），按每次最多涨 3 层补成 %d", R.tier, R.best); }   // 通关 1→4 后最高通关显示 0 | After clearing 1 -> 4 the highest clear showed 0
    if (R.tier < 1) R.tier = 1; if (R.tier > 100) R.tier = 100; if (R.best < 0) R.best = 0; if (R.best > 100) R.best = 100; if (R.unlock < R.tier) R.unlock = R.tier; if (R.unlock < RIFT_UNLOCK0) R.unlock = RIFT_UNLOCK0; if (R.unlock > 100) R.unlock = 100;
}
// 落盘：rec_save 只把此刻的记录拍一份快照、标「待落盘」；真正写文件在存档提交前钩子里（写盘线程，hle_fs.c Commit 开头），而且只在「这次提交里本角色的角色档也被游戏改写过」时写
// （工作目录里的 <角色>.d2s 修改时刻与已提交目录里那份不同；没改写过的文件是克隆来的，时刻相同）——进度侧文件和角色档同一次提交、一起原子换入。
// 原先 rec_save 直接写进工作目录：任何一次提交（包括没重写角色档的）都会把它带走，崩溃后可能「层数涨了、那一层打到的东西和经验没了」，
// 通关点数还会「记录说发过了、角色档里没有」（宿主实测：结算后 2 秒强杀，重进层数 27 → 30 而角色与进秘境前一模一样）。
// 已知不覆盖的一格：变更那一刻恰好有一次存盘「已序列化、还没写盘」（角色档由另一条线程写），那次提交会带上新进度配旧角色档；要撞上 5 分钟一次的定时存盘正落在结算那一拍。
// 提交被拒（存档写失败，工作目录重新克隆）时写进去的文件跟着没了：记下「写在第几次提交」，那次提交没成就接着算待落盘
// Writing to disk: rec_save only takes a snapshot of the record as it is now and marks it "pending write"; the actual file write happens in the save pre-commit hook (disk-writing thread, start of hle_fs.c Commit), and only when "this commit also contains this character's character save rewritten by the game"
// (the modification time of <character>.d2s in the working directory differs from that of the copy in the committed directory; a file that was not rewritten is a clone with the same time) -- the progress sidecar file and the character save go in the same commit and are swapped in atomically together.
// Previously rec_save wrote straight into the working directory: any commit (including ones that did not rewrite the character save) would carry it off, and after a crash it could be that "the level count went up, but the items and experience gained on that level are gone",
// and clear points could even be "the record says granted, the character save has none" (measured on the host: force-killed 2 seconds after completion, on re-entry the level count went 27 → 30 while the character was exactly as before entering the rift).
// One known case not covered: at the very moment of the change a save happens to be "already serialized, not yet written to disk" (the character save is written by another thread), and that commit carries the new progress with the old character save; hitting it requires the once-every-5-minutes timed save to land exactly on the completion tick.
// When a commit is refused (save write failure, the working directory is re-cloned) the file written into it is gone with it: "written in which commit" is recorded, and if that commit did not succeed it goes back to counting as pending write
uint64_t dm_fs_commit_count(void);
static void rec_save(void) { if (!K.who[0]) return; pthread_mutex_lock(&RS.mu); RS.snap = R; strlcpy(RS.who, K.who, sizeof RS.who); RS.dirty = 1; RS.wrote = 0; pthread_mutex_unlock(&RS.mu); }
static void rec_pre_commit(const char *wd) {
    pthread_mutex_lock(&RS.mu);
    if (RS.wrote && dm_fs_commit_count() >= RS.wrote) RS.wrote = 0;   // 上次写进去的那次提交成了 | the commit it was last written into succeeded
    else if (RS.wrote) { RS.wrote = 0; RS.dirty = 1; }                 // 没成（被拒）：重新算待落盘 | did not succeed (refused): count it as pending write again
    if (RS.dirty && wd) { char d[1200], c[1200], p[1200], q[1220]; size_t L = strlen(wd); struct stat a, b;
        snprintf(d, sizeof d, "%s/%s/%s.d2s", wd, RS.who, RS.who); snprintf(c, sizeof c, "%.*s/%s/%s.d2s", (int)(L > 5 && !strcmp(wd + L - 5, ".work") ? L - 5 : L), wd, RS.who, RS.who);
        int fresh = !stat(d, &a) && (stat(c, &b) || a.st_mtimespec.tv_sec != b.st_mtimespec.tv_sec || a.st_mtimespec.tv_nsec != b.st_mtimespec.tv_nsec || a.st_size != b.st_size);   // 已提交目录里还没有这个角色 = 新建的，也算 | this character is not yet in the committed directory = newly created, which also counts
        if (fresh) { snprintf(p, sizeof p, "%s/%s/%s.sw_rift", wd, RS.who, RS.who); snprintf(q, sizeof q, "%s.tmp", p); FILE *f = fopen(q, "w");
            if (!f) LOG("【红】秘境：侧文件写不进 %s", q);
            else { fprintf(f, "tier %d\nunlock %d\nbest %d\nclears %d\ndeaths %d\nms %x\nsunder %x\ngift %d\npts %d\n", RS.snap.tier, RS.snap.unlock, RS.snap.best, RS.snap.clears, RS.snap.deaths, RS.snap.ms, RS.snap.sunder, RS.snap.gift, RS.snap.pts);
                for (int t = 1; t <= 100; t++) if (RS.snap.fast[t]) fprintf(f, "fast %d %d\n", t, RS.snap.fast[t]);
                if (fclose(f) || rename(q, p)) { remove(q); LOG("【红】秘境：侧文件换名失败"); }
                else { RS.dirty = 0; RS.wrote = dm_fs_commit_count() + 1; LOG("秘境：存盘 下次第 %d 层、最高 %d、通关 %d、死亡 %d（随存档提交 #%llu，和角色档一起：%s）", RS.snap.tier, RS.snap.best, RS.snap.clears, RS.snap.deaths, (unsigned long long)RS.wrote, p); } } }
        else { static int n; if (n++ < 20) LOG("秘境：进度待落盘，这次提交没有改写 %s 的角色档，等下一次", RS.who); } }
    pthread_mutex_unlock(&RS.mu);
}
__attribute__((constructor)) static void rec_hook_install(void) { extern void (*g_dm_pre_commit_hook[4])(const char *); g_dm_pre_commit_hook[3] = rec_pre_commit; }

// ---- 通关点数（秘境里人物能力要跟上怪物成长；配套技能硬点上限 40，见 dm_actions.c skill_cap_pump）：
//   最高通关层每 PTS_EVERY 层给 PTS_SKILL 个技能点、PTS_STAT 个属性点，按角色、按最高通关层只发一次；侧文件键 pts = 已发到最高通关第几层
//   （旧文件没有这个键 = 0，按 R.best 把已通关的层数补发）。三个数可调：环境变量 DM_RIFT_PTS_EVERY / DM_RIFT_PTS_SKILL / DM_RIFT_PTS_STAT。
//   发法：服务端直接写玩家属性 5（未用技能点）、4（未用属性点）——同原版任务奖励落到的那两个属性，客户端跟着刷新（宿主实测）。
//   存档里未用技能点 8 位（≤255）、未用属性点 10 位（≤1023）：一份一份发，哪一份发下去会超就停在那里、pts 只记到已发的层，等玩家把点用掉、下次通关 / 进局再补。
//   两个时机：通关那一刻（finish，R.best 更新之后、rec_save 之前）；进局后延 75 拍补发（进局那一拍不动玩家——「进局第 1 拍换关坏佣兵」）。超时通关不给（不算过关）；老存档里历史上超时打出来的最高层照认、不回溯。
// ---- Clear points (in the Rift the character's power has to keep up with monster growth; paired with the hard skill point cap of 40, see
// skill_cap_pump in dm_actions.c):
//   Every PTS_EVERY levels of highest cleared level grant PTS_SKILL skill points and PTS_STAT stat points, issued only once per character and per highest
//   cleared level; sidecar file key pts = the highest cleared level up to which points have been granted
//   (an old file without this key = 0, and the levels already cleared are granted retroactively from R.best). All three numbers are tunable: environment
//   variables DM_RIFT_PTS_EVERY / DM_RIFT_PTS_SKILL / DM_RIFT_PTS_STAT.
//   How they are granted: the server writes player stats 5 (unspent skill points) and 4 (unspent stat points) directly, the same two stats that the
//   original quest rewards land in, and the client refreshes accordingly (measured on the host).
//   In the save file unspent skill points take 8 bits (≤255) and unspent stat points 10 bits (≤1023): grants are issued one at a time, stopping at the
//   one that would overflow, with pts recorded only up to the level already granted; the rest is issued on the next clear / game join after the player
//   has spent the points.
//   Two moments: the instant of a clear (finish, after R.best is updated and before rec_save); and a catch-up 75 ticks after joining a game (the player
//   is not touched on the join tick itself: "changing level on the first tick after joining breaks the mercenary"). A clear after the time limit grants
//   nothing (it does not count as passing); in old saves, a highest level that was reached after the time limit in the past is still honored and not
//   revisited.
static int pts_env(const char *k, int def, int lo, int hi) { const char *e = getenv(k); int v = e ? atoi(e) : def; return v < lo || v > hi ? def : v; }
static int pts_grant(uint64_t player) {   // 返回发了几份（0 = 没有该发的 / 一份都放不下） | returns how many grants were issued (0 = nothing due / not even one fits)
    int every = pts_env("DM_RIFT_PTS_EVERY", 2, 1, 100), ps = pts_env("DM_RIFT_PTS_SKILL", 1, 0, 20), pa = pts_env("DM_RIFT_PTS_STAT", 5, 0, 50);
    int due = R.best / every - R.pts / every, n = 0, sk0 = gs(player, 5), st0 = gs(player, 4);
    if (due <= 0) { if (R.pts < R.best) R.pts = R.best; return 0; }
    while (n < due && sk0 + (n + 1) * ps <= 255 && st0 + (n + 1) * pa <= 1023) n++;
    if (n) { if (ps) ss(player, 5, sk0 + n * ps); if (pa) ss(player, 4, st0 + n * pa); }
    R.pts = n == due ? R.best : (R.pts / every + n) * every;
    LOG("秘境：通关点数 最高通关 %d 层、该发 %d 份、发了 %d 份（每 %d 层 %d 技能点 %d 属性点）→ 未用技能点 %d → %d、未用属性点 %d → %d，已发到第 %d 层%s", R.best, due, n, every, ps, pa, sk0, gs(player, 5), st0, gs(player, 4), R.pts, n < due ? "【注意】未用点数太多放不下，剩下的等用掉后再补" : "");
    if (n) say("dmRiftPtsGot", R.best, n * ps, n * pa);
    if (n < due) say("dmRiftPtsHeld");
    return n;
}

// ---- 赌博加成（设计稿 v3，按绝对概率定）：通关过第 1 层起，地狱赌博出暗金 10%，最高层每多 1 层 +0.1 个百分点（100 层 19.9%）；套装同倍（原版地狱套装 = 暗金 × 2）。
//   写 difficultylevels（数据表根 +0x1518，每条 0x84）地狱那行的 GambleUnique +0x74 / GambleSet +0x70（十万分之一，0x7eebf0 掷一次 % 100000，不夹值；P0 读码）；稀有 +0x6c 不动。
//   只在有资格的角色、人在哈洛加斯时生效（只有安亚加成），离开哈洛加斯 / 换局写回原值
// ---- Gambling bonus (design doc v3, set by absolute probability): from clearing Rift level 1 on, Hell gambling yields uniques at 10%, +0.1 percentage point for each additional highest level (19.9% at Rift level 100); sets scale by the same factor (original Hell set = unique × 2).
//   Writes GambleUnique +0x74 / GambleSet +0x70 of the Hell row of difficultylevels (data table root +0x1518, 0x84 per entry) (in units of 1/100000; 0x7eebf0 rolls once % 100000, no clamping; P0 code reading); rare +0x6c is untouched.
//   Applies only for eligible characters while in Harrogath (only Anya gets the bonus); original values are written back on leaving Harrogath / run change
void dm_rift_gamble_vals(int best, uint32_t *uniq, uint32_t *set) { if (best > 100) best = 100; *uniq = best >= 1 ? 10000u + 100u * (uint32_t)(best - 1) : 0; *set = *uniq * 2; }
static struct { uint64_t row; uint32_t uniq, set; int on; } GB;
static void gamble_restore(void) { if (!GB.on) return; *(uint32_t *)(uintptr_t)(GB.row + 0x74) = GB.uniq; *(uint32_t *)(uintptr_t)(GB.row + 0x70) = GB.set; GB.on = 0; LOG("秘境：赌博加成撤掉（暗金 %u、套装 %u，十万分之一）", GB.uniq, GB.set); }
// ---- 赌博「绝对暗金率」（掷中暗金不降级）：原版掷中暗金后，底材在该物品等级没有合格暗金、或抽中的暗金本局已出过，会降成稀有（0x654914，P2 读码）。
//   做法不拦降级分支，而是让降级分支走不到：人在哈洛加斯时
//   ① 赌博货架只留「在最低可能物品等级下就有合格暗金」的底材。货架 0x7ea770：物品等级 = clamp(人物等级 − 5 + 随机 % 10, 5, 99)，
//      候选 = T+0x16d8 数组的前 cap[等级] 个（cap 在 T+0x16f0 + 等级 × 4，数组按物品表 +0x10d 等级升序），随机取一个；我们按同样的排序重写数组和 cap。
//   ② 原版「本局已出过」位图 G+0x3638 进城时存一份、在城里每拍清零、出城写回（城里只有赌博会生成暗金）。
//   合格暗金 = 暗金表 T+0x13e8（每条 0x15c）里 code 相同、+0x2c 位 0 = 1 且位 2 = 0、天梯赛季字段通过、+0x38 等级 ≤ 物品等级的行（0x6544f8 的条件）；掉落条件公式 3.x 只有「地狱」一种，只在地狱生效所以不查
// ---- Gambling "absolute unique rate" (a rolled unique is not downgraded): in the original game, after rolling a unique, if the base has no qualifying unique at that item level, or the drawn unique has already dropped this run, it is downgraded to rare (0x654914, P2 code reading).
//   The approach does not intercept the downgrade branch but makes it unreachable: while the player is in Harrogath
//   (1) the gambling shelf keeps only bases that "have a qualifying unique even at the lowest possible item level". Shelf 0x7ea770: item level = clamp(character level − 5 + random % 10, 5, 99),
//      candidates = the first cap[level] entries of the array at T+0x16d8 (cap at T+0x16f0 + level × 4; the array is in ascending order of the level at item table +0x10d), one picked at random; we rewrite the array and cap with the same ordering.
//   (2) the original "already dropped this run" bitmap G+0x3638 is saved on entering town, cleared every tick while in town, and written back on leaving (in town only gambling generates uniques).
//   Qualifying unique = a row in the unique table T+0x13e8 (0x15c per entry) with the same code, +0x2c bit 0 = 1 and bit 2 = 0, the ladder season field passing, and level at +0x38 <= item level (the conditions at 0x6544f8); for the drop condition formula, 3.x has only the "Hell" kind, and since this only applies in Hell it is not checked
static struct { uint64_t arr, caps; uint32_t *save_arr; uint32_t save_caps[100], n; int on; uint32_t bm[128]; int bm_on, nbm; uint64_t bmG; } GS;
static void shelf_restore(void) {
    if (GS.on) { memcpy((void *)(uintptr_t)GS.arr, GS.save_arr, GS.n * 4); memcpy((void *)(uintptr_t)GS.caps, GS.save_caps, sizeof GS.save_caps); GS.on = 0; LOG("秘境：赌博货架写回原版"); }
    if (GS.bm_on) { uint32_t *w = (uint32_t *)(uintptr_t)(GS.bmG + 0x3638); for (int i = 0; i < GS.nbm; i++) w[i] |= GS.bm[i]; GS.bm_on = 0; }
}
static void shelf_apply(uint64_t G, uint64_t player) {
    uint64_t T = tables(G); if (!T) return;
    if (!GS.bm_on) { uint64_t nu = rd(T + 0x13f0); GS.nbm = (int)((nu + 31) / 32); if (GS.nbm > 128) GS.nbm = 128; GS.bmG = G; memcpy(GS.bm, (void *)(uintptr_t)(G + 0x3638), GS.nbm * 4); GS.bm_on = 1; }
    memset((void *)(uintptr_t)(G + 0x3638), 0, GS.nbm * 4);   // 每拍清：刚赌出来的那件同局可再赌（出城时把进城前的位写回） | Cleared every tick: the item just gambled can be gambled again in the same run (on leaving town the bits from before entering town are written back)
    if (GS.on) return;
    uint64_t arr = rd(T + 0x16d8), it = rd(T + 0x15c0), ut = rd(T + 0x13e8); uint32_t n = *(uint32_t *)(uintptr_t)(T + 0x16e0), nu = (uint32_t)rd(T + 0x13f0), ni = *(uint32_t *)(uintptr_t)(T + 0x15c8);
    uint32_t *caps = (uint32_t *)(uintptr_t)(T + 0x16f0), *a = (uint32_t *)(uintptr_t)arr; if (!arr || !it || !ut || !n || n > 4096) return;
    // 自检：数组按物品表 +0x10d 等级升序、cap[L] = 等级 ≤ L 的个数（我们对货架结构的理解成立才动）
    // Self-check: the array is in ascending order of the level at item table +0x10d, and cap[L] = number with level <= L (we only act if our understanding of the shelf structure holds)
    for (int L = 5; L < 100; L++) { uint32_t c = 0; for (uint32_t k = 0; k < n; k++) if (a[k] < ni && *(uint8_t *)(uintptr_t)(it + (uint64_t)a[k] * 0x1c0 + 0x10d) <= L) c++;
        if (c != caps[L]) { LOG("【红】秘境：赌博货架结构和读码不符（等级 %d：cap %u，按 +0x10d 数得 %u），不改货架", L, caps[L], c); GS.on = 0; return; } }
    int lo = gs(player, 12) - 5; if (lo < 5) lo = 5; if (lo > 99) lo = 99;
    uint8_t g101 = *(uint8_t *)(uintptr_t)(G + 0x101), g108 = *(uint8_t *)(uintptr_t)(G + 0x108);
    static uint32_t keep[4096]; uint32_t nk = 0;
    for (uint32_t k = 0; k < n; k++) { uint32_t c = a[k]; if (c >= ni) continue; uint32_t code = *(uint32_t *)(uintptr_t)(it + (uint64_t)c * 0x1c0 + 0x80); int ok = 0;
        for (uint32_t u = 0; u < nu && !ok; u++) { uint64_t r = ut + (uint64_t)u * 0x15c; uint8_t fl = *(uint8_t *)(uintptr_t)(r + 0x2c); int16_t f1 = *(int16_t *)(uintptr_t)(r + 0x8c), f2 = *(int16_t *)(uintptr_t)(r + 0x8e);
            if (*(uint32_t *)(uintptr_t)(r + 0x28) != code || !(fl & 1) || (fl & 4) || *(int16_t *)(uintptr_t)(r + 0x38) > lo) continue;
            if (!g101 && !(f1 == 0 || f2 < 0 || (f1 < 0 && g108))) continue;
            ok = 1; }
        if (ok) keep[nk++] = c; }
    if (!nk) { LOG("【红】秘境：赌博货架筛完一件不剩，不改"); return; }
    GS.arr = arr; GS.caps = (uint64_t)(uintptr_t)caps; GS.n = n; free(GS.save_arr); GS.save_arr = malloc(n * 4); memcpy(GS.save_arr, a, n * 4); memcpy(GS.save_caps, caps, sizeof GS.save_caps);
    memcpy(a, keep, nk * 4);
    for (int L = 0; L < 100; L++) { uint32_t c = 0; for (uint32_t k = 0; k < nk; k++) if (*(uint8_t *)(uintptr_t)(it + (uint64_t)keep[k] * 0x1c0 + 0x10d) <= L) c++; caps[L] = c; }
    GS.on = 1; LOG("秘境：赌博货架 %u → %u 件（只留人物等级 − 5 = %d 级就有合格暗金的底材）；cap[%d] %u → %u", n, nk, lo, lo + 4 > 99 ? 99 : lo + 4, GS.save_caps[lo + 4 > 99 ? 99 : lo + 4], caps[lo + 4 > 99 ? 99 : lo + 4]);
}
static void gamble_apply(uint64_t G) {
    uint32_t u, st; dm_rift_gamble_vals(R.best, &u, &st); if (!u || *(uint8_t *)(uintptr_t)(G + 0x104) != 2) { gamble_restore(); return; }
    uint64_t T = tables(G), base = T ? rd(T + 0x1518) : 0; uint64_t n = T ? *(uint32_t *)(uintptr_t)(T + 0x1520) : 0; if (!base || n < 3) return;
    uint64_t row = base + 2 * 0x84; if (!GB.on) { GB.row = row; GB.uniq = *(uint32_t *)(uintptr_t)(row + 0x74); GB.set = *(uint32_t *)(uintptr_t)(row + 0x70); GB.on = 1; }
    *(uint32_t *)(uintptr_t)(row + 0x74) = u; *(uint32_t *)(uintptr_t)(row + 0x70) = st;
    LOG("秘境：赌博加成 最高第 %d 层 → 暗金 %u、套装 %u（十万分之一；原值 %u / %u）", R.best, u, st, GB.uniq, GB.set);
}

static _Atomic int g_run_seq;   // 开了第几层（start_rift 每次 +1；客户端每帧函数据此清自己的表） | Which level has been opened (start_rift adds 1 each time; the client per-frame function clears its own tables based on it)
static _Atomic int g_cli_lv = -1;   // 客户端此刻所在关卡（dm_rift_client_pump 写、服务端拍读：串图要等客户端落地） | The level the client is currently in (written by dm_rift_client_pump, read by the server tick: map chaining waits for the client to land)
// 随机数一律取系统真随机：原先用 C 库 rand()，而宿主与外壳里没有任何地方播种——每次进程启动都是同一个序列，
// 杀掉应用重开进同一层，抽到的图、守护者、奖励全都一样（22 层两次都是 7 → 59 → 122、守护者 12 次里 8 次墨菲斯托）。
// 回放 / 逐字节比对那条路（DM_DETCLOCK 的确定熵 dm_entropy64）不经过这里：开秘境要前端选层页发 req_start，回放只重放客户端发的包
// Random numbers always come from the system's true randomness: previously the C library rand() was used, and nothing in the host or the shell seeds it -- every process start gave the same sequence,
// so killing the app and reopening into the same level drew the same maps, guardian and rewards (level 22 was 7 -> 59 -> 122 both times; the guardian was Mephisto 8 times out of 12).
// The replay / byte-for-byte comparison path (the deterministic entropy dm_entropy64 of DM_DETCLOCK) does not go through here: opening a rift needs the front-end level selection page to send req_start, and replay only replays packets sent by the client
static int rnd(int n) { return n > 0 ? (int)arc4random_uniform((uint32_t)n) : 0; }
static int in_rift_map(int lv) { for (int i = 0; i < K.nmaps; i++) if (K.maps[i] == lv) return 1; return 0; }
int dm_rift_active(void) { return atomic_load(&K.st) == ST_RUN; }

// ---- 界面读数（前端每帧）
// 挑战计时一律读客体时钟 guest_ns()（hle_os.c）：App 在后台 / 锁屏、系统键盘弹着的时候它不走，倒计时也就不走；
// 回城、开面板、读取画面这些游戏照常运行的时间照常计入。开局时刻不落盘（侧文件里记的是用时秒数）。公开仓库
// ---- UI readouts (front end, every frame)
// All challenge timing reads the guest clock guest_ns() (hle_os.c): it does not advance while the App is in the background / the screen is locked or while the system keyboard is up, so the countdown does not advance either;
// time during which the game keeps running -- back in town, panels open, loading screens -- counts as usual. The start time is not written to disk (the sidecar file records elapsed seconds).
static int g_left_done;   // 结算时剩余秒数：打倒守护者后顶部条与面板都显示它，不再往下走 | Seconds left at completion: after the guardian is defeated both the top bar and the panel show it, and it no longer counts down
int dm_rift_hud(dm_rift_hud_t *o) {
    int st = atomic_load(&K.st); if (st != ST_RUN && st != ST_DONE) return 0;
    uint64_t now = guest_ns();
    int el = K.t0_ns ? (int)((now - K.t0_ns) / 1000000000ull) : 0; int need = atomic_load(&K.need);
    o->tier = K.tier; o->progress = need > 0 ? (float)atomic_load(&K.score) / need : 0; if (o->progress > 1) o->progress = 1;
    o->secs_left = st == ST_DONE ? g_left_done : RIFT_SECS - el; o->done = st == ST_DONE; o->guardian = K.guard != 0 && !atomic_load(&K.guard_dead); o->inc = K.inc;
    return 1;
}

// ---- 客户端：认击杀、记新刷出的敌对怪（游戏主线程，客户端单位表 main+0x2d603a0 + 类型 × 0x400；类别 0x2f7b7c(0x165, 玩家, 单位) = 1 是敌对怪，同瞄准锥）
// ---- Client: recognize kills, record newly spawned hostile monsters (game main thread, client unit table main+0x2d603a0 + type × 0x400; category 0x2f7b7c(0x165, player, unit) = 1 means hostile monster, same as the aiming cone)
enum { NQ = 256 };
static _Atomic uint32_t g_newq[NQ]; static _Atomic uint32_t g_newq_w;   // 新见到的敌对怪 id（服务端按目标等级改属性） | Newly seen hostile monster ids (the server changes stats by target level)
static uint32_t g_swept[1024]; int dm_rift_swept(uint32_t id) { return id && g_swept[id & 1023] == id; }   // 这只是结算后清掉的（积分牌不计击杀） | This one was cleared after completion (the kill board does not count the kill)
static int pts_of(uint8_t fl) { return fl & 0x02 ? 15 : fl & 0x08 ? 10 : fl & 0x04 ? 5 : fl & 0x10 ? 2 : 1; }   // 怪物数据 +0x1a：超级独特 / 金怪 / 冠军 / 随从 / 普通（同 hle_target.c） | Monster data +0x1a: super unique / unique (gold) monster / champion / minion / normal (same as hle_target.c)
void dm_rift_client_pump(uint8_t *b) {
    static uint32_t seen[4096], dead[4096]; static int lvp = -1;
    // 每开一层清一次「见过 / 已算死」两张表：表按单位号记，而单位号每局从头编——上一局（一键重开、退出再进）或上一层留下的记录会让新局里
    // 同号的怪「已算过死」不计分、「已见过」不按层改属性（同一张图上连开两层，上一层结算后才死的 7 只怪在新层第 1 拍全算进进度，守护者当拍就出）
    // Clear the two tables "seen / counted as dead" once for every level opened: the tables are keyed by unit id, and unit ids are numbered from scratch
    // every game, so records left by the previous game (one-tap restart, exit and re-enter) or the previous level would make
    // same-id monsters in the new game "already counted as dead" and score nothing, or "already seen" and not get per-level attributes (opening two levels
    // in a row on the same map: the 7 monsters that died only after the previous level's completion were all counted into progress on tick 1 of the new
    // level, and the Guardian appeared on that very tick)
    { static int seq0 = -1; int sq = atomic_load(&g_run_seq); if (sq != seq0) { seq0 = sq; memset(seen, 0, sizeof seen); memset(dead, 0, sizeof dead); } }
    if (!b || atomic_load(&K.st) == ST_IDLE) return;
    uint64_t pl = ((uint64_t (*)(uint32_t))(b + 0x45f9d0))(*(uint32_t *)(b + 0x2d20e4c)); if (!pl) { atomic_store(&g_cli_lv, -1); return; }
    // 测试开关 DM_RIFT_TEST_NOROOM=<第几次>：秘境进行中第 N 次走到这里时，把客户端玩家路径上的房间指针临时清空再读关卡、读完写回——
    // 造出换幕读取画面那几帧的现场（有路径、没有房间；是撞帧性质，自然跑撞不撞得上看运气）。修前的写法在这一行必崩，现在读到 0
    // Test switch DM_RIFT_TEST_NOROOM=<Nth time>: the Nth time this point is reached while a rift is in progress, temporarily null the room pointer on the client player's path, read the level, then write it back --
    // recreating the conditions of those few frames of the act-change loading screen (has a path, no room; it is a frame-collision issue, and whether a natural run hits it is down to luck). The pre-fix code always crashed on this line; now it reads 0
    { static int at = -2, n; if (at == -2) { const char *e = getenv("DM_RIFT_TEST_NOROOM"); at = e ? atoi(e) : -1; }
      uint64_t pth = rd(pl + 0x38); if (at > 0 && pth && atomic_load(&K.st) == ST_RUN && ++n == at) { uint64_t keep = rd(pth + 0x20); *(volatile uint64_t *)(uintptr_t)(pth + 0x20) = 0;
          LOG("秘境：测试 DM_RIFT_TEST_NOROOM 第 %d 次，客户端玩家房间指针临时清空后读关卡", n); int v = unit_level(pl); *(volatile uint64_t *)(uintptr_t)(pth + 0x20) = keep;
          LOG("秘境：测试 DM_RIFT_TEST_NOROOM 读到关卡 %d，没崩；房间指针已写回（现在读到 %d）", v, unit_level(pl)); } }
    int lv = unit_level(pl);   // 读取画面期间房间为空 → 0（不是秘境图），下面不扫怪 | During the loading screen the room is null -> 0 (not a rift map); no monster scan below
    atomic_store(&g_cli_lv, lv);   // 关卡号在打倒守护者之后也要更新（串图等它） | The level id must also be updated after the guardian is defeated (map chaining waits for it)
    if (atomic_load(&K.st) != ST_RUN) return;
    if (lv != lvp) { lvp = lv; LOG("秘境：客户端看到关卡 %d（%s秘境图）", lv, in_rift_map(lv) ? "是" : "不是"); }
    if (!in_rift_map(lv)) return;
    for (int k = 0; k < 128; k++) for (uint64_t u = rd((uint64_t)(uintptr_t)(b + 0x2d603a0 + 1 * 0x400 + k * 8)); u; u = rd(u + 0x158)) {
        uint32_t id = *(uint32_t *)(uintptr_t)(u + 8), m = *(uint32_t *)(uintptr_t)(u + 0xc), h = id & 4095;
        if (id == K.guard) continue;   // 守护者另算 | The guardian is counted separately
        int alive = m != 0 && m != 12;
        if (alive) { if (seen[h] == id) continue;
            if (((int (*)(int, uint64_t, uint64_t))(b + 0x2f7b7c))(0x165, pl, u) != 1) continue;
            seen[h] = id; uint32_t w = atomic_fetch_add(&g_newq_w, 1); atomic_store(&g_newq[w % NQ], id); continue; }
        if (seen[h] != id || dead[h] == id) continue;   // 只算活着时见过、认作敌对的 | Count only those seen alive and identified as hostile
        dead[h] = id; uint64_t md = rd(u + 0x10); uint8_t fl = md ? *(uint8_t *)(uintptr_t)(md + 0x1a) : 0; int p = pts_of(fl);
        int s = atomic_fetch_add(&K.score, p) + p; static int ln; if (ln++ < 40 || s % 50 < p) LOG("秘境：击杀 id %u（标志 %#x，+%d）→ 分数 %d / %d", id, fl, p, s, atomic_load(&K.need));
    }
}

// ---- 难度：9 层起按 MonLvl 表比例改刷出来的怪
// 层 → 怪物等级 / 总倍率 / 精英组倍数：
//   等级：9–25 层 = 角色等级 + 2 +（层 − 9），96 封顶；26 层起 96 之后每层 +1（不再看角色等级），38 层到 109 封顶（lvl_cap，MonLvl 表最大行是 110，不用它，见下）。
//     等级除了命中公式里「双方等级」那一项和面板显示，还带着原版自己的伤害 / 命中值查表（见下面伤害那条）；生命的量由下面这条线定。
//   总倍率（相对「同一只怪在起算层 9 层时的值」），从 9 层到 100 层全程线性——人物侧给点是匀速的（每 2 层 1 技能点），怪也得匀速，
//     先平后陡会让前 40 层人物反超、后面追不上（两侧一起调到「略有挑战」）：
//       生命(T) = 1 + (GROW_HP100 − 1) × (T − 9) / 91，伤害(T) = 1 + (GROW_DM100 − 1) × (T − 9) / 91；缺省 100 层 ×4.87 / ×3.88。
//     「起算层那只怪」= 这只怪按 MonLvl 表抬到 9 层等级（角色等级 + 2，96 封顶；原生等级更高的就是它自己）时的生命 / 伤害。
//     实现 = 等级表倍率 × 一个补足的百分比，两者相乘正好是上面的总倍率；所以每只怪的终值只和（原生值、9 层等级、层数）有关。
//   三个数可用环境变量覆盖，联调只改数不改代码：DM_RIFT_GROW_HP100（百分数，缺省 487）、DM_RIFT_GROW_DM100（缺省 388）、DM_RIFT_GROW_FROM（起算层，缺省 9）。
//   精英组数（原值的倍数）：1–40 层 ×2、41–50 ×2.5、51–60 ×3、61–80 ×3.5、81–100 ×4。密度全程 ×3（等真机帧率数再动）。
// 首版不含：冠军 / 金怪词缀数、抗性 / 免疫词缀权重（设计稿 41–60、81–100 层那两项，另读码）。
//   伤害那份补足的百分比（两次定）：40 层之前不加；从起加层的 0 起逐层线性加到 100 层的现值（100 层终点不变，中间不留台阶——人物给点也是平滑的）。
//     原版在怪物切动作模式时按「当前等级」重查 MonLvl 算最小 / 最大伤害和命中值（0x689220，实测 144 行读数逐值相等），我们抬了等级它自己就涨；
//     这份百分比是在那之上另乘的。起加层 DM_RIFT_GROW_DM_FROM（缺省 40），终点仍由 DM_RIFT_GROW_DM100 定
// ---- Difficulty: from Rift level 9, spawned monsters are modified in proportion to the MonLvl table
// Rift level → monster level / total multiplier / elite pack multiple:
//   Monster level: Rift levels 9–25 = character level + 2 + (Rift level − 9), capped at 96; from Rift level 26, +1 per level beyond 96 (character level no longer considered), capped at 109 at Rift level 38 (lvl_cap; the largest row of the MonLvl table is 110, which is not used, see below).
//     Besides the "both sides' levels" term in the hit formula and the panel display, the level also brings the original game's own damage / attack rating table lookup (see the damage item below); the amount of life is set by the line below.
//   Total multiplier (relative to "the same monster's value at the baseline Rift level 9"), linear all the way from Rift level 9 to Rift level 100 -- points on the character side come at a steady rate (1 skill point every 2 Rift levels), so monsters must be steady too,
//     flat first and steep later would let the character pull ahead in the first 40 Rift levels and be unable to keep up afterwards (both sides are tuned together to "slightly challenging"):
//       life(T) = 1 + (GROW_HP100 − 1) × (T − 9) / 91, damage(T) = 1 + (GROW_DM100 − 1) × (T − 9) / 91; defaults at Rift level 100 ×4.87 / ×3.88.
//     "The monster at the baseline Rift level" = this monster's life / damage when raised by the MonLvl table to the Rift level 9 monster level (character level + 2, capped at 96; one whose native level is higher stays as it is).
//     Implementation = level table multiplier × a make-up percentage, the product of the two being exactly the total multiplier above; so each monster's final value depends only on (native value, Rift level 9 monster level, Rift level number).
//   The three numbers can be overridden with environment variables, so tuning changes numbers only, not code: DM_RIFT_GROW_HP100 (percent, default 487), DM_RIFT_GROW_DM100 (default 388), DM_RIFT_GROW_FROM (baseline Rift level, default 9).
//   Elite pack count (multiple of the original value): Rift levels 1–40 ×2, 41–50 ×2.5, 51–60 ×3, 61–80 ×3.5, 81–100 ×4. Density ×3 throughout (to be changed only once real device frame rate numbers are in).
// Not in the first version: champion / unique monster affix counts, resistance / immunity affix weights (the two design items for Rift levels 41–60 and 81–100; the code is to be read separately).
//   The make-up percentage for damage (decided in two rounds): none before Rift level 40; from 0 at the start Rift level it rises linearly per Rift level to the current value at Rift level 100 (the Rift level 100 end point is unchanged, and no step is left in between -- character points are smooth too).
//     When a monster switches action mode the original game looks MonLvl up again by "current level" to compute min / max damage and attack rating (0x689220, measured: 144 rows of readouts equal value for value), so it rises by itself once we raise the level;
//     this percentage is multiplied on top of that. Start Rift level DM_RIFT_GROW_DM_FROM (default 40); the end point is still set by DM_RIFT_GROW_DM100
static int grow_cfg(int which) { static int v[4] = { -1, -1, -1, -1 }; static const char *const nm[4] = { "DM_RIFT_GROW_HP100", "DM_RIFT_GROW_DM100", "DM_RIFT_GROW_FROM", "DM_RIFT_GROW_DM_FROM" }; static const int def[4] = { 487, 388, 9, 40 };
    if (v[which] < 0) { const char *e = getenv(nm[which]); v[which] = e && atoi(e) > 0 ? atoi(e) : def[which]; if (which < 2 && v[which] < 100) v[which] = 100; if (which == 2 && (v[which] < 9 || v[which] > 99)) v[which] = 9; if (which == 3 && v[which] < 9) v[which] = 9; } return v[which]; }
// 怪物等级上限（封顶 109）：MonLvl 表 110 级那一行的命中值是断崖（109 级 4320 → 110 级 6532，伤害列只 124 → 130），怪一到 110 级对人物的命中率一步跳十几个点；
// 停在 109 就没有这一步。DM_RIFT_LVL_CAP 可调（96–110），守护者的「本层等级 + 3」同样受它封顶
// Monster level cap (capped at 109): the attack rating in the level 110 row of the MonLvl table is a cliff (level 109 4320 → level 110 6532, while the damage column only goes 124 → 130), so as soon as a monster reaches level 110 its chance to hit the character jumps by a dozen-odd points in one step;
// stopping at 109 avoids that step. Adjustable with DM_RIFT_LVL_CAP (96–110); the guardian's "this Rift level's monster level + 3" is capped by it as well
static int lvl_cap(void) { static int v = -1; if (v < 0) { const char *e = getenv("DM_RIFT_LVL_CAP"); int c = e ? atoi(e) : 109; v = c < 96 ? 96 : c > 110 ? 110 : c; } return v; }
int dm_rift_tier_level(int tier, int clvl) { if (tier < 9) return 0; if (tier <= 25) { int t = clvl + 2 + (tier - 9); return t > 96 ? 96 : t; } int t = 96 + (tier - 25), c = lvl_cap(); return t > c ? c : t; }
// 总倍率 ×1000（which 0 = 生命、1 = 伤害）：起算层及以下 = 1000
// Total multiplier ×1000 (which 0 = life, 1 = damage): baseline level and below = 1000
static int tier_total_x1000(int tier, int which) { int f = grow_cfg(2), top = grow_cfg(which); if (tier <= f) return 1000; if (tier > 100) tier = 100; return 1000 + (int)((long long)(top - 100) * 10 * (tier - f) / (100 - f)); }
static int tier_elite_x2(int tier) { return tier <= 40 ? 4 : tier <= 50 ? 5 : tier <= 60 ? 6 : tier <= 80 ? 7 : 8; }
static int target_level(uint64_t player) { return dm_rift_tier_level(K.tier, gs(player, 12)); }
static _Atomic uint32_t g_mlhp[112];   // MonLvl 表的生命列（资料片地狱）按等级缓存：选层面板在另一个线程上算「怪物强度」用（服务端拍里填一次） | life column of the MonLvl table (expansion, Hell) cached by level: used to compute "Monster Power" for the level selection panel on another thread (filled once in a server tick)
// 选层面板「怪物强度」= 生命总倍率 ×10（相对地狱 /players 1 的原生怪）：人数档（(人数 + 1) / 2）× 9 层等级（表内生命 ÷ 85 级那行，地狱野外原生等级的参照）× 本层总倍率。
// 普通怪的生命远低于原版上限，这个数就是实际生效值；守护者顶到上限时另记日志。表还没读到时只有人数档那一项
// Level selection panel "Monster Power" = total life multiplier ×10 (relative to a native Hell /players 1 monster): player-count factor ((players + 1) /
// 2) × level 9 level (table life ÷ the level 85 row, the reference for native Hell wilderness levels) × this level's total multiplier.
// Normal monsters' life is far below the original game's cap, so this number is the value actually in effect; when the Guardian hits the cap it is
// logged separately. Until the table has been read, only the player-count factor is included
int dm_rift_tier_hp_x10(int tier, int clvl) { int p = tier < 8 ? tier : 8, l9 = dm_rift_tier_level(9, clvl); long long v = (long long)(p + 1) * 5; if (tier < 9) return (int)v;
    uint32_t h85 = atomic_load(&g_mlhp[85]), hl = l9 > 0 && l9 < 112 ? atomic_load(&g_mlhp[l9]) : 0; if (h85 && hl && hl > h85) v = v * hl / h85; return (int)(v * tier_total_x1000(tier, 0) / 1000); }
static uint32_t monlvl(uint64_t G, int lvl, int grp) { uint64_t T = tables(G), a = rd(T + 0x1178), n = rd(T + 0x1180); if (!a || lvl < 0 || (uint64_t)lvl >= n) return 0; return *(uint32_t *)(uintptr_t)(a + (uint64_t)lvl * 0x78 + (grp * 6 + 5) * 4); }   // 组 0 AC、2 HP、3 DM；资料片地狱列 | Group 0 AC, 2 HP, 3 DM; expansion Hell column
static void scale_monster(uint64_t G, uint64_t u, int lt, int l9) {   // lt = 本层的等级（守护者 + 3）、l9 = 起算层的等级 | lt = this level's level (Guardian + 3), l9 = the baseline level's level
    int l0 = gs(u, 12); if (l0 <= 0) return; if (lt > lvl_cap()) lt = lvl_cap(); if (l9 > lt) l9 = lt;
    int tx = tier_total_x1000(K.tier, 0), lb = l0 > l9 ? l0 : l9, le = l0 > lt ? l0 : lt;   // lb：「起算层那只怪」的等级；le：写进单位的等级（不降级） | lb: the level of "that monster at the baseline level"; le: the level written into the unit (never lowered)
    if (le == l0 && lb == l0 && tx == 1000) return;   // 9 层、原生等级已不低于目标：什么都不用改 | level 9 and the native level is already no lower than the target: nothing to change
    uint32_t h0 = monlvl(G, l0, 2), hb = monlvl(G, lb, 2), d0 = monlvl(G, l0, 3), db = monlvl(G, lb, 3), a0 = monlvl(G, l0, 0), a1 = monlvl(G, le, 0); if (!h0 || !d0 || !a0) return;
    long long want = (long long)gs(u, 7) * hb / h0 * tx / 1000, hp = want; if (hp > 0x7fffff00LL) hp = 0x7fffff00LL;   // 定点 ×256；原版上限 2²³ 点 | Fixed point ×256; the original cap is 2²³ points
    // 顶到上限不静默：这只怪（多半是守护者）的生命没按线涨足，只剩伤害在涨。每开一层记前 3 只
    // Hitting the cap is not silent: this monster (most likely the Guardian) did not get its full life according to the line, and only its damage keeps
    // growing. The first 3 are logged for each level opened
    if (hp != want) { static int n, sq = -1; int cur = atomic_load(&g_run_seq); if (cur != sq) { sq = cur; n = 0; } if (n++ < 3) LOG("秘境：第 %d 层 怪 id %u（类号 %u）生命按层应为 %lld 点，顶到原版上限 %lld 点（实得 ×%.2f，应得 ×%.2f）；伤害照加", K.tier, *(uint32_t *)(uintptr_t)(u + 8), *(uint32_t *)(uintptr_t)(u + 4), want >> 8, hp >> 8, (double)hp / gs(u, 7), (double)want / gs(u, 7)); }
    int dpct;   // 伤害：加到属性 25 上的百分比 | Damage: percentage added to stat 25
    { int f = grow_cfg(3), tt = K.tier > 100 ? 100 : K.tier, full = (int)(((long long)db * tier_total_x1000(100, 1) / d0 - 1000 + 5) / 10);   // full = 这只怪在 100 层时的百分比（原算法的终点） | full = this monster's percentage at Rift level 100 (the end point of the original algorithm)
      dpct = tt <= f || f >= 100 ? 0 : (int)((long long)full * (tt - f) / (100 - f)); }   // 起加层及以下 0，之后逐层线性到 full（见 grow_cfg 上面的说明） | 0 at and below the start Rift level, then linear per Rift level up to full (see the note above grow_cfg)
    // 每开一层打前 6 只（原先整个进程只打前 6 只：同进程第二局 / 第二层就读不到这行， ④ 判不了「第二局的怪有没有按层改」）
    // Print the first 6 for every level opened (previously only the first 6 of the whole process were printed: the second game / second level in the same
    // process had no such line, so check ④ could not judge "whether the second game's monsters were modified by level")
    { static int n, sq = -1; int cur = atomic_load(&g_run_seq); if (cur != sq) { sq = cur; n = 0; } if (n++ < 6) LOG("秘境：按层改怪 id %u 等级 %d → %d：生命 %d → %lld、伤害 +%d%%、防御 %d → %lld（第 %d 层生命总倍率 ×%d.%02d，起算层等级 %d；伤害另随等级走原版查表）", *(uint32_t *)(uintptr_t)(u + 8), l0, le, gs(u, 7) >> 8, hp >> 8, dpct, gs(u, 31), (long long)gs(u, 31) * a1 / a0, K.tier, tx / 1000, tx % 1000 / 10, lb); }
    ss(u, 7, (int)hp); ss(u, 6, (int)hp); ss(u, 25, gs(u, 25) + dpct); ss(u, 31, (int)((long long)gs(u, 31) * a1 / a0)); ss(u, 12, le);
}

// 测试读数 DM_RIFT_TEST_DMGLOG=1：每开一层记前 12 只新刷的怪，刷出 50 拍后（原版在切动作模式时按当前等级重算最小 / 最大伤害和命中值，0x689220）
// 打出它实际生效的 等级 12、最小伤害 21、最大伤害 22、命中值 19、伤害百分比 25、类号——拿来对「等级查表 × 人数档 × 我们加的百分比」的算数
// Test readout DM_RIFT_TEST_DMGLOG=1: for each Rift level opened, record the first 12 newly spawned monsters, and 50 ticks after spawning (the original game recomputes min / max damage and attack rating from the current level when switching action mode, 0x689220)
// print the values actually in effect: level 12, min damage 21, max damage 22, attack rating 19, damage percent 25, class id -- used to check the arithmetic of "level table lookup × /players setting × the percentage we add"
static struct { uint32_t id, t; } g_dp[12]; static int g_dpn, g_dpseq = -1;
static void dmg_probe_add(uint32_t id, uint32_t t) { static int on = -1; if (on < 0) on = getenv("DM_RIFT_TEST_DMGLOG") != NULL; if (!on) return;
    int cur = atomic_load(&g_run_seq); if (cur != g_dpseq) { g_dpseq = cur; g_dpn = 0; } if (g_dpn < 12) { g_dp[g_dpn].id = id; g_dp[g_dpn].t = t; g_dpn++; } }
static void dmg_probe_pump(uint64_t G, uint32_t t) {
    for (int i = 0; i < g_dpn; i++) { if (!g_dp[i].id || t - g_dp[i].t < 50) continue; uint64_t u = find_unit(G, 1, g_dp[i].id); uint32_t id = g_dp[i].id; g_dp[i].id = 0; if (!u) continue;
        LOG("秘境：伤害读数 第 %d 层 怪 id %u 类号 %u 等级 %d：最小 %d 最大 %d 命中值 %d 伤害百分比 %d（人数档属性 100 = %d）", K.tier, id, *(uint32_t *)(uintptr_t)(u + 4), gs(u, 12), gs(u, 21), gs(u, 22), gs(u, 19), gs(u, 25), gs(u, 100)); }
}

// ---- 随从生命按层放大（「让召唤物和束缚恶魔跟着层数规则增长就行。也就是打破上限。」）：秘境进行中，玩家的召唤物 / 束缚恶魔（不含佣兵）的生命上限 = 基数 × 本层倍率，
// 倍率就是怪物生命那条线：起算层以下 ×1，到 100 层 ×(HP100 ÷ 100)，中间线性。缺省跟怪物的两个数走（DM_RIFT_GROW_HP100 / DM_RIFT_GROW_FROM，现为 487 / 9）；
// 要单独调：DM_RIFT_PET_HP100（百分数；小于 100 = 关）、DM_RIFT_PET_FROM（起算层）。
// 做法：每 10 拍沿玩家的宠物表（[[玩家+0x10]+0x98] 按宠物类型每项 0x20，项里是节点链：+4 单位号、+0x18 下一个；同 0x6c3130 的走法）枚举，
// 秘境进行中把上限设成 基数 × 倍率、不在秘境里设回基数。束缚恶魔的基数是转化那一刻被 monpet 表封顶后的值（16200–27000），所以对它就是「封顶同比放大」。
// 基数不从当前上限反推：束缚恶魔随存档保存，放大后的上限会被原样写进存档（60 层放大到 85536 后保存退出，关掉开关读档仍是 85536）。
// 所以每次放大都把「类号 基数 放大后的值」记进侧文件 <存档>/<角色名>/<角色名>.dm_pets（写在工作目录、随存档提交，同 .sw_rift）；
// 读档后头一次见到某只随从，若它的上限正好等于侧文件里同类号的「放大后的值」，基数取侧文件里的，不取当前值——连续存读不叠乘。
// 侧文件里有记录时，开关关着也照常跑「设回基数」那一半：关掉开关再读档，放大过的随从回到基数、记录随之清掉。游戏自己改了上限（升技能、光环）就把新值当基数。
// 测试读数 DM_RIFT_TEST_PETDUMP=1：每 125 拍打一行每只随从的类型 / 类号 / 等级 / 生命上限（开关关着也打）
// ---- Minion life scales by Rift level ("just let summons and bound demons grow by the Rift level rule. That is, break the cap."): during a rift run, the maximum life of the player's summons / bound demons (mercenary excluded) = base × this Rift level's multiplier,
// the multiplier being the monster life line: ×1 below the starting Rift level, ×(HP100 ÷ 100) at Rift level 100, linear in between. By default it follows the monsters' two numbers (DM_RIFT_GROW_HP100 / DM_RIFT_GROW_FROM, currently 487 / 9);
// to tune it separately: DM_RIFT_PET_HP100 (percent; below 100 = off), DM_RIFT_PET_FROM (starting Rift level).
// How: every 10 ticks enumerate along the player's pet table ([[player+0x10]+0x98], 0x20 per entry by pet type, each entry holding a node chain: +4 unit id, +0x18 next; the same walk as 0x6c3130),
// setting the maximum to base × multiplier during a rift run and back to base when not in a rift. A bound demon's base is the value capped by the monpet table at the moment of conversion (16200–27000), so for it this is "scaling the cap proportionally".
// The base is not derived back from the current maximum: bound demons are saved with the save, and the boosted maximum would be written into the save as-is (boosted to 85536 at Rift level 60, then save and exit: loading with the switch off still gives 85536).
// So every boost records "class id, base, boosted value" in the sidecar file <save>/<character name>/<character name>.dm_pets (written in the working directory and committed with the save, same as .sw_rift);
// the first time a minion is seen after loading, if its maximum exactly equals the "boosted value" of the same class id in the sidecar file, the base is taken from the sidecar file rather than from the current value -- repeated save / load does not compound.
// While the sidecar file has records, the "set back to base" half still runs even with the switch off: turn the switch off and load, and boosted minions return to base and their records are cleared along with that. If the game itself changes the maximum (skill level-up, auras), the new value becomes the base.
// Test readout DM_RIFT_TEST_PETDUMP=1: every 125 ticks print one line with each minion's type / class id / level / maximum life (printed even with the switch off)
static int pet_cfg(int which) { static int v[2] = { -1, -1 }; static const char *const nm[2] = { "DM_RIFT_PET_HP100", "DM_RIFT_PET_FROM" };
    if (v[which] < 0) { const char *e = getenv(nm[which]); int x = e ? atoi(e) : 0;   // 没给：跟怪物生命那条线走（grow_cfg：终点 DM_RIFT_GROW_HP100、起算层 DM_RIFT_GROW_FROM）；给了 HP100 < 100 = 关 | not given: follow the monster life line (grow_cfg: end point DM_RIFT_GROW_HP100, starting Rift level DM_RIFT_GROW_FROM); given HP100 < 100 = off
        v[which] = which == 0 ? (!e ? grow_cfg(0) : x >= 100 ? x : 0) : (e && x >= 1 && x <= 99 ? x : grow_cfg(2)); } return v[which]; }
static struct { uint32_t cls; int base, boosted; uint64_t gone; } g_pf[16]; static int g_pfn; static char g_pf_who[sizeof K.who];   // gone ≠ 0：这只已设回基数，记录再留到其后第 2 次存档提交（见 pet_hp_pump 末尾） | gone ≠ 0: this one has been set back to base, and the record is kept until the 2nd save commit after that (see the end of pet_hp_pump)
uint64_t dm_fs_commit_count(void);
static int pet_path(char *o, size_t n) { const char *w = dm_save_workdir(); if (!w || !K.who[0]) return 0; snprintf(o, n, "%s/%s/%s.dm_pets", w, K.who, K.who); return 1; }
static void pet_file_load(void) { g_pfn = 0; strlcpy(g_pf_who, K.who, sizeof g_pf_who); char p[1200], ln[96]; if (!pet_path(p, sizeof p)) return; FILE *f = fopen(p, "r"); if (!f) return;
    while (g_pfn < 16 && fgets(ln, sizeof ln, f)) { unsigned c; int b_, x; if (sscanf(ln, "%u %d %d", &c, &b_, &x) == 3 && b_ > 0 && x > 0) { g_pf[g_pfn].cls = c; g_pf[g_pfn].base = b_; g_pf[g_pfn].boosted = x; g_pf[g_pfn].gone = 0; g_pfn++; } }
    fclose(f); if (g_pfn) LOG("秘境：随从基数侧文件读到 %d 条（%s）", g_pfn, p); }
static void pet_file_save(void) { char p[1200], q[1220]; if (!pet_path(p, sizeof p)) return;
    if (!g_pfn) { remove(p); return; }
    snprintf(q, sizeof q, "%s.tmp", p); FILE *f = fopen(q, "w"); if (!f) { LOG("【红】秘境：随从基数侧文件写不进 %s", q); return; }
    for (int i = 0; i < g_pfn; i++) fprintf(f, "%u %d %d\n", g_pf[i].cls, g_pf[i].base, g_pf[i].boosted);
    if (fclose(f) || rename(q, p)) LOG("【红】秘境：随从基数侧文件改名失败 %s", p); }
// 束缚恶魔生命上限的原版封顶，现算：随从表（monpet）那一行的 BoundStat1 须是生命上限（属性 7），用它的算式 BoundCalc1 让游戏自己再算一遍——
// 就是转化那一刻原版的做法（main+0x7aaf78 起：0x51ea40 取行，行 +0x50 属性号、+0x5c 算式号，0x5285b0 求值，生命类属性左移 8 位）。
// 算式是 min(现值, 该类封顶)，人数档读恶魔自己身上记的那份（属性 100，转化时定下、随存档走），所以对没被动过的恶魔算出来就是它现在的值。返回原始定点值；取不到 = 0
// The original's cap on a bound demon's maximum life, computed on the spot: BoundStat1 of its row in the pet table (monpet) must be maximum life (stat 7), and its formula BoundCalc1 is evaluated by the game itself once more -
// exactly what the original does at the moment of conversion (from main+0x7aaf78: 0x51ea40 fetches the row, row +0x50 stat id, +0x5c formula id, 0x5285b0 evaluates, life-type stats shifted left by 8).
// The formula is min(current value, cap of this class), and the player-count tier is read from what is recorded on the demon itself (stat 100, fixed at conversion and saved with the save), so for an untouched demon the result is just its current value. Returns the raw fixed-point value; 0 = unavailable
static int bound_cap(uint64_t u, uint64_t player) {
    uint8_t ver = *(uint8_t *)(uintptr_t)(u + 0x1bd); if (ver < 1 || ver > 3) return 0;
    uint64_t row = ((uint64_t (*)(uint64_t))(B + 0x51ea40))(u); if (!row || *(uint16_t *)(uintptr_t)(row + 0x50) != 7) return 0;
    int v = ((int (*)(int, uint64_t, uint64_t, uint32_t))(B + 0x5285b0))(ver, u, player, *(uint32_t *)(uintptr_t)(row + 0x5c));
    return v > 0 && v <= 0x7fffff ? v << 8 : 0;
}
static void pet_hp_pump(uint64_t G, uint64_t player, uint32_t t) {
    static int dump = -1; if (dump < 0) dump = getenv("DM_RIFT_TEST_PETDUMP") != NULL;
    if (!K.who[0]) return;
    int top = pet_cfg(0), f = pet_cfg(1), tier = K.tier > 100 ? 100 : K.tier, on = top && atomic_load(&K.st) == ST_RUN && tier > f;
    // 每 10 拍一次；但「放大 ↔ 不放大」一变（开局 / 结算 / 放弃）当拍就做，不等：结算那一拍请了存盘（下一拍序列化），放大着的生命不能被写进角色档
    // Once every 10 ticks; but whenever "boosted ↔ not boosted" changes (run start / completion / abandon) it is done on that very tick without waiting: the completion tick requests a save (serialized on the next tick), and boosted life must not be written into the character save
    static int was_on = -1; if (t % 10 && on == was_on) return; was_on = on;
    if (strcmp(g_pf_who, K.who)) pet_file_load();
    if (!top && !dump && !g_pfn) return;   /* 开关关、侧文件里也没有要设回的：一个字不动 | switch off and nothing in the sidecar file to set back either: touch nothing at all */
    static struct { uint32_t id, cls; int base, last; } tb[64]; static unsigned tw;
    int mult = on ? 1000 + (int)((long long)(top - 100) * 10 * (tier - f) / (100 - f)) : 1000, act = top || g_pfn;
    uint64_t pd = rd(player + 0x10), lst = pd ? rd(pd + 0x98) : 0, arr = lst ? rd(lst) : 0, T = tables(G); uint32_t nt = T ? *(uint32_t *)(uintptr_t)(T + 0x1300) : 0; if (!arr || nt > 64) return;
    char line[700]; int lo = 0; line[0] = 0;
    struct { uint32_t cls; int base, boosted; } now[16]; int nn = 0, pf_dirty = 0;
    for (uint32_t ty = 1; ty < nt; ty++) { if (ty == 7) continue;   /* 7 = 佣兵，不算 | 7 = mercenary, not counted */
        int guard = 0; for (uint64_t nd = rd(arr + (uint64_t)ty * 0x20); nd && guard < 64; nd = rd(nd + 0x18), guard++) {
            uint32_t id = *(uint32_t *)(uintptr_t)(nd + 4); uint64_t u = find_unit(G, 1, id); if (!u) continue; uint32_t md = *(uint32_t *)(uintptr_t)(u + 0xc); if (md == 0 || md == 12) continue;
            // cur = 生命上限的基础项（不是 gs 读到的合计）：我们写的是基础项，基数、比较、侧文件全按同一个量走；游戏给随从的百分比生命加成（技能 / 光环）由它自己乘在合计上。
            // 原先这里读合计：有 +N% 加成的随从，写进去的「合计 × 倍率」又被游戏乘一遍 (1 + N%)，下一轮读回 ≠ 上次写入、被当成新基数再乘——每 10 拍涨 (1 + N%) 倍直到溢出
            // cur = the base entry of maximum life (not the total gs reads): what we write is the base entry, and base, comparison and sidecar file all use the same quantity; the percentage life bonus the game gives minions (skills / auras) is multiplied onto the total by the game itself.
            // This used to read the total: for a minion with a +N% bonus, the written "total x multiplier" was multiplied by (1 + N%) again by the game, the next readback != the last write and was taken as a new base and multiplied again - growing by (1 + N%) every 10 ticks until overflow
            int cur = gsb(u, 7), full = gs(u, 7); uint32_t cls = *(uint32_t *)(uintptr_t)(u + 4); if (cur <= 0 || full <= 0) continue;
            // 测试开关 DM_RIFT_TEST_PETPCT=<百分数>（造现场用）：给每只随从的基础项加一条「生命上限 +N%」（属性 76），模拟技能 / 光环给随从的常驻生命加成
            // Test switch DM_RIFT_TEST_PETPCT=<percent> (for staging a scene): add a "maximum life +N%" entry (stat 76) to the base of every minion, imitating a standing life bonus given to minions by a skill / aura
            { static int pct = -1; static uint32_t done[16]; static unsigned dn; if (pct < 0) { const char *e = getenv("DM_RIFT_TEST_PETPCT"); pct = e ? atoi(e) : 0; }
              if (pct > 0) { int seen = 0; for (int i = 0; i < 16; i++) seen |= done[i] == id; if (!seen) { done[dn++ % 16] = id; int b0 = gsb(u, 7), f0 = gs(u, 7); ss(u, 76, pct);
                  LOG("秘境：测试 DM_RIFT_TEST_PETPCT 给随从 id %u（类号 %u）加生命上限 +%d%%：基础 %d、合计 %d → 基础 %d、合计 %d", id, cls, pct, b0 >> 8, f0 >> 8, gsb(u, 7) >> 8, gs(u, 7) >> 8); full = gs(u, 7); } } }
            if (dump && t % 125 == 0 && lo < (int)sizeof line - 110) lo += snprintf(line + lo, sizeof line - lo, " [类型 %u 类号 %u id %u 等级 %d 生命上限 %d（基础 %d）当前 %d]", ty, cls, id, gs(u, 12), full >> 8, cur >> 8, gs(u, 6) >> 8);
            if (!act) continue;
            int k = -1, fresh = 1; for (int i = 0; i < 64; i++) if (tb[i].id == id && tb[i].cls == cls) { k = i; break; }
            if (k < 0) { k = (int)(tw++ % 64); tb[k].id = id; tb[k].cls = cls; tb[k].base = cur; tb[k].last = cur;
                for (int i = 0; i < g_pfn; i++) if (g_pf[i].cls == cls && g_pf[i].boosted == cur) { tb[k].base = g_pf[i].base; LOG("秘境：随从 id %u（类号 %u）读档带着放大后的生命上限 %d，基数按侧文件取 %d", id, cls, cur >> 8, tb[k].base >> 8); break; } }
            else if (cur != tb[k].last) { tb[k].base = cur; tb[k].last = cur; }   /* 游戏自己改过：新值当基数 | changed by the game itself: the new value becomes the base */
            else fresh = 0;
            // 一次性修正（留在存档里的值）：束缚恶魔的基数不可能高过原版封顶——高了就是修正前那个循环写进角色档的，设回封顶；
            // 侧文件里同类号、基数也高过封顶的记录是那条链上的，一并去掉。低于封顶的污染值和正常值分不开，不碰。只在基数刚定下时判（头一次见到 / 游戏改过）
            // One-time repair (values left in saves): a bound demon's base cannot be higher than the original's cap - if it is, it was written into the character save by the loop that existed before the fix, so set it back to the cap;
            // records in the sidecar file with the same class id whose base is also above the cap belong to that chain and are removed as well. Polluted values below the cap cannot be told apart from normal ones and are left alone. Checked only when the base has just been established (first seen / changed by the game)
            if (fresh && ty == 21) { int cap = bound_cap(u, player); if (cap > 0 && tb[k].base > cap) {
                LOG("秘境：%s 的束缚恶魔（类号 %u）生命基础 %d 超过上限，已设回 %d", K.who, cls, tb[k].base >> 8, cap >> 8); tb[k].base = cap;
                for (int i = 0; i < g_pfn; ) { if (g_pf[i].cls == cls && g_pf[i].base > cap) { g_pf[i] = g_pf[--g_pfn]; pf_dirty = 1; } else i++; } } }
            // 封顶按合计算：合计 = 基础 × (合计 ÷ 基础)，合计不能过属性的上限（0x7fffff00 = 8388607 点；再大就溢出成负数，游戏读成 1 点）
            // The cap is computed on the total: total = base x (total / base), and the total must not exceed the stat's limit (0x7fffff00 = 8388607 points; any larger and it overflows to a negative number, which the game reads as 1 point)
            long long want = (long long)tb[k].base * mult / 1000, lim = full > cur ? 0x7fffff00LL * cur / full : 0x7fffff00LL; if (want > lim) want = lim;
            if ((int)want != cur) { int hp = gs(u, 6); long long nf = (long long)full * want / cur, nh = (long long)hp * want / cur; if (nh < 256) nh = 256; if (nh > nf) nh = nf;   /* 当前生命同比例；nf = 写完后的合计 | current life in the same proportion; nf = the total after writing */
                ss(u, 7, (int)want); ss(u, 6, (int)nh);
                LOG("秘境：随从 id %u（宠物类型 %u，类号 %u）生命上限基础项 %d → %lld（基数 %d，第 %d 层 ×%d.%02d）；合计 %d → %d", id, ty, cls, cur >> 8, want >> 8, tb[k].base >> 8, K.tier, mult / 1000, mult % 1000 / 10, full >> 8, gs(u, 7) >> 8); }
            tb[k].last = (int)want;
            if (tb[k].last != tb[k].base && nn < 16) { now[nn].cls = cls; now[nn].base = tb[k].base; now[nn].boosted = tb[k].last; nn++; } } }
    // 侧文件 = 现在放大着的 + 刚设回基数的。设回基数的记录不当场删，留到其后第 2 次存档提交：那一刻若正有一次存盘「已序列化、还没写盘」，
    // 它带的是放大后的值，记录先没了的话读档会把放大后的值当基数、下次再乘一遍（角色档由另一条线程写，同编年史仓库的「第二次提交起」规则）
    // Sidecar file = those currently boosted + those just set back to base. A set-back record is not deleted on the spot but kept until the 2nd save commit after it: if at that moment a save is "already serialized, not yet written to disk",
    // it carries the boosted value, and with the record already gone, loading would take the boosted value as the base and multiply it once more next time (the character save is written by another thread; same as the chronicle vault's "from the second commit on" rule)
    if (act) { uint64_t cc = dm_fs_commit_count(); struct { uint32_t cls; int base, boosted; uint64_t gone; } nl[16]; int m = 0;
        for (int i = 0; i < nn; i++) { nl[m].cls = now[i].cls; nl[m].base = now[i].base; nl[m].boosted = now[i].boosted; nl[m].gone = 0; m++; }
        for (int i = 0; i < g_pfn && m < 16; i++) { int live = 0; for (int j = 0; j < nn; j++) live |= now[j].cls == g_pf[i].cls && now[j].base == g_pf[i].base && now[j].boosted == g_pf[i].boosted; if (live) continue;
            uint64_t gone = g_pf[i].gone ? g_pf[i].gone : cc + 1; if (cc >= gone + 1) continue;   /* 设回之后已经提交过两次：记录可以没了 | two commits have happened since it was set back: the record can go */
            nl[m].cls = g_pf[i].cls; nl[m].base = g_pf[i].base; nl[m].boosted = g_pf[i].boosted; nl[m].gone = gone; m++; }
        int same = m == g_pfn; for (int i = 0; same && i < m; i++) same = nl[i].cls == g_pf[i].cls && nl[i].base == g_pf[i].base && nl[i].boosted == g_pf[i].boosted && !nl[i].gone == !g_pf[i].gone;
        if (!same || pf_dirty) { g_pfn = m; for (int i = 0; i < m; i++) { g_pf[i].cls = nl[i].cls; g_pf[i].base = nl[i].base; g_pf[i].boosted = nl[i].boosted; g_pf[i].gone = nl[i].gone; } pet_file_save(); } }
    if (dump && t % 125 == 0) LOG("秘境：随从读数 拍 %u（%s，第 %d 层，倍率 ×%d.%02d，侧文件 %d 条）：%s", t, atomic_load(&K.st) == ST_RUN ? "秘境进行中" : "不在秘境", K.tier, mult / 1000, mult % 1000 / 10, g_pfn, lo ? line : " 没有随从");
}

// ---- 门
// 任务链表：头要解两次（0x73f870 门初始化同法），类型在记录 +0、下一项 +0x310
// ---- Doors
// Quest linked list: the head must be dereferenced twice (same as the door init at 0x73f870), type at record +0, next entry +0x310
static uint64_t quest_rec(uint64_t G, int type) { int n = 0; for (uint64_t q = rd(rd(G + 0x21d8)); q && n < 64; q = rd(q + 0x310), n++) { if (*(int32_t *)(uintptr_t)q == type) return q; } return 0; }
// 在 room 里 (cx, cy) 附近找空地建一扇 342 门（开着）。342 的初始化会改第三幕任务记录，建前备份、建后写回
// exact = 1：就建在 (cx, cy) 这一格，不再交给原版找空地（它只看物件位 0x400，会把点挪到墙里；结算处的落点已按玩家可达选好）
// Find free ground near (cx, cy) in room and build a 342 door (open). Initializing 342 modifies the Act 3 quest record; back it up before building and write it back after
// exact = 1: build it right on the cell (cx, cy), without handing it to the original game's free-ground search again (it only looks at the object bit 0x400 and would move the point into a wall; the landing point at the completion has already been chosen as reachable by the player)
static uint32_t place_door(uint64_t G, uint64_t room, int cx, int cy, const char *what, int cls, int exact) {
    int32_t pos[2] = { cx, cy }; uint64_t rr = !room ? 0 : exact ? room : free_spot(G, room, pos); if (!rr) return 0;
    uint64_t q = quest_rec(G, 0x14); uint8_t o21 = q ? *(uint8_t *)(uintptr_t)(q + 0x21) : 0; uint32_t o24 = q ? *(uint32_t *)(uintptr_t)(q + 0x24) : 0;
    uint64_t u = ((uint64_t (*)(uint64_t, uint64_t, int, int, int, int, int, int, uint64_t))(B + 0x7ce630))(G, rr, 2, cls, pos[0], pos[1], 0, 0, 0);
    if (q) { int changed = *(uint8_t *)(uintptr_t)(q + 0x21) != o21 || *(uint32_t *)(uintptr_t)(q + 0x24) != o24; *(uint8_t *)(uintptr_t)(q + 0x21) = o21; *(uint32_t *)(uintptr_t)(q + 0x24) = o24;
        LOG("秘境：建门%s第三幕任务记录 +0x21 / +0x24（已写回 %u / %u）", changed ? "改了" : "没改", o21, o24); }
    if (!u) return 0;
    ((void (*)(uint64_t, int))(B + 0x5ca8a0))(u, 2);   // 门开着（模式 2）才可点 | Clickable only when the door is open (mode 2)
    LOG("秘境：%s开门（物件 %d id %u 在 %d,%d，模式 %u）", what, cls, *(uint32_t *)(uintptr_t)(u + 8), pos[0], pos[1], *(uint32_t *)(uintptr_t)(u + 0xc));
    return *(uint32_t *)(uintptr_t)(u + 8);
}
// 城里的门落在哪：按规则算，不按某张图调一个坐标（原先固定落在关卡原点 +(60,80)，哈洛加斯那里正好在夸尔凯克来回走的范围里——
// Mac 实测他 48 秒内到过 (5062,5081)、(5073,5072)、(5056,5077)，门在 (5058,5082)；而开门那一刻他还没刷出来，只看当时在场的单位避不开）。
// 所以按地图自己的预设记录避让：关卡房间 +0x98 起的预设单位链（下一条 +0x10；+0x20 单位类型 1 怪或 NPC / 2 物件，+4 类号，+8 / +0x24 房内格坐标——原版放预设单位的循环 main+0x7ce9a8 起就是这么读的），
// NPC 的原位就是它的预设点。锚点周围 40 格内（步长 2 格）全部同时满足的格子里取离锚点最近的：所在房间已载入；原版找空地 main+0x7160d0 在它 3 格内给得出能站的空格；与锚点直线走得通（door_line_open）、周围 5 格内没有墙（door_clear，不堵台阶口 / 巷子）；
// 离任何 NPC 预设点 ≥ 14 格（夸尔凯克实测来回走约 10 格；20 格在这片广场上就没有成片空地了）、离任何物件预设点 ≥ 6 格；离此刻在场的怪物类单位（NPC / 佣兵 / 召唤物）≥ 10 格、在场物件（含别的门）≥ 6 格。别的幕将来开门只换锚点
// Where the town door lands: computed by rules, not a coordinate tuned for one particular map (previously fixed at level origin +(60,80), which in Harrogath is right in the range where Qual-Kehk walks back and forth --
// measured on Mac: within 48 seconds he reached (5062,5081), (5073,5072), (5056,5077), with the door at (5058,5082); and at the moment the door opened he had not spawned yet, so looking only at units present at that time cannot avoid him).
// So avoidance uses the map's own preset records: the preset unit chain starting at level room +0x98 (next +0x10; +0x20 unit type 1 monster or NPC / 2 object, +4 class id, +8 / +0x24 in-room tile coordinates -- this is exactly how the original preset-unit placement loop from main+0x7ce9a8 reads them),
// and an NPC's home position is its preset point. Among the tiles within 40 tiles of the anchor (step 2 tiles) that satisfy everything at once, take the one closest to the anchor: its room is loaded; the original free-ground search main+0x7160d0 yields a standable free tile within 3 tiles of it; the straight line to the anchor is walkable (door_line_open) and there is no wall within 5 tiles around (door_clear, not blocking step openings / alleys);
// >= 14 tiles from any NPC preset point (Qual-Kehk was measured walking back and forth about 10 tiles; at 20 tiles this plaza has no contiguous open ground left), >= 6 tiles from any object preset point; >= 10 tiles from monster-type units currently present (NPCs / mercenaries / summons), >= 6 tiles from objects present (including other doors). For doors in other acts later, only the anchor changes
enum { DOOR_NPC_GAP = 10, DOOR_OBJ_GAP = 6 }; enum { DOOR_HOME_GAP = 14 }; enum { ND_GAP = 8 };   // ND_GAP：结算处两扇门互相至少隔 8 格（原先左右各错 5 格 = 相隔 10 格） | ND_GAP: the two doors at the completion are at least 8 cells apart from each other (previously offset 5 cells to the left and to the right = 10 cells apart)
static int any_level(uint64_t u) { uint64_t p = rd(u + 0x38), room = p ? unit_room(u) : 0, ex = room ? rd(room + 0x18) : 0, lv = ex ? rd(ex + 0x90) : 0; return lv ? *(int32_t *)(uintptr_t)(lv + 0x1f8) : 0; }   // 单位所在关卡号，物件（静态路径）也能取；没有路径 / 房间的给 0 | Level id of where the unit is; works for objects (static path) too; gives 0 for those without a path / room
static int door_gap(uint64_t G, int type, int lvno, int x, int y) { int best = 1 << 30;   // 本关卡里此刻在场的这类单位离 (x, y) 最近的那个有多远（取横纵里大的那个） | How far the nearest unit of this type currently present in this level is from (x, y) (the larger of the horizontal and vertical distances)
    for (int b = 0; b < 128; b++) for (uint64_t u = rd(G + 0x2230 + type * 0x400 + 8 * b); u; u = rd(u + 0x158)) { int ux, uy; if (any_level(u) != lvno || !unit_xy(u, &ux, &uy)) continue;
        int d = abs(ux - x) > abs(uy - y) ? abs(ux - x) : abs(uy - y); if (d < best) best = d; }
    return best; }
static int preset_gap(uint64_t G, uint64_t lvt, int mon, int x, int y, int *cls) { int best = 1 << 30, n = 0;   // 离 (x, y) 最近的预设点（mon = 1 怪 / NPC、0 物件）；附近没初始化的房间先用原版房间初始化（记录这时才有，不刷怪） | The preset point nearest to (x, y) (mon = 1 monster / NPC, 0 object); nearby uninitialized rooms are first initialized with the original room init (the records only exist then; no monsters are spawned)
    for (uint64_t r = dm_level_rooms(lvt); r && n < 2000; r = rd(r + 0x48), n++) { int rx = *(int32_t *)(uintptr_t)(r + 0x60) * 5, ry = *(int32_t *)(uintptr_t)(r + 0x64) * 5, w = *(int32_t *)(uintptr_t)(r + 0x68) * 5, h = *(int32_t *)(uintptr_t)(r + 0x6c) * 5;
        if (x < rx - DOOR_HOME_GAP || x > rx + w + DOOR_HOME_GAP || y < ry - DOOR_HOME_GAP || y > ry + h + DOOR_HOME_GAP) continue;
        if (!rd(r + 0x58)) ((void (*)(int, uint64_t))(B + 0x52dcc0))(*(uint8_t *)(uintptr_t)(G + 0x106), r);
        for (uint64_t u = rd(r + 0x98), k = 0; u && k < 256; u = rd(u + 0x10), k++) { if (*(int32_t *)(uintptr_t)(u + 0x20) != (mon ? 1 : 2)) continue;
            int ux = rx + *(int32_t *)(uintptr_t)(u + 8), uy = ry + *(int32_t *)(uintptr_t)(u + 0x24), d = abs(ux - x) > abs(uy - y) ? abs(ux - x) : abs(uy - y); if (d < best) { best = d; if (cls) *cls = *(int32_t *)(uintptr_t)(u + 4); } } }
    return best; }
// 与锚点在同一片空地上：从锚点到这一格的直线上每一格都没有墙（原版碰撞查询 main+0x4ce020(房间, x, y, 掩码) 取位 0 = 挡人的墙；它自己会到邻近房间里找这一格）。
// 只看「有空格、离 NPC 远」会选到台阶上、塔后面（Mac 实测：选到 (5043,5061)，在北边塔楼后的高台上，门被建筑挡住、点不到）；锚点是人工看过的空地，直线走得通 = 没隔着墙和高差
// On the same open ground as the anchor: no tile on the straight line from the anchor to this tile is a wall (original collision query main+0x4ce020(room, x, y, mask), bit 0 = a wall that blocks the player; it looks up the tile in neighboring rooms by itself).
// Looking only at "has a free tile, far from NPCs" would pick spots on steps or behind towers (measured on Mac: (5043,5061) was picked, on the raised platform behind the northern tower, where the door was hidden by the building and could not be tapped); the anchor is open ground checked by eye, and a walkable straight line = no wall or height difference in between
static int door_line_open(uint64_t lvt, int ax, int ay, int x, int y) { int n = abs(x - ax) > abs(y - ay) ? abs(x - ax) : abs(y - ay); uint64_t room = room_at(lvt, ax, ay); if (!room) return 0;
    for (int i = 1; i <= n; i++) { int cx = ax + (x - ax) * i / n, cy = ay + (y - ay) * i / n; uint64_t r2 = room_at(lvt, cx, cy); if (!r2) return 0;
        if (((uint32_t (*)(uint64_t, int, int, int))(B + 0x4ce020))(r2, cx, cy, 1) & 0xffff) return 0; }
    return 1; }
// 不挡路：这一格周围 5 格内（11 × 11）没有一格是墙——开阔地上的门绕得过去；台阶口、巷子、门洞旁边都有墙，落不上（Mac 实测：只查直线时选到 (5047,5061)，正堵在北边石阶顶上）
// Not blocking the way: no tile within 5 tiles around this one (11 × 11) is a wall -- a door on open ground can be walked around; step openings, alleys and doorways all have walls nearby and cannot be picked (measured on Mac: when only the straight line was checked, (5047,5061) was picked, blocking the very top of the northern stone steps)
static int door_clear(uint64_t lvt, int x, int y) { for (int dy = -5; dy <= 5; dy++) for (int dx = -5; dx <= 5; dx++) { uint64_t r = room_at(lvt, x + dx, y + dy); if (!r || (((uint32_t (*)(uint64_t, int, int, int))(B + 0x4ce020))(r, x + dx, y + dy, 1) & 0xffff)) return 0; } return 1; }
static uint64_t pick_door_spot(uint64_t G, uint64_t lvt, int lvno, int ax, int ay, int32_t out[2]) {   // 锚点周围 40 格内全部合规的格子里取离锚点最近的 | Among all compliant tiles within 40 tiles of the anchor, take the one closest to the anchor
    long best = -1; uint64_t broom = 0; int bx = 0, by = 0, c[6] = {0};   // c：各条规则依次筛下来还剩多少格（找不到时打出来看卡在哪条） | c: how many tiles remain after each rule filters in turn (printed when nothing is found, to see which rule it is stuck on)
    for (int dy = -40; dy <= 40; dy += 2) for (int dx = -40; dx <= 40; dx += 2) { long d2 = (long)dx * dx + (long)dy * dy; c[0]++;
        int32_t pos[2] = { ax + dx, ay + dy }; uint64_t room = room_at(lvt, pos[0], pos[1]), rr = room ? free_spot(G, room, pos) : 0; if (!rr) continue; c[1]++;
        if (preset_gap(G, lvt, 1, pos[0], pos[1], NULL) < DOOR_HOME_GAP || preset_gap(G, lvt, 0, pos[0], pos[1], NULL) < DOOR_OBJ_GAP) continue; c[2]++;
        if (door_gap(G, 1, lvno, pos[0], pos[1]) < DOOR_NPC_GAP || door_gap(G, 2, lvno, pos[0], pos[1]) < DOOR_OBJ_GAP) continue; c[3]++;
        if (!door_line_open(lvt, ax, ay, pos[0], pos[1])) continue; c[4]++; if (!door_clear(lvt, pos[0], pos[1])) continue; c[5]++;
        if (best >= 0 && d2 >= best) continue; best = d2; broom = room; bx = pos[0]; by = pos[1]; }
    { static int n; if (n++ < 2) LOG("秘境：门位筛选 候选 %d → 有空格 %d → 离预设点够远 %d → 离在场单位够远 %d → 与锚点直线通 %d → 周围无墙 %d", c[0], c[1], c[2], c[3], c[4], c[5]); }
    if (!broom) return 0;
    { int hc = 0, hn = preset_gap(G, lvt, 1, bx, by, &hc), ho = preset_gap(G, lvt, 0, bx, by, NULL), gn = door_gap(G, 1, lvno, bx, by), go = door_gap(G, 2, lvno, bx, by);
      LOG("秘境：门的落点按规则取 %d,%d（离锚点 %d,%d 横 %d 纵 %d 格；离最近的 NPC 预设点 %d 格（类号 %d）、物件预设点 %d 格；在场 NPC %d 格、物件 %d 格；-1 = 没有）", bx, by, ax, ay, bx - ax, by - ay, hn > 9999 ? -1 : hn, hc, ho > 9999 ? -1 : ho, gn > 9999 ? -1 : gn, go > 9999 ? -1 : go); }
    out[0] = bx; out[1] = by; return broom; }
static uint32_t make_rift_door(uint64_t G, uint64_t player) {
    uint64_t lvt = level_of(player); int x0, y0; level_origin(lvt, &x0, &y0);
    int ax = x0 + 64, ay = y0 + 80; int32_t pos[2];   // 锚点：哈洛加斯营地西边水井左侧的空地（旧点 +(106,26) 压在台阶上；首领连战门在 +(76,80)）。看图定：+(64,80)，规则落在它正下方 15 格（门与两个火盆、矮墙的轮廓不相叠） | Anchor: the open ground left of the well on the west side of the Harrogath camp (the old point +(106,26) sat on the steps; the boss rush door is at +(76,80)). Decided by looking at the map: +(64,80); the rules land it 15 tiles directly below (the door does not overlap the outlines of the two braziers and the low wall)
    if (!room_at(lvt, ax, ay)) { static int n; int px, py; unit_xy(player, &px, &py); if (n++ < 3) LOG("秘境：门的锚点 %d,%d（关卡原点 %d,%d）所在房间还没载入，人在 %d,%d", ax, ay, x0, y0, px, py); return 0; }
    uint64_t room = pick_door_spot(G, lvt, unit_level(player), ax, ay, pos); if (!room) { static int n; if (n++ < 3) LOG("【红】秘境：锚点 %d,%d 周围 40 格内找不到合规的门位", ax, ay); return 0; }
    return place_door(G, room, pos[0], pos[1], "哈洛加斯", RIFT_OBJ, 0);
}
// ---- 出口换目的地（产物 r36 起）：玩家在秘境图里走任何出口，都在原版换关的入口把目的地换成秘境的下一张图，只发生一次换关。
// 原先是「发现人到了别的关卡再送一次」：点洞口时原版 112 → 113 的换关还没落地又发一次换关，客户端取玩家单位断言（main+0x45f9d0 ← 0x2e73c4），Mac 上游戏中止、真机帧率归零。
// 原版有两个换关入口：按出口砖的连接 main+0x7cc1e0(游戏, 玩家, 砖)（点洞口 / 楼梯、入口物件、任务）与按关卡号 main+0x634000(游戏, 玩家, 关卡, 方式)（小站 / 传送门 / 我们自己的 warp()）。
// ---- Exit destination swap (from build output r36): when the player takes any exit on a rift map, the destination is swapped to the rift's next map right at the original game's level-change entry, so only one level change happens.
// Previously it was "notice the player has reached another level, then send them again": when tapping a cave mouth, the original 112 -> 113 level change had not landed yet when another level change was issued, and the client asserted while fetching the player unit (main+0x45f9d0 <- 0x2e73c4); the game aborted on Mac and the frame rate dropped to zero on a real device.
// The original game has two level-change entries: by exit tile connection main+0x7cc1e0(game, player, tile) (tapping cave mouths / stairs, entrance objects, quests) and by level id main+0x634000(game, player, level, method) (waypoints / portals / our own warp()).
static int next_map_index(void) { return K.cur + 1 < K.nmaps ? K.cur + 1 : K.cur; }
static int rift_exit_active(uint64_t G, uint64_t player) { int st = atomic_load(&K.st); return (st == ST_RUN || st == ST_DONE) && G && G == L.G && player && player == L.player && in_rift_map(unit_level(player)); }
// 分支门 0x89（0x7cc1e0 入口）：它的目的地是砖的对端房间、换不成别的幕，所以整个改道去按关卡号换关——x2 / x3 换成（下一张图, 11）、x8 = 1，门块跳 0x634000；不是秘境局时 x8 = 0，原样执行
// Branch gate 0x89 (0x7cc1e0 entry): its destination is the room at the other end of the tile and cannot be changed to another act, so the whole thing is rerouted to a level change by level id -- x2 / x3 are replaced with (next map, 11), x8 = 1, and the gate block jumps to 0x634000; when not a rift run, x8 = 0 and it executes unchanged
void svc_rift_exit(uint64_t *regs) {
    uint64_t G = regs[0], pl = regs[1], tile = regs[2]; regs[8] = 0; if (!rift_exit_active(G, pl)) return;
    LOG("秘境：转场 run %llu 状态 %d 层 %d 来源 tile", (unsigned long long)K.t0_ns, atomic_load(&K.st), K.tier);
    int nx = next_map_index(); LOG("秘境：在关卡 %d 走出口砖（类号 %u）→ 入口处改道进第 %d 张图 %d", unit_level(pl), tile ? *(uint32_t *)(uintptr_t)(tile + 4) : 0, nx + 1, K.maps[nx]);
    K.cur = nx; L.lv_prev = K.maps[nx]; regs[2] = (uint64_t)(uint32_t)K.maps[nx]; regs[3] = 11; regs[8] = 1;
}
// 观测门 16（0x634000 入口）：目的地是秘境图或任意一幕的城时原样放行（含上面改道过来的那次、我们自己的 warp()、回城传送门），别的关卡号换成下一张图。
// 死亡后点「继续」：原版复活 main+0x6dbdbc 按人所在关卡查本幕的城、方式 0 调这里，随后 0x6c97c0 让人在城里活过来——调用时玩家模式是 0 / 17（死亡中 / 已死）。
// 原先只放行哈洛加斯：死在别的幕的图里（目的地 40 等）被当成「走出口」送进下一张图，人还是死的就进了战斗图（LoadScreenPanel 后 sig=11）。
// 设计：秘境进行中用回城卷轴 / 免费回城到了别的幕的城，照原版走——从那扇回城门回图；人自己步行出城去野外，计时照走、
// 超时算超时，是玩家自己的选择，不为它加门、也不把回城统一改到哈洛加斯（那会改掉原版回城的手感）。只有「死亡后继续统一回哈洛加斯」是我们定的。
// 按玩家状态认这条路，不按关卡号：死着换关 = 复活回城，目的地统一成哈洛加斯（秘境的城，城门在那里），秘境保持进行中，从城门点「继续」回去
// Observation gate 16 (entry 0x634000): when the destination is a Rift map or the town of any act it passes through unchanged (including the redirected
// call above, our own warp(), and town portals); any other level id is replaced with the next map.
// "Continue" after death: the original resurrection main+0x6dbdbc looks up the town of the act from the player's level and calls this with mode 0, then
// 0x6c97c0 brings the player back to life in town; at the time of the call the player mode is 0 / 17 (dying / dead).
// Previously only Harrogath was let through: dying in a map of another act (destination 40 etc.) was treated as "taking an exit" and sent into the next
// map, so a still-dead player entered a combat map (sig=11 after LoadScreenPanel).
// Design: during a Rift run, using a town portal scroll / free town portal that ends up in another act's town follows the original: return to the map
// through that town portal; if the player walks out of town into the wilderness on foot, the timer keeps running and
// a timeout counts as a timeout. That is the player's own choice; no gate is added for it and town portals are not unified to Harrogath (that would
// change the feel of the original town portal). Only "continue after death always returns to Harrogath" is our decision.
// This path is identified by player state, not by level id: changing level while dead = resurrect in town, the destination is unified to Harrogath
// (the Rift's town, where the Rift gate is), the Rift stays in progress, and "Continue" at the gate goes back
static void rift_level_obs(const uint64_t *regs) {
    uint64_t G = regs[0], pl = regs[1]; int lv = (int)(uint32_t)regs[2]; if (!rift_exit_active(G, pl)) return;
    uint32_t pm = *(uint32_t *)(uintptr_t)(pl + 0xc);
    // 城镇 / 本次秘境图的放行以前没有记录：读取画面中崩溃时，最后一次目标和方式会丢失。只在实际换关入口记，不逐帧轮询。
    // The clearance of the town / this rift's maps previously left no record: on a crash during the loading screen, the last target and method were lost. Recorded only at the actual level change entry, not polled every frame.
    LOG("秘境：转场 run %llu 状态 %d 层 %d 来源 level 目标 %d 方式 %u 玩家模式 %u", (unsigned long long)K.t0_ns, atomic_load(&K.st), K.tier, lv, (uint32_t)regs[3], pm);
    if (pm == 0 || pm == 17) { LOG("秘境：在关卡 %d 死亡后复活回城（原版目的地 %d，方式 %d）→ 统一回哈洛加斯，秘境保持进行中", unit_level(pl), lv, (int)(uint32_t)regs[3]);
        ((uint64_t *)(uintptr_t)regs)[2] = LV_TOWN; return; }
    if (dm_hud_area_is_town(lv) || in_rift_map(lv)) return;
    int nx = next_map_index(); LOG("秘境：从关卡 %d 去 %d（按关卡号换关）→ 入口处换成第 %d 张图 %d", unit_level(pl), lv, nx + 1, K.maps[nx]);
    K.cur = nx; L.lv_prev = K.maps[nx]; ((uint64_t *)(uintptr_t)regs)[2] = (uint64_t)(uint32_t)K.maps[nx];
}
// 分支门 0x8c（main+0x6fa9d0，产物 r38 起）：原版写存档头的函数 0x6fa3e0 在上一条 0x6fa9cc 取「玩家当前在第几幕」进 w9（ldrb w9, [x24, #0x2e8]，0 = 第一幕），
// 随后写成难度字节 0x80 | 幕号（.d2s 0x98–0x9a），下次进局落哪个城由它定。秘境每层的图跨幕：在第二幕的图里保存退出 / 一键重开 / 进程被杀，
// 重进会落在鲁高因，而秘境的门只在哈洛加斯（和「死亡后继续统一回哈洛加斯」同一个道理）。
// 人在秘境图里存档时把 w9 换成 4（第五幕）；只动写进存档的这一个值，运行中的幕号（[x24+0x2e8] 是活状态）不碰，也不在进局后再挪人。
// x22 = 玩家单位、x21 = 游戏（同一函数里 [x22] == 0 判玩家、[x21+0x104] 取难度）。x8 恒置 0：不走门块的跳转那一路，原指令照常执行
// 存档函数只有一条调用链（0x6fa3e0 ← 0x6f2278 ← 0x6f24e4 ← 0x6f23ac / 0x6f2400 / 0x6f241c），所有存档路径都经过这里
// Branch gate 0x8c (main+0x6fa9d0, since build output r38): the original save-header writer 0x6fa3e0 loads "which act the player is currently in" into w9 at
// the preceding instruction 0x6fa9cc (ldrb w9, [x24, #0x2e8], 0 = Act 1),
// then writes it as the difficulty byte 0x80 | act number (.d2s 0x98–0x9a), which decides which town the next session starts in. The maps of Rift levels
// span acts: after save-and-exit / one-tap restart / the process being killed in an Act 2 map,
// re-entering would land in Lut Gholein, while the Rift gate exists only in Harrogath (same reasoning as "continue after death always returns to
// Harrogath").
// When saving while the player is in a Rift map, w9 is replaced with 4 (Act 5); only this one value written to the save is changed, the running act
// number ([x24+0x2e8] is live state) is untouched, and the player is not moved again after joining.
// x22 = player unit, x21 = game (in the same function [x22] == 0 tests for a player and [x21+0x104] reads the difficulty). x8 is always set to 0: the
// gate block's jump path is not taken and the original instruction executes as usual
// The save function has a single call chain (0x6fa3e0 ← 0x6f2278 ← 0x6f24e4 ← 0x6f23ac / 0x6f2400 / 0x6f241c), so every save path goes through here
void svc_save_act(uint64_t *regs) {
    uint64_t pl = regs[22], G = regs[21]; uint32_t act = (uint32_t)regs[9] & 0xff; int st = atomic_load(&K.st); regs[8] = 0;
    if ((st == ST_RUN || st == ST_DONE) && G && G == L.G && pl && pl == L.player) { int lv = unit_level(pl);
        if (in_rift_map(lv) && act != 4) { static int n; if (n++ < 6) LOG("秘境：人在秘境图 %d 里存档，存档里的当前幕 %u → 4（下次进局落哈洛加斯）", lv, act); regs[9] = 4; } }
}
// 分支门 0x8d（main+0x64a34c，产物 r39 起）：佣兵加装备位（秘境超过 10 层给佣兵按角色的布局加腰带 / 鞋 / 戒指 ×2 / 项链 / 手套）。
// 原版通用放置校验 0x6493a8 对怪物类主人只放行护甲（类型 3）和头盔（0x25），其余按 monstats 的左右手武器类型判；门挂在「是不是头盔」返回之后，
// w0 = 那次判断的结果、x20 = 物品、[x22] = 主人单位。主人是佣兵（类号在佣兵表里：数据表 +0x558 每行 0x150、行 +8 = 类号）且物品是戒指 / 项链 / 鞋 / 手套 / 腰带类时把 w0 改成 1，
// 走头盔同一条放行路径（部位格子有没有定义、部位空不空、等级属性需求等通用检查照常；格子定义在 inventory 表佣兵行，wide_stash.py 补）。
// 解锁只拦「玩家新穿」（这一拍收到过 0x51 / 0x5b 包）：秘境最高通关不到 10 层时不放行。读档落位（进局时把存档里佣兵身上的物品逐件放回去，走的是同一个校验）一律放行——
// 那里拒掉 = 这件物品被销毁、下次存档就永久没了，不能因为「这一局没读到秘境记录」丢玩家的装备。x8 恒置 0
// Branch gate 0x8d (main+0x64a34c, from build output r39): extra mercenary equipment slots (past rift level 10 the mercenary gets a belt / boots / ring ×2 / amulet / gloves, following the character's layout).
// The original game's generic placement check 0x6493a8 lets through only armor (type 3) and helms (0x25) for monster-type owners, and judges the rest by the left / right hand weapon types in monstats; the gate is hooked after the "is it a helm" call returns,
// w0 = the result of that check, x20 = item, [x22] = owner unit. When the owner is a mercenary (class id is in the mercenary table: data table +0x558, 0x150 per row, row +8 = class id) and the item is of the ring / amulet / boots / gloves / belt type, w0 is changed to 1,
// taking the same pass path as the helm (generic checks such as whether the body location cell is defined, whether the location is empty, and level / stat requirements run as usual; the cell definitions are in the mercenary row of the inventory table, added by wide_stash.py).
// The unlock only blocks "the player newly equipping" (a 0x51 / 0x5b packet was received this tick): not let through when the highest rift level cleared is below 10. Placement on load (on entering a run the items on the mercenary in the save are put back one by one, through the same check) is always let through --
// rejecting there = the item is destroyed and permanently gone at the next save, and the player's equipment must not be lost because "this run did not read the rift record". x8 is always set to 0
int dm_rift_cleared_any(void) { return R.best >= 1; }   // 最高通关层只在限时内过关时更新；记录进局时按角色读入（rec_load），所以跟着角色走 | highest cleared level only updates on a clear within the time limit; the record is loaded per character on game entry (rec_load), so it follows the character
int dm_rift_merc_unlocked(void) { static int test = -1; if (test < 0) test = getenv("DM_TEST_MERC_SLOTS") != NULL; return R.best >= 10 || test; }   // 界面（dm_rift_ui.c）每帧问：六个新格要不要盖暗红 | Asked by the UI (dm_rift_ui.c) every frame: whether the six new cells should be covered in dark red
void dm_rift_merc_locked_hint(void) {   // 没解锁时点那六格 / 往里放东西：弹一句系统提示。半秒内不重复——只为把同一下手势里的几次调用（按下命中、松手落点、服务端那道门）并成一次； | Tapping those six cells / putting something into them while not unlocked: pops up a one-line system notice. Not repeated within half a second -- only to merge the several calls within one gesture (press hit, release drop point, the server-side gate) into one;
    // 提示框在屏上不到 1.8 秒就收（实测 0.5 秒时在、1.8 秒时已收），原先 2 秒不重复会让隔 1.6 秒的下一次点击没有任何反馈
    // The hint box is dismissed after less than 1.8 seconds on screen (measured: present at 0.5 seconds, already gone at 1.8 seconds); the previous 2-second no-repeat would leave the next tap 1.6 seconds later with no feedback at all
    static uint64_t last; struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); uint64_t now = (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000; if (!B || now - last < 500) return; last = now;
    dm_game_notice(B, dm_str(B, "dmMercSlotLocked")); LOG("佣兵装备位：提示「%s」", dm_str(B, "dmMercSlotLocked")); }
static int merc_class(uint64_t u) { uint8_t v = *(uint8_t *)(uintptr_t)(u + 0x1bd); if (!B || v < 1 || v > 3) return 0; uint64_t T = rd((uint64_t)(uintptr_t)(B + 0x2d64a18 + v * 16)), a = rd(T + 0x558), n = rd(T + 0x560);
    int cls = *(int32_t *)(uintptr_t)(u + 4); for (uint64_t k = 0; a && k < n && k < 4096; k++) if (*(int32_t *)(uintptr_t)(a + k * 0x150 + 8) == cls) return 1; return 0; }
// 触摸层问（游戏主线程，dm_cursor.c）：客户端这边玩家的佣兵身上，这个部位是不是空的。1 = 找到佣兵且该部位没有物品；找不到佣兵返回 0（照旧处理）。
// 用来挡「点佣兵面板的空格」：原版手柄光标落在空格上会被吸到旁边有物品的格，弹出那件物品的说明框盖住面板（点腰带锁格弹了盔甲的说明）
// Asked by the touch layer (game main thread, dm_cursor.c): on the client side, whether this body location on the player's mercenary is empty. 1 = mercenary found and no item at that location; returns 0 if the mercenary is not found (handled as before).
// Used to block "tapping an empty cell in the mercenary panel": the original game's controller cursor, landing on an empty cell, is snapped to a neighboring cell that has an item and pops up that item's tooltip, covering the panel (tapping the locked belt cell popped up the armor's tooltip)
int dm_rift_merc_slot_empty(int loc) {
    if (!B) return 0;
    for (int k = 0; k < 128; k++) for (uint64_t u = rd((uint64_t)(uintptr_t)(B + 0x2d603a0 + 1 * 0x400 + k * 8)); u; u = rd(u + 0x158)) { if (!merc_class(u)) continue;
        uint64_t a0 = rd(u + 0xa0), iv = a0 && *(int32_t *)(uintptr_t)(a0 + 8) && rd(u + 0x98) ? rd(u + 0x98) : rd(u + 0x90); int n = 0;
        for (uint64_t it = iv ? rd(iv + 0x10) : 0; it && n < 40; n++) { uint64_t d = rd(it + 0x10); if (!d) break; if (*(uint8_t *)(uintptr_t)(d + 0x54) == loc) return 0; it = rd(d + 0xb0); }
        return 1; }
    return 0;
}
void svc_merc_slot(uint64_t *regs) {
    static const int ty[] = { 10, 12, 15, 16, 19 }; static int test = -1, nlog; regs[8] = 0; if ((uint32_t)regs[0]) return;   /* 已经是头盔 | Already a helm */
    if (!B) { for (int i = 0; i < g_nmod; i++) if (!strcmp(g_mod[i].name, "main")) B = (uint8_t *)g_mod[i].base; if (!B) return; }
    uint64_t it = regs[20], own = rd(regs[22]); if (!it || !own || *(uint32_t *)(uintptr_t)own != 1 || !merc_class(own)) return;
    int hit = 0; for (unsigned i = 0; i < sizeof ty / sizeof *ty && !hit; i++) hit = ((int (*)(uint64_t, int))(B + 0x574bc0))(it, ty[i]) != 0; if (!hit) return;
    if (test < 0) test = getenv("DM_TEST_MERC_SLOTS") != NULL;
    int req = atomic_load(&g_merc_req), unlocked = R.best >= 10 || test;
    if (req && !unlocked) { if (nlog++ < 12) LOG("佣兵装备位：玩家要给佣兵穿新部位的物品（单位 id %u），秘境最高通关 %d 层不到 10 层 → 不放行", *(uint32_t *)(uintptr_t)(it + 8), R.best); dm_rift_merc_locked_hint(); return; }
    regs[0] = 1; if (nlog++ < 12) LOG("佣兵装备位：%s，物品（单位 id %u，类号 %u）放行到佣兵（类号 %d）身上", req ? "玩家新穿" : "读档落位", *(uint32_t *)(uintptr_t)(it + 8), *(uint32_t *)(uintptr_t)(it + 4), *(int32_t *)(uintptr_t)(own + 4));
}
// 客户端那一半（门 0x8e，main+0x3beb58）：客户端「佣兵能不能穿这件」0x3bea44(佣兵, 物品) 正要拿 w1 = 0x25（头盔）问物品类型。已解锁时，物品是那五类之一就把 w1 换成它自己的类型号，
// 判断成立、函数返回 1——物品格悬停不再画「不可放置」的红、原版松手放下能成。x19 = 物品。没解锁不动（红 + 放不下正是要的）
// The client half (gate 0x8e, main+0x3beb58): the client's "can the mercenary wear this" 0x3bea44(mercenary, item) is about to ask the item type with w1 = 0x25 (helm). When unlocked, if the item is one of those five types w1 is replaced with its own type number,
// the check passes and the function returns 1 -- hovering over the item cell no longer draws the "cannot place" red, and the original game's drop on release succeeds. x19 = item. Left alone when not unlocked (red + cannot be placed is exactly what is wanted)
void svc_merc_slot_cl(uint64_t *regs) {
    static const int ty[] = { 10, 12, 15, 16, 19 }; regs[8] = 0; uint64_t it = regs[19]; if (!it || (uint32_t)regs[1] != 0x25) return;
    if (!B) { for (int i = 0; i < g_nmod; i++) if (!strcmp(g_mod[i].name, "main")) B = (uint8_t *)g_mod[i].base; if (!B) return; }
    if (!dm_rift_merc_unlocked()) return;
    for (unsigned i = 0; i < sizeof ty / sizeof *ty; i++) if (((int (*)(uint64_t, int))(B + 0x574bc0))(it, ty[i])) { regs[1] = (uint64_t)ty[i]; return; }
}
// 测试（验证包）：DM_TEST_MERC_EQUIP="拍:物品码:部位[;…]"——到拍时在玩家物品栏里找这种物品，替玩家发原版「从背包格给佣兵穿」包 0x5b；
// DM_TEST_MERC_DUMP="拍[,拍…]"——到拍时打出佣兵身上每件物品（物品码、部位、模式）和几项属性（生命上限 7、防御 31、力量 0、敏捷 2、等级 12）。每局都按本局拍数触发
// Test (verification build): DM_TEST_MERC_EQUIP="tick:item code:body location[;...]" -- at that tick, find an item of this kind in the player's inventory and send the original game's "equip the mercenary from an inventory cell" packet 0x5b on the player's behalf;
// DM_TEST_MERC_DUMP="tick[,tick...]" -- at that tick, print every item on the mercenary (item code, body location, mode) and a few stats (max life 7, defense 31, strength 0, dexterity 2, level 12). Triggered in every run by that run's own tick count
static void merc_dump(uint64_t G, uint64_t pl, uint32_t t, const char *why) {
    uint64_t m = ((uint64_t (*)(uint64_t, uint64_t, int, int))(B + 0x6c3130))(G, pl, 7, 0); if (!m) { LOG("佣兵装备位：拍 %u（%s）没有佣兵", t, why); return; }
    uint64_t a0 = rd(m + 0xa0), iv = a0 && *(int32_t *)(uintptr_t)(a0 + 8) && rd(m + 0x98) ? rd(m + 0x98) : rd(m + 0x90); char o[600]; int c = 0, n = 0; o[0] = 0;
    for (uint64_t it = iv ? rd(iv + 0x10) : 0; it && n < 40; n++) { uint64_t d = rd(it + 0x10); if (!d) break; uint32_t code = ((uint32_t (*)(uint64_t))(B + 0x570260))(it);
        c += snprintf(o + c, sizeof o - (size_t)c, " [%.3s 部位 %u 模式 %u id %u]", (const char *)&code, *(uint8_t *)(uintptr_t)(d + 0x54), *(uint32_t *)(uintptr_t)(it + 0xc), *(uint32_t *)(uintptr_t)(it + 8)); if (c > 520) break; it = rd(d + 0xb0); }
    { uint32_t mid = *(uint32_t *)(uintptr_t)(m + 8); uint64_t cu = rd((uint64_t)(uintptr_t)(B + 0x2d603a0 + 1 * 0x400 + (mid & 0x7f) * 8)); while (cu && *(uint32_t *)(uintptr_t)(cu + 8) != mid) cu = rd(cu + 0x158);   /* 客户端那只佣兵身上有什么（面板画的是它） | What the client-side mercenary has on it (it is the one the panel draws) */
      uint64_t ca0 = cu ? rd(cu + 0xa0) : 0, civ = cu ? (ca0 && *(int32_t *)(uintptr_t)(ca0 + 8) && rd(cu + 0x98) ? rd(cu + 0x98) : rd(cu + 0x90)) : 0; char co[400]; int cc = 0, cn = 0; co[0] = 0;
      for (uint64_t it = civ ? rd(civ + 0x10) : 0; it && cn < 40; cn++) { uint64_t d = rd(it + 0x10); if (!d) break; uint32_t code = ((uint32_t (*)(uint64_t))(B + 0x570260))(it); cc += snprintf(co + cc, sizeof co - (size_t)cc, " [%.3s 部位 %u]", (const char *)&code, *(uint8_t *)(uintptr_t)(d + 0x54)); if (cc > 340) break; it = rd(d + 0xb0); }
      LOG("佣兵装备位：拍 %u（%s）客户端佣兵%s物品 %d 件：%s", t, why, cu ? "" : "（没找到）", cn, co); }
    LOG("佣兵装备位：拍 %u（%s）佣兵 id %u 类号 %d 物品 %d 件：%s｜生命上限 %d 防御 %d 力量 %d 敏捷 %d 等级 %d 全抗(火) %d", t, why, *(uint32_t *)(uintptr_t)(m + 8), *(int32_t *)(uintptr_t)(m + 4), n, o, gs(m, 7) >> 8, gs(m, 31), gs(m, 0), gs(m, 2), gs(m, 12), gs(m, 39));
}
static void merc_test(uint64_t G, uint64_t pl, uint32_t t) {
    static const char *eq, *dp; static int init; if (!init) { init = 1; eq = getenv("DM_TEST_MERC_EQUIP"); dp = getenv("DM_TEST_MERC_DUMP"); }
    for (const char *p = dp; p && *p; ) { if ((uint32_t)atoi(p) == t) merc_dump(G, pl, t, "定时"); p = strchr(p, ','); if (p) p++; }
    for (const char *p = eq; p && *p; ) { unsigned at = 0, loc = 0; char code[8] = ""; if (sscanf(p, "%u:%3[^:]:%u", &at, code, &loc) == 3 && at == t) {
            uint64_t m = ((uint64_t (*)(uint64_t, uint64_t, int, int))(B + 0x6c3130))(G, pl, 7, 0), a0 = rd(pl + 0xa0), iv = a0 && *(int32_t *)(uintptr_t)(a0 + 8) && rd(pl + 0x98) ? rd(pl + 0x98) : rd(pl + 0x90), found = 0;
            for (uint64_t it = iv ? rd(iv + 0x10) : 0, n = 0; it && n < 400 && !found; n++) { uint64_t d = rd(it + 0x10); if (!d) break; uint32_t cd = ((uint32_t (*)(uint64_t))(B + 0x570260))(it);
                if (!memcmp(&cd, code, 3) && *(uint32_t *)(uintptr_t)(it + 0xc) == 0) found = it; it = rd(d + 0xb0); }
            if (!m || !found) LOG("佣兵装备位：测试 拍 %u 给佣兵穿 %s → 部位 %u：%s", t, code, loc, !m ? "没有佣兵" : "玩家物品栏里没找到这种物品");
            else { merc_dump(G, pl, t, "穿之前"); uint8_t pk[25] = { 0x5b }; uint32_t none = 0xffffffff; uint64_t ip = rd(found + 0x38); uint32_t cell = ip ? ((*(uint32_t *)(uintptr_t)(ip + 0x10) & 0xffff) | (*(uint32_t *)(uintptr_t)(ip + 0x14) << 16)) : 0;
                /* 原版「直接从背包格给佣兵穿」0x5b（客户端 0x3bf2cc 组包）：[物品号][佣兵号][被换下物品号，没有 = −1][部位][物品所在格 x | y << 16][同一个格]。0x51 那条要物品先在手上 | The original game's "equip the mercenary directly from an inventory cell" 0x5b (packet built by the client at 0x3bf2cc): [item id][mercenary id][id of the item swapped out, none = −1][body location][cell the item is in, x | y << 16][the same cell]. The 0x51 one needs the item to be in hand first */
                memcpy(pk + 1, (void *)(uintptr_t)(found + 8), 4); memcpy(pk + 5, (void *)(uintptr_t)(m + 8), 4); memcpy(pk + 9, &none, 4); memcpy(pk + 13, &loc, 4); memcpy(pk + 17, &cell, 4); memcpy(pk + 21, &cell, 4);
                atomic_store(&g_merc_req, 1); *(uint32_t *)(uintptr_t)(G + 0x19050) = 1; ((void (*)(uint64_t, uint64_t, uint8_t *, uint32_t))(B + 0x6e5b40))(G, pl, pk, 25); *(uint32_t *)(uintptr_t)(G + 0x19050) = 0;
                LOG("佣兵装备位：测试 拍 %u 替玩家发 0x5b：物品 %s（id %u）→ 佣兵部位 %u；发完物品模式 %u、部位字段 %u、佣兵回执字节 %#x", t, code, *(uint32_t *)(uintptr_t)(found + 8), loc, *(uint32_t *)(uintptr_t)(found + 0xc), *(uint8_t *)(uintptr_t)(rd(found + 0x10) + 0x54), *(uint8_t *)(uintptr_t)(m + 0xb2));
                merc_dump(G, pl, t, "穿之后"); } }
        p = strchr(p, ';'); if (p) p++; }
}
// 观测门 17（main+0x711040 入口，产物 r36 起）：原版「怪物被杀 → 任务事件」（事件号 8）的唯一入口 (游戏, 怪, 攻击者)，杀怪函数 0x7d8550 与另一条死亡路径 0x7ab8dc 都调它。
// 秘境里的首领只带战斗、不带剧情任务：秘境图里的怪（守护者是原版剧情首领，串图里也会遇到带任务的超级独特怪）死了不走任务——不写任务位、不弹「任务已更新」
// 和通关那段「世界之石崩坏的余波…」、不触发挂在任务事件上的后续。做法：把参数里的怪换成一个 +0xc0 为空的替身，函数开头自己的判断（0x711064）就原路返回。
// 结算清怪用空攻击者杀怪时原版会自己挑一个玩家来触发这个事件（0x71106c），同一扇门一并挡住
// Observation gate 17 (main+0x711040 entry, from build output r36): the sole entry of the original "monster killed -> quest event" (event number 8) (game, monster, attacker), called by both the monster kill function 0x7d8550 and the other death path 0x7ab8dc.
// Bosses in the rift bring only the fight, not the story quest: monsters on rift maps (the guardian is an original story boss, and quest-bearing super uniques are also met on chained maps) do not go through quests when they die -- no quest bits written, no 「任务已更新」(Quest updated) popup
// and no 「世界之石崩坏的余波…」(The aftermath of the Worldstone's collapse...) passage on completion, and none of the follow-ups hooked on the quest event are triggered. Method: replace the monster in the arguments with a stand-in whose +0xc0 is null, and the function's own check at its start (0x711064) returns the normal way.
// When the completion sweep kills monsters with a null attacker, the original game picks a player by itself to trigger this event (0x71106c); the same gate blocks that too
static void rift_quest_obs(const uint64_t *regs) {
    static uint8_t stand_in[0x200]; uint64_t G = regs[0], mon = regs[1]; int st = atomic_load(&K.st);
    if ((st != ST_RUN && st != ST_DONE) || !G || G != L.G || !mon || !in_rift_map(any_level(mon))) return;
    static int n; if (n++ < 6) LOG("秘境：怪物类号 %u（单位 id %u）死在秘境图里，不走原版任务事件", *(uint32_t *)(uintptr_t)(mon + 4), *(uint32_t *)(uintptr_t)(mon + 8));
    ((uint64_t *)(uintptr_t)regs)[1] = (uint64_t)(uintptr_t)stand_in;
}
static void rift_gates(void) { static int once; if (once) return; once = 1;
    if (gate_obs_present(OBS_QUEST_KILL)) gate_obs_set(OBS_QUEST_KILL, rift_quest_obs); else LOG("秘境：这个产物不带任务事件的门（r36 起的 touch 产物才带），秘境里的首领死亡会走原版任务");
    if (gate_obs_present(OBS_LEVEL_WARP)) gate_obs_set(OBS_LEVEL_WARP, rift_level_obs);
    else LOG("秘境：这个产物不带出口换目的地的门（只有 r36 起的 touch 产物带），走出口会先落到原版的下一关、再由每拍检测送进下一张图"); }
void dm_rift_c2s(uint64_t G, const uint8_t *pkt, uint32_t len) {
    if (len && (pkt[0] == 0x51 || pkt[0] == 0x5b)) atomic_store(&g_merc_req, 1);
    if (G != L.G || len < 9 || pkt[0] != 0x41 || (!L.door && !L.next_door && !L.town_door)) return;
    uint32_t id, ty; memcpy(&id, pkt + 1, 4); memcpy(&ty, pkt + 5, 4);
    if (ty == 2 && id && (id == L.door || id == L.next_door || id == L.town_door)) { L.pend = id; L.pend_at = tick_of(G); }
}

// ---- 开局：改区域密度 / 精英、改 LvlMaze 房间数（结束写回）
// LvlMaze 这张表服务端和客户端共用，两边各自在自己的时刻按当时的表值建图：服务端建一张迷宫时会顺带把相邻的那张也建好（建亡者大厅 3 时亡者大厅 2 已是 32 间），
// 而且离开那一幕后也不丢；客户端换幕就把整幕的图丢掉，回来时按当时的表值重建。所以一张图的房间数从服务端建它起到本局结束必须不变，否则两边布局不同，
// 服务端发来的房间客户端没有，客户端收「加载房间」包时取到空指针（main+0x52d6f8，上一层用亡者大厅 3、下一层抽到亡者大厅 2，原先是开局改小、结算写回）。
// 做法：服务端已建的图不改（也不抽）；改小的值留到换局，换局时新局里已经按小值建好的图继续留着。
// ---- Start of a run: change area density / elites, change the LvlMaze room counts (written back at the end)
// The LvlMaze table is shared by the server and the client, and each side builds a map at its own moment from the table value at that time: when the server builds one maze it also builds the adjacent one in passing (when Halls of the Dead Level 3 is built, Halls of the Dead Level 2 already has 32 rooms),
// and it keeps them after leaving that act; the client throws away the whole act's maps on an act change and rebuilds them from the table value at that time when it comes back. So a map's room count must not change from the moment the server builds it until the end of the game, otherwise the two sides have different layouts,
// the client does not have the rooms the server sends, and the client hits a null pointer when it receives a "load room" packet (main+0x52d6f8; the previous level used Halls of the Dead Level 3 and the next level drew Halls of the Dead Level 2; originally the count was reduced at the start and written back at completion).
// Approach: maps the server has already built are not changed (and not drawn); the reduced values are kept until the game changes, and when it changes, maps already built from the reduced values in the new game stay as they are.
static struct { int row, old, lv; } g_mz[32]; static int g_nmz; static uint8_t g_built[140];   // 改过的 LvlMaze 行 / 原值 / 关卡；本局服务端已建过房间的关卡（只增） | The LvlMaze rows that were changed / their original values / the levels; the levels whose rooms the server has already built in this game (only grows)
// 关卡 +0x1c8 = 所属的幕级结构，其 +0x868 = 本幕关卡链头、关卡 +0x1b8 下一个（新建的插在头上，0x530780）、+0x10 房间链（非空 = 已建）。每拍扫玩家所在的幕
// Level +0x1c8 = the act-level structure it belongs to; that structure's +0x868 = head of this act's level chain, level +0x1b8 = next (new ones are inserted at the head, 0x530780), +0x10 = room chain (non-empty = built). Every tick, scan the act the player is in
static void built_scan(uint64_t player) { uint64_t dr = rd(level_of(player) + 0x1c8); int n = 0;
    for (uint64_t lv = dr ? rd(dr + 0x868) : 0; lv && n < 200; lv = rd(lv + 0x1b8), n++) { int no = *(int32_t *)(uintptr_t)(lv + 0x1f8); if (no > 0 && no < 140 && rd(lv + 0x10)) g_built[no] = 1; } }
static void maze_restore(uint64_t G, uint64_t player) {   // 只在换局时调：G / player 是新局的 | Called only when the game changes: G / player belong to the new game
    uint64_t T = tables(G), a = T ? rd(T + 0x18f8) : 0; int k = 0; memset(g_built, 0, sizeof g_built); built_scan(player);
    for (int i = 0; i < g_nmz; i++) { if (!a) continue;
        if (g_built[g_mz[i].lv]) { LOG("秘境：关卡 %d 新局里已按改小的房间数建好，LvlMaze 第 %d 行不写回", g_mz[i].lv, g_mz[i].row); g_mz[k++] = g_mz[i]; continue; }
        *(int32_t *)(uintptr_t)(a + (uint64_t)g_mz[i].row * 0x1c + 0xc) = g_mz[i].old; LOG("秘境：换局，LvlMaze 第 %d 行地狱房间数写回 %d", g_mz[i].row, g_mz[i].old); }
    g_nmz = k;
}
// 本局怪物区域（游戏 +0x1d0 + 8 × 关卡号；+0x2c4 密度、+0x2c8 / +0x2c9 精英组数下 / 上限）的原值：开局按原值 × 3 / × 2；结算后置 0（已结算的图不再填怪）；秘境结束写回
// Original values of this run's monster regions (game +0x1d0 + 8 × level id; +0x2c4 density, +0x2c8 / +0x2c9 lower / upper limit of elite pack count): at rift start use original × 3 / × 2; set to 0 after completion (completed maps are not populated again); written back when the rift ends
static struct { uint64_t G; int have, den; uint8_t umin, umax; } g_reg[140];
static int32_t *region_of(uint64_t G, int lv, uint8_t **umin, uint8_t **umax) { uint64_t r = lv > 0 && lv < 140 ? rd(G + 0x1d0 + 8ull * lv) : 0; if (!r) return 0;
    if (g_reg[lv].G != G || !g_reg[lv].have) { g_reg[lv].G = G; g_reg[lv].have = 1; g_reg[lv].den = *(int32_t *)(uintptr_t)(r + 0x2c4); g_reg[lv].umin = *(uint8_t *)(uintptr_t)(r + 0x2c8); g_reg[lv].umax = *(uint8_t *)(uintptr_t)(r + 0x2c9); }
    *umin = (uint8_t *)(uintptr_t)(r + 0x2c8); *umax = (uint8_t *)(uintptr_t)(r + 0x2c9); return (int32_t *)(uintptr_t)(r + 0x2c4); }
static void region_set(uint64_t G, int mode) {   // mode 0 写回原值、1 置 0（不填怪） | mode 0 writes back the original values, 1 sets 0 (no monster population)
    for (int i = 0; i < K.nmaps; i++) { int lv = K.maps[i]; uint8_t *umin, *umax; int32_t *den = region_of(G, lv, &umin, &umax); if (!den) continue;
        int d = mode ? 0 : g_reg[lv].den, a = mode ? 0 : g_reg[lv].umin, b = mode ? 0 : g_reg[lv].umax; if (*den == d && *umin == a && *umax == b) continue;
        LOG("秘境：关卡 %d 本局怪物区域 密度 %d → %d、精英组 %d–%d → %d–%d（%s）", lv, *den, d, *umin, *umax, a, b, mode ? "已结算，不再填怪" : "写回原值"); *den = d; *umin = (uint8_t)a; *umax = (uint8_t)b; } }
// 已结算的图里，地图预设点上的怪也不再放：给预设记录置原版自己的「跳过」位（记录 +0x28 位 0，放预设单位的循环 main+0x7ce9bc 读它；+0x20 = 1 是怪）。记录在房间初始化时才有，所以每拍对一遍
// 单位阵营（0 敌对 / 2 玩家一方）：原版存房间时自己的读法（main+0x7e0f3c 起）——单位 +0x88 的属性表，+0x1c 位 31 没置 = 0；否则在 +0xd0 / +0xd8 两条链（下一条 +0x68）里找状态 0x69 的节点，
// 对它取属性 0xac：main+0x5bc810([单位 +0x1bd] 字节, 节点, 0xac, 0)；没有这个节点 = 0
// On completed maps, monsters on map preset points are no longer placed either: set the original game's own "skip" bit on the preset record (record +0x28 bit 0, read by the preset-unit placement loop main+0x7ce9bc; +0x20 = 1 means monster). Records only exist once the room is initialized, so reconcile every tick
// Unit alignment (0 hostile / 2 player side): the original game's own way of reading it when saving a room (from main+0x7e0f3c) -- the stat table at unit +0x88, +0x1c bit 31 not set = 0; otherwise look in the two chains at +0xd0 / +0xd8 (next +0x68) for the node with state 0x69,
// and get stat 0xac from it: main+0x5bc810([unit +0x1bd] byte, node, 0xac, 0); no such node = 0
static int unit_align(uint64_t u) { uint64_t sl = rd(u + 0x88); if (!sl || !(*(uint32_t *)(uintptr_t)(sl + 0x1c) & 0x80000000u)) return 0;
    for (int pass = 0; pass < 2; pass++) for (uint64_t n = rd(sl + (pass ? 0xd8 : 0xd0)), k = 0; n && k < 512; n = rd(n + 0x68), k++)
        if (*(uint32_t *)(uintptr_t)(n + 0x20) == 0x69) return ((int (*)(int, uint64_t, int, int))(B + 0x5bc810))(*(uint8_t *)(uintptr_t)(u + 0x1bd), n, 0xac, 0);
    return 0; }
// 卸载房间的存档（游戏 +0x1b0 → 每幕一条节点链，节点 +0x20 下一个、+0x10 怪物记录链；记录 0xb8 字节：+0 / +4 单位坐标、+0xc 单位号、+0x14 单位标志 2、下一条 +0xb0）：
// 房间重新激活时原版 main+0x7debe0 把记录还原成怪。已结算的图里，把落在本图范围内的怪物记录改成「单位还在（+0x14 位 8）+ 一个不存在的单位号」——
// 原版恢复时按单位号找不到就只释放这条记录、不生怪（0x7deefc）；物件链（我们的门）和物品链不碰。记录本来就带「单位还在」的（佣兵 / 召唤物）不动
// Saved state of unloaded rooms (game +0x1b0 -> one node chain per act, node +0x20 next, +0x10 monster record chain; a record is 0xb8 bytes: +0 / +4 unit coordinates, +0xc unit id, +0x14 unit flags 2, next record +0xb0):
// when a room is reactivated the original main+0x7debe0 restores the records into monsters. On completed maps, monster records that fall within this map's range are changed to "unit still exists (+0x14 bit 8) + a nonexistent unit id" --
// on restore the original game cannot find it by unit id, so it only frees the record and spawns no monster (0x7deefc); the object chain (our doors) and the item chain are not touched. Records that already carry "unit still exists" (mercenaries / summons) are left alone
static struct { uint64_t G; int have, x0, y0, x1, y1; } g_box[140];   // 每张秘境图的格坐标范围（人在这张图里时量） | Tile coordinate range of each rift map (measured while the player is on that map)
static void level_box(uint64_t G, int lv, uint64_t lvt) { if (lv <= 0 || lv >= 140 || (g_box[lv].have && g_box[lv].G == G)) return; int x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1, n = 0;
    for (uint64_t r = dm_level_rooms(lvt); r && n < 2000; r = rd(r + 0x48), n++) { int x = *(int32_t *)(uintptr_t)(r + 0x60) * 5, y = *(int32_t *)(uintptr_t)(r + 0x64) * 5, w = *(int32_t *)(uintptr_t)(r + 0x68) * 5, h = *(int32_t *)(uintptr_t)(r + 0x6c) * 5;
        if (x < x0) x0 = x; if (y < y0) y0 = y; if (x + w > x1) x1 = x + w; if (y + h > y1) y1 = y + h; }
    if (x1 < 0) return; g_box[lv].G = G; g_box[lv].have = 1; g_box[lv].x0 = x0; g_box[lv].y0 = y0; g_box[lv].x1 = x1; g_box[lv].y1 = y1; }
static void saved_purge(uint64_t G) { uint64_t T = tables(G), heads = rd(G + 0x1b0); int acts = T ? *(int32_t *)(uintptr_t)(T + 0x108) : 0, n = 0; if (!heads || acts <= 0 || acts > 8) return;
    for (int a = 0; a < acts; a++) for (uint64_t nd = rd(heads + 8ull * a), k = 0; nd && k < 4096; nd = rd(nd + 0x20), k++) for (uint64_t r = rd(nd + 0x10), j = 0; r && j < 1024; r = rd(r + 0xb0), j++) {
        if (*(uint8_t *)(uintptr_t)(r + 0x15) & 1) continue; int x = *(int32_t *)(uintptr_t)r, y = *(int32_t *)(uintptr_t)(r + 4), in = 0;
        for (int i = 0; i < K.nmaps && !in; i++) { int lv = K.maps[i]; in = lv > 0 && lv < 140 && g_box[lv].have && g_box[lv].G == G && x >= g_box[lv].x0 && x < g_box[lv].x1 && y >= g_box[lv].y0 && y < g_box[lv].y1; }
        if (!in) continue; *(uint32_t *)(uintptr_t)(r + 0x14) |= 0x100; *(uint32_t *)(uintptr_t)(r + 0xc) = 0xffffffffu; n++; }
    if (n) LOG("秘境：卸载房间的存档里 %d 条怪物记录改成不还原", n); }
static void presets_skip(uint64_t lvt) { int n = 0, set = 0; for (uint64_t r = dm_level_rooms(lvt); r && n < 2000; r = rd(r + 0x48), n++) for (uint64_t u = rd(r + 0x98), k = 0; u && k < 256; u = rd(u + 0x10), k++)
        if (*(int32_t *)(uintptr_t)(u + 0x20) == 1 && !(*(uint8_t *)(uintptr_t)(u + 0x28) & 1)) { *(uint8_t *)(uintptr_t)(u + 0x28) |= 1; set++; }
    if (set) LOG("秘境：已结算的图里 %d 个怪物预设点置了跳过位", set); }
static void rift_setup(uint64_t G) {
    uint64_t T = tables(G), a = T ? rd(T + 0x18f8) : 0, n = T ? rd(T + 0x1900) : 0;
    for (int i = 0; i < K.nmaps; i++) { int lv = K.maps[i]; uint64_t r = rd(G + 0x1d0 + 8ull * lv); if (!r) { LOG("【红】秘境：关卡 %d 没有怪物区域", lv); continue; }
        uint8_t *umin, *umax; int32_t *den = region_of(G, lv, &umin, &umax); int d0 = g_reg[lv].den, a0 = g_reg[lv].umin, b0 = g_reg[lv].umax;   // 从原值算（上一次秘境结算后置过 0） | Computed from the original values (they were set to 0 after the previous rift completion)
        int e2 = tier_elite_x2(K.tier); *den = d0 * 3 > 10000 ? 10000 : d0 * 3; *umin = a0 * e2 / 2 > 255 ? 255 : a0 * e2 / 2; *umax = b0 * e2 / 2 > 255 ? 255 : b0 * e2 / 2;   // 精英组倍数按层（tier_elite_x2） | elite pack multiplier by level (tier_elite_x2)
        LOG("秘境：关卡 %d 本局怪物区域 密度 %d → %d、精英组 %d–%d → %d–%d（第 %d 层 ×%d.%d）", lv, d0, *den, a0, b0, *umin, *umax, K.tier, e2 / 2, e2 % 2 * 5);
        for (uint64_t k = 0; a && k < n; k++) if (*(int32_t *)(uintptr_t)(a + k * 0x1c) == lv) { int32_t *rm = (int32_t *)(uintptr_t)(a + k * 0x1c + 0xc); int had = 0; for (int q = 0; q < g_nmz; q++) had |= g_mz[q].lv == lv;
            if (g_built[lv]) LOG("秘境：关卡 %d 服务端已建，LvlMaze 地狱房间数不动（%d）", lv, *rm);
            else if (*rm > 4 && !had && g_nmz < 32) { g_mz[g_nmz].row = (int)k; g_mz[g_nmz].old = *rm; g_mz[g_nmz++].lv = lv; *rm = (*rm * 6 + 9) / 10; LOG("秘境：关卡 %d LvlMaze 地狱房间数 %d → %d（留到换局）", lv, g_mz[g_nmz - 1].old, *rm); } break; } }
}

// ---- 成功：涨层、碎片
// urow = 暗金行号（uniqueitems 的 *ID，0 起；参数 +0x50 填行号 + 1），-1 = 不指定；fl = 参数 +0x54 标志（0x400000 无形、0x800 带孔）
// ---- Success: level increase, shard
// urow = unique row number (*ID of uniqueitems, 0-based; parameter +0x50 holds row number + 1), -1 = not specified; fl = flags at parameter +0x54 (0x400000 ethereal, 0x800 socketed)
static void give_item2(uint64_t G, uint64_t player, const char *code4, int q, int ilvl, int urow, uint32_t fl);
static void give_item(uint64_t G, uint64_t player, const char *code4, int q, int ilvl) { give_item2(G, player, code4, q, ilvl, -1, 0); }
static void give_item2(uint64_t G, uint64_t player, const char *code4, int q, int ilvl, int urow, uint32_t fl) {
    uint64_t T = tables(G), ix = T ? rd(T + 0x258) : 0, ar = ix ? rd(ix) : 0, cnt = ix ? rd(ix + 8) : 0; uint32_t code; memcpy(&code, code4, 4); int cls = -1;
    for (uint64_t k = 0; ar && k < cnt; k++) if (*(uint32_t *)(uintptr_t)(ar + k * 8) == code) { cls = *(int32_t *)(uintptr_t)(ar + k * 8 + 4); break; }
    if (cls < 0) { LOG("【红】秘境：物品码 %.4s 查不到", code4); return; }
    int32_t p0[2] = { 0, 0 }, p1[2] = { 0, 0 }; ((void (*)(uint64_t, int32_t *))(B + 0x5c84f0))(player, p0); uint64_t pth = rd(player + 0x38), room0 = pth ? rd(pth + 0x20) : 0;
    uint64_t room = ((uint64_t (*)(uint64_t, uint64_t, int32_t *, int32_t *, int))(B + 0x6529e0))(G, room0, p0, p1, 1); if (!room) return;
    uint8_t prm[0xd8]; memset(prm, 0, sizeof prm);
    *(uint64_t *)(prm + 0) = player; *(uint64_t *)(prm + 0x10) = G; *(int32_t *)(prm + 0x18) = ilvl; *(int32_t *)(prm + 0x20) = cls; *(int32_t *)(prm + 0x24) = 3;
    *(int32_t *)(prm + 0x28) = p1[0]; *(int32_t *)(prm + 0x2c) = p1[1]; *(uint64_t *)(prm + 0x30) = room; *(uint16_t *)(prm + 0x38) = 1; *(uint16_t *)(prm + 0x3a) = *(uint16_t *)(uintptr_t)(G + 0x128); *(int32_t *)(prm + 0x40) = q;
    if (urow >= 0) { int32_t ur = urow + 1; memcpy(prm + 0x50, &ur, 4);   // 生成参数 +0x50 = 暗金行号 + 1（不是控件可见位，native_write_scan 认的是「(x + 0x50) =」写法）。 原版「本局已出过这件暗金」位图 G+0x3638（u32 字，第 row>>5 个字的第 row&31 位；0x654850 查、0x654d28 置）：我们指定的这一件先清掉，否则同局第二件必降级 | Generation parameter +0x50 = unique row number + 1 (not the widget visible bit; native_write_scan matches the "(x + 0x50) =" form). The original game's "this unique has already dropped this run" bitmap G+0x3638 (u32 words, bit row&31 of word row>>5; checked at 0x654850, set at 0x654d28): clear it first for the one we specify, otherwise a second one in the same run is always downgraded
        uint32_t *w = (uint32_t *)(uintptr_t)(G + 0x3638 + 4 * ((uint32_t)urow >> 5)); *w &= ~(1u << (urow & 31)); }
    *(uint32_t *)(prm + 0x54) = fl;
    uint64_t it = ((uint64_t (*)(uint64_t, void *, int))(B + 0x656780))(G, prm, 0); uint64_t d = it ? rd(it + 0x14 * 0 + 0x10) : 0;
    LOG("秘境：掉落 %.4s 品质 %d → %s（物品数据 品质 %d、暗金行 %d、物品等级 %d、标志 %#x）", code4, q, it ? "成" : "【红】败", d ? *(int32_t *)(uintptr_t)d : -1, d ? *(int32_t *)(uintptr_t)(d + 0x34) : -1, d ? *(int32_t *)(uintptr_t)(d + 0x38) : -1, d ? *(uint32_t *)(uintptr_t)(d + 0x18) : 0);
}
// ---- 每 10 层首通里程碑（设计稿五节，基础包 + 累计叠加）：限时内通关第 m 层（m = 10, 20, …, 100）且没领过 → 发基础包 + 第 1..N 档全部物品，掉在人脚下。
//   暗金行号照 uniqueitems.txt 的 *ID（从原版资源包读：泰瑞尔 311、格利风 336、死亡深度 354、毁灭 381、彩虹刻面 392–399（首次进秘境送）、地狱火炬 400、先祖暗金珠宝 420–425、破免大板 426 / 428–432）
// ---- First-clear milestones every 10 levels (design doc section 5, base pack + cumulative stacking): clearing level m (m = 10, 20, ..., 100) within the time limit and not yet claimed -> give the base pack + all items of grades 1..N, dropped at the player's feet.
//   Unique row numbers follow *ID in uniqueitems.txt (read from the original resource pack: Tyrael's Might 311, Griffon's Eye 336, Death's Fathom 354, Annihilus 381, Rainbow Facet 392–399 (given on first rift entry), Hellfire Torch 400, Ancients' unique jewels 420–425, sundering grand charms 426 / 428–432)
static const char *k_eth[] = { "7s8", "7wc", "7pa", "7vo", "7h7", "7gd", "7cr", "7wa", "utp", "uui", "uar", "uit" };   // 无形精英底材候选：常做无形符文之语的长柄 / 剑斧 / 铠甲 / 盾 | Ethereal elite base candidates: the polearms / swords and axes / armors / shields commonly used for ethereal runewords
// 「术士君临」模式（[main+0x2d20b80]+0x5c == 3，同编年史 / 暂停菜单的判断）才能生成 3.x 新加的暗金（*ID 408 起：PreCrafted 破免大板、先祖珠宝等）；
// 别的模式的角色强制这些行号会降级（宿主：标准存档 joe 模式 2 下 426 → 魔法、420 → 普通；另一份模式 3 的存档全成）。这种角色用旧行替代
// Only the "Reign of the Warlock" mode ([main+0x2d20b80]+0x5c == 3, same check as the chronicle / pause menu) can generate the uniques added in 3.x (*ID 408 and up: PreCrafted sundering grand charms, Ancients' jewels, etc.);
// characters in other modes get downgraded when these row numbers are forced (host: with the standard save joe in mode 2, 426 -> magic, 420 -> normal; another save in mode 3 succeeded on all). Such characters use the old rows instead
static int rotw(void) { uint64_t g = rd((uint64_t)(uintptr_t)(B + 0x2d20b80)); return g && *(uint8_t *)(uintptr_t)(g + 0x5c) == 3; }
static void give_sunder(uint64_t G, uint64_t player) { int left[6], n = 0; for (int k = 0; k < 6; k++) if (!(R.sunder & (1u << k))) left[n++] = k;
    int k = n ? left[rnd(n)] : rnd(6); R.sunder |= 1u << k; static const int row[6] = { 426, 428, 429, 430, 431, 432 }; give_item2(G, player, "cm3 ", 7, 99, rotw() ? row[k] : 401 + k, 0); }   // 地狱实际掉的「PreCrafted」六行（401–406 原版 spawnable 为空、不掉落）   // 先给还没送过的那种（90 层「补齐」同此） | The six "PreCrafted" rows that Hell actually drops (401–406 have an empty spawnable in the original game and do not drop)   // Give a kind not yet given first (same for the level-90 "fill the set")
static void milestone(uint64_t G, uint64_t player, int m) {
    int N = m / 10; LOG("秘境：第 %d 层首通里程碑（第 %d 个）", m, N);
    char rc[5] = "r18 "; int r = 18 + rnd(16); rc[1] = (char)('0' + r / 10); rc[2] = (char)('0' + r % 10); give_item(G, player, rc, 2, 99);   // 18–33 号符文随机一颗 | A random rune from No. 18–33
    give_item(G, player, "toa ", 2, 99);   // 赦免之证 | Token of Absolution
    { uint8_t d = *(uint8_t *)(uintptr_t)(G + 0x104); uint64_t q = rd(rd(rd(player + 0x10) + 0x40 + 8 * d)); if (q) { uint8_t *b = (uint8_t *)(uintptr_t)(q + 0x46); *b = (uint8_t)((*b & ~1) | 2); LOG("秘境：拉苏克打孔机会重置（任务块 +0x46 → %#x）", *b); } }
    for (int k = 1; k <= N; k++) switch (k) {
        case 1: case 9: give_sunder(G, player); break;
        case 2: give_item2(G, player, "cm1 ", 7, 110, 381, 0); break;
        case 3: give_item2(G, player, "cm2 ", 7, 110, 400, 0); break;
        case 4: { char c[5]; memcpy(c, k_eth[rnd((int)(sizeof k_eth / sizeof *k_eth))], 3); c[3] = ' '; c[4] = 0; give_item2(G, player, c, 2, 87, -1, 0x400000 | 0x800); break; }
        case 5: if (rotw()) give_item2(G, player, "cjw ", 7, 110, 420 + rnd(6), 0); else give_item2(G, player, "jew ", 7, 99, 392 + rnd(8), 0); break;   // 非君临模式没有先祖珠宝：给彩虹刻面   // 先祖暗金珠宝随机一种（420–425：防御者之怒 / 守护者之雷 / 保卫者之霜 / 防御者之火 / 保卫者之石 / 守护者之光；原版巨像之巅先祖掉落，物品等级 110） | Non-Reign modes have no Ancients' jewels: give a Rainbow Facet   // A random one of the Ancients' unique jewels (420–425: Defender's Fury / Guardian's Thunder / Protector's Frost / Defender's Fire / Protector's Stone / Guardian's Light; dropped by the Ancients at the Colossus Summit in the original game, item level 110)
        case 6: give_item2(G, player, "obf ", 7, 99, 354, 0); break;
        case 7: give_item2(G, player, "ci3 ", 7, 99, 336, 0); break;
        case 8: give_item2(G, player, "uar ", 7, 99, 311, 0); break;
        case 10: { char rc[5] = "r28 "; for (int r = 28; r <= 33; r++) { rc[1] = (char)('0' + r / 10); rc[2] = (char)('0' + r % 10); give_item(G, player, rc, 2, 99); } break; } }   // 28–33 号符文各一颗：Lo / Sur / Ber / Jah / Cham / Zod | One each of runes No. 28–33: Lo / Sur / Ber / Jah / Cham / Zod
    // 首通奖励（以及秘境里给出的其它暗金）掉出来是未鉴定状态——有意为之（「秘境的首通奖励掉出来是未鉴定状态就对了」），玩家双击免费鉴定；别当缺陷改
    // First-clear rewards (and other Uniques given out in the Rift) drop unidentified. This is intentional ("it is right for the Rift's first-clear reward
    // to drop unidentified"); the player double-taps to identify for free. Do not fix it as a defect
    R.ms |= 1u << (N - 1); say("dmRiftSayFirst", m);
}
static void finish(uint64_t G, uint64_t player, uint64_t guard) {
    uint64_t now = guest_ns();
    int left = RIFT_SECS - (int)((now - K.t0_ns) / 1000000000ull); g_left_done = left;   // 结算这一拍停表 | Stop the clock on the completion tick
    K.inc = left >= 600 ? 3 : left >= 300 ? 2 : left > 0 ? 1 : 0; int next = K.tier + K.inc; if (next > 100) next = 100; if (!K.inc) next = K.tier;
    if (K.inc) { R.clears++; if (K.tier > R.best) R.best = K.tier; } if (K.inc && (!R.fast[K.tier] || RIFT_SECS - left < R.fast[K.tier])) R.fast[K.tier] = RIFT_SECS - left;
    if (K.inc) pts_grant(player);   // 通关点数：R.best 更新之后、下面 rec_save 之前。只算限时内通关（「超时通关不算过关，15 分钟内才是过关」），与上一行最高通关层同一口径 | clear points: after R.best is updated and before rec_save below. Only clears within the time limit count ("a clear after the time limit does not count as passing; only within 15 minutes does"), same rule as the highest cleared level on the previous line
    if (K.inc) for (int m = 10; m <= 100; m += 10) if (K.tier >= m && !(R.ms & (1u << (m / 10 - 1)))) milestone(G, player, m);   // 限时内通关才算首通 | Only a clear within the time limit counts as a first clear
    R.tier = next; if (next > R.unlock) R.unlock = next; rec_save(); if (GB.on) gamble_apply(G);
    { int dm_request_save(void); LOG("秘境：结算请游戏存盘 → %d", dm_request_save()); }   // 层数进度（侧文件，上一行刚写进工作目录）与刚打到的东西随这次提交落盘，见 start_rift | the level progress (side file, just written into the working directory on the line above) and what was just looted go to disk with this commit, see start_rift
    if (K.inc) { char c[5] = "xa1 "; c[2] = (char)('1' + rnd(5)); give_item(G, player, c, 2, 99); }
    if (K.inc) LOG("秘境：第 %d 层完成（剩 %d:%02d）→ 下次第 %d 层", K.tier, left > 0 ? left / 60 : 0, left > 0 ? left % 60 : 0, next); else LOG("秘境：第 %d 层超时未过关 → 不涨层、不计最高层与通关数，下次仍第 %d 层", K.tier, next);   // 剩余时间与下次层数在左上角任务条上（dm_rift_hud），不再单发一条提示 | Time left and next level are on the top-left quest bar (dm_rift_hud); no separate notice is sent anymore
    atomic_store(&K.st, ST_DONE); region_set(G, 1);
    pet_hp_pump(G, player, tick_of(G));   // 随从生命当拍设回基数（结算后紧跟着会存盘，放大着的值不能进角色档） | set minion life back to the base on this very tick (a save follows right after completion, and the boosted value must not go into the character save)
    if (K.players_old) dm_hud_request_players(K.players_old);
    // 结算后的两扇门（打完要回第五幕门才能进下一层很奇怪；「完成后清怪，给一个回城传送门」）：点下一层的门直接开下一层，点回城门回哈洛加斯，两扇并存。
    // 掉落堆在两处：首通奖励在人脚下、守护者的掉落在它倒下处，物品名字往屏幕上方叠。两扇门开在「人背对守护者」的那一侧：从人的位置朝远离守护者的方向走 10 格，再向两边各错开 5 格。
    // 试过并否掉的摆法（真机 / Mac 截图）：开在守护者倒下处——被名字和光柱盖住；固定开在屏幕正下方——落到底栏后面；固定左右两侧——哪一侧正好是守护者那侧就又被盖住
    // The two doors after completion (having to go back to the Act 5 door to enter the next level after finishing felt odd; "clear monsters on completion, give a town portal"): tapping the next-level door opens the next level directly, tapping the town door returns to Harrogath; both exist side by side.
    // Drops pile up in two places: the first-clear reward at the player's feet and the guardian's drops where it fell, with item names stacking toward the top of the screen. The two doors open on the side where "the player has their back to the guardian": from the player's position go 10 tiles in the direction away from the guardian, then offset 5 tiles to each side.
    // Placements tried and rejected (real device / Mac screenshots): opening where the guardian fell -- covered by names and light beams; fixed directly below on screen -- ends up behind the bottom bar; fixed on the left and right -- whichever side happens to be the guardian's side gets covered again
    { int x, y, gx, gy; uint64_t room = unit_room(player); if (!unit_xy(player, &x, &y) || !room) { LOG("【红】秘境：结算时读不到玩家位置，两扇门没开"); return; }
      float vx = -1, vy = 1; if (guard && unit_xy(guard, &gx, &gy) && (abs(x - gx) + abs(y - gy)) >= 2) { vx = (float)(x - gx); vy = (float)(y - gy); }   // 人和守护者叠在一起时取屏幕左侧 | When the player and the guardian overlap, take the left side of the screen
      { float n = sqrtf(vx * vx + vy * vy); vx /= n; vy /= n; } int bx = x + (int)lroundf(vx * 10), by = y + (int)lroundf(vy * 10), ox = (int)lroundf(-vy * 5), oy = (int)lroundf(vx * 5);
      L.nd_lv = unit_level(player); L.nd_x = x; L.nd_y = y;
      // 测试开关 DM_RIFT_TEST_ND_FAR=1（造现场用）：把记下的「守护者处」换成这张图里很远处一个能站人的点——专压「继续」时目标离换关落点很远这一环。
      // 取法：房间按离人的距离从远到近试，先用原版房间初始化把它载入（预设记录要载入后才有），取它的第一个怪物预设点（预设点是能站人的地方；
      // 直接取房间中心会落在墙里 / 图外的填充房间，原版传送自己找不到空位，那是量具造出来的失败）
      // Test switch DM_RIFT_TEST_ND_FAR=1 (for setting up a scenario): replace the recorded "Guardian's spot" with a walkable point far away in this map,
      // specifically to stress the case where the "Continue" target is far from the level-change landing point.
      // How it is picked: try rooms from farthest to nearest from the player, first loading each with the original room initialization (preset records exist
      // only after loading), and take its first monster preset point (preset points are walkable places;
      // taking the room center directly lands inside walls / in filler rooms outside the map, where the original teleport cannot find a free spot by itself,
      // and that would be a failure created by the test tool)
      if (getenv("DM_RIFT_TEST_ND_FAR")) { uint64_t lvp = level_of(player); int fx = 0, fy = 0, fd = 0, n = 0, tried = 0; uint64_t done[24]; int nd = 0;
          while (!fx && tried < 24) { uint64_t br = 0; int best = -1; n = 0;
              for (uint64_t r = dm_level_rooms(lvp); r && n < 2000; r = rd(r + 0x48), n++) { int sk = 0; for (int q = 0; q < nd; q++) sk |= done[q] == r; if (sk) continue;
                  int cx = *(int32_t *)(uintptr_t)(r + 0x60) * 5 + *(int32_t *)(uintptr_t)(r + 0x68) * 5 / 2, cy = *(int32_t *)(uintptr_t)(r + 0x64) * 5 + *(int32_t *)(uintptr_t)(r + 0x6c) * 5 / 2, d = abs(cx - x) + abs(cy - y); if (d > best) { best = d; br = r; } }
              if (!br) break; done[nd++] = br; tried++;
              if (!rd(br + 0x58)) ((void (*)(int, uint64_t))(B + 0x52dcc0))(*(uint8_t *)(uintptr_t)(G + 0x106), br);
              int rx = *(int32_t *)(uintptr_t)(br + 0x60) * 5, ry = *(int32_t *)(uintptr_t)(br + 0x64) * 5;
              for (uint64_t u = rd(br + 0x98), k = 0; u && k < 256 && !fx; u = rd(u + 0x10), k++) if (*(uint32_t *)(uintptr_t)(u + 0x20) == 1) { fx = rx + *(int32_t *)(uintptr_t)(u + 8); fy = ry + *(int32_t *)(uintptr_t)(u + 0x24); fd = abs(fx - x) + abs(fy - y); } }
          if (fx) { LOG("秘境：测试 DM_RIFT_TEST_ND_FAR 把守护者处从 %d,%d 换成远处的怪物预设点 %d,%d（相距 %d 格；试了 %d 间房，共 %d 间）", x, y, fx, fy, fd, tried, n); L.nd_x = fx; L.nd_y = fy; }
          else LOG("秘境：测试 DM_RIFT_TEST_ND_FAR 试了 %d 间最远的房都没有怪物预设点，守护者处不变", tried); }
      // 上面算的只是「想放在哪」；真正的落点必须是玩家走得到的格（dm_doorspot.h：从人脚下按玩家的走法广搜，只在搜得到的格里取离想放处最近的；窄到摆不开就退到脚下最近的可走格）。
      // 原先把想放处直接交给原版找空地（只看物件位 0x400、不看墙 0x1，也不管走不走得到），洞穴 / 地穴里两扇门会落进墙里的黑区、点不到。
      // 挡不挡照玩家自己的碰撞掩码（路径结构 +0x64，读不到用 0x1c09，同点地寻路 dm_world.c）问原版碰撞查询 main+0x4ce020；没载入的房间它按全挡答；人自己中心格的 0x1000 不算
      // What is computed above is only "where we would like to put it"; the actual landing point must be a cell the player can walk to (dm_doorspot.h: breadth-first search from the player's feet with the player's way of walking, taking only among the reached cells the one nearest to the desired spot; when too narrow to fit, fall back to the nearest walkable cell by the feet).
      // Previously the desired spot was handed directly to the original game's free-ground search (looks only at the object bit 0x400, not at walls 0x1, and does not care whether it can be walked to), and in the Cave / the Pit the two doors would land in the black area inside the walls and could not be tapped.
      // Whether a cell blocks goes by the player's own collision mask (path structure +0x64, 0x1c09 when it cannot be read, same as ground-tap pathfinding dm_world.c), asked of the original game's collision query main+0x4ce020; for rooms not loaded it answers all blocked; the 0x1000 of the player's own center cell does not count
      { static struct ds_grid dg; uint64_t lvp = level_of(player), pp = rd(player + 0x38); uint32_t mk = pp ? *(uint32_t *)(uintptr_t)(pp + 0x64) : 0; if (!(mk & 1) || (mk & ~0xffffu)) mk = 0x1c09;
        for (int iy = 0; iy < DS_W; iy++) for (int ix = 0; ix < DS_W; ix++) { int cx = x + ix - DS_R, cy = y + iy - DS_R; uint64_t r = room_at(lvp, cx, cy);
            uint32_t v = r ? ((uint32_t (*)(uint64_t, int, int, int))(B + 0x4ce020))(r, cx, cy, (int)mk) & 0xffff : 1; if (ix == DS_R && iy == DS_R) v &= ~0x1000u; dg.blk[iy * DS_W + ix] = v != 0; }
        ds_flood(&dg);
        int nx, ny, tx, ty, t1 = ds_pick(&dg, bx + ox - x, by + oy - y, 0, 0, 0, ND_GAP, &nx, &ny), t2 = ds_pick(&dg, bx - ox - x, by - oy - y, 1, nx, ny, ND_GAP, &tx, &ty);
        // 「想放处」后面括号里是它自己的状态（挡 = 墙 / 物件 / 图外；走 −1 步 = 不挡但走不到）——旧规则落在哪、为什么不能用，日志里直接看得到
        // The parentheses after 「想放处」(desired spot) hold its own state (挡 (blocked) = wall / object / outside the map; 走 −1 步 (−1 steps) = not blocked but unreachable) -- where the old rule lands and why it cannot be used can be read directly in the log
        LOG("秘境：结算处门位 人 %d,%d 掩码 %#x 窗口内可达 %d 格；下一层想放 %d,%d（%s，走 %d 步）→ 取 %d,%d（档 %d，走 %d 步）；回城想放 %d,%d（%s，走 %d 步）→ 取 %d,%d（档 %d，走 %d 步）", x, y, mk, dg.reach,
            bx + ox, by + oy, ds_blk(&dg, bx + ox - x + DS_R, by + oy - y + DS_R) ? "挡" : "不挡", ds_dist(&dg, bx + ox - x + DS_R, by + oy - y + DS_R), x + nx, y + ny, t1, ds_dist(&dg, nx + DS_R, ny + DS_R),
            bx - ox, by - oy, ds_blk(&dg, bx - ox - x + DS_R, by - oy - y + DS_R) ? "挡" : "不挡", ds_dist(&dg, bx - ox - x + DS_R, by - oy - y + DS_R), x + tx, y + ty, t2, ds_dist(&dg, tx + DS_R, ty + DS_R));
        L.next_door = place_door(G, room_at(lvp, x + nx, y + ny), x + nx, y + ny, "结算处下一层", RIFT_OBJ, 1); if (!L.next_door) LOG("【红】秘境：下一层的门开门失败");
        L.town_door = place_door(G, room_at(lvp, x + tx, y + ty), x + tx, y + ty, "结算处回城", TOWN_OBJ, 1); if (!L.town_door) LOG("【红】秘境：回城门开门失败"); }
      { int nt = K.inc ? K.tier + K.inc : K.tier;   // 一条提示说清两扇门，长度与原先那句相当、系统提示框不多换行 | One notice explains both doors; about as long as the previous sentence, with no extra line wrap in the system notice box
        // 结算只发这一条（原先「第 N 层完成…」「门已开启」各一条，加上首通奖励那条，原版系统提示框一条一条放，连着通关时会落后一层）
        // 文字控制在 16 个字以内（加上「秘境」前缀与「初入秘境…」那句等长，提示框里是一行）；层数、剩余时间、下次层数都在左上角任务条上
        // Completion sends only this one notice (previously 「第 N 层完成…」(Level N complete...) and 「门已开启」(The door is open) were one notice each, plus the first-clear reward one; the original system notice box shows them one at a time, so with back-to-back clears it would lag one level behind)
        // Text kept within 16 characters (with the 「秘境」(Rift) prefix it is as long as the 「初入秘境…」(First time in the rift...) sentence, one line in the notice box); the level, time left and next level are all on the top-left quest bar
        (void)nt; if (L.next_door && L.town_door) say(K.inc ? "dmRiftSayDoneBoth" : "dmRiftSayLateBoth");
        else if (L.next_door) say(K.inc ? "dmRiftSayDoneNext" : "dmRiftSayLateNext"); else if (L.town_door) say(K.inc ? "dmRiftSayDoneTown" : "dmRiftSayLateTown"); else say(K.inc ? "dmRiftSayDone" : "dmRiftSayLate"); } }
}

// 本层完成后的下一层层号：限时内完成涨 K.inc 层（封顶 100），超时完成不涨。守护者处那扇门取这个数（结算日志、任务条上的「下次第 N 层」同源）
// Level number of the next level after this one is completed: completing within the time limit raises it by K.inc levels (capped at 100), completing
// over time does not raise it.
// The portal at the Guardian's spot uses this number (same source as the completion log and the "Next: level N" on the quest bar)
static int next_tier(void) { int nt = K.inc ? K.tier + K.inc : K.tier; return nt > 100 ? 100 : nt; }
static void menu_fill(dm_rift_menu_t *o, int m) {
    uint64_t now = guest_ns();
    int need = atomic_load(&K.need); o->menu = m; o->best = R.best; o->unlock = R.unlock < 1 ? 1 : R.unlock; o->def = R.tier; o->ms = R.ms; o->clvl = atomic_load(&M.clvl);
    o->tier = K.tier; o->done = atomic_load(&K.st) == ST_DONE; o->inc = K.inc; o->secs_left = o->done ? g_left_done : K.t0_ns ? RIFT_SECS - (int)((now - K.t0_ns) / 1000000000ull) : RIFT_SECS; if (o->secs_left < 0) o->secs_left = 0;
    o->progress = need > 0 ? (float)atomic_load(&K.score) / need : 0; if (o->progress > 1) o->progress = 1;
}
int dm_rift_menu(dm_rift_menu_t *o) { int m = atomic_load(&M.menu); if (m == MENU_NONE) return 0; menu_fill(o, m); return 1; }
int dm_rift_stats(dm_rift_stats_t *o) {   // 积分牌「秘境」页（dm_kills.c）：本局角色的侧文件读数；没资格 / 没读过返回 0 | Kill board 「秘境」(Rift) page (dm_kills.c): sidecar file readout for this run's character; returns 0 if not eligible / not read yet
    if (!K.who[0]) return 0; o->best = R.best; o->clears = R.clears; o->deaths = R.deaths; o->tier = R.tier; o->unlock = R.unlock; o->ms = R.ms; memcpy(o->fast, R.fast, sizeof o->fast); return 1; }
void dm_rift_menu_close(void) { atomic_store(&M.menu, MENU_NONE); }
void dm_rift_menu_peek(dm_rift_menu_t *o) { int m = atomic_load(&K.st) == ST_RUN || (atomic_load(&K.st) == ST_DONE && L.next_door) ? MENU_CHOICE : MENU_SELECT; menu_fill(o, m); }   // 没点门时也给页面读数（玩家自己翻到编年史「秘境」页） | Provide page readouts even when the door was not tapped (the player browsed to the chronicle 「秘境」(Rift) page themselves)
void dm_rift_menu_start(int tier) { atomic_store(&M.req_start, tier); atomic_store(&M.menu, MENU_NONE); }
void dm_rift_menu_continue(void) { atomic_store(&M.req_cont, 1); atomic_store(&M.menu, MENU_NONE); }
void dm_rift_menu_abandon(void) { atomic_store(&M.req_abandon, 1); atomic_store(&M.menu, MENU_NONE); }
static _Atomic int g_menu_seq; int dm_rift_menu_seq(void) { return atomic_load(&g_menu_seq); }   // 点城里的门的次数（面板据此认「又点了一次」） | Number of taps on the town door (the panel uses it to recognize "tapped again")
int dm_rift_menu_pending(void) { return atomic_load(&M.req_abandon) != 0; }   // 放弃还没在服务端拍里执行（大秘境面板据此放开暂停） | Abandon has not yet been executed in the server tick (the greater rift panel releases the pause based on this)

// 开一层：抽图（一张野外 + 1–2 张迷宫，不重复，且本局没用过）、改区域、计时、换关。成功返回 1
// Open a level: draw maps (one wilderness map + 1–2 mazes, no repeats, not used in this run), modify regions, start the timer, change level. Returns 1 on success
static int start_rift(uint64_t G, uint64_t player, uint32_t t, int tier) {
    // 选图：一张野外 + 1–2 张迷宫，不重复，且本局没用过（同一局里生成过的关卡不会重新生成：怪打过了、图也一样）
    // Map selection: one wilderness map + 1–2 mazes, no repeats, and not used in this run (a level generated in a run is not regenerated in the same run: the monsters are already killed and the map is the same)
    int fo[8], nf = 0, fm[24], nm = 0; for (unsigned k = 0; k < sizeof k_open / sizeof *k_open; k++) { int u = 0; for (int q = 0; q < L.nused; q++) u |= L.used[q] == k_open[k]; if (!u) fo[nf++] = k_open[k]; }
    built_scan(player);   // 服务端顺带建过的迷宫（没进去过也算）不抽：它的房间数已经定了，改不得 | Mazes the server has already built in passing (even if never entered) are not drawn: their room count is already fixed and must not be changed
    for (unsigned k = 0; k < sizeof k_maze / sizeof *k_maze; k++) { int u = 0; for (int q = 0; q < L.nused; q++) u |= L.used[q] == k_maze[k]; if (!u && !g_built[k_maze[k]]) fm[nm++] = k_maze[k]; }
    { char bf[120]; int o = 0; bf[0] = 0; for (unsigned k = 0; k < sizeof k_maze / sizeof *k_maze; k++) if (g_built[k_maze[k]] && o < 100) o += snprintf(bf + o, sizeof bf - o, " %d", k_maze[k]); LOG("秘境：抽图 野外剩 %d、迷宫剩 %d（服务端已建不抽：%s）", nf, nm, o ? bf : " 无"); }
    if (!nf || nm < 2 || getenv("DM_RIFT_TEST_EXHAUST")) {   // 玩家不该感觉到「图用完」→ 走一键重开（约 1.5 秒，dm_cursor.c），新局第一时间直接开这一层 | The player should not notice "maps ran out" -> take the one-tap restart (about 1.5 seconds, dm_cursor.c); the new run opens this level right away
        void dm_restart_request(void); K.resume_tier = tier; setenv("DM_RIFT_TEST_EXHAUST_DONE", "1", 1); unsetenv("DM_RIFT_TEST_EXHAUST");
        LOG("秘境：本局新图用完（野外剩 %d、迷宫剩 %d）→ 一键重开，新局直接开第 %d 层", nf, nm, tier); dm_restart_request(); return 0; }
    K.tier = tier; { const char *e = getenv("DM_RIFT_TIER"); if (e) K.tier = atoi(e); }   // 测试：钉层数 | Test: pin the level
    K.nmaps = 2 + rnd(2); K.maps[0] = fo[rnd(nf)]; K.maps[1] = fm[rnd(nm)]; K.maps[2] = 0;
    if (K.nmaps == 3) { do K.maps[2] = fm[rnd(nm)]; while (K.maps[2] == K.maps[1]); }
    { static int n = -1; const char *e = getenv("DM_RIFT_NEED"); if (n < 0) n = e ? atoi(e) : 250; atomic_store(&K.need, n); }
    // 测试：钉图。分号隔开 = 本局第 1、2…次开局各用哪一段（次数超出用最后一段），用来造「上一层用过相邻迷宫」的现场
    // Test: pin the map. Semicolon-separated = which segment each of the 1st, 2nd... starts of this game uses (past the end, the last segment is used); used to set up the situation where "the previous level used an adjacent maze"
    { static int nth; const char *e = getenv("DM_RIFT_MAPS"); if (e) { for (int q = 0; q < nth; q++) { const char *nx = strchr(e, ';'); if (!nx) break; e = nx + 1; } nth++; K.maps[2] = 0; K.nmaps = sscanf(e, "%d,%d,%d", &K.maps[0], &K.maps[1], &K.maps[2]); } }
    // 保险：图表不成立就不开局（测试包：插注释吞了抽图那半行，图表全 0，换关到关卡 0，真机点门即闪退）
    // Safeguard: do not start if the map table is invalid (test build: an inserted comment swallowed the half line that draws maps, the map table was all 0, the level change went to level 0, and tapping the door on a real device crashed immediately)
    { int bad = K.nmaps < 1 || K.nmaps > 3; for (int q = 0; q < K.nmaps && !bad; q++) bad = K.maps[q] <= 0 || K.maps[q] >= 137;
      if (bad) { LOG("【红】秘境：图表不成立（%d 张：%d %d %d），不开局", K.nmaps, K.maps[0], K.maps[1], K.maps[2]); say("dmRiftSayErr"); return 0; } }
    for (int q = 0; q < K.nmaps && L.nused < 24; q++) L.used[L.nused++] = K.maps[q];
    atomic_fetch_add(&g_run_seq, 1);
    rift_setup(G); K.cur = 0; atomic_store(&K.score, getenv("DM_RIFT_TEST_SCORE") ? atoi(getenv("DM_RIFT_TEST_SCORE")) : 0);   // 测试：初始分数（看进度条） | Test: initial score (to see the progress bar)
    atomic_store(&K.guard_dead, 0); atomic_store(&K.timeout, 0); K.guard = 0; K.inc = 0;
    K.t0_ns = guest_ns();
    if (getenv("DM_RIFT_TEST_LATE")) { K.t0_ns -= (uint64_t)RIFT_SECS * 1000000000ull; LOG("秘境：测试 DM_RIFT_TEST_LATE 开局即超时"); }   // 造「超时打倒守护者」现场 | set up the "Guardian defeated after time ran out" test case
    K.players_old = dm_hud_requested_players(); if (!K.players_old) K.players_old = 1;
    dm_quiet_difficulty_msg(1);   // 本局原版「游戏难度设为 N」提示不弹（观测门 15，diag.c；换局关） | The original 「游戏难度设为 N」(Game difficulty set to N) notice is not shown this run (observation gate 15, diag.c; turned off on run change)
    dm_hud_request_players(K.tier < 8 ? K.tier : 8);
    atomic_store(&K.st, ST_RUN); say("dmRiftSayStart", K.tier);   // 一行放得下（原句在提示框和聊天栏里折行，把「15 分钟」拆开；攒进度 / 打守护者在左上角任务条上有） | Fits on one line (the original sentence wrapped in the notice box and chat bar, splitting 「15 分钟」(15 minutes); building progress / fighting the guardian are on the top-left quest bar)
    LOG("秘境：拍 %u 开局 第 %d 层，图 %d → %d → %d（共 %d 张），/players %d", t, K.tier, K.maps[0], K.maps[1], K.maps[2], K.nmaps, K.tier < 8 ? K.tier : 8);
    // 进秘境 / 换层前请游戏存一次盘（dm_request_save：下一拍它走自己的定时存盘）：秘境是连着换图的高风险转场，这里崩了不该回退到 5 分钟一次的定时存盘那一刻
    // （连层崩溃后玩家报仓库里的装备没了 = 进秘境前拖进仓库的那几件随崩溃退回了拖之前）。结算时同样请一次（finish）
    // Before entering a rift / changing levels, ask the game to save once (dm_request_save: on the next tick it runs its own timed autosave): a rift is a high-risk transition with one map change after another, and a crash here should not roll back to the moment of the once-every-5-minutes timed autosave
    // (after a crash while chaining levels a player reported that the gear in the stash was gone = the few pieces dragged into the stash before entering the rift went back, with the crash, to before the drag). One is requested at completion as well (finish)
    { int dm_request_save(void); LOG("秘境：开局前请游戏存盘 → %d", dm_request_save()); }
    L.lv_prev = K.maps[0]; warp(G, player, K.maps[0]);
    if (!R.gift) L.gift_due = 1;   // 第一次进秘境送一颗彩虹刻面，只送一次：落到秘境图里再掉在脚下 | The first rift entry gives one Rainbow Facet, only once: dropped at the player's feet after landing on a rift map
    return 1;
}
void dm_rift_tick(uint64_t G, uint64_t player, uint8_t *main_base) {
    B = main_base; if (!G || !player || !B) return;
    atomic_store(&g_merc_req, 0); if (getenv("DM_TEST_MERC_EQUIP") || getenv("DM_TEST_MERC_DUMP")) merc_test(G, player, *(uint32_t *)(uintptr_t)(G + 0x170));
    rift_gates();
    pet_hp_pump(G, player, *(uint32_t *)(uintptr_t)(G + 0x170));
    { static int got; if (!got) { for (int l = 1; l < 112; l++) atomic_store(&g_mlhp[l], monlvl(G, l, 2)); got = atomic_load(&g_mlhp[85]) != 0; if (got) LOG("秘境：MonLvl 生命列已缓存（85 级 %u、96 级 %u、110 级 %u）", atomic_load(&g_mlhp[85]), atomic_load(&g_mlhp[96]), atomic_load(&g_mlhp[110])); } }   // 给选层面板算「怪物强度」 | compute "Monster Power" for the level selection panel
    uint32_t t = tick_of(G);
    if (G != L.G || player != L.player || t < L.last_t) {   // 换局（同首领连战：拍号倒退 / 玩家换了） | Run change (same as boss rush: tick number went backwards / player changed)
        int st = atomic_load(&K.st);
        maze_restore(G, player);
        if (st == ST_RUN || st == ST_DONE) { region_set(G, 0); if (K.players_old) dm_hud_request_players(K.players_old); atomic_store(&K.st, ST_IDLE); LOG("秘境：换局，%s", st == ST_RUN ? "本次秘境作废" : "结束"); }
        dm_quiet_difficulty_msg(0); GS.bm_on = 0; shelf_restore(); gamble_restore(); memset(&L, 0, sizeof L); L.G = G; L.player = player; L.eligible = -1; L.lv_prev = -1; L.resume = K.resume_tier; K.resume_tier = 0;   // resume：图用完走的一键重开，到新局才接着开；GS.bm_on = 0：位图属于上一局（已释放），不写回 | resume: the one-tap restart taken when maps ran out continues only once in the new run; GS.bm_on = 0: the bitmap belongs to the previous run (already freed), not written back
    }
    L.last_t = t; built_scan(player);
    int lv = unit_level(player); if (!lv || !unit_room(player)) return;
    if (L.eligible < 0) { static int test = -1; if (test < 0) test = getenv("DM_RIFT_TEST") != NULL;
        uint8_t diff = *(uint8_t *)(uintptr_t)(G + 0x104); uint64_t q = rd(rd(rd(player + 0x10) + 0x40 + 8 * diff));
        L.eligible = test || (diff == 2 && q && (*(uint8_t *)(uintptr_t)(q + 0x50) & 1));
        { const char *nm = (const char *)(uintptr_t)rd(player + 0x10); if (nm) strlcpy(K.who, nm, sizeof K.who); }
        LOG("秘境：本局%s资格（难度 %u%s，角色「%s」）", L.eligible ? "有" : "没有", diff, test ? "，测试开关" : "", K.who);
        { const char *e = getenv("DM_RIFT_TEST_GIVE"); char b2[200]; if (e) { strlcpy(b2, e, sizeof b2); for (char *sv, *k = strtok_r(b2, ";", &sv); k; k = strtok_r(NULL, ";", &sv)) { int r, lv; char c[5] = "    "; if (sscanf(k, "%d:%3[a-z0-9]:%d", &r, c, &lv) == 3) { c[3] = ' '; give_item2(G, player, c, 7, lv, r, 0); } } } }   // 测试：进局按「行:底材:物品等级;…」生成指定暗金，看成不成 | Test: on entering a run, generate the specified uniques per "row:base:item level;..." to see whether it works
        rec_load();   /* 秘境记录任何局都读（不看这一局有没有秘境资格）：佣兵装备位按最高通关层解锁，解锁状态要跟角色走，普通 / 噩梦局里也认 | Rift records are read in every run (regardless of whether this run qualifies for rifts): mercenary equipment slots unlock by highest level cleared, and the unlock state must follow the character, so it counts in Normal / Nightmare runs too */
        if (L.eligible) { { const char *e = getenv("DM_RIFT_TEST_BEST"); if (e) R.best = atoi(e); e = getenv("DM_RIFT_TEST_UNLOCK"); if (e) R.unlock = atoi(e); } } }   // 测试：钉最高层（看赌博加成）   // 玩家数据开头即角色名（同极乐世界） | Test: pin the highest level (to see the gambling bonus)   // The player data starts with the character name (same as Elysium)
    if (!L.eligible) return;
    { static uint64_t seen; static uint32_t at; uint32_t tq = *(uint32_t *)(uintptr_t)(G + 0x170);   // 通关点数的补发：换了玩家单位（进局）后延 75 拍 | catch-up grant of clear points: delayed 75 ticks after the player unit changes (joining a game)
      if (player != seen) { seen = player; at = tq + 75; } else if (at && tq >= at) { at = 0; int p0 = R.pts; if (pts_grant(player) || R.pts != p0) rec_save(); } }
    int st = atomic_load(&K.st);
    dm_door_label(B, G, RIFT_OBJ, "dmDoorRift", "大秘境", "Greater Rift", lv == LV_TOWN || in_rift_map(lv));
    dm_door_label(B, G, TOWN_OBJ, "dmDoorRiftTown", "回城", "Return to Town", in_rift_map(lv));
    if (lv == LV_TOWN) { if (!GB.on) gamble_apply(G); } else gamble_restore();   // 加成只在哈洛加斯（安亚是秘境的兑换点）；别的城赌博保持原版（表是全局的，离开哈洛加斯即写回） | The bonus applies only in Harrogath (Anya is the rift's exchange point); gambling in other towns stays original (the table is global, so it is written back on leaving Harrogath)
    if (lv == LV_TOWN && GB.on) shelf_apply(G, player); else shelf_restore();   // 赌博加成生效时（地狱、通关过）才筛货架   // 秘境图里有「下一层」门（342 原版只在憎恨囚牢 102，不在秘境图表里）   // 门名牌（dm_bossrush.c） | Filter the shelf only when the gambling bonus is active (Hell, has cleared)   // Rift maps have a 「下一层」(Next Level) door (342 originally exists only in Durance of Hate 102, which is not in the rift map table)   // Door nameplate (dm_bossrush.c)

    // 门：城里一进来就开（秘境进行中也在，点了回当前图）
    // Door: opens as soon as the player enters town (also present while a rift is in progress; tapping it returns to the current map)
    if (lv == LV_TOWN && !L.door && !L.door_tried) { L.door_tried = 1; L.door = make_rift_door(G, player); if (!L.door) L.door_tried = 0; }
    if (L.eligible > 0) atomic_store(&M.clvl, gs(player, 12));
    if (L.resume && L.eligible > 0 && lv == LV_TOWN && t > 50) { int tq = L.resume; L.resume = 0; LOG("秘境：一键重开后的新局 → 直接开第 %d 层", tq); start_rift(G, player, t, tq); return; }
    { static int at = -1, tr; if (at < 0) { const char *e = getenv("DM_RIFT_TEST_START"); at = e ? atoi(e) : 0; const char *c = e ? strchr(e, ':') : NULL; tr = c ? atoi(c + 1) : 1; }   // 测试开关 DM_RIFT_TEST_START=<拍>[:<层>]：人在哈洛加斯时到这一拍直接开这一层（裸宿主上选层页点不了；走的是一键重开后续局的同一句） | Test switch DM_RIFT_TEST_START=<tick>[:<floor>]: while the player is in Harrogath, start that floor directly at that tick (the floor selection page cannot be tapped on the bare host; this goes through the same statement as the follow-up run after a quick restart)
      if (at > 0 && t >= (uint32_t)at && lv == LV_TOWN && L.eligible > 0 && atomic_load(&K.st) == ST_IDLE) { at = 0; LOG("秘境：测试 DM_RIFT_TEST_START 拍 %u 直接开第 %d 层", t, tr); start_rift(G, player, t, tr); return; } }
    { static int at = -1; if (at < 0) { const char *e = getenv("DM_RIFT_TEST_RESTART"); at = e ? atoi(e) : 0; } if (at > 0 && t >= (uint32_t)at) { at = 0; void dm_restart_request(void); LOG("秘境：测试 DM_RIFT_TEST_RESTART 拍 %u 请求一键重开", t); dm_restart_request(); } }   // 测试：拍 N 替玩家一键重开 | Test: at tick N, do a one-tap restart on the player's behalf
    if (lv != LV_TOWN && atomic_load(&M.menu)) atomic_store(&M.menu, MENU_NONE);   // 离城自动收起 | Closes automatically on leaving town
    { int tq = atomic_exchange(&M.req_start, 0); if (tq > 0 && lv == LV_TOWN && st != ST_RUN) { if (tq > R.unlock) tq = R.unlock; if (tq < 1) tq = 1; LOG("秘境：选层页开始第 %d 层", tq); L.next_door = L.town_door = 0; start_rift(G, player, t, tq); return; } }
    { static int at = -1; if (at < 0) { const char *e = getenv("DM_RIFT_TEST_SAVE"); at = e ? atoi(e) : 0; } if (at > 0 && t >= (uint32_t)at) { at = 0; int dm_request_save(void); LOG("秘境：测试 DM_RIFT_TEST_SAVE 拍 %u 请游戏存盘 → %d", t, dm_request_save()); } }   // 测试开关 DM_RIFT_TEST_SAVE=<拍>：到这一拍请游戏存一次盘（验放大着的随从生命进不进角色档） | test switch DM_RIFT_TEST_SAVE=<tick>: ask the game to save once at this tick (to check whether boosted minion life gets into the character save)
    { static int at = -1; if (at < 0) { const char *e = getenv("DM_RIFT_TEST_ABANDON"); at = e ? atoi(e) : 0; } if (at > 0 && t >= (uint32_t)at && st == ST_RUN) { at = 0; LOG("秘境：测试 DM_RIFT_TEST_ABANDON 拍 %u 替玩家放弃本层", t); atomic_store(&M.req_abandon, 1); } }   // 测试开关 DM_RIFT_TEST_ABANDON=<拍>：秘境进行中到这一拍替玩家放弃（裸宿主上点不了「放弃」） | test switch DM_RIFT_TEST_ABANDON=<tick>: during a rift run, abandon on the player's behalf at this tick ("Abandon" cannot be clicked on the bare host)
    if (atomic_exchange(&M.req_abandon, 0) && (st == ST_RUN || st == ST_DONE)) {   // 放弃：本层作废（不涨层、不发奖），写回改过的表，回选层页 | Abandon: this level is void (no level increase, no rewards); write back the modified tables and return to the level selection page
        region_set(G, 0); if (K.players_old) dm_hud_request_players(K.players_old); K.guard = 0; L.next_door = L.town_door = 0; atomic_store(&K.st, ST_IDLE); st = ST_IDLE;
        LOG("秘境：放弃第 %d 层", K.tier); atomic_store(&M.menu, MENU_SELECT);
        pet_hp_pump(G, player, t);   // 随从生命当拍设回基数，同结算处：放弃后回到选层页会走原版暂停，等下一拍的话要到玩家关掉面板才设回 | minion life is set back to base on this very tick, as at completion: after abandoning, returning to the level-selection page goes through the original's pause, and waiting for the next tick would delay it until the player closes the panel
        return; }
    if (atomic_exchange(&M.req_cont, 0)) {   // 继续：进行中 = 回到离开时的位置；本层已完成 = 回到守护者处（结算时人站的地方，两扇门就在旁边） | Continue: in progress = back to the position at the time of leaving; this level completed = back to the Guardian's spot (where the player stood at completion, with both portals right beside it)
        // 已完成后点「继续」就该回到战斗终止的地方（从那里捡东西、走红门去下一层）；城门面板不提供「直接开下一层」
        // After completion, tapping "Continue" should return to where the fight ended (pick things up there, take the red portal to the next level); the town
        // gate panel does not offer "open the next level directly"
        int tl = 0, tx = 0, ty = 0; if (st == ST_RUN && L.last_lv) { tl = L.last_lv; tx = L.last_x; ty = L.last_y; } else if (st == ST_DONE && L.nd_lv) { tl = L.nd_lv; tx = L.nd_x; ty = L.nd_y; }
        if (!tl && st == ST_RUN) tl = K.maps[K.cur];
        if (tl) { LOG("秘境：继续 → 关卡 %d（%d,%d）", tl, tx, ty); L.lv_prev = tl; warp(G, player, tl); if (tx) { L.tp_lv = tl; L.tp_x = tx; L.tp_y = ty; } return; } }
    // 继续之后：换关落地，等那间房载入了就原地传送到离开时的位置（5 秒内没载入就留在落点）
    // After continue: once the level change lands, wait for that room to load and then teleport in place to the position at leaving (if not loaded within 5 seconds, stay at the landing point)
    if (L.tp_lv && lv == L.tp_lv && atomic_load(&g_cli_lv) == lv) { uint64_t lvp = level_of(player); int32_t pos[2] = { L.tp_x, L.tp_y }; uint64_t rs = room_load(G, lvp, pos[0], pos[1]), rr = rs ? free_spot(G, rs, pos) : 0;   // 客户端也到了这张图再挪；目标房间没载入就载入 | Move only once the client has also arrived on this map; load the target room if it is not loaded
        // 目标房间交给公共的定点传送（dm_unit.h）：原先第三个参数传 0，目标离换关落点远了就失败、人留在那张图的缺省落点（落在小站上）
        // 测试开关 DM_RIFT_TEST_TP_NOROOM=1（对照用）：照修前的调法不给目标房间（第三个参数 0），用来在同一个现场上复现「留在落点」
        // The target room is handed to the shared fixed-point teleport (dm_unit.h): the third argument used to be 0, which failed when the target was far from
        // the level-change landing point and left the player at that map's default landing point (on the waypoint)
        // Test switch DM_RIFT_TEST_TP_NOROOM=1 (for comparison): call it the pre-fix way without a target room (third argument 0), to reproduce "stays at the
        // landing point" in the same scenario
        int old = getenv("DM_RIFT_TEST_TP_NOROOM") != NULL, okk = !rr ? 0 : old ? ((int (*)(uint64_t, uint64_t, uint64_t, int, int, int, int))(B + 0x7cc530))(G, player, 0, pos[0], pos[1], 0, 0) : dm_teleport(B, G, player, rr, pos[0], pos[1]);
        if (okk) LOG("秘境：继续 落到 %d,%d%s", pos[0], pos[1], old ? "（测试：没给目标房间）" : "");
        else LOG("【红】秘境：继续 没能落到离开时的位置 %d,%d（房间 %s），留在落点", L.tp_x, L.tp_y, !rs ? "载入不了" : !rr ? "有、附近找不到空地" : "有、有空地、传送不成");
        L.tp_lv = 0; }
    if ((st == ST_RUN || st == ST_DONE) && in_rift_map(lv)) { int x, y; if (unit_xy(player, &x, &y)) { L.last_lv = lv; L.last_x = x; L.last_y = y; } }
    if (L.gift_due && st == ST_RUN && in_rift_map(lv) && !R.gift) { L.gift_due = 0; R.gift = 1; rec_save(); give_item2(G, player, "jew ", 7, 99, 392 + rnd(8), 0); say("dmRiftSayGift"); }
    { static uint32_t at[16]; static int n = -1; if (n < 0) { n = 0; const char *e = getenv("DM_RIFT_TEST_OPEN"); char b[160]; if (e) { strlcpy(b, e, sizeof b); for (char *sv, *k = strtok_r(b, ",", &sv); k && n < 16; k = strtok_r(NULL, ",", &sv)) at[n++] = (uint32_t)atoi(k); } }
      for (int i = 0; i < n; i++) if (at[i] && t >= at[i] && lv == LV_TOWN) { at[i] = 0; atomic_store(&M.menu, (st == ST_RUN || (st == ST_DONE && L.next_door)) ? MENU_CHOICE : MENU_SELECT); atomic_fetch_add(&g_menu_seq, 1); LOG("秘境：测试 DM_RIFT_TEST_OPEN 拍 %u 当作点了城里的门 → %s", t, atomic_load(&M.menu) == MENU_CHOICE ? "继续 / 放弃" : "选层页"); } }   // 测试：拍 N 在城里当作点了门（脚本点门会被 NPC / 营地遮挡） | Test: at tick N in town, act as if the door was tapped (a scripted tap on the door gets blocked by NPCs / the camp)
    if (L.pend) { uint64_t du = find_unit(G, 2, L.pend); int px, py, dx, dy;
        if (!du || t - L.pend_at > 250) L.pend = 0;
        else if (unit_xy(player, &px, &py) && unit_xy(du, &dx, &dy) && abs(px - dx) <= 4 && abs(py - dy) <= 4) { L.pend = 0;
            int is_next = du && *(uint32_t *)(uintptr_t)(du + 8) == L.next_door;
            if (du && *(uint32_t *)(uintptr_t)(du + 8) == L.town_door) { LOG("秘境：点了守护者处的回城门 → 回哈洛加斯"); L.lv_prev = LV_TOWN; warp(G, player, LV_TOWN); return; }
            if (is_next) { L.next_door = L.town_door = 0; int nt = next_tier(); LOG("秘境：点了守护者处的门，直接开第 %d 层", nt); start_rift(G, player, t, nt); return; }
            // 城里的门：没有进行中的秘境 → 选层页；进行中（正在打，或打完还没进下一层）→ 继续 / 放弃
            // The door in town: no rift in progress -> level selection page; in progress (fighting, or finished but not yet entered the next level) -> continue / abandon
            atomic_store(&M.menu, (st == ST_RUN || (st == ST_DONE && L.next_door)) ? MENU_CHOICE : MENU_SELECT); atomic_fetch_add(&g_menu_seq, 1); LOG("秘境：点城里的门 → %s", atomic_load(&M.menu) == MENU_CHOICE ? "继续 / 放弃" : "选层页"); return; } }
    // 守护者处的两扇门按「类号 + 位置」认、并保持开着：人回城再回来，那张图的房间卸载重建，门还在但单位号变了、模式也回到关着（不可点）——
    // Mac 实测回来后点下一层的门「没有目标，不发令」，就是玩家说的「回去的门也不动」。在这张图里每拍核一遍：单位号对上、模式不是 2 就再开
    // The two doors at the guardian are identified by "class id + position" and kept open: when the player goes back to town and returns, that map's rooms are unloaded and rebuilt; the doors are still there but their unit ids have changed and their mode is back to closed (not clickable) --
    // measured on Mac: after returning, tapping the next-level door gave 「没有目标，不发令」(no target, no command issued), which is what the player described as 「回去的门也不动」(the door back does nothing either). While on this map check every tick: if the unit id matches and the mode is not 2, open it again
    if (st == ST_DONE && lv == L.nd_lv && (L.next_door || L.town_door)) for (int b = 0; b < 128; b++) for (uint64_t u = rd(G + 0x2230 + 2 * 0x400 + 8 * b); u; u = rd(u + 0x158)) {
        uint32_t cls = *(uint32_t *)(uintptr_t)(u + 4), id = *(uint32_t *)(uintptr_t)(u + 8); int x, y; if ((cls != RIFT_OBJ && cls != TOWN_OBJ) || !unit_xy(u, &x, &y) || abs(x - L.nd_x) > 32 || abs(y - L.nd_y) > 32) continue;
        uint32_t *mine = cls == RIFT_OBJ ? &L.next_door : &L.town_door; if (!*mine) continue;
        if (*mine != id) { LOG("秘境：守护者处的%s门单位号 %u → %u（房间重建过）", cls == RIFT_OBJ ? "下一层" : "回城", *mine, id); *mine = id; }
        if (*(uint32_t *)(uintptr_t)(u + 0xc) != 2) { LOG("秘境：守护者处的%s门模式 %u → 2（重新开着）", cls == RIFT_OBJ ? "下一层" : "回城", *(uint32_t *)(uintptr_t)(u + 0xc)); ((void (*)(uint64_t, int))(B + 0x5ca8a0))(u, 2); } }
    if ((st == ST_RUN || st == ST_DONE) && in_rift_map(lv)) level_box(G, lv, level_of(player));
    if (st == ST_DONE) { region_set(G, 1); if (in_rift_map(lv)) presets_skip(level_of(player)); }   // 状态对账：这几张图已结算 → 不填怪、不放预设怪（区域对象 / 预设记录重建后也保持） | State reconciliation: these maps are completed -> no monster population, no preset monsters (kept even after region objects / preset records are rebuilt)
    // 结算后清怪（「完成后清怪」）。状态对账：已结算的图里没有活的敌对怪——
    //   在场的：服务端怪物表里本图、活着、没有主人（单位标志 +0x124 位 31）、阵营 0 的，用原版杀怪 main+0x7d82b0(游戏, 怪, 攻击者 = 空, 1)。原版对空攻击者有自己的分支（0x7d8300 不做击杀结算，没有经验归属）；
    //     杀之前置 +0x124 位 17：原版进死亡模式的处理 main+0x68d1c0 在 0x68d248 读它，置着就绕过宝藏表掉落（原版给召唤物 / 技能随从置的就是这一位；不置时实测清 88 只多掉 40 件）。
    //   卸载房间里存着的：见 saved_purge。新填的 / 预设点上的：见上面 region_set、presets_skip。分数只在进行中计、积分牌见 dm_rift_swept 不计
    // Clearing monsters after completion ("clear monsters on completion"). State reconciliation: a completed map has no living hostile monsters --
    //   Present ones: those in the server monster table that are on this map, alive, without an owner (unit flags +0x124 bit 31) and of alignment 0 are killed with the original monster kill main+0x7d82b0(game, monster, attacker = null, 1). The original game has its own branch for a null attacker (0x7d8300 skips kill settlement, no experience attribution);
    //     before killing, set +0x124 bit 17: the original handler for entering death mode main+0x68d1c0 reads it at 0x68d248 and, when set, bypasses the treasure class drop (this is the very bit the original game sets on summons / skill minions; without it, clearing 88 monsters was measured to drop 40 extra items).
    //   Those stored in unloaded rooms: see saved_purge. Newly populated ones / those on preset points: see region_set and presets_skip above. Score is only counted while in progress; the kill board does not count them, see dm_rift_swept
    if (st == ST_DONE) { saved_purge(G); if (in_rift_map(lv)) { static uint32_t n_tot, chk_at; static int exp0, it0; int nk = 0;
        for (int b = 0; b < 128; b++) for (uint64_t u = rd(G + 0x2230 + 1 * 0x400 + 8 * b), nx; u; u = nx) { nx = rd(u + 0x158); uint32_t m = *(uint32_t *)(uintptr_t)(u + 0xc), fl = *(uint32_t *)(uintptr_t)(u + 0x124);
            if (m == 0 || m == 12 || (fl & 0x80000000u) || any_level(u) != lv || unit_align(u) != 0) continue;
            if (!n_tot && !nk) { exp0 = gs(player, 13); it0 = ground_items(G); }
            g_swept[*(uint32_t *)(uintptr_t)(u + 8) & 1023] = *(uint32_t *)(uintptr_t)(u + 8); *(uint32_t *)(uintptr_t)(u + 0x124) = fl | 0x20000; ((void (*)(uint64_t, uint64_t, uint64_t, int))(B + 0x7d82b0))(G, u, 0, 1); nk++; }
        if (nk) { if (!n_tot) chk_at = t + 75; n_tot += nk; LOG("秘境：拍 %u 清掉在场的敌对怪 %d 只（本局累计 %u）", t, nk, n_tot); }
        if (chk_at && t >= chk_at) { chk_at = 0; LOG("秘境：清怪核对（首批后 75 拍）经验 %d → %d，地上物品 %d → %d 件", exp0, gs(player, 13), it0, ground_items(G)); } } }
    if (st != ST_RUN) return;
    { uint32_t pm = *(uint32_t *)(uintptr_t)(player + 0xc); int d = pm == 0 || pm == 17;   // 玩家模式 0 死亡 / 17 已死：秘境里死一次记一次 | Player mode 0 dying / 17 dead: each death in the rift is recorded once
      if (d && !L.dead) { R.deaths++; rec_save(); LOG("秘境：第 %d 层里死亡（累计 %d 次）", K.tier, R.deaths); } L.dead = d; }

    // 串图：从第 i 张走出口去了别的关卡（城里除外）→ 进第 i+1 张；最后一张走出去就回最后一张。
    // 点出口砖（洞口 / 楼梯 / 入口物件）与按关卡号换关（小站 / 传送门）在原版换关的入口就把目的地换掉（svc_rift_exit / rift_level_obs），只发生一次换关，走不到这里；
    // 这里只剩「没有原版换关」的走法（沿开放地形走过关卡边界）：人已在新关卡，客户端关卡号对上了就送，这时没有进行中的换关
    // Map chaining: leaving map i through an exit to another level (except town) -> enter map i+1; walking out of the last map returns to the last map.
    // Tapping an exit tile (cave mouth / stairs / entrance object) and level changes by level id (waypoint / portal) get their destination swapped right at the original game's level-change entry (svc_rift_exit / rift_level_obs), so only one level change happens and this code is not reached;
    // what remains here is only the path "without an original level change" (walking across the level boundary over open terrain): the player is already in the new level and is sent once the client level id matches, at which point no level change is in progress
    if (lv != L.lv_prev) { int was = L.lv_prev; L.lv_prev = lv; L.redir = 0;
        if (in_rift_map(lv)) { for (int i = 0; i < K.nmaps; i++) if (K.maps[i] == lv) K.cur = i; }
        else if (!dm_hud_area_is_town(lv) && in_rift_map(was)) { L.redir = next_map_index() + 1; LOG("秘境：从关卡 %d 走过边界到了 %d（没有经过原版换关）", was, lv); } }
    if (L.redir) { if (atomic_load(&g_cli_lv) != lv) return;
        int nx = L.redir - 1; L.redir = 0; LOG("秘境：拍 %u 送进第 %d 张图 %d", t, nx + 1, K.maps[nx]); K.cur = nx; L.lv_prev = K.maps[nx]; warp(G, player, K.maps[nx]); return; }

    // 新刷出的敌对怪按层改属性（9 层起）。测试开关 DM_RIFT_TEST_LOWHP=1：改成生命 1 点（按住主攻击键一路打死，验计分 / 守护者 / 涨层）
    // Newly spawned hostile monsters get their stats changed by level (from level 9). Test switch DM_RIFT_TEST_LOWHP=1: change to 1 life point (hold the main attack button and kill all the way through; verifies scoring / guardian / level increase)
    static int lowhp = -1; if (lowhp < 0) lowhp = getenv("DM_RIFT_TEST_LOWHP") != NULL;
    if (K.tier >= 9 || lowhp) { static uint32_t r_; uint32_t w = atomic_load(&g_newq_w); if (w - r_ > NQ) r_ = w - NQ; int lt = target_level(player);
        for (; r_ != w; r_++) { uint32_t id = atomic_load(&g_newq[r_ % NQ]); uint64_t u = find_unit(G, 1, id); if (u && id != K.guard) { if (K.tier >= 9) scale_monster(G, u, lt, dm_rift_tier_level(9, gs(player, 12))); if (lowhp) ss(u, 6, 256); dmg_probe_add(id, t); } } }
    dmg_probe_pump(G, t);
    // 测试读数 DM_RIFT_TEST_ILVL=1：每 10 秒数一次地上的物品（服务端物品表 G+0x2e30，模式 3），报物品等级（数据块 +0x38）范围——验 B 方案下掉落物品等级跟不跟改过的怪物等级
    // Test readout DM_RIFT_TEST_ILVL=1: every 10 seconds count the items on the ground (server item table G+0x2e30, mode 3) and report the item level (data block +0x38) range -- verifies whether drop item levels follow the modified monster level under plan B
    { static int il = -1; if (il < 0) il = getenv("DM_RIFT_TEST_ILVL") != NULL;
      if (il && t % 250 == 0) { int n = 0, mn = 999, mx = 0; for (int bb = 0; bb < 128; bb++) for (uint64_t it = rd(G + 0x2e30 + 8 * bb); it; it = rd(it + 0x158)) { uint64_t d = rd(it + 0x10); if (!d || *(uint32_t *)(uintptr_t)(it + 0xc) != 3) continue; int v = *(int32_t *)(uintptr_t)(d + 0x38); n++; if (v < mn) mn = v; if (v > mx) mx = v; }
          LOG("秘境：测试读数 拍 %u 地上物品 %d 件，物品等级 %d–%d", t, n, n ? mn : 0, mx); } }

    // 测试开关 DM_RIFT_TEST_PULL=1：每 3 秒用原版传送 0x7cc530 把人放到一只还活着的敌对怪（客户端认的）旁边——脚本按主攻击键就能一路打（验计分 / 守护者）
    // Test switch DM_RIFT_TEST_PULL=1: every 3 seconds use the original game's teleport 0x7cc530 to put the player next to a still-living hostile monster (as recognized by the client) -- a script only has to press the main attack button to fight all the way through (verifies scoring / guardian)
    { static int pull = -1; if (pull < 0) pull = getenv("DM_RIFT_TEST_PULL") != NULL;
      if (pull && t % 38 == 0 && in_rift_map(lv) && K.guard && !atomic_load(&K.guard_dead)) { uint64_t g = find_unit(G, 1, K.guard); int x, y; if (g && unit_xy(g, &x, &y)) dm_teleport(B, G, player, unit_room(g), x + 2, y + 2); }
      else if (pull && t % 38 == 0 && in_rift_map(lv)) { uint32_t w = atomic_load(&g_newq_w); int px, py, bd = 1 << 30, bx = 0, by = 0; unit_xy(player, &px, &py);   // 最近的那只 | The nearest one
          for (uint32_t k = 0; k < NQ && k < w; k++) { uint64_t u = find_unit(G, 1, atomic_load(&g_newq[(w - 1 - k) % NQ])); if (!u || unit_level(u) != lv) continue; uint32_t m = *(uint32_t *)(uintptr_t)(u + 0xc); if (m == 0 || m == 12) continue;
              int x, y; if (!unit_xy(u, &x, &y)) continue; int d = abs(x - px) + abs(y - py); if (d < bd) { bd = d; bx = x; by = y; } }
          if (bd < (1 << 30) && bd > 3) { uint64_t lvp = level_of(player); int32_t pos[2] = { bx + 1, by + 1 }; uint64_t rs = room_at(lvp, pos[0], pos[1]), rr = rs ? free_spot(G, rs, pos) : 0;
              int ok = dm_teleport(B, G, player, rr, pos[0], pos[1]); static int ln; if (ln++ < 5) LOG("秘境：测试拉人 → %d,%d（最近的怪 %d 格）%s", pos[0], pos[1], bd, ok ? "成" : "不成"); } } }
    // 计时
    // Timer
    { uint64_t now = guest_ns();
      if (!atomic_load(&K.timeout) && now - K.t0_ns > (uint64_t)RIFT_SECS * 1000000000ull) { atomic_store(&K.timeout, 1); say("dmRiftSayTimeout"); } }
    // 守护者
    // Guardian
    if (!K.guard && atomic_load(&K.score) >= atomic_load(&K.need) && in_rift_map(lv)) {
        int x, y; unit_xy(player, &x, &y); int32_t pos[2] = { x + 6, y + 6 }; uint64_t rr = free_spot(G, unit_room(player), pos);
        K.guard_cls = k_guard[rnd(5)];
        uint64_t u = rr ? ((uint64_t (*)(uint64_t, uint64_t, int, int, int, int, int, int, uint64_t))(B + 0x7ce630))(G, rr, 1, K.guard_cls, pos[0], pos[1], 1, 0, 0x10) : 0;
        if (u) { K.guard = *(uint32_t *)(uintptr_t)(u + 8); if (lowhp) ss(u, 6, 256); else if (K.tier >= 9) scale_monster(G, u, target_level(player) + 3, dm_rift_tier_level(9, gs(player, 12)) + 3);   // 守护者 = 本层怪物等级 + 3（110 封顶），起算层也按 + 3 算，同吃本层总倍率 | Guardian = this level's monster level + 3 (capped at 110); the baseline level is computed with + 3 as well, and the same total multiplier of this level applies
            say("dmRiftSayGuardian"); LOG("秘境：拍 %u 刷守护者 类号 %d id %u 等级 %d 生命 %d", t, K.guard_cls, K.guard, gs(u, 12), gs(u, 7) >> 8); } }
    if (K.guard && !atomic_load(&K.guard_dead)) { uint64_t u = find_unit(G, 1, K.guard);
        if (u) { uint32_t m = *(uint32_t *)(uintptr_t)(u + 0xc); if (m == 0 || m == 12) { atomic_store(&K.guard_dead, 1); LOG("秘境：拍 %u 守护者死亡", t); finish(G, player, u); } } }
}
