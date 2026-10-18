#include "touch_bridge.h"
#include "touch_game_bridge.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

struct dm_touch_bridge {
    dm_android_pad *pad;
    dm_android_touch *touch;
    dm_touch_game_bridge *game;
    uint64_t game_epoch;
    float density;
    float width_pt;
    float height_pt;
    int belt_rows;
    int belt_open;
    int64_t last_time_ns;
    int32_t active[DM_TOUCH_MAX_POINTS];
    uint32_t active_count;
    dm_touch_diag diag;
};

static int finite_positive(float value) {
    return isfinite(value) && value > 0.0f;
}

static int active_index(const dm_touch_bridge *bridge, int32_t id) {
    for (uint32_t i = 0; i < bridge->active_count; i++) {
        if (bridge->active[i] == id) return (int)i;
    }
    return -1;
}

static int active_add(dm_touch_bridge *bridge, int32_t id) {
    if (bridge->active_count >= DM_TOUCH_MAX_POINTS) return 0;
    bridge->active[bridge->active_count++] = id;
    return 1;
}

static void active_remove(dm_touch_bridge *bridge, int32_t id) {
    int index = active_index(bridge, id);
    if (index < 0) return;
    for (uint32_t i = (uint32_t)index + 1; i < bridge->active_count; i++) {
        bridge->active[i - 1] = bridge->active[i];
    }
    bridge->active_count--;
}

static void active_clear(dm_touch_bridge *bridge) {
    bridge->active_count = 0;
}

static void sync_game_epoch(dm_touch_bridge *bridge) {
    if (!bridge->game) return;
    uint64_t epoch = dm_touch_game_bridge_invalidation_epoch(bridge->game);
    if (epoch == bridge->game_epoch) return;
    bridge->game_epoch = epoch;
    active_clear(bridge);
    memset(&bridge->diag.state, 0, sizeof(bridge->diag.state));
}

static dm_npad_state consume(dm_touch_bridge *bridge) {
    dm_npad_state state = dm_android_touch_read(bridge->touch);
    bridge->diag.state = state;
    bridge->diag.diagnostic_reads++;
    return state;
}

static int valid_event(const dm_touch_bridge *bridge, const dm_touch_event *event) {
    if (!bridge || !event) return 0;
    if (event->time_ns < 0) return 0;
    if (event->action < DM_TOUCH_ACTION_DOWN ||
        event->action > DM_TOUCH_ACTION_POINTER_UP) return 0;
    if (event->action == DM_TOUCH_ACTION_CANCEL) {
        /* A platform CANCEL may have no usable coordinates/pointers; it still
           must release all ownership and queued pulses. */
        return event->pointer_count <= DM_TOUCH_MAX_POINTS;
    }
    if (event->pointer_count == 0 || event->pointer_count > DM_TOUCH_MAX_POINTS) return 0;
    if (event->action != DM_TOUCH_ACTION_MOVE) {
        if (event->action_index < 0 ||
            (uint32_t)event->action_index >= event->pointer_count) return 0;
    }
    for (uint32_t i = 0; i < event->pointer_count; i++) {
        const dm_touch_point *point = &event->points[i];
        if (point->id < 0 || !isfinite(point->x_px) || !isfinite(point->y_px)) return 0;
        for (uint32_t j = 0; j < i; j++) {
            if (event->points[j].id == point->id) return 0;
        }
    }
    if (event->action == DM_TOUCH_ACTION_MOVE) {
        for (uint32_t i = 0; i < event->pointer_count; i++) {
            if (active_index(bridge, event->points[i].id) < 0) return 0;
        }
    } else if (event->action == DM_TOUCH_ACTION_DOWN ||
               event->action == DM_TOUCH_ACTION_POINTER_DOWN) {
        if (bridge->active_count >= DM_TOUCH_MAX_POINTS) return 0;
        int32_t id = event->points[event->action_index].id;
        if (active_index(bridge, id) >= 0) return 0;
        /* Every other pointer in the Android frame must already be owned; this
           rejects a malformed DOWN stream instead of leaving hidden pointers. */
        for (uint32_t i = 0; i < event->pointer_count; i++) {
            if ((int32_t)i == event->action_index) continue;
            if (active_index(bridge, event->points[i].id) < 0) return 0;
        }
    } else if (event->action == DM_TOUCH_ACTION_UP ||
               event->action == DM_TOUCH_ACTION_POINTER_UP) {
        int32_t id = event->points[event->action_index].id;
        if (active_index(bridge, id) < 0) return 0;
        for (uint32_t i = 0; i < event->pointer_count; i++) {
            if ((int32_t)i == event->action_index) continue;
            if (active_index(bridge, event->points[i].id) < 0) return 0;
        }
    }
    return 1;
}

