#include "touch_adapter.h"
#include "dm_touch_controls.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

struct dm_android_touch {
    dm_android_pad *pad;
    dm_tc_state state;
    int32_t pointer_id[DM_TC_MAX_TOUCH];
    uint8_t used[DM_TC_MAX_TOUCH];
    int64_t last_ns;
    int suspended;
    /* Bounded pending release-to-cast pulses, each with its own button/aim.
       A same-button repeat is separated by one neutral read. */
    struct {
        uint64_t buttons;
        int32_t rx, ry;
    } pulses[DM_ANDROID_TOUCH_PULSE_CAPACITY];
    int pulse_head;
    int pulse_count;
    int release_waiting;
    int game_available;
    int game_scene_epoch_set;
    uint64_t game_scene_epoch;
    uint64_t game_world_epoch;
    uint64_t game_ui_identity;
    uint64_t game_object_generation;
    uint32_t game_panel_bits;
    uint64_t seen_world_tap;
    uint64_t seen_ident;
    uint64_t seen_use;
    uint64_t seen_long_press;
    uint64_t seen_stack_take;
    uint64_t seen_tab_tap;
    uint64_t seen_shop_drop;
    uint64_t seen_mouse_click;
};

static int accept_time(dm_android_touch *touch, int64_t time_ns, double *now_s) {
    if (time_ns < 0) return 0;
    if (time_ns < touch->last_ns) time_ns = touch->last_ns;   /* 时钟回拨不许倒放定时器 | a clock rollback must not rewind timers */
    touch->last_ns = time_ns;
    if (now_s) *now_s = (double)time_ns / 1e9;
    return 1;
}

static int find_pointer(const dm_android_touch *touch, int32_t pointer_id) {
    for (int i = 0; i < DM_TC_MAX_TOUCH; ++i)
        if (touch->used[i] && touch->pointer_id[i] == pointer_id) return i;
    return -1;
}

static int free_slot(const dm_android_touch *touch) {
    for (int i = 0; i < DM_TC_MAX_TOUCH; ++i) if (!touch->used[i]) return i;
    return -1;
}

/* One coherent publish per event. A release-to-cast pulse is captured before
   the core pulse is withdrawn and queued in bounded acceptance order. The
   oldest queued pulse is published until dm_android_touch_read takes it; a
   same-button successor gets a neutral release read in between. */
static void publish_core(dm_android_touch *touch) {
    dm_tc_state *s = &touch->state;
    dm_pad_sample sample = {
        .buttons = s->buttons,
        .lx = s->lx,
        .ly = s->ly,
        .rx = s->rx,
        .ry = s->ry,
        .connected = 1,
    };
    dm_android_pad_publish(touch->pad, &sample);
}

static void publish_entry(dm_android_touch *touch, uint64_t buttons, int32_t rx, int32_t ry) {
    dm_tc_state *s = &touch->state;
    dm_pad_sample sample = {
        .buttons = buttons,
        .lx = s->lx,
        .ly = s->ly,
        .rx = rx,
        .ry = ry,
        .connected = 1,
    };
    dm_android_pad_publish(touch->pad, &sample);
}

static int pending_full(const dm_android_touch *touch) {
    return touch->pulse_count >= DM_ANDROID_TOUCH_PULSE_CAPACITY;
}

static void publish(dm_android_touch *touch) {
    dm_tc_state *s = &touch->state;
    if (s->pulse_button) {
        if (pending_full(touch)) return;   /* defensive; callers backpressure first */
        int tail = (touch->pulse_head + touch->pulse_count) % DM_ANDROID_TOUCH_PULSE_CAPACITY;
        touch->pulses[tail].buttons = s->buttons;
        touch->pulses[tail].rx = s->pulse_rx;
        touch->pulses[tail].ry = s->pulse_ry;
        ++touch->pulse_count;
        uint64_t epoch = s->pulse_epoch;
        (void)dm_tc_pulse_end(s, epoch);
    }
    if (touch->pulse_count > 0) {
        if (touch->release_waiting) {
            publish_core(touch);   /* consumer boundary: ticks/moves cannot overwrite the release read */
        } else {
            publish_entry(touch, touch->pulses[touch->pulse_head].buttons,
                          touch->pulses[touch->pulse_head].rx, touch->pulses[touch->pulse_head].ry);
        }
    } else {
        publish_core(touch);
    }
}

static void action_baseline(dm_android_touch *touch) {
    dm_tc_state *s = &touch->state;
    touch->seen_world_tap = s->world_tap_seq;
    touch->seen_ident = s->ident_seq;
    touch->seen_use = s->use_seq;
    touch->seen_long_press = s->long_press_seq;
    touch->seen_stack_take = s->stack_take_seq;
    touch->seen_tab_tap = s->tab_tap_seq;
    touch->seen_shop_drop = s->shop_drop_seq;
    touch->seen_mouse_click = s->mouse_click_seq;
}

