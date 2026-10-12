#include <stdint.h>
// 「极乐世界」：建角时勾选、之后不可改的按角色开关。该角色在局内时：暗金掉率 ×5、套装 ×3（品质比例表）。实现与依据见 dm_elysium.c
// 「极乐世界」(Elysium): a per-character switch checked at character creation and unchangeable afterwards. While that character is in a run: unique drop rate ×5, sets ×3 (quality ratio table). For the implementation and its basis see dm_elysium.c
#pragma once
void dm_elysium_arm(int on);     // 建角页的勾选项调：1 = 这次建出来的角色是极乐世界角色，0 = 不是。只对紧接着的一次建角生效，用掉即清 | Called by the checkbox on the character creation page: 1 = the character created this time is an Elysium character, 0 = it is not. Applies only to the immediately following character creation and is cleared once used
int dm_elysium_active(void);     // 当前这一局是不是极乐世界角色的局（表已改） | Whether the current run is the run of an Elysium character (tables changed)
void dm_elysium_pump(void);      // 游戏主线程每帧调（dm_actions_pump） | Called every frame on the game main thread (dm_actions_pump)
void dm_elysium_stash_step(uint64_t game, uint64_t player, uint8_t *main_base);   // 游戏服务端那一侧每拍调（diag.c 读玩家状态处）：新建极乐世界角色后补齐共享仓库 | Called every tick on the game server side (where diag.c reads the player state): tops up the shared stash after a new Elysium character is created
