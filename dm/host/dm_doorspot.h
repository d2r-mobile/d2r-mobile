// 秘境结算处两扇门的落点（纯算法，不碰游戏内存；dm_rift.c 用，tests/t_doorspot.c 测）。
// 要保证的事：门落在玩家「走得到」的格上。原先把「人背对守护者走 10 格、左右各错 5 格」直接交给原版找空地 main+0x7160d0（掩码 0x400 = 只看物件），
// 墙位（0x1）不在掩码里，也没有任何可达检查——洞穴 / 地穴这类窄图里人贴着墙站，10 格外就是墙里或图外（图：地穴第一层第 26 层结算，两扇门都在黑区）。
// 做法：以人为中心取一个窗口，blk[] = 这一格挡不挡玩家（调用方按玩家自己的碰撞掩码填，窗口外一律算挡）；从人脚下那一格按「玩家占一个十字」的走法做广搜，
// 搜得到的格才是候选 = 必为可达。候选里按三档取离理想点最近的：
//   档 0：周围 2 格（5 × 5）全是可达格——门身不贴墙、人能绕着走；档 1：周围 1 格（3 × 3）；档 2：只要可达，改成离人最近（窄到摆不开时退到脚下最近的可走格）。
// 都不成立（人被围死在一格里）就落在人脚下那一格。
// Landing points of the two doors at the rift completion (pure algorithm, does not touch game memory; used by dm_rift.c, tested by tests/t_doorspot.c).
// What must be guaranteed: the doors land on cells the player "can walk to". Previously "the player walks 10 cells with their back to the guardian, offset 5 cells to the left and to the right" was handed directly to the original game's free-ground search main+0x7160d0 (mask 0x400 = objects only),
// the wall bit (0x1) is not in the mask and there is no reachability check at all -- in narrow maps such as the Cave / the Pit the player stands right against a wall, and 10 cells away is inside the wall or outside the map (figure: completion of rift level 26 on Pit Level 1, both doors in the black area).
// Approach: take a window centered on the player, blk[] = whether this cell blocks the player (the caller fills it by the player's own collision mask; everything outside the window counts as blocked); from the cell at the player's feet do a breadth-first search with the "player occupies a cross" way of walking,
// and only the cells the search reaches are candidates = necessarily reachable. Among the candidates take the one nearest to the ideal point, in three tiers:
//   tier 0: the 2 cells around it (5 × 5) are all reachable cells -- the door body does not touch a wall and the player can walk around it; tier 1: the 1 cell around it (3 × 3); tier 2: reachable is enough, switched to nearest to the player (when too narrow to fit, fall back to the nearest walkable cell by the feet).
// If none holds (the player is boxed into a single cell), land on the cell at the player's feet.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { DS_R = 40, DS_W = 2 * DS_R + 1, DS_N = DS_W * DS_W };   // 窗口半径 40 格：门的理想点在 10 格外再错开 5 格；闯入首领落在 24–40 格（ds_pick_ring） | Window radius 40 tiles: the door's ideal point is beyond 10 tiles, offset by another 5; the invading boss lands at 24–40 tiles (ds_pick_ring)
enum { DS_FAR = 36 };       // 档 0 / 1 的门离人最多走这么多步（直线约 11 格；绕过一道墙才到的不要） | doors of tier 0 / 1 are at most this many steps of walking from the player (about 11 cells in a straight line; ones reached only by going around a wall are not wanted)
enum { DS_SELF_GAP = 3 };   // 档 0 / 1 的门离人至少 3 格（不压在人和脚下的首通奖励上） | doors of tier 0 / 1 are at least 3 cells from the player (not on top of the player and the first-clear reward at the player's feet)
struct ds_grid { uint8_t blk[DS_N]; int16_t dist[DS_N]; int reach; };   // blk 调用方填；dist / reach 由 ds_flood 算（-1 = 走不到） | blk is filled by the caller; dist / reach are computed by ds_flood (-1 = unreachable)

