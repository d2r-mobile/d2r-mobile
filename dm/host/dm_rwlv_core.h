// 符文之语清单的「需要等级」：纯算法部分（不碰游戏内存布局以外的东西，宿主单测 tests/t_rwlv.c 直接链）
// "Required level" in the runeword list: the pure-algorithm part (touches nothing beyond the game's memory layout; the host unit test tests/t_rwlv.c links it directly)
#pragma once
#include <stdint.h>
// 在物品表（weapons + armor + misc 合成的一张，每行 rowsize 字节，行内 +0x80 起 3 字节是物品码）里找「需求等级」那一个字节的行内偏移。
// 找不到或不唯一返回 -1。
// Find the in-row offset of the one byte holding "required level" in the item table (weapons + armor + misc combined, rowsize bytes per row, with the 3-byte item code at +0x80 in the row).
// Returns -1 when not found or not unique.
int dm_rw_find_lvoff(const uint8_t *rows, uint32_t n, uint32_t rowsize);
// 做成以后的需要等级 = 底材需求等级与各符文需求等级的最大值
// Required level once made = the maximum of the base item's required level and each rune's required level
int dm_rw_need_level(int base_req, const int *rune_req, int n);
