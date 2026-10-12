// 进局落在城镇小站旁（进入游戏和一键重开的出生位置都移到城镇小站旁边；
// 不精准落在小站上，而是隔一个固定的安全距离，免得点脚下尸体时误触传送站；尸体和人物落点一致）。
// 做法：改原版进局那一拍取到的出生点本身，人、尸体、佣兵由原版一次放好。
//   · 原版玩家进局 main+0x5d7f70 给玩家（0x5d8040）、给存档里带回来的尸体（0x5d8400）各取一次出生点：0x5596c0(数据版本, 幕, 关卡, 格类型 0, &x, &y, 单位尺寸) → 落点所在房间。
//     0x5596c0 = 按格类型找那一格（0x534250，顺带建好那间房）→ 在格中心周围用原版「找能站的空格」0x4d2c60 定最终位置。
//   · 这两处调用点剪成宿主调用 0x8a / 0x8b（patch_nso.py，产物 r37 起）。进局在城镇时：先按小站格（格类型 13，原版走小站进城用的就是它）取到小站旁的点 S，
//     再从小站物件中心 W 沿 W→S 方向（小站前那片空地）量出固定安全距离，用同一个「找能站的空格」定点；两处调用得到同一个中心，尸体就在人脚边。
//   · 原先是进局第 2 拍用换关 0x634000(…, 13) 把人挪到小站上：尸体留在原出生点（「死后重进游戏得跑尸体」），且第 1 拍挪会让客户端佣兵 0 属性——现在整段删掉，没有第二次落位。
//   · 只管进局那一下；局内换关、走小站进城不经过这两处调用。一键重开 = 登出再登入，新局照样走这里。
//   · 回放原版录制（DM_C2S_REPLAY）与 DM_TOWNWP_OFF=1 时原样代调：录制是从原版出生点走起的。
// 五幕城镇关卡号与小站物件类号（objects.txt 行号）：1 / 119、40 / 156、75 / 237、103 / 398、109 / 429。
// Join lands next to the town waypoint (the spawn position for both entering the game and one-tap restart is moved next to the town waypoint;
// not exactly on the waypoint but a fixed safe distance away, to avoid triggering the waypoint by mistake when tapping the corpse underfoot; the corpse and the character land at the same point).
// Approach: change the spawn point itself that the original game obtains on the join tick, and let the original game place the player, corpse and mercenary in one go.
//   · The original game's player join main+0x5d7f70 fetches a spawn point once for the player (0x5d8040) and once for the corpse brought back from the save (0x5d8400): 0x5596c0(data version, act, level, tile type 0, &x, &y, unit size) → room containing the landing point.
//     0x5596c0 = find that tile by tile type (0x534250, also building that room along the way) → settle the final position around the tile center with the original game's "find a free standable cell" 0x4d2c60.
//   · These two call sites are cut into host calls 0x8a / 0x8b (patch_nso.py, from build output r37 on). When joining in town: first get the point S next to the waypoint via the waypoint tile (tile type 13, the very one the original game uses when arriving in town by waypoint),
//     then measure the fixed safe distance from the waypoint object center W along the W→S direction (the open ground in front of the waypoint) and settle the point with the same "find a free standable cell"; both call sites get the same center, so the corpse is right at the player's feet.
//   · Previously, on tick 2 after joining, the level change 0x634000(..., 13) moved the player onto the waypoint: the corpse stayed at the original spawn point ("after dying and re-entering the game you have to run to the corpse"), and moving on tick 1 left the client mercenary with 0 stats - that whole section is now deleted, and there is no second placement.
//   · Only covers the moment of joining; in-run level changes and arriving in town by waypoint do not go through these two call sites. One-tap restart = log out and log in again, and the new run goes through here just the same.
//   · When replaying an original-game recording (DM_C2S_REPLAY) and with DM_TOWNWP_OFF=1, the call is forwarded unchanged: the recording starts walking from the original spawn point.
// Town level ids and waypoint object class ids (objects.txt row numbers) for the five acts: 1 / 119, 40 / 156, 75 / 237, 103 / 398, 109 / 429.
#include "hle.h"
#include "dm_unit.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const struct { int lv, cls; } k_town[] = { {1, 119}, {40, 156}, {75, 237}, {103, 398}, {109, 429} };
enum { TILE_WAYPOINT = 13, CHECK_AFTER = 5, SPAWN_GAP = 10, NPC_GAP = 6, OBJ_GAP = 3 };   // SPAWN_GAP：落点中心离小站物件中心的格数。Mac 量命中框（2868 宽画面）：8 格时尸体框右缘到小站选择框左缘 122 像素（约 6.5 毫米），不相叠但余量小，取 10 | SPAWN_GAP: number of cells from the landing point center to the waypoint object center. Hit boxes measured on Mac (2868-wide frame): at 8 cells the corpse box's right edge is 122 pixels (about 6.5 mm) from the waypoint selection box's left edge, not overlapping but with little margin, so 10 is used