static inline int ds_blk(const struct ds_grid *g, int ix, int iy) { return ix < 0 || iy < 0 || ix >= DS_W || iy >= DS_W || g->blk[iy * DS_W + ix]; }
// 玩家占一个十字（中心 + 四邻），与点地寻路同口径（dm_world.c wp_free）
// The player occupies a cross (center + four neighbors), same convention as ground-tap pathfinding (dm_world.c wp_free)
static inline int ds_free(const struct ds_grid *g, int ix, int iy) { return !(ds_blk(g, ix, iy) || ds_blk(g, ix + 1, iy) || ds_blk(g, ix - 1, iy) || ds_blk(g, ix, iy + 1) || ds_blk(g, ix, iy - 1)); }
static inline int ds_dist(const struct ds_grid *g, int ix, int iy) { return ix < 0 || iy < 0 || ix >= DS_W || iy >= DS_W ? -1 : g->dist[iy * DS_W + ix]; }
// 从窗口中心（人脚下）广搜。起点格不要求十字都空：人此刻就站在那里
// relax：离起点不超过这么多步的格只要求「本身不挡」（人此刻就站在这里，入口窄道两边墙凹凸不齐时十字占位会把四邻全判成挡；闯入首领用 2，结算门用 0）
// Breadth-first search from the window center (the player's feet). The start tile does not require its cross to be clear: the player is standing there right now
// relax: tiles within this many steps of the start only need to be "not blocked themselves" (the player is standing here right now, and in a narrow entrance passage with uneven walls the cross footprint would mark all four neighbors as blocked; the invading boss uses 2, the settlement door uses 0)
static inline void ds_flood_r(struct ds_grid *g, int relax) {
    static uint16_t q[DS_N]; int h = 0, t = 0; memset(g->dist, 0xff, sizeof g->dist); g->reach = 0;
    g->dist[DS_R * DS_W + DS_R] = 0; q[t++] = (uint16_t)(DS_R * DS_W + DS_R);
    while (h < t) { int c = q[h++], cx = c % DS_W, cy = c / DS_W; g->reach++;
        static const int d4[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
        for (int k = 0; k < 4; k++) { int nx = cx + d4[k][0], ny = cy + d4[k][1]; if (ds_dist(g, nx, ny) != -1 || nx < 0 || ny < 0 || nx >= DS_W || ny >= DS_W || !(ds_free(g, nx, ny) || (g->dist[c] < relax && !ds_blk(g, nx, ny)))) continue;
            g->dist[ny * DS_W + nx] = (int16_t)(g->dist[c] + 1); q[t++] = (uint16_t)(ny * DS_W + nx); } }
}
static inline void ds_flood(struct ds_grid *g) { ds_flood_r(g, 0); }
static inline int ds_clear(const struct ds_grid *g, int ix, int iy, int k) { for (int dy = -k; dy <= k; dy++) for (int dx = -k; dx <= k; dx++) if (ds_dist(g, ix + dx, iy + dy) < 0) return 0; return 1; }
// 取一扇门的落点。坐标都是相对人的格偏移（人 = 0,0）。(wx, wy) 理想点；have_o 时 (ox, oy) 是已经定下的另一扇门，新门离它至少 gap 格（档 2 降到 2 格）。
// 返回用了第几档（0 / 1 / 2；3 = 落在人脚下），落点写进 *rx / *ry
// 闯入首领的落点：离人（切比雪夫）lo–DS_R 格、走得到且不超过 maxd 步、四周 k 圈都可达（体型大的首领不卡墙）；按 k = 2、1、0 依次放宽，
// 取离理想点 (wx, wy) 最近的。返回用的 k（-1 = 没有合格格），落点写进 *rx / *ry
// Picks the landing spot for a door. Coordinates are all tile offsets relative to the player (player = 0,0). (wx, wy) is the ideal point; with have_o, (ox, oy) is the other door already placed, and the new door must be at least gap tiles from it (tier 2 lowers this to 2 tiles).
// Returns the tier used (0 / 1 / 2; 3 = at the player's feet), with the spot written to *rx / *ry
// Landing spot for the invading boss: lo–DS_R tiles from the player (Chebyshev), reachable within maxd steps, with k rings around it all reachable (so large bosses do not get stuck in walls); relaxed in order k = 2, 1, 0,
// picking the spot closest to the ideal point (wx, wy). Returns the k used (-1 = no qualifying tile), with the spot written to *rx / *ry
static inline int ds_pick_ring(const struct ds_grid *g, int wx, int wy, int lo, int maxd, int *rx, int *ry) {
    for (int k = 2; k >= 0; k--) { long best = -1; int bx = 0, by = 0;
        for (int y = -DS_R; y <= DS_R; y++) for (int x = -DS_R; x <= DS_R; x++) { int d = ds_dist(g, x + DS_R, y + DS_R), far = abs(x) > abs(y) ? abs(x) : abs(y);
            if (d < 0 || d > maxd || far < lo || !ds_clear(g, x + DS_R, y + DS_R, k)) continue;
            long s = (long)(x - wx) * (x - wx) + (long)(y - wy) * (y - wy); if (best >= 0 && s >= best) continue; best = s; bx = x; by = y; }
        if (best >= 0) { *rx = bx; *ry = by; return k; } }
    return -1;
}
static inline int ds_pick(const struct ds_grid *g, int wx, int wy, int have_o, int ox, int oy, int gap, int *rx, int *ry) {
    for (int tier = 0; tier < 3; tier++) { long best = -1; int bx = 0, by = 0, tx = tier == 2 ? 0 : wx, ty = tier == 2 ? 0 : wy;
        for (int y = -DS_R; y <= DS_R; y++) for (int x = -DS_R; x <= DS_R; x++) { int d = ds_dist(g, x + DS_R, y + DS_R); if (d < 0) continue;
            int far = abs(x) > abs(y) ? abs(x) : abs(y), og = !have_o ? 1 << 20 : abs(x - ox) > abs(y - oy) ? abs(x - ox) : abs(y - oy);
            if (tier < 2 ? (far < DS_SELF_GAP || d > DS_FAR || og < gap || !ds_clear(g, x + DS_R, y + DS_R, 2 - tier)) : (far < 1 || og < 2)) continue;
            long s = (long)(x - tx) * (x - tx) + (long)(y - ty) * (y - ty); if (best >= 0 && s >= best) continue; best = s; bx = x; by = y; }
        if (best >= 0) { *rx = bx; *ry = by; return tier; } }
    *rx = 0; *ry = 0; return 3;
}
