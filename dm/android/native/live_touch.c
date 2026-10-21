#include "live_touch.h"
#include "../../ios/Sources/dm_touch_controls.h"
#include "../../host/dm_input.h"
#include "touch_pointer_map.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

struct dm_live_touch {
    dm_tc_state state;
    dm_touch_pointer_map pointers;
    float width, height;
    int64_t last_ns;
    int64_t pulse_started_ns, pulse_seen_ns, pulse_free_ns;
    uint64_t pulse_epoch, pulse_samples_start, pulse_samples_free;
    uint32_t belt_fingers;
    int portal_request;
    int have_context;
    uint64_t scene_epoch, world_epoch, ui_identity, object_generation;
    dm_tg_snapshot skill_snapshot;
    int skill_snapshot_valid;
    uint64_t skill_scene_epoch, skill_world_epoch;
    int skill_bind_active;
    uint64_t skill_bind_request, skill_bind_key, skill_bind_scene_epoch, skill_bind_world_epoch;
    dm_live_touch_tap_hook tap_hook;
    void *tap_context;
    struct {
        int active;
        int pointer_slot;
        int32_t pointer_id;
        uint64_t key;
        int skill_set;
        float x0, y0, x, y;
    } skill_bind[DM_TC_MAX_TOUCH];
};

static double now_s(dm_live_touch *touch, int64_t time_ns) {
    if (time_ns < 0) time_ns = 0;
    if (time_ns < touch->last_ns) time_ns = touch->last_ns;
    touch->last_ns = time_ns;
    return (double)time_ns / 1e9;
}

static float point_x(const dm_live_touch *touch, float x_px, float left, float width) {
    float n = width > 0 ? (x_px - left) / width : 0;
    if (!isfinite(n) || n < 0) n = 0;
    if (n > 0.999f) n = 0.999f;
    return n * touch->width;
}

static float point_y(const dm_live_touch *touch, float y_px, float top, float height) {
    float n = height > 0 ? (y_px - top) / height : 0;
    if (!isfinite(n) || n < 0) n = 0;
    if (n > 0.999f) n = 0.999f;
    return n * touch->height;
}

static void sync_pointers(dm_live_touch *touch) {
    for (int i = 0; i < DM_TC_MAX_TOUCH; ++i) {
        if (touch->pointers.platform[i] >= 0 && !touch->state.finger[i].active &&
            !touch->skill_bind[i].active) {
            touch->pointers.platform[i] = -1;
        }
    }
}

static void clear_skill_bind(dm_live_touch *touch, int release_pointers) {
    if (!touch) return;
    for (int i = 0; i < DM_TC_MAX_TOUCH; ++i) {
        if (!touch->skill_bind[i].active) continue;
        if (release_pointers && touch->skill_bind[i].pointer_id >= 0)
            dm_touch_pointer_release(&touch->pointers, touch->skill_bind[i].pointer_id);
        memset(&touch->skill_bind[i], 0, sizeof touch->skill_bind[i]);
    }
    touch->skill_bind_request = 0;
    touch->skill_bind_key = 0;
    touch->skill_bind_scene_epoch = 0;
    touch->skill_bind_world_epoch = 0;
}

// Exact empty-slot probe. This is the original dm_ta_current_set empty path:
// all snapshot/identity/freshness/bank checks are kept, the same mapping slot is
// selected (normal or alt for the ZL set), and only the original UINT32_MAX raw
// id is empty. Unknown values and absent art are never reported empty.
static int skill_slot_empty(const dm_live_touch *touch, uint64_t button,
                            uint64_t now_ns, uint64_t scene_epoch, uint64_t world_epoch) {
    if (!touch || !touch->skill_snapshot_valid) return 0;
    if (touch->skill_scene_epoch != scene_epoch || touch->skill_world_epoch != world_epoch) return 0;
    const dm_tg_snapshot *s = &touch->skill_snapshot;
    if (!s->supported || !s->valid || !s->ready ||
        s->source != DM_TG_SOURCE_NATIVE_GETTER ||
        s->identity_scope != DM_TG_IDENTITY_NATIVE ||
        !s->captured_ns || now_ns < s->captured_ns ||
        now_ns - s->captured_ns > DM_TG_MAX_AGE_NS ||
        (s->selected_bank != 0 && s->selected_bank != 16)) return 0;
    if (s->scene_epoch != scene_epoch || s->world_epoch != world_epoch) return 0;
    uint32_t logical = 0;
    switch (button) {
        case DM_BTN_A: logical = 0x2000; break;
        case DM_BTN_B: logical = 0x1000; break;
        case DM_BTN_X: logical = 0x8000; break;
        case DM_BTN_Y: logical = 0x4000; break;
        case DM_BTN_R: logical = 0x200; break;
        default: return 0;
    }
    int slot = -1;
    for (int i = 0; i < DM_TG_FACETS; ++i) {
        if (s->mapping[i].logical != logical) continue;
        if (slot >= 0) return 0;
        slot = (int)(touch->state.skill_set ? s->mapping[i].alt : s->mapping[i].normal);
    }
    if (slot < 0 || slot >= (touch->state.skill_set ? 12 : 6)) return 0;
    return s->raw_skill_id[slot] == UINT32_MAX;
}