static uint8_t *main_base(void) { static uint8_t *b; if (!b) for (int i = 0; i < g_nmod; i++) if (!strcmp(g_mod[i].name, "main")) b = (uint8_t *)g_mod[i].base; return b; }
static uint64_t rd(uint64_t a) { return a ? *(volatile uint64_t *)(uintptr_t)a : 0; }
static uint64_t level_of(uint64_t u) { return dm_unit_level_ptr(u); }   // 路径 → 房间 → 关卡 | Path → room → level
static void unit_xy(uint64_t u, int *x, int *y) { uint64_t p = rd(u + 0x38); *x = p ? *(uint16_t *)(uintptr_t)(p + 2) : 0; *y = p ? *(uint16_t *)(uintptr_t)(p + 6) : 0; }
static int town_cls(int lv) { for (unsigned i = 0; i < sizeof k_town / sizeof k_town[0]; i++) if (k_town[i].lv == lv) return k_town[i].cls; return 0; }
// 关卡房间的预设单位记录（关卡 +0x10 房间链、下一间 +0x48；房间 +0x60 / +0x64 格坐标 ÷ 5；房间 +0x98 记录链、下一条 +0x10；记录 +0x20 类型 1 怪或 NPC / 2 物件、+4 类号、+8 / +0x24 房内格坐标）：
// 找类号为 cls 的物件（小站），以及离 (x, y) 最近的某类预设点有多远（skip = 不算的那条记录）
// Preset unit records of a level's rooms (level +0x10 room chain, next room +0x48; room +0x60 / +0x64 cell coordinates ÷ 5; room +0x98 record chain, next record +0x10; record +0x20 type 1 monster or NPC / 2 object, +4 class id, +8 / +0x24 in-room cell coordinates):
// find the object with class id cls (the waypoint), and how far the nearest preset point of a given type is from (x, y) (skip = the record to exclude)
static uint64_t preset_find(uint64_t lvt, int cls, int *x, int *y) { int n = 0;
    for (uint64_t r = dm_level_rooms(lvt); r && n < 2000; r = rd(r + 0x48), n++) for (uint64_t u = rd(r + 0x98), k = 0; u && k < 256; u = rd(u + 0x10), k++)
        if (*(int32_t *)(uintptr_t)(u + 0x20) == 2 && *(int32_t *)(uintptr_t)(u + 4) == cls) { *x = *(int32_t *)(uintptr_t)(r + 0x60) * 5 + *(int32_t *)(uintptr_t)(u + 8); *y = *(int32_t *)(uintptr_t)(r + 0x64) * 5 + *(int32_t *)(uintptr_t)(u + 0x24); return u; }
    return 0; }
static int preset_gap(uint64_t lvt, int type, int x, int y, uint64_t skip) { int best = 1 << 30, n = 0;
    for (uint64_t r = dm_level_rooms(lvt); r && n < 2000; r = rd(r + 0x48), n++) for (uint64_t u = rd(r + 0x98), k = 0; u && k < 256; u = rd(u + 0x10), k++) { if (u == skip || *(int32_t *)(uintptr_t)(u + 0x20) != type) continue;
        int dx = abs(*(int32_t *)(uintptr_t)(r + 0x60) * 5 + *(int32_t *)(uintptr_t)(u + 8) - x), dy = abs(*(int32_t *)(uintptr_t)(r + 0x64) * 5 + *(int32_t *)(uintptr_t)(u + 0x24) - y), d = dx > dy ? dx : dy; if (d < best) best = d; }
    return best; }

