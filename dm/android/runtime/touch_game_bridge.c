#include "touch_game_bridge.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

struct dm_touch_game_bridge {
    dm_android_touch *touch;
    dm_tgp_provider provider;
    dm_tgp_snapshot snapshot;
    int available;
    int reason;
    uint64_t invalidations;
    int have_context;
    int last_navigation;
    uint32_t last_panel_bits;
    uint64_t last_scene_epoch, last_world_epoch, last_ui_identity, last_object_generation;
    uint64_t acked;
    uint64_t rejected;
};

static int valid_scene(int scene) {
    return scene >= DM_TGP_SCENE_GAME && scene <= DM_TGP_SCENE_DEAD;
}

static int valid_hit_kind(int kind) {
    return kind == DM_TGP_HIT_NONE || kind == DM_TGP_HIT_ITEM ||
           kind == DM_TGP_HIT_TAB || kind == DM_TGP_HIT_OUTSIDE ||
           kind == DM_TGP_HIT_PANEL || kind == DM_TGP_HIT_VENDOR ||
           kind == DM_TGP_HIT_STACK || kind == DM_TGP_HIT_LOCKED ||
           kind == DM_TGP_HIT_WORLD;
}

static int valid_drop(int kind) {
    return kind == 0 || kind == 1 || kind == 3 || kind == 4 || kind == 8;
}

static int object_range_contains(const dm_touch_game_bridge *bridge,
                                 const dm_tgp_hit *hit) {
    if (hit->kind == DM_TGP_HIT_NONE) return 1;
    if (hit->object_id == 0 || hit->object_size == 0) return 0;
    if (bridge->provider.object_range_size == 0) return 0;
    if (hit->object_address < bridge->provider.object_range_base) return 0;
    if (hit->object_address - bridge->provider.object_range_base >=
        bridge->provider.object_range_size) return 0;
    if ((uint64_t)hit->object_size >
        bridge->provider.object_range_size -
            (hit->object_address - bridge->provider.object_range_base)) return 0;
    return 1;
}

static int valid_hit(const dm_touch_game_bridge *bridge, const dm_tgp_hit *hit) {
    if (!hit->valid || !valid_hit_kind(hit->kind) || !valid_drop(hit->drop_kind))
        return 0;
    if (hit->object_generation != bridge->snapshot.object_generation ||
        hit->scene_epoch != bridge->snapshot.scene_epoch)
        return 0;
    return object_range_contains(bridge, hit);
}

static void mark_unavailable(dm_touch_game_bridge *bridge, int reason) {
    bridge->available = 0;
    bridge->reason = reason;
    bridge->invalidations++;
    (void)dm_android_touch_set_game_available(bridge->touch, 0);
}