// The original core marks a bottom belt cell directly; an upper-row press is
// represented by DM_TC_DIFF, so identify that region with the same geometry as
// dm_tc_begin_at. Only the touched belt gesture is quarantined; unrelated
// fingers keep their slots and actions.
static int belt_upper_hit(const dm_live_touch *touch, float x, float y) {
    const dm_tc_state *s = &touch->state;
    if (!s->belt_open || s->belt_rows <= 1) return 0;
    float u = s->layout.height / 1320.0f;
    for (int r = 1; r < s->belt_rows && r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            const dm_tc_target *t = &s->layout.target[DM_TC_BELT1 + c];
            if (fabsf(x - t->x) <= 60.0f * u &&
                fabsf(y - (t->y - (165.0f + 162.0f * (r - 1)) * u)) <= 53.75f * u)
                return 1;
        }
    }
    return 0;
}

static int belt_finger_at(const dm_live_touch *touch, int slot) {
    if (slot < 0 || slot >= DM_TC_MAX_TOUCH || !touch->state.finger[slot].active) return 0;
    const dm_tc_finger *f = &touch->state.finger[slot];
    if (f->target >= DM_TC_BELT1 && f->target <= DM_TC_BELT4) return 1;
    return f->target == DM_TC_DIFF && belt_upper_hit(touch, f->x, f->y);
}

static void quarantine_belt_fingers(dm_live_touch *touch) {
    uint32_t bits = touch->belt_fingers;
    touch->belt_fingers = 0;
    for (int i = 0; i < DM_TC_MAX_TOUCH; ++i) {
        if (!(bits & (1u << i))) continue;
        int32_t id = touch->pointers.platform[i];
        dm_tc_cancel_finger(&touch->state, i);
        if (id >= 0) dm_touch_pointer_release(&touch->pointers, id);
    }
    if (bits) sync_pointers(touch);
}

dm_live_touch *dm_live_touch_create(float width_pt, float height_pt) {
    if (!isfinite(width_pt) || !isfinite(height_pt) || width_pt <= 0 || height_pt <= 0) return NULL;
    dm_live_touch *touch = calloc(1, sizeof *touch);
    if (!touch) return NULL;
    touch->width = width_pt;
    touch->height = height_pt;
    dm_tc_init(&touch->state, width_pt, height_pt);
    touch->state.mouse_click_mode = 1;
    dm_touch_pointer_clear(&touch->pointers);
    return touch;
}

void dm_live_touch_destroy(dm_live_touch *touch) {
    if (!touch) return;
    dm_tc_cancel_all(&touch->state);
    free(touch);
}

void dm_live_touch_set_holding(dm_live_touch *touch, int holding, int targeting) {
    if (touch) dm_tc_set_holding(&touch->state, holding && !targeting);
}

void dm_live_touch_resize(dm_live_touch *touch, float width_pt, float height_pt) {
    if (!touch || !isfinite(width_pt) || !isfinite(height_pt) || width_pt <= 0 || height_pt <= 0) return;
    if (touch->width == width_pt && touch->height == height_pt) return;
    dm_tc_resize(&touch->state, width_pt, height_pt);
    clear_skill_bind(touch, 1);
    touch->state.belt_request = 0;
    touch->state.diff_request = 0;
    touch->portal_request = 0;
    touch->belt_fingers = 0;
    touch->pulse_started_ns = 0;
    touch->width = width_pt;
    touch->height = height_pt;
    dm_touch_pointer_clear(&touch->pointers);
}

int dm_live_touch_set_belt(dm_live_touch *touch, int rows, int open) {
    if (!touch) return 0;
    if (rows < 0 || rows > 4 || open < 0 || open > 1 ||
        (open && rows <= 1) || (rows == 0 && open != 0)) {
        quarantine_belt_fingers(touch);
        // Unknown/invalid host state must not leave stale gestures or requests.
        touch->state.belt_rows = 0;
        touch->state.belt_open = 0;
        touch->state.belt_request = 0;
        return 0;
    }
    if (rows == 0) {
        quarantine_belt_fingers(touch);
        touch->state.belt_request = 0;
    }
    touch->state.belt_rows = rows;
    touch->state.belt_open = open ? 1 : 0;
    return 1;
}