static struct { int on, lv, wx, wy, sx, sy, calls; uint64_t room; } g_sp;   // 本局进局落点：玩家那次定下的点（尸体那次直接用它）与给每拍那边记日志的读数 | This run's join landing point: the point settled in the player's call (the corpse's call uses it directly) and the readings for the per-tick side to log
// 宿主调用 0x8a（给玩家）/ 0x8b（给尸体）：regs = 0x5596c0 的参数（x0 数据版本、x1 幕、w2 关卡、w3 格类型、x4 &x、x5 &y、w6 单位尺寸），返回值 x0 = 落点所在房间
// Host call 0x8a (for the player) / 0x8b (for the corpse): regs = arguments of 0x5596c0 (x0 data version, x1 act, w2 level, w3 tile type, x4 &x, x5 &y, w6 unit size), return value x0 = room containing the landing point
void svc_join_spawn(uint64_t *regs, int corpse) {
    uint8_t *B = main_base(); typedef uint64_t (*spot_fn)(int, uint64_t, int, int, int32_t *, int32_t *, int);
    spot_fn orig = (spot_fn)(B + 0x5596c0); int ver = (int)(uint32_t)regs[0], lv = (int)(uint32_t)regs[2], type = (int)(uint32_t)regs[3], size = (int)(uint32_t)regs[6]; uint64_t act = regs[1];
    int32_t *px = (int32_t *)(uintptr_t)regs[4], *py = (int32_t *)(uintptr_t)regs[5]; int cls = town_cls(lv);
    const char *off = getenv("DM_TOWNWP_OFF"); int no = off && *off && strcmp(off, "0");   // 测试开关 DM_TOWNWP_OFF=1 不改（uireg 公共环境带 =1，wp 局再写 =0） | Test switch DM_TOWNWP_OFF=1 leaves it unchanged (the uireg common environment carries =1, and the wp run then sets =0)
    if (type != 0 || !cls || no || getenv("DM_C2S_REPLAY")) { regs[0] = orig(ver, act, lv, type, px, py, size);
        if (!corpse && !g_sp.calls++) LOG("城镇小站落点：%s，原版出生点 %d,%d（关卡 %d）", !cls ? "进局不在城镇" : no ? "DM_TOWNWP_OFF" : "回放原版录制", *px, *py, lv);
        return; }
    if (corpse && g_sp.on && g_sp.lv == lv && g_sp.room) {   // 尸体：就用玩家那次定下的点，在它 3 格内找能站的空格——两次各自挑方向会落到不同的两处（Mac 实测第二、四幕尸体离人 9–11 格） | Corpse: just use the point settled in the player's call and find a free standable cell within 3 cells of it - picking directions separately in the two calls would land in two different places (measured on Mac: in acts 2 and 4 the corpse was 9–11 cells from the player)
        int32_t pos[2] = { g_sp.sx, g_sp.sy }; uint64_t r2 = ((uint64_t (*)(uint64_t, int32_t *, uint64_t, int, int, int, int, int, int))(B + 0x4d2c60))(g_sp.room, pos, 0, size, 0x1c09, 0x1c09, 0, 3, 1);
        if (r2) { *px = pos[0]; *py = pos[1]; regs[0] = r2; g_sp.calls++; LOG("城镇小站落点：给尸体取点 → %d,%d（人的落点 %d,%d）", pos[0], pos[1], g_sp.sx, g_sp.sy); return; }
        LOG("【红】城镇小站落点：人的落点 %d,%d 旁 3 格内给尸体找不到空格，尸体按小站另取", g_sp.sx, g_sp.sy); }
    int32_t sx = 0, sy = 0; uint64_t room = orig(ver, act, lv, TILE_WAYPOINT, &sx, &sy, size);   // S：小站格旁原版给的落点（顺带把小站那间房建好） | S: the landing point the original game gives next to the waypoint tile (also builds the waypoint's room along the way)
    if (!room) { regs[0] = orig(ver, act, lv, type, px, py, size); LOG("【红】城镇小站落点：关卡 %d 取不到小站格，退回原版出生点 %d,%d", lv, *px, *py); return; }
    uint64_t lvt = rd(rd(room + 0x18) + 0x90); int wx = 0, wy = 0; uint64_t wrec = lvt ? preset_find(lvt, cls, &wx, &wy) : 0;
    int32_t bx = sx, by = sy; uint64_t broom = room; int found = 0;
    if (wrec) {   // 从小站中心 W 沿 W→S 起、每次转 45 度，取第一个合规的方向：安全距离处原版找得到能站的空格（半径 2 格内）、离小站仍 ≥ 安全距离 − 1、离 NPC 预设点 ≥ NPC_GAP、离别的物件预设点 ≥ OBJ_GAP | Starting from waypoint center W along W→S and turning 45 degrees each time, take the first direction that qualifies: at the safe distance the original game can find a free standable cell (within a 2-cell radius), still ≥ safe distance − 1 from the waypoint, ≥ NPC_GAP from NPC preset points, ≥ OBJ_GAP from other object preset points
        float vx = (float)(sx - wx), vy = (float)(sy - wy), n = sqrtf(vx * vx + vy * vy); if (n < 0.5f) { vx = 1; vy = 1; n = sqrtf(2); } vx /= n; vy /= n;
        for (int k = 0; k < 8 && !found; k++) { float a = (float)(k % 2 ? -(k + 1) / 2 : k / 2) * 0.7853982f, cx = vx * cosf(a) - vy * sinf(a), cy = vx * sinf(a) + vy * cosf(a);
            int32_t pos[2] = { wx + (int)lroundf(cx * SPAWN_GAP), wy + (int)lroundf(cy * SPAWN_GAP) };
            uint64_t r2 = ((uint64_t (*)(uint64_t, int32_t *, uint64_t, int, int, int, int, int, int))(B + 0x4d2c60))(room, pos, 0, size, 0x1c09, 0x1c09, 0, 2, 1); if (!r2) continue;
            int dw = abs(pos[0] - wx) > abs(pos[1] - wy) ? abs(pos[0] - wx) : abs(pos[1] - wy);
            if (dw < SPAWN_GAP - 1 || preset_gap(lvt, 1, pos[0], pos[1], 0) < NPC_GAP || preset_gap(lvt, 2, pos[0], pos[1], wrec) < OBJ_GAP) continue;
            bx = pos[0]; by = pos[1]; broom = r2; found = 1; } }
    *px = bx; *py = by; regs[0] = broom;
    g_sp.calls++; if (!corpse) { g_sp.on = 1; g_sp.lv = lv; g_sp.wx = wx; g_sp.wy = wy; g_sp.sx = bx; g_sp.sy = by; g_sp.room = broom;
        if (found) LOG("城镇小站落点：关卡 %d 小站 %d,%d，进局落点 %d,%d（离小站横 %d 纵 %d 格；小站格旁原版点 %d,%d）", lv, wx, wy, bx, by, bx - wx, by - wy, sx, sy);
        else LOG("【红】城镇小站落点：关卡 %d 小站 %s，安全距离 %d 格处八个方向都不合规，落在小站格旁 %d,%d", lv, wrec ? "找到了" : "的预设记录没找到", SPAWN_GAP, bx, by); }
    else LOG("城镇小站落点：给尸体取点 → %d,%d", bx, by);
}