static int apply_down(dm_touch_bridge *bridge, const dm_touch_point *point, int64_t time_ns) {
    float x_pt = point->x_px / bridge->density;
    float y_pt = point->y_px / bridge->density;
    if (dm_android_touch_down(bridge->touch, point->id, x_pt, y_pt, time_ns)) return 1;
    (void)consume(bridge);
    bridge->diag.drained_pulses++;
    return dm_android_touch_down(bridge->touch, point->id, x_pt, y_pt, time_ns);
}

static int apply_move(dm_touch_bridge *bridge, const dm_touch_point *point, int64_t time_ns) {
    float x_pt = point->x_px / bridge->density;
    float y_pt = point->y_px / bridge->density;
    if (dm_android_touch_move(bridge->touch, point->id, x_pt, y_pt, time_ns)) return 1;
    (void)consume(bridge);
    bridge->diag.drained_pulses++;
    return dm_android_touch_move(bridge->touch, point->id, x_pt, y_pt, time_ns);
}

static int apply_up(dm_touch_bridge *bridge, int32_t id, int64_t time_ns) {
    if (dm_android_touch_up(bridge->touch, id, time_ns)) return 1;
    (void)consume(bridge);
    bridge->diag.drained_pulses++;
    if (dm_android_touch_up(bridge->touch, id, time_ns)) {
        bridge->diag.up_retries++;
        return 1;
    }
    /* A release that cannot fit must never leave the pointer owned. Cancel all
       active pointers and queued pulses rather than silently dropping UP. */
    (void)dm_android_touch_cancel_all(bridge->touch, time_ns);
    active_clear(bridge);
    (void)consume(bridge);
    bridge->diag.cancel_fallbacks++;
    return 1;
}

dm_touch_bridge *dm_touch_bridge_create(float width_pt, float height_pt, float density) {
    if (!finite_positive(width_pt) || !finite_positive(height_pt) ||
        !finite_positive(density)) return NULL;
    dm_touch_bridge *bridge = calloc(1, sizeof(*bridge));
    if (!bridge) return NULL;
    bridge->pad = dm_android_pad_create();
    bridge->touch = bridge->pad ? dm_android_touch_create(bridge->pad, width_pt, height_pt) : NULL;
    if (!bridge->touch) {
        if (bridge->pad) dm_android_pad_destroy(bridge->pad);
        free(bridge);
        return NULL;
    }
    bridge->density = density;
    bridge->width_pt = width_pt;
    bridge->height_pt = height_pt;
    return bridge;
}

void dm_touch_bridge_destroy(dm_touch_bridge *bridge, dm_npad_state *neutral_out) {
    if (!bridge) return;
    if (bridge->game) dm_touch_game_bridge_destroy(bridge->game);
    (void)dm_android_touch_cancel_all(bridge->touch, bridge->last_time_ns);
    if (neutral_out) *neutral_out = consume(bridge);
    dm_android_touch_destroy(bridge->touch);
    dm_android_pad_destroy(bridge->pad);
    free(bridge);
}