int dm_live_touch_consume_belt_request(dm_live_touch *touch,
                                       uint64_t scene_epoch, uint64_t world_epoch,
                                       uint64_t ui_identity,
                                       const dm_live_touch_belt_actions *actions) {
    if (!touch) return 0;
    if (!actions || !actions->expand || !actions->use) {
        touch->state.belt_request = 0;
        return 0;
    }
    int matches = touch->have_context && touch->scene_epoch == scene_epoch &&
                  touch->world_epoch == world_epoch && touch->ui_identity == ui_identity;
    if (!matches) {
        touch->state.belt_request = 0;
        return 0;
    }
    int request = touch->state.belt_request;
    if (!request) return 0;
    touch->state.belt_request = 0;
    // Recheck the context immediately before handing the request to the host.
    matches = touch->have_context && touch->scene_epoch == scene_epoch &&
              touch->world_epoch == world_epoch && touch->ui_identity == ui_identity;
    if (!matches) return 0;

    if (request == 1 || request == 2) {
        if (touch->state.belt_rows <= 1) return 0;
        actions->expand(actions->context, request == 1);
        return 1;
    }
    if (request >= 0x100) {
        int col = request & 0xf;
        int row = (request >> 4) & 0xf;
        if (col > 3 || row < 1 || row > 3 || row >= touch->state.belt_rows) return 0;
        actions->use(actions->context, col, row);
        return 1;
    }
    return 0;
}

int dm_live_touch_consume_difficulty_request(dm_live_touch *touch,
                                             uint64_t scene_epoch, uint64_t world_epoch,
                                             uint64_t ui_identity,
                                             const dm_live_touch_difficulty_actions *actions) {
    if (!touch) return 0;
    if (!actions || !actions->request_players) {
        touch->state.diff_request = 0;
        return 0;
    }
    int matches = touch->have_context && touch->scene_epoch == scene_epoch &&
                  touch->world_epoch == world_epoch && touch->ui_identity == ui_identity;
    if (!matches) {
        touch->state.diff_request = 0;
        return 0;
    }
    int request = touch->state.diff_request;
    if (!request) return 0;
    touch->state.diff_request = 0;
    // Recheck immediately before handing the selected level to the real host.
    matches = touch->have_context && touch->scene_epoch == scene_epoch &&
              touch->world_epoch == world_epoch && touch->ui_identity == ui_identity;
    if (!matches) return 0;
    if (request < 1 || request > 8) return 0;
    actions->request_players(actions->context, request);
    return 1;
}

int dm_live_touch_consume_portal_request(dm_live_touch *touch,
                                         uint64_t scene_epoch, uint64_t world_epoch,
                                         uint64_t ui_identity,
                                         const dm_live_touch_portal_actions *actions) {
    if (!touch) return 0;
    if (!actions || !actions->request_portal) {
        touch->portal_request = 0;
        return 0;
    }
    int matches = touch->have_context && touch->scene_epoch == scene_epoch &&
                  touch->world_epoch == world_epoch && touch->ui_identity == ui_identity;
    if (!matches) {
        touch->portal_request = 0;
        return 0;
    }
    if (!touch->portal_request) return 0;
    touch->portal_request = 0;
    matches = touch->have_context && touch->scene_epoch == scene_epoch &&
              touch->world_epoch == world_epoch && touch->ui_identity == ui_identity;
    if (!matches) return 0;
    actions->request_portal(actions->context);
    return 1;
}

void dm_live_touch_set_skill_snapshot(dm_live_touch *touch,
                                      const dm_tg_snapshot *snapshot,
                                      uint64_t scene_epoch, uint64_t world_epoch) {
    if (!touch) return;
    int changed = !snapshot || !touch->skill_snapshot_valid ||
                  touch->skill_scene_epoch != scene_epoch ||
                  touch->skill_world_epoch != world_epoch;
    if (changed) clear_skill_bind(touch, 1);
    touch->skill_snapshot_valid = snapshot != NULL;
    if (snapshot) touch->skill_snapshot = *snapshot;
    else memset(&touch->skill_snapshot, 0, sizeof touch->skill_snapshot);
    touch->skill_scene_epoch = scene_epoch;
    touch->skill_world_epoch = world_epoch;
}

void dm_live_touch_set_skill_bind_active(dm_live_touch *touch, int active) {
    if (!touch) return;
    active = !!active;
    if (active && !touch->skill_bind_active) clear_skill_bind(touch, 1);
    touch->skill_bind_active = active;
}

void dm_live_touch_set_tap_hook(dm_live_touch *touch, dm_live_touch_tap_hook hook,
                                void *context) {
    if (!touch) return;
    touch->tap_hook = hook;
    touch->tap_context = context;
}

void dm_live_touch_pointer_move(dm_live_touch *touch, float nx, float ny) {
    if (!touch || !isfinite(nx) || !isfinite(ny)) return;
    if (nx < 0) nx = 0;
    if (nx > 0.999f) nx = 0.999f;
    if (ny < 0) ny = 0;
    if (ny > 0.999f) ny = 0.999f;
    touch->state.pointer_valid = 1;
    touch->state.pointer_x = nx * touch->width;
    touch->state.pointer_y = ny * touch->height;
}

unsigned dm_live_touch_tap_move_seq(const dm_live_touch *touch) {
    return touch ? touch->state.tap_move_seq : 0;
}

