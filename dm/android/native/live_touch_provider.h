// #33 observation-only Android touch scene/identity seam.
//
// This module reads the existing host state entry points:
// dm_uiscreen_ingame / dm_uiscreen_panel, dm_ui_panel_state, dm_ui_gen and the
// per-tick dm_hud_get publication. It is deliberately NOT a module-backed
// dm_tgp_provider: this slice has no pinned guest module, item hit model or
// action consumer. The generic provider interface is explicitly unavailable;
// callers must not treat these observations as game identity or readiness for
// module/hit/action work.
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
} dm_live_touch_scene_observation;

typedef struct dm_live_touch_provider {
    dm_live_touch_provider_host host;
    uint64_t scene_epoch;
    uint64_t world_epoch;
    uint64_t last_ui_identity;
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
// Scene/identity observation does not construct a generic dm_tgp_provider.
// Always NULL; this makes the missing module/hit/action capability explicit.
const dm_tgp_provider *dm_live_touch_provider_generic_interface(
    const dm_live_touch_provider *provider);

#ifdef __cplusplus
}
#endif