int dm_touch_bridge_attach_game(dm_touch_bridge *bridge, const dm_tgp_provider *provider) {
    if (!bridge || !provider) return 0;
    if (bridge->game) {
        dm_touch_game_bridge_destroy(bridge->game);
        bridge->game = NULL;
    }
    active_clear(bridge);
    memset(&bridge->diag.state, 0, sizeof(bridge->diag.state));
    bridge->game = dm_touch_game_bridge_create(bridge->touch, provider);
    bridge->game_epoch = bridge->game ? dm_touch_game_bridge_invalidation_epoch(bridge->game) : 0;
    return bridge->game != NULL;
}

int dm_touch_bridge_set_density(dm_touch_bridge *bridge, float density) {
    if (!bridge || !finite_positive(density)) return 0;
    if (bridge->density != density) {
        /* The divisor changes; a held gesture cannot be rebased safely. Cancel
           ownership and queued pulses, consume the neutral sample, then adopt
           the new density. */
        (void)dm_android_touch_cancel_all(bridge->touch, bridge->last_time_ns);
        active_clear(bridge);
        (void)consume(bridge);
        bridge->density = density;
    }
    return 1;
}

float dm_touch_bridge_density(const dm_touch_bridge *bridge) {
    return bridge ? bridge->density : 0.0f;
}

int dm_touch_bridge_resize(dm_touch_bridge *bridge, float width_pt, float height_pt,
                           int64_t time_ns) {
    if (!bridge || !finite_positive(width_pt) || !finite_positive(height_pt) ||
        time_ns < 0) return 0;
    if (!dm_android_touch_resize(bridge->touch, width_pt, height_pt, time_ns)) return 0;
    /* The adapter's resize cancels core pointer ownership; mirror that in the
       bridge and consume the neutral sample so a reused ID is not rejected. */
    active_clear(bridge);
    (void)consume(bridge);
    bridge->width_pt = width_pt;
    bridge->height_pt = height_pt;
    bridge->last_time_ns = time_ns;
    return 1;
}

int dm_touch_bridge_set_belt_context(dm_touch_bridge *bridge, int rows, int open) {
    if (!bridge) return 0;
    if (bridge->belt_rows == rows && bridge->belt_open == open) return 1;   /* same snapshot: no cancel */
    /* The adapter validates rows/open before mutating and, on a real change,
       cancels its pointer ownership and queued pulses. Only after that
       succeeds do we mirror the invalidation in the bridge table and consume
       the neutral sample. An invalid update therefore leaves both layers
       untouched. */
    if (!dm_android_touch_set_belt_context(bridge->touch, rows, open)) return 0;
    active_clear(bridge);
    (void)consume(bridge);
    bridge->belt_rows = rows;
    bridge->belt_open = open;
    return 1;
}