static void release_all(dm_android_touch *touch) {
    memset(touch->pointer_id, 0, sizeof touch->pointer_id);
    memset(touch->used, 0, sizeof touch->used);
    touch->pulse_head = touch->pulse_count = 0;
    touch->release_waiting = 0;
    dm_tc_cancel_all(&touch->state);
    action_baseline(touch);
    dm_android_pad_clear_edges(touch->pad);
}

dm_android_touch *dm_android_touch_create(dm_android_pad *pad, float width_pt, float height_pt) {
    if (!pad || !isfinite(width_pt) || !isfinite(height_pt) || width_pt <= 0 || height_pt <= 0)
        return NULL;
    dm_android_touch *touch = calloc(1, sizeof *touch);
    if (!touch) return NULL;
    touch->pad = pad;
    dm_tc_init(&touch->state, width_pt, height_pt);
    touch->game_available = 1;
    action_baseline(touch);
    memset(touch->pointer_id, -1, sizeof touch->pointer_id);
    publish(touch);
    return touch;
}

void dm_android_touch_destroy(dm_android_touch *touch) {
    if (!touch) return;
    /* Safe shutdown for a surviving pad: drop unread edges and publish neutral
       before freeing the adapter. The pad remains owned by the caller. */
    dm_android_pad_clear_edges(touch->pad);
    dm_tc_cancel_all(&touch->state);
    dm_pad_sample neutral = {.connected = 1};
    dm_android_pad_publish(touch->pad, &neutral);
    free(touch);
}

int dm_android_touch_down(dm_android_touch *touch, int32_t pointer_id,
                          float x_pt, float y_pt, int64_t time_ns) {
    if (!touch || touch->suspended || !touch->game_available || pointer_id < 0 ||
        !isfinite(x_pt) || !isfinite(y_pt))
        return 0;
    if (pending_full(touch)) return 0;   /* bounded backpressure: no partial mutation */
    if (find_pointer(touch, pointer_id) >= 0) return 0;   /* duplicate down: keep the first owner */
    int slot = free_slot(touch);
    if (slot < 0) return 0;
    double now_s;
    if (!accept_time(touch, time_ns, &now_s)) return 0;
    dm_tc_begin_at(&touch->state, slot, x_pt, y_pt, now_s);
    if (!touch->state.finger[slot].active) return 0;
    touch->used[slot] = 1;
    touch->pointer_id[slot] = pointer_id;
    publish(touch);
    return 1;
}

int dm_android_touch_move(dm_android_touch *touch, int32_t pointer_id,
                          float x_pt, float y_pt, int64_t time_ns) {
    if (!touch || touch->suspended || !touch->game_available || pointer_id < 0 ||
        !isfinite(x_pt) || !isfinite(y_pt)) return 0;
    if (pending_full(touch)) return 0;
    int slot = find_pointer(touch, pointer_id);
    if (slot < 0) return 0;
    double now_s;
    if (!accept_time(touch, time_ns, &now_s)) return 0;
    dm_tc_move_at(&touch->state, slot, x_pt, y_pt, now_s);
    publish(touch);
    return 1;
}

int dm_android_touch_up(dm_android_touch *touch, int32_t pointer_id, int64_t time_ns) {
    if (!touch || touch->suspended || !touch->game_available || pointer_id < 0) return 0;
    if (pending_full(touch)) return 0;   /* leave the pointer intact for retry after read */
    int slot = find_pointer(touch, pointer_id);
    if (slot < 0) return 0;
    double now_s;
    if (!accept_time(touch, time_ns, &now_s)) return 0;
    dm_tc_end_at(&touch->state, slot, now_s);
    touch->used[slot] = 0;
    touch->pointer_id[slot] = -1;
    publish(touch);
    return 1;
}

int dm_android_touch_cancel(dm_android_touch *touch, int32_t pointer_id, int64_t time_ns) {
    if (!touch || touch->suspended || pointer_id < 0) return 0;
    int slot = find_pointer(touch, pointer_id);
    if (slot < 0) return 0;
    double now_s;
    if (!accept_time(touch, time_ns, &now_s)) return 0;
    uint64_t canceled_mask = 0;
    const dm_tc_finger *f = &touch->state.finger[slot];
    if (f->active && f->target >= 0 && f->target < DM_TC_COUNT) {
        canceled_mask = touch->state.layout.target[f->target].button;
    }
    /* ACTION_CANCEL's own timestamp starts the 0.35 s stick-resume window;
       the core records it from last_time_s, which only begin/move/end/tick
       otherwise advance. Without this, a finger held still between the last
       move and the cancel loses a valid resume (or keeps a stale one). */
    touch->state.last_time_s = now_s;
    dm_tc_cancel_finger(&touch->state, slot);
    touch->used[slot] = 0;
    touch->pointer_id[slot] = -1;
    dm_android_pad_clear_edges_mask(touch->pad, canceled_mask);
    publish(touch);
    return 1;
}

