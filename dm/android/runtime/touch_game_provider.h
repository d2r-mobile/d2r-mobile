// Bounded Android game scene/identity/hit provider boundary for #33.
//
// The provider is owned by the caller and must outlive the bridge. All
// callbacks run on the single Android input/core thread. A provider returning
// unavailable, a mismatched ABI/module/generation, a stale scene, or an object
// outside its declared module/range is rejected: no synthetic game scene and
// no successful action acknowledgement is manufactured.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DM_TGP_ABI_VERSION 1u
#define DM_TGP_MAX_AGE_NS 250000000ull
#define DM_TGP_MAX_MODULE_BYTES (1ull << 32)

enum dm_tgp_scene {
    DM_TGP_SCENE_NONE = 0,
    DM_TGP_SCENE_GAME = 1,
    DM_TGP_SCENE_MENU = 2,
    DM_TGP_SCENE_PANEL = 3,
    DM_TGP_SCENE_DIALOG = 4,
    DM_TGP_SCENE_DEAD = 5
};

enum dm_tgp_hit_kind {
    DM_TGP_HIT_NONE = 0,
    DM_TGP_HIT_ITEM = 1,
    DM_TGP_HIT_TAB = 2,
    DM_TGP_HIT_OUTSIDE = 3,
    DM_TGP_HIT_PANEL = 4,
    DM_TGP_HIT_VENDOR = 7,
    DM_TGP_HIT_STACK = 8,
    DM_TGP_HIT_LOCKED = 10,
    DM_TGP_HIT_WORLD = 11
};

enum dm_tgp_action_kind {
    DM_TGP_ACTION_WORLD_TAP = 1,
    DM_TGP_ACTION_IDENTIFY,
    DM_TGP_ACTION_USE,
    DM_TGP_ACTION_LONG_PRESS,
    DM_TGP_ACTION_STACK_TAKE,
    DM_TGP_ACTION_TAB_TAP,
    DM_TGP_ACTION_SHOP_DROP,
    DM_TGP_ACTION_UI_CLICK,
    DM_TGP_ACTION_DIFFICULTY,
    DM_TGP_ACTION_BELT
};

typedef struct dm_tgp_snapshot {
    uint32_t abi_version;
    uint32_t module_id;
    uint64_t generation;
    uint64_t scene_epoch;
    uint64_t world_epoch;
    uint64_t object_generation; /* selected object cohort, separate from module code */
    uint64_t captured_ns;
    uint64_t ui_identity;
    uint32_t panel_bits; /* dm_cursor.h DM_PS_* bits, supplied by the provider */
    int available;
    int scene;           /* dm_tgp_scene */
    int dialogue_only;
    int dead;
} dm_tgp_snapshot;

typedef struct dm_tgp_hit {
    int valid;
    int kind;            /* dm_tgp_hit_kind */
    int drop_kind;       /* 1 place / 3 discard / 4 finger point / 8 stack */
    uint64_t object_id;
    uint64_t object_generation;
    uint64_t scene_epoch;
    uint64_t object_address; /* address in the provider's pinned read range */
    uint32_t object_size;
} dm_tgp_hit;

typedef struct dm_tgp_action {
    int kind;            /* dm_tgp_action_kind */
    float x, y;
    uint64_t object_id;
    uint64_t scene_epoch;
    uint64_t generation;
} dm_tgp_action;

typedef struct dm_tgp_provider {
    uint32_t abi_version;
    uint32_t module_id;
    uint64_t generation;
    uint64_t module_base;
    uint64_t module_size;
    uint64_t object_range_base; /* provider-owned pinned read range; not ELF */
    uint64_t object_range_size;
    void *context;
    int (*snapshot)(void *context, dm_tgp_snapshot *out);
    int (*hit)(void *context, float x, float y, dm_tgp_hit *out);
    int (*action)(void *context, const dm_tgp_action *action, uint64_t now_ns);
} dm_tgp_provider;

#ifdef __cplusplus
}
#endif
