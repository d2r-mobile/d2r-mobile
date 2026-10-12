#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { DM_TG_FACETS=6, DM_TG_SLOTS=12, DM_TG_STRIDE=28,
       DM_TG_MAX_AGE_NS=250000000,
       DM_TG_IDENTITY_UNCONFIRMED=0, DM_TG_IDENTITY_NATIVE=1, DM_TG_IDENTITY_SYNTHETIC=2,
       DM_TG_SOURCE_NONE=0, DM_TG_SOURCE_NATIVE_GETTER=1 };
typedef struct { uint32_t logical, normal, alt; } dm_tg_mapping;
typedef struct {
    uint64_t buttons;
    uint32_t raw_skill_id;
    int slot, valid;
} dm_tg_portal;
typedef struct {
    uint64_t captured_ns, group_started_ns, scene_epoch, world_epoch, binding_identity, caller_identity, hud_identity;
    int ready, secondary_group, identity_scope;
    uint32_t group_seen;
    /* valid只证明捕获表解析完整，不证明当前武器bank、ammo或游戏施法状态。 | valid only proves the captured table parsed completely; it does not prove the current weapon bank, ammo or the game's casting state. */
    int supported, valid, source, selected_bank, getter_input_slot, observed_index;
    dm_tg_mapping mapping[DM_TG_FACETS];
    uint32_t raw_skill_id[DM_TG_SLOTS];
    dm_tg_portal portal;
} dm_tg_snapshot;
/* 只解析已复制的32位小端原表；不读取客体内存、不调用客体getter。 | Parses only the already-copied 32-bit little-endian original table; does not read guest memory and does not call guest getters. */
int dm_tg_parse(dm_tg_snapshot *out, const void *mapping, size_t mapping_bytes,
                const void *bank, size_t bank_bytes, int selected_bank, int getter_input_slot,
                int observed_index, uint32_t observed_id, uint64_t captured_ns);
typedef struct { dm_tg_snapshot pending, completed; uint32_t seen; uint64_t started_ns; } dm_tg_group;
/* 单normal/alt组的六facet完整且身份/scene一致才ready。身份未知则不ready。 | ready only when the six facets of a single normal/alt group are complete and identity/scene match. If identity is unknown, not ready. */
int dm_tg_group_observe(dm_tg_group *group, const dm_tg_snapshot *table, int game,
                        uint64_t scene_epoch, uint64_t caller_identity, uint64_t hud_identity, int identity_scope);
/* host now_ns时间域；未来/零时间戳及年龄>250ms时Portal输入归零。 | host now_ns time domain; for future/zero timestamps and age >250ms the Portal input is zeroed. */
uint64_t dm_tg_portal_mask(const dm_tg_snapshot *snapshot, uint64_t host_now_ns);
/* 生产桥：load_modules完成后的客体线程attach；UI只取锁内快照副本。 | Production bridge: attach on the guest thread after load_modules completes; the UI only takes a copy of the snapshot inside the lock. */
int dm_touch_game_attach(void);
/* 开：快照里把「绑着但玩家现在没有的技能」（如摘掉给技能的装备）按空位报；要调游戏函数，只在真游戏里开。 | On: in the snapshot, report "skills that are bound but the player does not currently have" (e.g. after taking off the gear that grants the skill) as empty slots; this calls game functions, so turn it on only in the real game. */
void dm_touch_game_live_check(int on);
int dm_touch_game_snapshot(dm_tg_snapshot *out);
void dm_touch_game_set_scene(int game_no_panel, uint64_t scene_epoch, uint64_t world_epoch);
#ifdef DM_TG_TEST_SYNTHETIC_IDENTITY
/* 仅SDK_STUB显式条件构建可提供此opaque seam；生产走strong Mach原HUD cohort。 | Only an explicitly conditional SDK_STUB build may provide this opaque seam; production uses the strong Mach original HUD cohort. */
int dm_touch_game_test_identity(const uint64_t *regs, uint64_t *caller_identity, uint64_t *hud_identity);
#endif
#ifdef __cplusplus
}
#endif
