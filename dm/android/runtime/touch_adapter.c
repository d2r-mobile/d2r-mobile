#include "touch_adapter.h"
#include "dm_touch_controls.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

struct dm_android_touch {
    dm_android_pad *pad;
    dm_tc_state state;
    int32_t pointer_id[DM_TC_MAX_TOUCH];
    uint64_t pointer_token[DM_TC_MAX_TOUCH];
    uint64_t next_token;
    uint8_t used[DM_TC_MAX_TOUCH];
    int64_t last_ns;
    int suspended;
    /* Bounded pending release-to-cast pulses, each with its own button/aim.
       A same-button repeat is separated by one neutral read. */
    struct {
        uint64_t buttons;
        int32_t rx, ry;
        double hold_s;
        int hold_started;
        int64_t hold_until_ns;
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
    int32_t use_pointer;
    uint64_t use_token;
    float use_x, use_y;
    int use_ambiguous;
    int portal_available;
    uint64_t portal_mask;
    uint64_t portal_identity;
    uint64_t portal_scene;
    uint64_t portal_seen_epoch;
    int portal_request_pending;
    dm_android_portal_request portal_request;
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

static void portal_sync(dm_android_touch *touch) {
    dm_tc_state *s = &touch->state;
    if (s->portal_phase == 0 || !touch->portal_available || !touch->portal_mask) {
        touch->portal_request_pending = 0;
        return;
    }
    /* One request per armed gesture epoch: a second portal finger while the
       portal is already pending/armed does not re-arm in the core. */
    if (!touch->portal_request_pending && s->gesture_epoch != touch->portal_seen_epoch) {
        touch->portal_request.gesture_epoch = s->gesture_epoch;
        touch->portal_request.binding_identity = touch->portal_identity;
        touch->portal_request.scene_epoch = touch->portal_scene;
        touch->portal_request.mask = touch->portal_mask;
        touch->portal_request.phase = s->portal_phase;
        touch->portal_seen_epoch = s->gesture_epoch;
        touch->portal_request_pending = 1;
    }
}

static void sync_pointer_slots(dm_android_touch *touch) {
    /* Core-owned cancellation (e.g. dm_tc_set_portal_binding clearing portal
       fingers) must release the matching adapter IDs exactly once. Unrelated
       active fingers keep their slots; a stale MOVE/UP then finds no owner and
       a reused ID can press again instead of being a duplicate. */
    for (int i = 0; i < DM_TC_MAX_TOUCH; ++i) {
        if (touch->used[i] && !touch->state.finger[i].active) {
            touch->used[i] = 0;
            touch->pointer_id[i] = -1;
        }
    }
}

static int timed_head_due(const dm_android_touch *touch, int64_t time_ns) {
    if (touch->pulse_count == 0) return 0;
    const int head = touch->pulse_head;
    return touch->pulses[head].hold_s > 0 && touch->pulses[head].hold_started &&
           time_ns >= touch->pulses[head].hold_until_ns;
}

static void expire_timed_pulse(dm_android_touch *touch) {
    if (touch->pulse_count == 0) return;
    int head = touch->pulse_head;
    if (touch->pulses[head].hold_s <= 0 || !touch->pulses[head].hold_started) return;
    if (touch->last_ns < touch->pulses[head].hold_until_ns) return;
    uint64_t buttons = touch->pulses[head].buttons;
    touch->pulse_head = (head + 1) % DM_ANDROID_TOUCH_PULSE_CAPACITY;
    --touch->pulse_count;
    publish_core(touch);
    if (touch->pulse_count > 0 && touch->pulses[touch->pulse_head].buttons == buttons)
        touch->release_waiting = 1;   /* neutral read before a same-button successor */
}

static void publish(dm_android_touch *touch) {
    expire_timed_pulse(touch);
    sync_pointer_slots(touch);
    portal_sync(touch);
    dm_tc_state *s = &touch->state;
    if (s->pulse_button) {
        if (pending_full(touch)) return;   /* defensive; callers backpressure first */
        int tail = (touch->pulse_head + touch->pulse_count) % DM_ANDROID_TOUCH_PULSE_CAPACITY;
        touch->pulses[tail].buttons = s->buttons;
        touch->pulses[tail].rx = s->pulse_rx;
        touch->pulses[tail].ry = s->pulse_ry;
        touch->pulses[tail].hold_s = s->pulse_hold_s;
        touch->pulses[tail].hold_started = 0;
        touch->pulses[tail].hold_until_ns = 0;
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
    memset(touch->pointer_token, 0, sizeof touch->pointer_token);
    memset(touch->used, 0, sizeof touch->used);
    touch->pulse_head = touch->pulse_count = 0;
    touch->release_waiting = 0;
    touch->use_pointer = -1;
    touch->use_token = 0;
    touch->use_x = touch->use_y = 0;
    touch->use_ambiguous = 0;
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
    if (++touch->next_token == 0) touch->next_token = 1;
    touch->pointer_token[slot] = touch->next_token;
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
    uint64_t token = touch->pointer_token[slot];
    uint64_t use_before = touch->state.use_seq;
    if (touch->state.use_seq != touch->seen_use) touch->use_ambiguous = 1;
    dm_tc_end_at(&touch->state, slot, now_s);
    if (touch->state.use_seq != use_before) {
        touch->use_pointer = pointer_id;
        touch->use_token = token;
        touch->use_x = touch->state.pointer_x;   /* freeze the originating gesture point */
        touch->use_y = touch->state.pointer_y;
    }
    touch->used[slot] = 0;
    touch->pointer_id[slot] = -1;
    touch->pointer_token[slot] = 0;
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
    touch->pointer_token[slot] = 0;
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

int dm_android_touch_set_portal_context(dm_android_touch *touch, int available,
                                        uint64_t mask, uint64_t binding_identity,
                                        uint64_t scene_epoch) {
    if (!touch || (available != 0 && available != 1)) return 0;
    available = available != 0;
    if (available && mask == 0) return 0;   /* unproven binding stays unavailable */
    uint64_t effective = available ? mask : 0;
    if (touch->portal_available == available && touch->portal_mask == effective &&
        touch->portal_identity == binding_identity && touch->portal_scene == scene_epoch)
        return 1;
    uint64_t old_mask = touch->portal_mask;
    touch->portal_available = available;
    touch->portal_mask = effective;
    touch->portal_identity = binding_identity;
    touch->portal_scene = scene_epoch;
    /* A real mask/identity/scene change makes the core cancel the portal arm
       and invalidate the gesture epoch, so a stale request cannot be acked.
       Drop any unread pad edge of the cancelled portal mask too, mirroring the
       per-pointer cancel path: the arm is gone before the next read. */
    dm_tc_set_portal_binding(&touch->state, effective, binding_identity, scene_epoch);
    if (old_mask) dm_android_pad_clear_edges_mask(touch->pad, old_mask);
    publish(touch);
    return 1;
}

int dm_android_touch_take_portal_request(dm_android_touch *touch,
                                         dm_android_portal_request *out) {
    if (!touch || !out || !touch->portal_request_pending) return 0;
    *out = touch->portal_request;
    touch->portal_request_pending = 0;
    return 1;
}

int dm_android_touch_portal_ack(dm_android_touch *touch, uint64_t gesture_epoch,
                                uint64_t binding_identity, uint64_t scene_epoch) {
    if (!touch) return 0;
    if (!dm_tc_portal_ack(&touch->state, gesture_epoch, binding_identity, scene_epoch))
        return 0;
    touch->portal_request_pending = 0;
    publish(touch);
    return 1;
}

int dm_android_touch_has_pointer(const dm_android_touch *touch, int32_t pointer_id) {
    return touch && pointer_id >= 0 && find_pointer(touch, pointer_id) >= 0;
}

int dm_android_touch_pointer_token(const dm_android_touch *touch, int32_t pointer_id,
                                   uint64_t *out_token) {
    if (!touch || !out_token || pointer_id < 0) return 0;
    int slot = find_pointer(touch, pointer_id);
    if (slot < 0 || touch->pointer_token[slot] == 0) return 0;
    *out_token = touch->pointer_token[slot];
    return 1;
}

int dm_android_touch_pending_use_token(const dm_android_touch *touch,
                                       uint64_t *out_token) {
    if (!touch || !out_token) return 0;
    if (touch->state.use_seq == touch->seen_use) return 0;
    if (touch->use_ambiguous || touch->use_token == 0) return 0;
    *out_token = touch->use_token;
    return 1;
}

int dm_android_touch_token_active(const dm_android_touch *touch, uint64_t token) {
    if (!touch || token == 0) return 0;
    for (int i = 0; i < DM_TC_MAX_TOUCH; ++i)
        if (touch->used[i] && touch->pointer_token[i] == token) return 1;
    return 0;
}

int dm_android_touch_observe(const dm_android_touch *touch, float density,
                             dm_android_touch_observation *out) {
    if (!touch || !out || !isfinite(density) || density <= 0) return 0;
    const dm_tc_state *s = &touch->state;
    if (!isfinite(s->layout.width) || !isfinite(s->layout.height) ||
        s->layout.width <= 0 || s->layout.height <= 0) return 0;
    if (s->enabled_targets & ~((1u << DM_TC_COUNT) - 1u)) return 0;
    if (!isfinite(s->layout.joy_radius) || s->layout.joy_radius <= 0 ||
        !isfinite(s->layout.joy_x) || !isfinite(s->layout.joy_y)) return 0;
    for (int i = 0; i < DM_TC_COUNT; ++i) {
        const dm_tc_target *t = &s->layout.target[i];
        if (!isfinite(t->x) || !isfinite(t->y) || !isfinite(t->r) || t->r <= 0) return 0;
    }
    dm_android_touch_observation o = {
        .width_pt = s->layout.width,
        .height_pt = s->layout.height,
        .density = density,
        .enabled_mask = s->enabled_targets,
        .pressed_mask = 0,
        .available_mask = 0,
        .joystick_active = 0,
        .suspended = touch->suspended,
        .game_available = touch->game_available,
    };
    /* Only availability already proven by a host context is published. Main,
       skill, belt, menu and map bindings, glyphs/art, cooldown and mana remain
       unknown here even though the core has layout buttons. */
    if (touch->portal_available && touch->portal_mask)
        o.available_mask |= 1u << DM_TC_PORTAL;
    for (int i = 0; i < DM_TC_MAX_TOUCH; ++i) {
        const dm_tc_finger *f = &s->finger[i];
        if (!f->active || f->target < 0 || f->target >= DM_TC_COUNT) continue;
        if (s->enabled_targets & (1u << f->target)) o.pressed_mask |= 1u << f->target;
    }
    if (s->joy_slot >= 0 && s->joy_slot < DM_TC_MAX_TOUCH &&
        s->finger[s->joy_slot].active) {
        const dm_tc_finger *f = &s->finger[s->joy_slot];
        if (!isfinite(s->joy_origin_x) || !isfinite(s->joy_origin_y) ||
            !isfinite(s->joy_touch_x) || !isfinite(s->joy_touch_y) ||
            !isfinite(f->x) || !isfinite(f->y)) return 0;
        o.joystick_active = 1;
        o.joy_origin_x = s->joy_origin_x;
        o.joy_origin_y = s->joy_origin_y;
        o.joy_touch_x = s->joy_touch_x;
        o.joy_touch_y = s->joy_touch_y;
        o.joy_current_x = f->x;
        o.joy_current_y = f->y;
    }
    o.joy_radius = s->layout.joy_radius;
    *out = o;
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
    out->source_pointer = -1;
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
        touch->seen_use++;   /* one action per sequence; do not collapse two pending USEs */
        out->kind = DM_ANDROID_ACTION_USE;
        out->x = touch->use_ambiguous ? 0 : touch->use_x;
        out->y = touch->use_ambiguous ? 0 : touch->use_y;
        out->sequence = touch->seen_use;
        out->source_pointer = touch->use_pointer;
        out->source_token = touch->use_ambiguous ? 0 : touch->use_token;
        if (touch->seen_use == s->use_seq) touch->use_ambiguous = 0;
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
    if (!touch || time_ns < 0) return 0;
    /* A full queue normally rejects timer mutation (explicit backpressure),
       but an already observed timed head must still be allowed to expire:
       otherwise the held key can never be released and the queue can never
       drain. An unobserved head stays queued for a full first-read hold. */
    int64_t effective_ns = time_ns < touch->last_ns ? touch->last_ns : time_ns;
    if (pending_full(touch) && !timed_head_due(touch, effective_ns)) return 0;
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
    if (touch->pulses[head].hold_s > 0) {
        /* Standalone consumption boundary: the hold starts at the first read,
           using the last accepted timestamp, so a delayed consumer still gets
           the full upstream duration. Ticks before this read cannot expire it. */
        if (!touch->pulses[head].hold_started) {
            touch->pulses[head].hold_started = 1;
            touch->pulses[head].hold_until_ns =
                touch->last_ns + (int64_t)llround(touch->pulses[head].hold_s * 1e9);
        }
        if (touch->last_ns < touch->pulses[head].hold_until_ns) {
            publish_entry(touch, buttons, rx, ry);   /* still held across reads */
            return dm_android_pad_read(touch->pad);
        }
        touch->pulse_head = (head + 1) % DM_ANDROID_TOUCH_PULSE_CAPACITY;
        --touch->pulse_count;
        publish_core(touch);
        out = dm_android_pad_read(touch->pad);
        if (touch->pulse_count > 0 && touch->pulses[touch->pulse_head].buttons == buttons)
            touch->release_waiting = 1;
        return out;
    }
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