int dm_touch_game_bridge_refresh(dm_touch_game_bridge *bridge, uint64_t now_ns) {
    if (!bridge) return 0;
    dm_tgp_snapshot snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    if (bridge->provider.abi_version != DM_TGP_ABI_VERSION ||
        bridge->provider.module_id == 0 || bridge->provider.generation == 0) {
        mark_unavailable(bridge, DM_TGB_REASON_ABI);
        return 0;
    }
    if (!bridge->provider.snapshot ||
        !bridge->provider.snapshot(bridge->provider.context, &snapshot)) {
        mark_unavailable(bridge, DM_TGB_REASON_SNAPSHOT);
        return 0;
    }
    if (snapshot.abi_version != DM_TGP_ABI_VERSION ||
        snapshot.module_id != bridge->provider.module_id) {
        mark_unavailable(bridge, DM_TGB_REASON_IDENTITY);
        return 0;
    }
    if (snapshot.generation != bridge->provider.generation) {
        mark_unavailable(bridge, DM_TGB_REASON_GENERATION);
        return 0;
    }
    if (!snapshot.available || !valid_scene(snapshot.scene) ||
        snapshot.scene_epoch == 0 || snapshot.world_epoch == 0 ||
        snapshot.captured_ns == 0 ||
        snapshot.captured_ns > now_ns ||
        now_ns - snapshot.captured_ns > DM_TGP_MAX_AGE_NS ||
        (snapshot.dialogue_only && snapshot.scene != DM_TGP_SCENE_DIALOG) ||
        (snapshot.dead && snapshot.scene != DM_TGP_SCENE_DEAD)) {
        mark_unavailable(bridge, DM_TGB_REASON_STALE);
        return 0;
    }
    if (snapshot.panel_bits & ~0x7fu) {
        mark_unavailable(bridge, DM_TGB_REASON_SCENE);
        return 0;
    }
    uint32_t panel_bits = snapshot.panel_bits | (snapshot.dead ? 4u : 0u);
    int navigation = snapshot.scene != DM_TGP_SCENE_GAME;
    int context_changed = bridge->have_context &&
        (navigation != bridge->last_navigation ||
         panel_bits != bridge->last_panel_bits ||
         snapshot.scene_epoch != bridge->last_scene_epoch ||
         snapshot.world_epoch != bridge->last_world_epoch ||
         snapshot.ui_identity != bridge->last_ui_identity ||
         snapshot.object_generation != bridge->last_object_generation);
    if (context_changed) bridge->invalidations++;
    if (!dm_android_touch_set_game_available(bridge->touch, 1) ||
        !dm_android_touch_set_game_context(bridge->touch, navigation, panel_bits,
                                           snapshot.scene_epoch, snapshot.world_epoch,
                                           snapshot.ui_identity,
                                           snapshot.object_generation)) {
        mark_unavailable(bridge, DM_TGB_REASON_SCENE);
        return 0;
    }
    bridge->have_context = 1;
    bridge->last_navigation = navigation;
    bridge->last_panel_bits = panel_bits;
    bridge->last_scene_epoch = snapshot.scene_epoch;
    bridge->last_world_epoch = snapshot.world_epoch;
    bridge->last_ui_identity = snapshot.ui_identity;
    bridge->last_object_generation = snapshot.object_generation;
    bridge->snapshot = snapshot;
    bridge->available = 1;
    bridge->reason = DM_TGB_REASON_OK;
    return 1;
}

int dm_touch_game_bridge_pointer_down(dm_touch_game_bridge *bridge, int32_t pointer_id,
                                      float x, float y, uint64_t now_ns) {
    if (!bridge || !isfinite(x) || !isfinite(y)) return 0;
    if (!dm_touch_game_bridge_refresh(bridge, now_ns)) return 0;
    dm_tgp_hit hit;
    memset(&hit, 0, sizeof(hit));
    if (!bridge->provider.hit ||
        !bridge->provider.hit(bridge->provider.context, x, y, &hit) ||
        !valid_hit(bridge, &hit)) {
        bridge->reason = DM_TGB_REASON_HIT;
        return 0;
    }
    return dm_android_touch_pointer_hit(bridge->touch, pointer_id, hit.kind);
}

int dm_touch_game_bridge_pointer_release(dm_touch_game_bridge *bridge,
                                         int32_t pointer_id, float x, float y,
                                         uint64_t now_ns) {
    if (!bridge || !isfinite(x) || !isfinite(y)) return 0;
    if (!dm_touch_game_bridge_refresh(bridge, now_ns)) return 0;
    dm_tgp_hit hit;
    memset(&hit, 0, sizeof(hit));
    if (!bridge->provider.hit ||
        !bridge->provider.hit(bridge->provider.context, x, y, &hit) ||
        !valid_hit(bridge, &hit)) {
        bridge->reason = DM_TGB_REASON_HIT;
        return 0;
    }
    return dm_android_touch_pointer_release(bridge->touch, pointer_id, hit.drop_kind);
}

static uint32_t action_kind(uint32_t core_kind) {
    switch (core_kind) {
    case DM_ANDROID_ACTION_WORLD_TAP: return DM_TGP_ACTION_WORLD_TAP;
    case DM_ANDROID_ACTION_IDENTIFY: return DM_TGP_ACTION_IDENTIFY;
    case DM_ANDROID_ACTION_USE: return DM_TGP_ACTION_USE;
    case DM_ANDROID_ACTION_LONG_PRESS: return DM_TGP_ACTION_LONG_PRESS;
    case DM_ANDROID_ACTION_STACK_TAKE: return DM_TGP_ACTION_STACK_TAKE;
    case DM_ANDROID_ACTION_TAB_TAP: return DM_TGP_ACTION_TAB_TAP;
    case DM_ANDROID_ACTION_SHOP_DROP: return DM_TGP_ACTION_SHOP_DROP;
    case DM_ANDROID_ACTION_UI_CLICK: return DM_TGP_ACTION_UI_CLICK;
    case DM_ANDROID_ACTION_DIFFICULTY: return DM_TGP_ACTION_DIFFICULTY;
    case DM_ANDROID_ACTION_BELT: return DM_TGP_ACTION_BELT;
    default: return 0;
    }
}

