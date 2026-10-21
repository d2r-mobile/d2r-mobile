#include "live_touch.h"
#include "../../ios/Sources/dm_touch_controls.h"
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
    int have_context;
    uint64_t scene_epoch, world_epoch, ui_identity, object_generation;
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
        if (touch->pointers.platform[i] >= 0 && !touch->state.finger[i].active) {
            touch->pointers.platform[i] = -1;
        }
    }
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
    touch->pulse_started_ns = 0;
    touch->width = width_pt;
    touch->height = height_pt;
    dm_touch_pointer_clear(&touch->pointers);
}

void dm_live_touch_set_mode(dm_live_touch *touch, int navigation, uint32_t mask) {
    if (!touch) return;
    navigation = !!navigation;
    if (navigation != touch->state.navigation_mode) dm_tc_set_navigation_mode(&touch->state, navigation);
    mask &= (1u << DM_TC_COUNT) - 1;
    if (mask != touch->state.enabled_targets) dm_tc_enable_targets(&touch->state, mask);
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
        dm_tc_begin_at(&touch->state, slot,
                       point_x(touch, x_px[action_index], picture_left, picture_width),
                       point_y(touch, y_px[action_index], picture_top, picture_height), t);
        if (!touch->state.finger[slot].active) {
            dm_touch_pointer_release(&touch->pointers, id);
            return 0;
        }
        changed = 1;
    } else if (action == DM_LIVE_ACTION_MOVE) {
        for (int i = 0; i < pointer_count; ++i) {
            int slot = dm_touch_pointer_slot(&touch->pointers, ids[i]);
            if (slot < 0) continue;
            dm_tc_move_at(&touch->state, slot,
                          point_x(touch, x_px[i], picture_left, picture_width),
                          point_y(touch, y_px[i], picture_top, picture_height), t);
            if (!touch->state.finger[slot].active) dm_touch_pointer_release(&touch->pointers, ids[i]);
            changed = 1;
        }
    } else if (action == DM_LIVE_ACTION_UP || action == DM_LIVE_ACTION_POINTER_UP) {
        int32_t id = ids[action_index];
        int slot = dm_touch_pointer_slot(&touch->pointers, id);
        if (slot < 0) return 0;
        dm_tc_end_at(&touch->state, slot, t);
        dm_touch_pointer_release(&touch->pointers, id);
        changed = 1;
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
    dm_tc_cancel_all(&touch->state);
    dm_touch_pointer_clear(&touch->pointers);
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
