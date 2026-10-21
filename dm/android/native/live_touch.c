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
    int64_t pulse_started_ns;
    uint32_t belt_fingers;
    int portal_request;
    int have_context;
    uint64_t scene_epoch, world_epoch, ui_identity, object_generation;
    dm_tg_snapshot skill_snapshot;
    int skill_snapshot_valid;
    uint64_t skill_scene_epoch, skill_world_epoch;
    int skill_bind_active;
    uint64_t skill_bind_request, skill_bind_key, skill_bind_scene_epoch, skill_bind_world_epoch;
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
    dm_touch_pointer_clear(&touch->pointers);
    return touch;
}

void dm_live_touch_destroy(dm_live_touch *touch) {
    if (!touch) return;
    dm_tc_cancel_all(&touch->state);
    free(touch);
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
            dm_tc_end_at(&touch->state, slot, t);
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

int dm_live_touch_scheduler_step(dm_live_touch *touch, int64_t time_ns) {
    if (!touch || time_ns < 0) return 0;
    dm_tc_tick(&touch->state, now_s(touch, time_ns));
    if (!touch->state.pulse_button && touch->state.pointer_pulse_pending)
        dm_tc_pulse_resume(&touch->state);
    if (touch->state.pulse_button) {
        if (touch->pulse_started_ns == 0) touch->pulse_started_ns = time_ns;
        int64_t hold_ns = (int64_t)(touch->state.pulse_hold_s * 1000000000.0);
        if (hold_ns < 200000000) hold_ns = 200000000;
        hold_ns += 100000000;
        if (time_ns - touch->pulse_started_ns >= hold_ns) {
            dm_tc_pulse_end(&touch->state, touch->state.pulse_epoch);
            touch->pulse_started_ns = 0;
        }
    } else {
        touch->pulse_started_ns = 0;
    }
    sync_pointers(touch);
    return dm_live_touch_needs_work(touch);
}

uint64_t dm_live_touch_buttons(const dm_live_touch *touch) { return touch ? touch->state.buttons : 0; }
int32_t dm_live_touch_lx(const dm_live_touch *touch) { return touch ? touch->state.lx : 0; }
int32_t dm_live_touch_ly(const dm_live_touch *touch) { return touch ? touch->state.ly : 0; }
int32_t dm_live_touch_rx(const dm_live_touch *touch) { return touch ? touch->state.rx : 0; }
int32_t dm_live_touch_ry(const dm_live_touch *touch) { return touch ? touch->state.ry : 0; }

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

int dm_live_touch_end_released_pulse(dm_live_touch *touch) {
    if (!touch || !touch->state.pulse_button || touch->state.pulse_hold_s > 0) return 0;
    int ended = dm_tc_pulse_end(&touch->state, touch->state.pulse_epoch);
    touch->pulse_started_ns = 0;
    return ended;
}