static int object_action(uint32_t core_kind) {
    return core_kind == DM_ANDROID_ACTION_IDENTIFY ||
           core_kind == DM_ANDROID_ACTION_USE ||
           core_kind == DM_ANDROID_ACTION_LONG_PRESS ||
           core_kind == DM_ANDROID_ACTION_STACK_TAKE ||
           core_kind == DM_ANDROID_ACTION_SHOP_DROP;
}

int dm_touch_game_bridge_tick(dm_touch_game_bridge *bridge, uint64_t now_ns) {
    if (!bridge) return 0;
    if (!dm_touch_game_bridge_refresh(bridge, now_ns)) return 0;
    dm_android_touch_action action;
    while (dm_android_touch_take_action(bridge->touch, &action)) {
        /* This bounded slice has no retained, revalidated object target for
           inventory/vendor actions. Reject them explicitly instead of sending
           object_id=0 to a consumer. */
        if (object_action(action.kind)) {
            bridge->rejected++;
            continue;
        }
        dm_tgp_action provider_action;
        memset(&provider_action, 0, sizeof(provider_action));
        provider_action.kind = (int)action_kind(action.kind);
        provider_action.x = action.x;
        provider_action.y = action.y;
        provider_action.scene_epoch = bridge->snapshot.scene_epoch;
        provider_action.generation = bridge->snapshot.generation;
        if (provider_action.kind == 0 || !bridge->provider.action ||
            !bridge->provider.action(bridge->provider.context, &provider_action, now_ns)) {
            bridge->rejected++;
            continue;
        }
        bridge->acked++;
    }
    return 1;
}

dm_touch_game_bridge *dm_touch_game_bridge_create(dm_android_touch *touch,
                                                  const dm_tgp_provider *provider) {
    if (!touch || !provider || provider->abi_version != DM_TGP_ABI_VERSION ||
        provider->module_id == 0 || provider->generation == 0 ||
        provider->module_base == 0 || provider->module_size == 0 ||
        provider->module_size > DM_TGP_MAX_MODULE_BYTES ||
        provider->module_base > UINT64_MAX - provider->module_size ||
        (provider->object_range_size &&
         (provider->object_range_base == 0 ||
          provider->object_range_base > UINT64_MAX - provider->object_range_size)) ||
        !provider->snapshot || !provider->hit || !provider->action)
        return NULL;
    dm_touch_game_bridge *bridge = calloc(1, sizeof(*bridge));
    if (!bridge) return NULL;
    bridge->touch = touch;
    bridge->provider = *provider;
    bridge->reason = DM_TGB_REASON_SNAPSHOT;
    (void)dm_android_touch_set_game_available(touch, 0);
    return bridge;
}

void dm_touch_game_bridge_destroy(dm_touch_game_bridge *bridge) {
    if (!bridge) return;
    (void)dm_android_touch_set_game_available(bridge->touch, 0);
    free(bridge);
}

int dm_touch_game_bridge_available(const dm_touch_game_bridge *bridge) {
    return bridge ? bridge->available : 0;
}

uint64_t dm_touch_game_bridge_invalidation_epoch(const dm_touch_game_bridge *bridge) {
    return bridge ? bridge->invalidations : 0;
}

int dm_touch_game_bridge_last_reason(const dm_touch_game_bridge *bridge) {
    return bridge ? bridge->reason : DM_TGB_REASON_ABI;
}

uint64_t dm_touch_game_bridge_acked_actions(const dm_touch_game_bridge *bridge) {
    return bridge ? bridge->acked : 0;
}

uint64_t dm_touch_game_bridge_rejected_actions(const dm_touch_game_bridge *bridge) {
    return bridge ? bridge->rejected : 0;
}