int dm_android_touch_cancel_all(dm_android_touch *touch, int64_t time_ns) {
    if (!touch || !accept_time(touch, time_ns, NULL)) return 0;
    release_all(touch);
    publish(touch);
    return 1;
}

int dm_android_touch_focus_lost(dm_android_touch *touch, int64_t time_ns) {
    if (!touch || !accept_time(touch, time_ns, NULL)) return 0;
    release_all(touch);
    touch->suspended = 1;
    publish(touch);
    return 1;
}

int dm_android_touch_pause(dm_android_touch *touch, int64_t time_ns) {
    if (!touch || !accept_time(touch, time_ns, NULL)) return 0;
    release_all(touch);
    touch->suspended = 1;
    publish(touch);
    return 1;
}

int dm_android_touch_resume(dm_android_touch *touch, int64_t time_ns) {
    if (!touch || !accept_time(touch, time_ns, NULL)) return 0;
    touch->suspended = 0;
    publish(touch);
    return 1;
}

int dm_android_touch_resize(dm_android_touch *touch, float width_pt, float height_pt,
                            int64_t time_ns) {
    if (!touch || !isfinite(width_pt) || !isfinite(height_pt) || width_pt <= 0 || height_pt <= 0)
        return 0;
    if (!accept_time(touch, time_ns, NULL)) return 0;
    release_all(touch);
    dm_tc_resize(&touch->state, width_pt, height_pt);
    publish(touch);
    return 1;
}

int dm_android_touch_set_game_available(dm_android_touch *touch, int available) {
    if (!touch) return 0;
    available = available != 0;
    if (touch->game_available == available) return 1;
    touch->game_available = available;
    if (!available) release_all(touch);
    publish(touch);
    return 1;
}

int dm_android_touch_set_game_context(dm_android_touch *touch, int navigation_mode,
                                      uint32_t panel_bits, uint64_t scene_epoch,
                                      uint64_t world_epoch, uint64_t ui_identity,
                                      uint64_t object_generation) {
    if (!touch || scene_epoch == 0) return 0;
    navigation_mode = navigation_mode != 0;
    int changed = navigation_mode != touch->state.navigation_mode ||
                  (touch->game_scene_epoch_set &&
                   (panel_bits != touch->game_panel_bits ||
                    scene_epoch != touch->game_scene_epoch ||
                    world_epoch != touch->game_world_epoch ||
                    ui_identity != touch->game_ui_identity ||
                    object_generation != touch->game_object_generation));
    if (changed) release_all(touch);
    dm_tc_set_panel_state((int)panel_bits);
    if (navigation_mode != touch->state.navigation_mode)
        dm_tc_set_navigation_mode(&touch->state, navigation_mode);
    touch->game_panel_bits = panel_bits;
    touch->game_scene_epoch = scene_epoch;
    touch->game_world_epoch = world_epoch;
    touch->game_ui_identity = ui_identity;
    touch->game_object_generation = object_generation;
    touch->game_scene_epoch_set = 1;
    publish(touch);
    return 1;
}

int dm_android_touch_set_belt_context(dm_android_touch *touch, int rows, int open) {
    if (!touch || rows < 0 || rows > 4 || (open != 0 && open != 1)) return 0;
    if (touch->state.belt_rows == rows && touch->state.belt_open == open) return 1;
    /* A real context change is a gesture boundary: a finger pressed under the
       old rows/open must not be resolved against the new one. Use the core's
       cancel-all semantics (same path as scene/panel changes), then store the
       host snapshot. Requests already produced stay one-shot in the core. */
    release_all(touch);
    touch->state.belt_rows = rows;
    touch->state.belt_open = open;
    publish(touch);
    return 1;
}

int dm_android_touch_pointer_hit(dm_android_touch *touch, int32_t pointer_id, int kind) {
    if (!touch || pointer_id < 0 || kind < 0 || kind > 11) return 0;
    int slot = find_pointer(touch, pointer_id);
    if (slot < 0) return 0;
    dm_tc_pointer_item(&touch->state, kind);
    return 1;
}

int dm_android_touch_pointer_release(dm_android_touch *touch, int32_t pointer_id,
                                     int drop_kind) {
    if (!touch || pointer_id < 0 || drop_kind < 0 || drop_kind > 8) return 0;
    int slot = find_pointer(touch, pointer_id);
    if (slot < 0) return 0;
    dm_tc_pointer_release(&touch->state, drop_kind);
    return 1;
}