// Called only at the real pointer end, after the async item hit is known. The
// hook owns the host classification; this function only translates the verdict
// into the original core's tap fields. Mode 2 is exactly the skill-bind Y case;
// a null/unknown verdict leaves the original tab/close route alone.
static void apply_tap_action(dm_live_touch *touch) {
    if (!touch) return;
    touch->state.tap_button = 0;
    touch->state.tap_move = 0;
    if (!touch->state.navigation_mode || !touch->tap_hook) return;
    float nx = 0, ny = 0;
    int mode = touch->tap_hook(touch->tap_context, &nx, &ny);
    if (!isfinite(nx) || !isfinite(ny)) return;
    if (nx < 0) nx = 0;
    if (nx > 0.999f) nx = 0.999f;
    if (ny < 0) ny = 0;
    if (ny > 0.999f) ny = 0.999f;
    if (mode == 2) {
        touch->state.tap_button = DM_BTN_Y;
        touch->state.tap_x = nx * touch->width;
        touch->state.tap_y = ny * touch->height;
    } else if (mode == 1) {
        touch->state.tap_move = 1;
        touch->state.tap_x = nx * touch->width;
        touch->state.tap_y = ny * touch->height;
    }
}

static void clear_tap_action(dm_live_touch *touch) {
    if (!touch) return;
    touch->state.tap_button = 0;
    touch->state.tap_move = 0;
}

int dm_live_touch_consume_skill_bind_request(dm_live_touch *touch,
                                             uint64_t scene_epoch, uint64_t world_epoch,
                                             const dm_live_touch_skill_bind_actions *actions) {
    if (!touch) return 0;
    if (!touch->skill_bind_request) return 0;
    uint64_t key = touch->skill_bind_key;
    touch->skill_bind_request = 0;
    touch->skill_bind_key = 0;
    if (!actions || !actions->begin_bind || !key ||
        !touch->have_context || touch->scene_epoch != scene_epoch ||
        touch->world_epoch != world_epoch ||
        touch->skill_bind_scene_epoch != scene_epoch ||
        touch->skill_bind_world_epoch != world_epoch) return 0;
    uint64_t button = key & ~(uint64_t)DM_BTN_ZL;
    if (!skill_slot_empty(touch, button, (uint64_t)touch->last_ns,
                          scene_epoch, world_epoch)) return 0;
    actions->begin_bind(actions->context, key);
    return 1;
}

void dm_live_touch_set_mode(dm_live_touch *touch, int navigation, uint32_t mask) {
    if (!touch) return;
    navigation = !!navigation;
    mask &= (1u << DM_TC_COUNT) - 1;
    int changed = navigation != touch->state.navigation_mode || mask != touch->state.enabled_targets;
    if (navigation != touch->state.navigation_mode) dm_tc_set_navigation_mode(&touch->state, navigation);
    if (mask != touch->state.enabled_targets) dm_tc_enable_targets(&touch->state, mask);
    if (changed) {
        clear_skill_bind(touch, 1);
        touch->state.diff_request = 0;
        touch->portal_request = 0;
        touch->belt_fingers = 0;
    }
    sync_pointers(touch);
}

void dm_live_touch_set_context(dm_live_touch *touch, int navigation, uint32_t mask,
                               uint64_t scene_epoch, uint64_t world_epoch,
                               uint64_t ui_identity, uint64_t object_generation) {
    if (!touch) return;
    int changed = (!touch->have_context && dm_live_touch_active(touch)) ||
                  (touch->have_context &&
                   (touch->scene_epoch != scene_epoch || touch->world_epoch != world_epoch ||
                    touch->ui_identity != ui_identity ||
                    touch->object_generation != object_generation));
    if (changed) dm_live_touch_cancel_all(touch, touch->last_ns);
    touch->have_context = 1;
    touch->scene_epoch = scene_epoch;
    touch->world_epoch = world_epoch;
    touch->ui_identity = ui_identity;
    touch->object_generation = object_generation;
    dm_live_touch_set_mode(touch, navigation, mask);
}

