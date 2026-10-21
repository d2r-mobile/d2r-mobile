// #33 observation-only Android touch scene/identity seam.
//
// This module reads the existing host state entry points:
// dm_uiscreen_ingame / dm_uiscreen_panel, dm_ui_panel_state, dm_ui_gen and the
// per-tick dm_hud_get publication. Scene observation is always available when
// those entries publish state. A generic dm_tgp_provider is exposed only when
// the loader has published immutable real module metadata for "main"; this
// slice still has no object range, hit model or action consumer, so hit/action
// remain unavailable.
#pragma once

#include <stdint.h>
#include "dm_hud.h"
#include "../runtime/touch_game_provider.h"

#ifdef __cplusplus
extern "C" {
#endif

enum dm_live_touch_scene {
    DM_LIVE_TOUCH_SCENE_UNKNOWN = 0,
    DM_LIVE_TOUCH_SCENE_GAME,
    DM_LIVE_TOUCH_SCENE_MENU,
    DM_LIVE_TOUCH_SCENE_PANEL,
    DM_LIVE_TOUCH_SCENE_DEAD
};

typedef struct dm_live_touch_provider_host {
    void *context;
    int (*ingame)(void *context);
    int (*panel_open)(void *context);
    int (*panel_bits)(void *context);
    uint32_t (*ui_gen)(void *context);
    void (*hud)(void *context, dm_hud_info *out);
    uint64_t (*now_ns)(void *context);
} dm_live_touch_provider_host;

typedef struct dm_live_touch_scene_observation {
    int available;
    int scene;                 /* dm_live_touch_scene */
    int dead;
    uint32_t panel_bits;       /* dm_cursor.h DM_PS_* bits */
    uint64_t scene_epoch;
    uint64_t world_epoch;
    uint64_t ui_identity;      /* actual dm_ui_gen; never synthesized */
    uint64_t captured_ns;
    int movement_continuity; /* Same live player/game, nearby position and fresh observation only. */
} dm_live_touch_scene_observation;

typedef struct dm_live_touch_provider {
    dm_live_touch_provider_host host;
    dm_tgp_provider iface;
    uint64_t scene_epoch;
    uint64_t world_epoch;
    uint64_t last_ui_identity;
    uint64_t last_player_identity, last_game_identity, last_captured_ns;
    int last_x, last_y;
    int have_context;
    int last_scene;
    int last_ingame;
    int last_panel;
    int last_area;
} dm_live_touch_provider;

// host == NULL uses the real dm/host entry points. A caller-supplied host is
// the owned test seam and must return the same kinds of values.
void dm_live_touch_provider_init(dm_live_touch_provider *provider,
                                 const dm_live_touch_provider_host *host);
// Returns 0 and leaves the observation unavailable for any unknown identity
// (including dm_ui_gen()==0), unknown in-run player state or invalid panel bits.
// Front-end pages (not in a run) remain a valid menu observation.
int dm_live_touch_provider_observe(dm_live_touch_provider *provider,
                                   dm_live_touch_scene_observation *out);
// Scene/identity observation does not itself construct a generic provider.
// Returns NULL until real successful-loader metadata for "main" is published;
// when available, the returned provider carries exact base/size and the real
// loader publication generation. object range is unknown, and hit/action stay
// unavailable.
const dm_tgp_provider *dm_live_touch_provider_generic_interface(
    dm_live_touch_provider *provider);

#ifdef __cplusplus
}
#endif
