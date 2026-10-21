#pragma once

#include <stdint.h>
#include "touch_adapter.h"
#include "touch_game_provider.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dm_touch_game_bridge dm_touch_game_bridge;

enum {
    DM_TGB_REASON_OK = 0,
    DM_TGB_REASON_ABI,
    DM_TGB_REASON_IDENTITY,
    DM_TGB_REASON_GENERATION,
    DM_TGB_REASON_SNAPSHOT,
    DM_TGB_REASON_STALE,
    DM_TGB_REASON_SCENE,
    DM_TGB_REASON_HIT,
    DM_TGB_REASON_RANGE
};

// The provider struct is copied; provider context/functions must outlive the
// bridge. A structurally valid but unavailable provider is allowed and gates
// touch until a later refresh succeeds.
dm_touch_game_bridge *dm_touch_game_bridge_create(dm_android_touch *touch,
                                                  const dm_tgp_provider *provider);
void dm_touch_game_bridge_destroy(dm_touch_game_bridge *bridge);

// Refresh and validate the scene/identity snapshot. On any mismatch or
// unavailable snapshot the adapter is made unavailable (neutral) and 0 is
// returned; no synthetic scene is applied.
int dm_touch_game_bridge_refresh(dm_touch_game_bridge *bridge, uint64_t now_ns);
int dm_touch_game_bridge_pointer_down(dm_touch_game_bridge *bridge, int32_t pointer_id,
                                      float x, float y, uint64_t now_ns);
int dm_touch_game_bridge_pointer_release(dm_touch_game_bridge *bridge, int32_t pointer_id,
                                         float x, float y, uint64_t now_ns);
// Refresh and consume core action sequences. Actions are acknowledged only by
// provider->action returning 1; blocked actions count as rejected, never acked.
// Only USE is enabled for object actions: it is dispatched with the item
// identity retained at the source down under an immutable gesture token and
// revalidated against the current module/object cohort/scene/range plus a fresh
// hit at the retained coordinates. Other object actions remain rejected.
int dm_touch_game_bridge_tick(dm_touch_game_bridge *bridge, uint64_t now_ns);

int dm_touch_game_bridge_available(const dm_touch_game_bridge *bridge);
uint64_t dm_touch_game_bridge_invalidation_epoch(const dm_touch_game_bridge *bridge);
int dm_touch_game_bridge_last_reason(const dm_touch_game_bridge *bridge);
uint64_t dm_touch_game_bridge_acked_actions(const dm_touch_game_bridge *bridge);
uint64_t dm_touch_game_bridge_rejected_actions(const dm_touch_game_bridge *bridge);

#ifdef __cplusplus
}
#endif