int dm_live_touch_event(dm_live_touch *touch, int action, int action_index,
                        int pointer_count, const int32_t *ids,
                        const float *x_px, const float *y_px,
                        int viewport_width, int viewport_height,
                        float picture_left, float picture_top,
                        float picture_width, float picture_height,
                        int64_t time_ns) {
    (void)viewport_width; (void)viewport_height;
    if (!touch || !ids || !x_px || !y_px || time_ns < 0 || pointer_count < 0 ||
        pointer_count > DM_TC_MAX_TOUCH) return 0;
    if (action == DM_LIVE_ACTION_CANCEL) return dm_live_touch_cancel_all(touch, time_ns);

    int changing = action == DM_LIVE_ACTION_DOWN || action == DM_LIVE_ACTION_POINTER_DOWN ||
                   action == DM_LIVE_ACTION_UP || action == DM_LIVE_ACTION_POINTER_UP;
    if (changing && (action_index < 0 || action_index >= pointer_count)) return 0;
    for (int i = 0; i < pointer_count; ++i) {
        if (ids[i] < 0 || !isfinite(x_px[i]) || !isfinite(y_px[i])) return 0;
        for (int j = 0; j < i; ++j) if (ids[j] == ids[i]) return 0;
    }

    double t = now_s(touch, time_ns);
    int changed = 0;
    if (action == DM_LIVE_ACTION_DOWN || action == DM_LIVE_ACTION_POINTER_DOWN) {
        int32_t id = ids[action_index];
        int slot = dm_touch_pointer_acquire(&touch->pointers, id);
        if (slot < 0) return 0;
        float px = point_x(touch, x_px[action_index], picture_left, picture_width);
        float py = point_y(touch, y_px[action_index], picture_top, picture_height);
        // Empty skill slot: the original front end consumes this finger before
        // the gesture layer and binds only if it is a tap. Unknown/stale data
        // never enters this branch.
        int target = dm_tc_hit(&touch->state.layout, px, py);
        if (!touch->state.navigation_mode && !touch->skill_bind_active &&
            touch->have_context && target >= DM_TC_MAIN && target <= DM_TC_SKILL4 &&
            (touch->state.enabled_targets & (1u << target))) {
            const dm_tc_target *t = &touch->state.layout.target[target];
            uint64_t key = t->button | (touch->state.skill_set ? (uint64_t)DM_BTN_ZL : 0);
            if (key && skill_slot_empty(touch, t->button, (uint64_t)time_ns,
                                        touch->scene_epoch, touch->world_epoch)) {
                touch->skill_bind[slot].active = 1;
                touch->skill_bind[slot].pointer_slot = slot;
                touch->skill_bind[slot].pointer_id = id;
                touch->skill_bind[slot].key = key;
                touch->skill_bind[slot].skill_set = touch->state.skill_set;
                touch->skill_bind[slot].x0 = touch->skill_bind[slot].x = px;
                touch->skill_bind[slot].y0 = touch->skill_bind[slot].y = py;
                touch->portal_request = 0;
                return 1;
            }
        }
        dm_tc_begin_at(&touch->state, slot, px, py, t);
        if (!touch->state.finger[slot].active) {
            dm_touch_pointer_release(&touch->pointers, id);
            return 0;
        }
        touch->belt_fingers &= ~(1u << slot);
        if (belt_finger_at(touch, slot)) {
            if (touch->state.belt_rows <= 0) {
                // The host HUD is unknown: do not let this touch become a belt
                // gesture merely because the core still has a target there.
                dm_tc_cancel_finger(&touch->state, slot);
                dm_touch_pointer_release(&touch->pointers, id);
                sync_pointers(touch);
                return 0;
            }
            touch->belt_fingers |= 1u << slot;
        }
        // The original iOS front end requests the free town portal from the
        // same fresh valid portal-target down. This is one-shot host state; the
        // core's existing portal modifier/binding fields are not rewritten.
        touch->portal_request = 0;
        if (!touch->state.navigation_mode &&
            touch->state.finger[slot].target == DM_TC_PORTAL)
            touch->portal_request = 1;
        changed = 1;
    } else if (action == DM_LIVE_ACTION_MOVE) {
        for (int i = 0; i < pointer_count; ++i) {
            int slot = dm_touch_pointer_slot(&touch->pointers, ids[i]);
            if (slot < 0) continue;
            if (touch->skill_bind[slot].active) {
                touch->skill_bind[slot].x = point_x(touch, x_px[i], picture_left, picture_width);
                touch->skill_bind[slot].y = point_y(touch, y_px[i], picture_top, picture_height);
                changed = 1;
                continue;
            }
            dm_tc_move_at(&touch->state, slot,
                          point_x(touch, x_px[i], picture_left, picture_width),
                          point_y(touch, y_px[i], picture_top, picture_height), t);
            if (!touch->state.finger[slot].active) {
                touch->belt_fingers &= ~(1u << slot);
                dm_touch_pointer_release(&touch->pointers, ids[i]);
            }
            changed = 1;
        }
    } else if (action == DM_LIVE_ACTION_UP || action == DM_LIVE_ACTION_POINTER_UP) {
        int32_t id = ids[action_index];
        int slot = dm_touch_pointer_slot(&touch->pointers, id);
        if (slot < 0) return 0;
        if (touch->skill_bind[slot].active) {
            float px = point_x(touch, x_px[action_index], picture_left, picture_width);
            float py = point_y(touch, y_px[action_index], picture_top, picture_height);
            uint64_t key = touch->skill_bind[slot].key;
            float dx = px - touch->skill_bind[slot].x0;
            float dy = py - touch->skill_bind[slot].y0;
            int tap = dx * dx + dy * dy <= 14.0f * 14.0f;
            if (tap && key && touch->state.skill_set == touch->skill_bind[slot].skill_set &&
                skill_slot_empty(touch, key & ~(uint64_t)DM_BTN_ZL, (uint64_t)time_ns,
                                 touch->scene_epoch, touch->world_epoch)) {
                touch->skill_bind_request = 1;
                touch->skill_bind_key = key;
                touch->skill_bind_scene_epoch = touch->scene_epoch;
                touch->skill_bind_world_epoch = touch->world_epoch;
            }
            memset(&touch->skill_bind[slot], 0, sizeof touch->skill_bind[slot]);
            dm_touch_pointer_release(&touch->pointers, id);
            changed = 1;
        } else {
            touch->belt_fingers &= ~(1u << slot);
            apply_tap_action(touch);
            dm_tc_end_at(&touch->state, slot, t);
            clear_tap_action(touch);
            dm_touch_pointer_release(&touch->pointers, id);
            changed = 1;
        }
    } else {
        return 0;
    }
    if (changed) dm_tc_tick(&touch->state, t);
    sync_pointers(touch);
    return changed;
}