int dm_touch_bridge_event(dm_touch_bridge *bridge, const dm_touch_event *event) {
    if (!bridge || !event) return 0;
    if (bridge->game && event->action != DM_TOUCH_ACTION_CANCEL) {
        if (!dm_touch_game_bridge_refresh(bridge->game, (uint64_t)event->time_ns)) {
            sync_game_epoch(bridge);
            bridge->diag.rejected_events++;
            return 0;
        }
    }
    sync_game_epoch(bridge);
    if (!valid_event(bridge, event)) {
        bridge->diag.invalid_events++;
        return 0;
    }
    bridge->last_time_ns = event->time_ns;
    switch (event->action) {
    case DM_TOUCH_ACTION_DOWN:
    case DM_TOUCH_ACTION_POINTER_DOWN: {
        const dm_touch_point *point = &event->points[event->action_index];
        if (!apply_down(bridge, point, event->time_ns)) {
            bridge->diag.rejected_events++;
            return 0;
        }
        if (!active_add(bridge, point->id)) {
            (void)dm_android_touch_cancel(bridge->touch, point->id, event->time_ns);
            (void)consume(bridge);
            bridge->diag.rejected_events++;
            return 0;
        }
        if (bridge->game &&
            !dm_touch_game_bridge_pointer_down(bridge->game, point->id,
                                               point->x_px / bridge->density,
                                               point->y_px / bridge->density,
                                               (uint64_t)event->time_ns)) {
            (void)dm_android_touch_cancel(bridge->touch, point->id, event->time_ns);
            active_remove(bridge, point->id);
            (void)consume(bridge);
            bridge->diag.rejected_events++;
            return 0;
        }
        bridge->diag.accepted_events++;
        if (bridge->game) {
            (void)dm_touch_game_bridge_tick(bridge->game, (uint64_t)event->time_ns);
            sync_game_epoch(bridge);
        }
        return 1;
    }
    case DM_TOUCH_ACTION_MOVE:
        for (uint32_t i = 0; i < event->pointer_count; i++) {
            if (!apply_move(bridge, &event->points[i], event->time_ns)) {
                bridge->diag.rejected_events++;
                return 0;
            }
        }
        bridge->diag.accepted_events++;
        if (bridge->game) {
            (void)dm_touch_game_bridge_tick(bridge->game, (uint64_t)event->time_ns);
            sync_game_epoch(bridge);
        }
        return 1;
    case DM_TOUCH_ACTION_UP:
    case DM_TOUCH_ACTION_POINTER_UP: {
        int32_t id = event->points[event->action_index].id;
        if (bridge->game &&
            !dm_touch_game_bridge_pointer_release(bridge->game, id,
                                                  event->points[event->action_index].x_px / bridge->density,
                                                  event->points[event->action_index].y_px / bridge->density,
                                                  (uint64_t)event->time_ns)) {
            (void)dm_android_touch_cancel_all(bridge->touch, event->time_ns);
            active_clear(bridge);
            (void)consume(bridge);
            bridge->diag.rejected_events++;
            return 0;
        }
        if (!apply_up(bridge, id, event->time_ns)) {
            bridge->diag.rejected_events++;
            return 0;
        }
        active_remove(bridge, id);
        bridge->diag.accepted_events++;
        if (bridge->game) {
            (void)dm_touch_game_bridge_tick(bridge->game, (uint64_t)event->time_ns);
            sync_game_epoch(bridge);
        }
        return 1;
    }
    case DM_TOUCH_ACTION_CANCEL:
        (void)dm_android_touch_cancel_all(bridge->touch, event->time_ns);
        active_clear(bridge);
        (void)consume(bridge);
        bridge->diag.accepted_events++;
        return 1;
    }
    return 0;
}

int dm_touch_bridge_tick(dm_touch_bridge *bridge, int64_t time_ns) {
    if (!bridge || time_ns < 0) return 0;
    if (!dm_android_touch_tick(bridge->touch, time_ns)) return 0;
    if (bridge->game) {
        (void)dm_touch_game_bridge_tick(bridge->game, (uint64_t)time_ns);
        sync_game_epoch(bridge);
    }
    bridge->last_time_ns = time_ns;
    return 1;
}

int dm_touch_bridge_cancel_all(dm_touch_bridge *bridge, int64_t time_ns) {
    if (!bridge || time_ns < 0) return 0;
    if (!dm_android_touch_cancel_all(bridge->touch, time_ns)) return 0;
    active_clear(bridge);
    (void)consume(bridge);
    bridge->last_time_ns = time_ns;
    return 1;
}

int dm_touch_bridge_take_action(dm_touch_bridge *bridge, dm_android_touch_action *out) {
    if (!bridge || !out) return 0;
    /* With a game provider attached its tick consumes core actions; do not let
       this outlet steal from it. */
    if (bridge->game) return 0;
    return dm_android_touch_take_action(bridge->touch, out);
}

dm_npad_state dm_touch_bridge_read(dm_touch_bridge *bridge) {
    if (!bridge) {
        dm_npad_state empty;
        memset(&empty, 0, sizeof(empty));
        return empty;
    }
    return consume(bridge);
}

dm_npad_state dm_touch_bridge_snapshot(const dm_touch_bridge *bridge) {
    dm_npad_state state;
    if (!bridge) {
        memset(&state, 0, sizeof(state));
        return state;
    }
    return bridge->diag.state;
}

const dm_touch_diag *dm_touch_bridge_diag(const dm_touch_bridge *bridge) {
    return bridge ? &bridge->diag : NULL;
}