void dm_townwp_tick(uint64_t G, uint64_t player, uint8_t *B) {   // 服务端拍里调（diag.c）：只记读数——进局后第 5 拍人、尸体各在哪、离小站多远 | Called in the server tick (diag.c): only logs readings - at tick 5 after joining, where the player and the corpse each are and how far from the waypoint
    static uint64_t g0, p0; static uint32_t last_t, join_t; static int logged; (void)B;
    uint32_t t = *(volatile uint32_t *)(uintptr_t)(G + 0x170);
    if (G != g0 || player != p0 || t < last_t) { g0 = G; p0 = player; join_t = t; logged = 0; }   // 新的一局（退出再进常复用同一块 G，拍号倒退也算） | A new run (exiting and re-entering often reuses the same G; a tick number going backwards also counts)
    last_t = t; if (logged || t - join_t < CHECK_AFTER) return; logged = 1;
    uint64_t lv = level_of(player); int id = lv ? *(int32_t *)(uintptr_t)(lv + 0x1f8) : 0, x, y; unit_xy(player, &x, &y);
    if (!g_sp.on) { if (town_cls(id) && !g_sp.calls) LOG("城镇小站落点：这个产物不带进局出生点的剪口（要 r37 起），人在原版出生点 %d,%d", x, y); g_sp.calls = 0; return; }
    LOG("城镇小站落点：拍 %u 人在 %d,%d，小站 %d,%d（差 %d,%d）", t, x, y, g_sp.wx, g_sp.wy, x - g_sp.wx, y - g_sp.wy);
    for (int b = 0; b < 128; b++) for (uint64_t u = rd(G + 0x2230 + 8 * b); u; u = rd(u + 0x158)) { if (u == player || *(uint32_t *)(uintptr_t)(u + 0xc) != 17) continue;   // 玩家表里模式 17 = 尸体 | In the player table, mode 17 = corpse
        int cx, cy; unit_xy(u, &cx, &cy); LOG("城镇小站落点：拍 %u 尸体（单位 id %u）在 %d,%d，离人 %d,%d、离小站 %d,%d", t, *(uint32_t *)(uintptr_t)(u + 8), cx, cy, cx - x, cy - y, cx - g_sp.wx, cy - g_sp.wy); }
    g_sp.on = 0; g_sp.calls = 0; g_sp.room = 0;
}
