// 单位 → 所在关卡（宿主直接读游戏内存，服务端单位与客户端单位同一套偏移）：
//   单位 +0x38 路径 → 路径 +0x20 房间（物件 / 物品类单位的「路径」是另一种结构，房间在 +0）→ 房间 +0x18 → +0x90 关卡 → 关卡 +0x1f8 关卡号。
// 每一步都可能是空的，必须逐步判空：换幕的读取画面期间客户端玩家有路径、没有房间。
// 原先各处一口气连读四层 rd(rd(rd(rd(u + 0x38) + 0x20) + 0x18) + 0x90)，rd 只挡地址 0，房间为空时去读地址 0x18 →
// 真机 / Mac 都是 sig=11 addr=0x18（秘境里死在别的幕的图里点「继续」、换幕读取那几帧）。单测 tests/t_unit_level.c。
// Unit → the level it is in (the host reads game memory directly; server units and client units share the same offsets):
//   unit +0x38 path → path +0x20 room (for object / item class units the "path" is a different struct, with the room at +0) → room +0x18 → +0x90 level → level +0x1f8 level id.
// Every step can be NULL and must be null-checked one by one: during the act-change loading screen the client player has a path but no room.
// Previously various places read four levels in one go, rd(rd(rd(rd(u + 0x38) + 0x20) + 0x18) + 0x90); rd only guards address 0, so with a NULL room it went on to read address 0x18 →
// sig=11 addr=0x18 on both real device and Mac (in a rift, tapping 「继续」(Continue) after dying in another act's map, during those few act-change loading frames). Unit test tests/t_unit_level.c.
#ifndef DM_UNIT_H
#define DM_UNIT_H
#include <stdint.h>
static inline uint64_t dm_unit_rd64(uint64_t a) { return a ? *(volatile uint64_t *)(uintptr_t)a : 0; }
// 关卡结构指针；任何一步为空返回 0
// Level struct pointer; returns 0 if any step is NULL
static inline uint64_t dm_unit_level_ptr(uint64_t u) {
    uint64_t path = u ? dm_unit_rd64(u + 0x38) : 0, room = path ? dm_unit_rd64(path + 0x20) : 0, ex = room ? dm_unit_rd64(room + 0x18) : 0;
    return ex ? dm_unit_rd64(ex + 0x90) : 0;
}
// 关卡号；读不到（单位 / 路径 / 房间任何一步为空）返回 0 = 未知关卡
// Level id; returns 0 = unknown level when it cannot be read (unit / path / room NULL at any step)
static inline int dm_unit_level(uint64_t u) { uint64_t lv = dm_unit_level_ptr(u); return lv ? *(volatile int32_t *)(uintptr_t)(lv + 0x1f8) : 0; }
// 关卡的房间链头（关卡 +0x10，下一间 = 房间 +0x48）；关卡指针为空返回 0——拿 dm_unit_level_ptr 的结果往下遍历房间一律走这里，
// 不让「读不到关卡」变成去读地址 0x10
// Head of a level's room chain (level +0x10, next room = room +0x48); returns 0 when the level pointer is NULL - anything that walks rooms from the result of dm_unit_level_ptr must go through here,
// so that "level could not be read" never turns into reading address 0x10
static inline uint64_t dm_level_rooms(uint64_t lv) { return lv ? dm_unit_rd64(lv + 0x10) : 0; }
// 定点传送：把单位放到 (目标房间, x, y) 旁的空位并给客户端发重定位 = 原版 main+0x7cc530(游戏, 单位, 目标房间, x, y, 0, 0)。
// 目标房间必须给：原版按关卡号换关 0x634000 自己就是「0x5596c0 取落点与房间 → 0x7cc530(…, 房间, x, y, 0, 0)」（0x634168–0x634188）。
// 传 0 时只有目标就在单位所在房间附近才成，目标在另一片房间就返回失败（秘境打完回城后点「继续」落在那张图的小站上）。
// 房间为 0（目标那间没载入 / 不在这张图里）直接返回 0，不去碰运气
// Fixed-point teleport: put the unit in a free spot next to (target room, x, y) and send the client a relocation = the original main+0x7cc530(game,
// unit, target room, x, y, 0, 0).
// The target room must be given: the original change-level-by-number 0x634000 is itself "0x5596c0 gets the landing point and room → 0x7cc530(..., room,
// x, y, 0, 0)" (0x634168–0x634188).
// Passing 0 only works when the target is near the unit's current room; a target in another cluster of rooms returns failure (after finishing a Rift and
// returning to town, tapping "Continue" landed on that map's waypoint).
// A room of 0 (the target room is not loaded / not in this map) returns 0 directly without taking chances
static inline int dm_teleport(const uint8_t *main_base, uint64_t G, uint64_t unit, uint64_t room, int x, int y) {
    if (!main_base || !G || !unit || !room) return 0;
    return ((int (*)(uint64_t, uint64_t, uint64_t, int, int, int, int))(uintptr_t)(main_base + 0x7cc530))(G, unit, room, x, y, 0, 0);
}
#endif