int dm_android_touch_take_action(dm_android_touch *touch, dm_android_touch_action *out) {
    if (!touch || !out) return 0;
    dm_tc_state *s = &touch->state;
    memset(out, 0, sizeof(*out));
    if (s->world_tap_seq != touch->seen_world_tap) {
        touch->seen_world_tap = s->world_tap_seq;
        out->kind = DM_ANDROID_ACTION_WORLD_TAP;
        out->x = s->world_tap_x; out->y = s->world_tap_y;
        out->sequence = s->world_tap_seq;
        return 1;
    }
    if (s->ident_seq != touch->seen_ident) {
        touch->seen_ident = s->ident_seq;
        out->kind = DM_ANDROID_ACTION_IDENTIFY;
        out->x = s->pointer_x; out->y = s->pointer_y;
        out->sequence = s->ident_seq;
        return 1;
    }
    if (s->use_seq != touch->seen_use) {
        touch->seen_use = s->use_seq;
        out->kind = DM_ANDROID_ACTION_USE;
        out->x = s->pointer_x; out->y = s->pointer_y;
        out->sequence = s->use_seq;
        return 1;
    }
    if (s->long_press_seq != touch->seen_long_press) {
        touch->seen_long_press = s->long_press_seq;
        out->kind = DM_ANDROID_ACTION_LONG_PRESS;
        out->x = s->pointer_x; out->y = s->pointer_y;
        out->sequence = s->long_press_seq;
        return 1;
    }
    if (s->stack_take_seq != touch->seen_stack_take) {
        touch->seen_stack_take = s->stack_take_seq;
        out->kind = DM_ANDROID_ACTION_STACK_TAKE;
        out->x = s->pointer_x; out->y = s->pointer_y;
        out->sequence = s->stack_take_seq;
        return 1;
    }
    if (s->tab_tap_seq != touch->seen_tab_tap) {
        touch->seen_tab_tap = s->tab_tap_seq;
        out->kind = DM_ANDROID_ACTION_TAB_TAP;
        out->x = s->pointer_x; out->y = s->pointer_y;
        out->sequence = s->tab_tap_seq;
        return 1;
    }
    if (s->shop_drop_seq != touch->seen_shop_drop) {
        touch->seen_shop_drop = s->shop_drop_seq;
        out->kind = DM_ANDROID_ACTION_SHOP_DROP;
        out->x = s->pointer_x; out->y = s->pointer_y;
        out->sequence = s->shop_drop_seq;
        return 1;
    }
    if (s->mouse_click_seq != touch->seen_mouse_click) {
        touch->seen_mouse_click = s->mouse_click_seq;
        out->kind = DM_ANDROID_ACTION_UI_CLICK;
        out->x = s->mouse_click_x; out->y = s->mouse_click_y;
        out->sequence = s->mouse_click_seq;
        return 1;
    }
    if (s->diff_request) {
        out->kind = DM_ANDROID_ACTION_DIFFICULTY;
        out->x = (float)s->diff_request;
        out->sequence = (uint64_t)s->diff_request;
        s->diff_request = 0;
        return 1;
    }
    if (s->belt_request) {
        out->kind = DM_ANDROID_ACTION_BELT;
        out->x = (float)s->belt_request;
        out->sequence = (uint64_t)s->belt_request;
        s->belt_request = 0;
        return 1;
    }
    return 0;
}

int dm_android_touch_tick(dm_android_touch *touch, int64_t time_ns) {
    double now_s;
    if (!touch) return 0;
    if (pending_full(touch)) return 0;   /* backpressure before timer/core mutation */
    if (!accept_time(touch, time_ns, &now_s)) return 0;
    if (!touch->suspended) {
        dm_tc_tick(&touch->state, now_s);
        publish(touch);
    } else {
        publish(touch);
    }
    return 1;
}

dm_npad_state dm_android_touch_read(dm_android_touch *touch) {
    dm_npad_state out = {0};
    if (!touch) return out;
    if (touch->release_waiting) {
        touch->release_waiting = 0;
        return dm_android_pad_read(touch->pad);   /* neutral/held release between same-button pulses */
    }
    if (touch->pulse_count == 0) return dm_android_pad_read(touch->pad);

    int head = touch->pulse_head;
    uint64_t buttons = touch->pulses[head].buttons;
    int32_t rx = touch->pulses[head].rx, ry = touch->pulses[head].ry;
    touch->pulse_head = (head + 1) % DM_ANDROID_TOUCH_PULSE_CAPACITY;
    --touch->pulse_count;
    publish_entry(touch, buttons, rx, ry);
    out = dm_android_pad_read(touch->pad);
    if (touch->pulse_count > 0 && touch->pulses[touch->pulse_head].buttons == buttons) {
        touch->release_waiting = 1;
        publish_core(touch);   /* make the next read a release, not a duplicate edge */
    } else if (touch->pulse_count == 0) {
        publish_core(touch);   /* last pulse consumed: next read is neutral/held */
    }
    return out;
}