int dm_live_touch_cancel_all(dm_live_touch *touch, int64_t time_ns) {
    if (!touch || time_ns < 0) return 0;
    (void)now_s(touch, time_ns);
    clear_skill_bind(touch, 0);
    dm_tc_cancel_all(&touch->state);
    dm_touch_pointer_clear(&touch->pointers);
    touch->state.belt_request = 0;
    touch->state.diff_request = 0;
    touch->portal_request = 0;
    touch->belt_fingers = 0;
    touch->pulse_started_ns = 0;
    touch->have_context = 0;
    return 1;
}

void dm_live_touch_cancel_sources(dm_live_touch *touch, int64_t time_ns,
                                  void (*ui_cancel)(void)) {
    if (ui_cancel) ui_cancel();
    if (touch) dm_live_touch_cancel_all(touch, time_ns < 0 ? 0 : time_ns);
}

void dm_live_touch_tick(dm_live_touch *touch, int64_t time_ns) {
    if (!touch || time_ns < 0) return;
    dm_tc_tick(&touch->state, now_s(touch, time_ns));
    sync_pointers(touch);
}

int dm_live_touch_needs_work(const dm_live_touch *touch) {
    if (!touch) return 0;
    if (dm_live_touch_active(touch)) return 1;
    const dm_tc_state *s = &touch->state;
    return s->pulse_button != 0 || s->defer_button != 0 ||
           s->pointer_pulse_pending || s->belt_gap_cell != 0 ||
           s->belt_q > 0 || s->close_left > 0 || s->stash_x_check_s > 0;
}

int dm_live_touch_scheduler_step(dm_live_touch *touch, int64_t time_ns,
                                  uint64_t samples, int cursor_pending) {
    if (!touch || time_ns < 0) return 0;
    dm_tc_tick(&touch->state, now_s(touch, time_ns));
    if (touch->state.pulse_button) {
        if (!touch->pulse_started_ns || touch->pulse_epoch != touch->state.pulse_epoch) {
            touch->pulse_epoch = touch->state.pulse_epoch;
            touch->pulse_started_ns = time_ns;
            touch->pulse_samples_start = samples;
            touch->pulse_seen_ns = touch->pulse_free_ns = 0;
        }
        if (samples < touch->pulse_samples_start) goto unacknowledged;
        if (!touch->pulse_seen_ns && samples > touch->pulse_samples_start)
            touch->pulse_seen_ns = time_ns;
        if (cursor_pending) touch->pulse_free_ns = 0;
        if (touch->pulse_seen_ns && !touch->pulse_free_ns && !cursor_pending) {
            touch->pulse_free_ns = time_ns;
            touch->pulse_samples_free = samples;
        }
        int64_t hold = (int64_t)(touch->state.pulse_hold_s * 1000000000.0);
        // Withheld A polls do not acknowledge a pickup: keep its origin fixed
        // for three polls and 120ms after the cursor releases A to the game.
        int taken = touch->pulse_free_ns && time_ns - touch->pulse_free_ns >= 120000000 &&
            samples >= touch->pulse_samples_free && samples - touch->pulse_samples_free >= 3 && !cursor_pending &&
            (hold <= 0 || (touch->pulse_free_ns && time_ns - touch->pulse_free_ns >= hold));
        if (taken) {
            dm_tc_pulse_end(&touch->state, touch->pulse_epoch);
            touch->pulse_samples_start = samples;
            touch->pulse_started_ns = time_ns;
        } else if (time_ns - touch->pulse_started_ns > 2000000000LL + hold) {
            goto unacknowledged;
        }
    } else if (dm_tc_pulse_pending(&touch->state)) {
        if (samples < touch->pulse_samples_start) goto unacknowledged;
        // The queued drop must see two real released polls before its next A.
        if (samples - touch->pulse_samples_start >= 2) {
            dm_tc_pulse_resume(&touch->state);
            touch->pulse_started_ns = 0;
        } else if (time_ns - touch->pulse_started_ns > 500000000) {
            goto unacknowledged;
        }
    } else {
        touch->pulse_started_ns = 0;
    }
    sync_pointers(touch);
    return dm_live_touch_needs_work(touch);
unacknowledged:
    // A timeout cancels; it must never mark an unreceived pickup as successful.
    dm_live_touch_cancel_all(touch, time_ns);
    return -1;
}

