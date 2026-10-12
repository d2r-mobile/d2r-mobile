#pragma once
#include <stddef.h>
#include <stdint.h>
#include "dm_touch_game_state.h"
typedef struct {
    uint64_t world_epoch, widget[6], hud_identity;
    uint32_t seen;
    int confirmed;
} dm_tg_hud_cohort;
typedef int (*dm_tg_read_fn)(uint64_t address, void *out, size_t bytes);
/* 同进程Mach只读，不直接解引用未知FramePointer或widget堆地址。 | Same-process Mach read-only; never dereference an unknown FramePointer or widget heap address directly. */
int dm_tg_vm_read(uint64_t address, void *out, size_t bytes);
void dm_tg_hud_reset(dm_tg_hud_cohort *cohort, uint64_t world_epoch);
/* 首组六指针只确认cohort；返回身份从下一帧六组开始，不追认替换指针。 | The first group of six pointers only confirms the cohort; identity is returned starting from the next frame's six groups, and replaced pointers are not recognized retroactively. */
extern int dm_tg_identity_reason; extern uint64_t dm_tg_identity_lr; extern uint32_t dm_tg_identity_slot;   /* 诊断用 | For diagnostics */
int dm_tg_hud_identity(dm_tg_hud_cohort *cohort, const uint64_t *regs,
                       uint64_t main_base, int game_no_panel, uint64_t world_epoch,
                       dm_tg_read_fn read_fn, uint64_t *caller, uint64_t *hud);