uint64_t dm_live_touch_buttons(const dm_live_touch *touch) { return touch ? touch->state.buttons : 0; }
int dm_live_touch_skill_set(const dm_live_touch *touch) { return touch ? touch->state.skill_set : 0; }
uint64_t dm_live_touch_target_button(const dm_live_touch *touch, int target) {
    return touch && target >= 0 && target < DM_TC_COUNT ? touch->state.layout.target[target].button : 0;
}
int32_t dm_live_touch_lx(const dm_live_touch *touch) { return touch ? touch->state.lx : 0; }
int32_t dm_live_touch_ly(const dm_live_touch *touch) { return touch ? touch->state.ly : 0; }
int32_t dm_live_touch_rx(const dm_live_touch *touch) { return touch ? touch->state.rx : 0; }
int32_t dm_live_touch_ry(const dm_live_touch *touch) { return touch ? touch->state.ry : 0; }
int dm_live_touch_visual_snapshot(const dm_live_touch *touch, dm_live_touch_visual *out) {
    if (!touch || !out) return 0;
    memset(out, 0, sizeof *out);
    const dm_tc_layout *l = &touch->state.layout;
    if (!(l->width > 0 && l->height > 0)) return 0;
    out->valid = 1;
    out->width = l->width; out->height = l->height;
    out->joy_radius = l->joy_radius;
    out->joy_rest_x = l->joy_x; out->joy_rest_y = l->joy_y;
    out->joy_active = touch->state.joy_slot >= 0;
    out->joy_origin_x = out->joy_active ? touch->state.joy_origin_x : 0;
    out->joy_origin_y = out->joy_active ? touch->state.joy_origin_y : 0;
    out->joy_x = out->joy_active ? out->joy_origin_x : out->joy_rest_x;
    out->joy_y = out->joy_active ? out->joy_origin_y : out->joy_rest_y;
    out->swap_x = l->swap.x; out->swap_y = l->swap.y; out->swap_radius = l->swap.r;
    out->diff_x = l->diff.x; out->diff_y = l->diff.y; out->diff_radius = l->diff.r;
    for (int i = 0; i < 8; ++i) {
        out->pick_x[i] = l->pick[i].x; out->pick_y[i] = l->pick[i].y; out->pick_r[i] = l->pick[i].r;
    }
    for (int i = 0; i < DM_TC_COUNT; ++i) {
        out->target_x[i] = l->target[i].x; out->target_y[i] = l->target[i].y; out->target_r[i] = l->target[i].r;
    }
    out->enabled_targets = touch->state.enabled_targets;
    out->buttons = touch->state.buttons;
    out->skill_set = touch->state.skill_set;
    out->diff_open = touch->state.diff_open;
    out->navigation_mode = touch->state.navigation_mode;
    out->lx = touch->state.lx; out->ly = touch->state.ly;
    return 1;
}
uint64_t dm_live_touch_visual_signature(const dm_live_touch_visual *v,
                                        const dm_live_touch_geometry *g) {
    uint64_t h = 1469598103934665603ull;
    const uint64_t parts[] = {(uint64_t)v->buttons, (uint64_t)(uint32_t)v->lx,
        (uint64_t)(uint32_t)v->ly, (uint64_t)v->skill_set, (uint64_t)v->diff_open,
        (uint64_t)v->navigation_mode, (uint64_t)v->frontend, (uint64_t)v->joy_active,
        (uint64_t)(uint32_t)v->joy_origin_x,
        (uint64_t)(uint32_t)v->joy_origin_y, (uint64_t)v->enabled_targets,
        (uint64_t)(uint32_t)g->left, (uint64_t)(uint32_t)g->top,
        (uint64_t)(uint32_t)g->width, (uint64_t)(uint32_t)g->height};
    for (unsigned i = 0; i < sizeof parts / sizeof *parts; i++) h = (h ^ parts[i]) * 1099511628211ull;
    return h;
}

int dm_live_touch_has_pointer(const dm_live_touch *touch, int32_t id) {
    return touch && id >= 0 && dm_touch_pointer_slot(&touch->pointers, id) >= 0;
}

int dm_live_touch_active(const dm_live_touch *touch) {
    if (!touch) return 0;
    for (int i = 0; i < DM_TC_MAX_TOUCH; ++i)
        if (touch->pointers.platform[i] >= 0 && touch->state.finger[i].active) return 1;
    return 0;
}

int dm_live_touch_pointer_long(const dm_live_touch *touch) {
    if (!touch) return 0;
    int slot = touch->state.pointer_slot;
    return slot >= 0 && slot < DM_TC_MAX_TOUCH && touch->state.finger[slot].active &&
           touch->state.finger[slot].pointer_long;
}

int dm_live_touch_navigation(const dm_live_touch *touch) {
    return touch ? touch->state.navigation_mode : 0;
}

int dm_live_touch_pointer_slot_for_id(const dm_live_touch *touch, int32_t id) {
    if (!touch || id < 0) return -1;
    return dm_touch_pointer_slot(&touch->pointers, id);
}

int dm_live_touch_pointer_slot(const dm_live_touch *touch) {
    return touch ? touch->state.pointer_slot : -1;
}

int dm_live_touch_pointer_dragged_for_id(const dm_live_touch *touch, int32_t id) {
    int slot = dm_live_touch_pointer_slot_for_id(touch, id);
    return slot >= 0 && slot < DM_TC_MAX_TOUCH && touch->state.finger[slot].active &&
           touch->state.finger[slot].pointer_dragged;
}

float dm_live_touch_pointer_lift_norm(const dm_live_touch *touch) {
    if (!touch || touch->height <= 0) return 0;
    return DM_TC_POINTER_LIFT_PT * dm_tc_scale_for_size(touch->width, touch->height) / touch->height;
}

int dm_live_touch_pointer_position(const dm_live_touch *touch,
                                   float *x, float *y, float *w, float *h) {
    if (!touch || !touch->state.pointer_valid) return 0;
    if (x) *x = touch->state.pointer_x;
    if (y) *y = touch->state.pointer_y;
    if (w) *w = touch->width;
    if (h) *h = touch->height;
    return 1;
}

void dm_live_touch_apply_item_hit(dm_live_touch *touch, int slot, int kind) {
    if (!touch || slot < 0 || slot >= DM_TC_MAX_TOUCH) return;
    if (!touch->state.finger[slot].active || touch->state.pointer_slot != slot) return;
    dm_tc_pointer_item(&touch->state, kind);
}

void dm_live_touch_apply_drop(dm_live_touch *touch, int kind, float nx, float ny) {
    if (!touch) return;
    if (kind == 5 && isfinite(nx) && isfinite(ny)) {
        float x = nx * touch->width;
        float y = ny * touch->height;
        dm_tc_pointer_release_at(&touch->state, x, y);
    } else {
        // 7/11/unsupported are deliberately mapped to "put back" by the core,
        // never to a sell/advanced-page action from this seam.
        dm_tc_pointer_release(&touch->state, kind);
    }
}

int dm_live_touch_finish_pointer(dm_live_touch *touch, int32_t id, int64_t time_ns) {
    if (!touch || id < 0 || time_ns < 0) return 0;
    int slot = dm_live_touch_pointer_slot_for_id(touch, id);
    if (slot < 0) return 0;
    double t = now_s(touch, time_ns);
    apply_tap_action(touch);
    dm_tc_end_at(&touch->state, slot, t);
    clear_tap_action(touch);
    dm_touch_pointer_release(&touch->pointers, id);
    dm_tc_tick(&touch->state, t);
    sync_pointers(touch);
    return 1;
}

void dm_live_touch_item_snapshot_get(const dm_live_touch *touch,
                                     dm_live_touch_item_snapshot *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!touch) return;
    const dm_tc_state *s = &touch->state;
    out->use_seq = s->use_seq;
    out->ident_seq = s->ident_seq;
    out->item_tap_seq = s->item_tap_seq;
    out->long_press_seq = s->long_press_seq;
    out->stack_take_seq = s->stack_take_seq;
    out->tab_tap_seq = s->tab_tap_seq;
    out->mouse_click_seq = s->mouse_click_seq;
    out->shop_drop_seq = s->shop_drop_seq;
    out->mouse_click_x = touch->width > 0 ? s->mouse_click_x / touch->width : 0;
    out->mouse_click_y = touch->height > 0 ? s->mouse_click_y / touch->height : 0;
    out->pointer_dragged = s->pointer_slot >= 0 && s->pointer_slot < DM_TC_MAX_TOUCH ?
        s->finger[s->pointer_slot].pointer_dragged : 0;
    out->pointer_shop = s->pointer_shop;
    out->pointer_stack = s->pointer_stack;
}

int dm_live_touch_end_released_pulse(dm_live_touch *touch) {
    if (!touch || !touch->state.pulse_button || touch->state.pulse_hold_s > 0) return 0;
    // The input callback's publish-once shortcut must not lift a pickup or
    // release a drop before the owner scheduler observes game input receipts.
    if (touch->state.pulse_button == DM_BTN_A &&
        (touch->state.pointer_pulse_active || touch->state.pointer_pulse_pending)) return 0;
    int ended = dm_tc_pulse_end(&touch->state, touch->state.pulse_epoch);
    touch->pulse_started_ns = 0;
    return ended;
}
