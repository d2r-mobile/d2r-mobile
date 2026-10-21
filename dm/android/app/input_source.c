#include "input_source.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

struct dm_input_arbiter {
    dm_touch_bridge *touch;
    dm_touch_bridge *probe;
    dm_android_pad *gamepad;
    dm_input_source source;
    dm_input_layout layout;
    uint32_t device_id;
    uint32_t generation;
    int connected;
    int focused;
    int paused;
    int fresh_required;
    int pending_neutral;
    int64_t last_time_ns;
    uint64_t neutral_sampling;
    float width_pt;
    float height_pt;
    float density;
};

static int valid_device(uint32_t id, uint32_t generation) {
    return id <= (uint32_t)INT32_MAX && generation != 0;
}

static int gamepad_active(const dm_pad_sample *sample) {
    if (!sample || !sample->connected) return 0;
    if (sample->buttons != 0) return 1;
    return sample->lx > DM_INPUT_GAMEPAD_DEADZONE ||
           sample->lx < -DM_INPUT_GAMEPAD_DEADZONE ||
           sample->ly > DM_INPUT_GAMEPAD_DEADZONE ||
           sample->ly < -DM_INPUT_GAMEPAD_DEADZONE ||
           sample->rx > DM_INPUT_GAMEPAD_DEADZONE ||
           sample->rx < -DM_INPUT_GAMEPAD_DEADZONE ||
           sample->ry > DM_INPUT_GAMEPAD_DEADZONE ||
           sample->ry < -DM_INPUT_GAMEPAD_DEADZONE;
}

static dm_npad_state neutral_state(dm_input_arbiter *arbiter) {
    dm_npad_state state;
    memset(&state, 0, sizeof state);
    state.sampling = (int64_t)(++arbiter->neutral_sampling & INT64_MAX);
    return state;
}

static void clear_touch(dm_input_arbiter *arbiter) {
    if (!arbiter->touch) return;
    int64_t time = arbiter->last_time_ns > 0 ? arbiter->last_time_ns : 0;
    (void)dm_touch_bridge_retire_requests(arbiter->touch, time);
    /* Consume the neutral snapshot so a queued edge cannot appear as the next
       source's fresh state. */
    (void)dm_touch_bridge_read(arbiter->touch);
}

static void clear_gamepad(dm_input_arbiter *arbiter) {
    if (!arbiter->gamepad) return;
    dm_pad_sample off;
    memset(&off, 0, sizeof off);
    dm_android_pad_publish(arbiter->gamepad, &off);
    (void)dm_android_pad_read(arbiter->gamepad);
}

static void claim_source(dm_input_arbiter *arbiter, dm_input_source source) {
    if (arbiter->source == source) return;
    clear_touch(arbiter);
    clear_gamepad(arbiter);
    arbiter->source = source;
    arbiter->layout = source == DM_INPUT_SOURCE_GAMEPAD
                          ? DM_INPUT_LAYOUT_CONTROLLER
                          : DM_INPUT_LAYOUT_TOUCH_HUD;
    arbiter->fresh_required = 0;
    arbiter->pending_neutral = 1;
}

static int invalidate(dm_input_arbiter *arbiter) {
    if (!arbiter) return 0;
    clear_touch(arbiter);
    clear_gamepad(arbiter);
    arbiter->source = DM_INPUT_SOURCE_NONE;
    arbiter->layout = DM_INPUT_LAYOUT_TOUCH_HUD;
    arbiter->fresh_required = 1;
    arbiter->pending_neutral = 1;
    return 1;
}

/* A fresh touch takeover is validated on a staged real bridge first. The
   bridge performs complete ownership/coordinate validation; only an accepted
   event is committed by clearing the old sources (including the real touch
   stream) and applying the already-validated event to the production bridge.
   A rejected event therefore leaves the prior source, layout, held gamepad
   sample and pending boundary untouched. */
static int take_touch(dm_input_arbiter *arbiter, const dm_touch_event *event) {
    if (!arbiter->probe) return 0;
    if (!dm_touch_bridge_event(arbiter->probe, event)) {
        (void)dm_touch_bridge_retire_requests(arbiter->probe, event->time_ns);
        return 0;
    }
    (void)dm_touch_bridge_retire_requests(arbiter->probe, event->time_ns);
    claim_source(arbiter, DM_INPUT_SOURCE_TOUCH);
    if (!dm_touch_bridge_event(arbiter->touch, event)) return 0;
    arbiter->last_time_ns = event->time_ns;
    return 1;
}

static int touch_shape_valid(const dm_touch_event *event) {
    if (!event || event->time_ns < 0) return 0;
    if (event->action < DM_TOUCH_ACTION_DOWN ||
        event->action > DM_TOUCH_ACTION_POINTER_UP) return 0;
    if (event->action == DM_TOUCH_ACTION_CANCEL)
        return event->pointer_count <= DM_TOUCH_MAX_POINTS;
    if (event->pointer_count == 0 || event->pointer_count > DM_TOUCH_MAX_POINTS)
        return 0;
    if (event->action != DM_TOUCH_ACTION_MOVE &&
        (event->action_index < 0 ||
         (uint32_t)event->action_index >= event->pointer_count))
        return 0;
    for (uint32_t i = 0; i < event->pointer_count; i++) {
        const dm_touch_point *point = &event->points[i];
        if (point->id < 0 || !isfinite(point->x_px) || !isfinite(point->y_px))
            return 0;
        for (uint32_t j = 0; j < i; j++) {
            if (event->points[j].id == point->id) return 0;
        }
    }
    return 1;
}

dm_input_arbiter *dm_input_arbiter_create(float width_pt, float height_pt,
                                          float density) {
    if (!isfinite(width_pt) || !isfinite(height_pt) || !isfinite(density) ||
        width_pt <= 0.0f || height_pt <= 0.0f || density <= 0.0f)
        return NULL;
    dm_input_arbiter *arbiter = calloc(1, sizeof *arbiter);
    if (!arbiter) return NULL;
    arbiter->touch = dm_touch_bridge_create(width_pt, height_pt, density);
    arbiter->probe = dm_touch_bridge_create(width_pt, height_pt, density);
    arbiter->gamepad = dm_android_pad_create();
    if (!arbiter->touch || !arbiter->probe || !arbiter->gamepad) {
        if (arbiter->touch) dm_touch_bridge_destroy(arbiter->touch, NULL);
        if (arbiter->probe) dm_touch_bridge_destroy(arbiter->probe, NULL);
        if (arbiter->gamepad) dm_android_pad_destroy(arbiter->gamepad);
        free(arbiter);
        return NULL;
    }
    arbiter->source = DM_INPUT_SOURCE_NONE;
    arbiter->layout = DM_INPUT_LAYOUT_TOUCH_HUD;
    arbiter->focused = 1;
    arbiter->width_pt = width_pt;
    arbiter->height_pt = height_pt;
    arbiter->density = density;
    return arbiter;
}

void dm_input_arbiter_destroy(dm_input_arbiter *arbiter, dm_npad_state *neutral_out) {
    if (!arbiter) return;
    if (arbiter->touch) dm_touch_bridge_destroy(arbiter->touch, neutral_out);
    if (arbiter->probe) dm_touch_bridge_destroy(arbiter->probe, NULL);
    if (arbiter->gamepad) dm_android_pad_destroy(arbiter->gamepad);
    free(arbiter);
}

dm_input_source dm_input_arbiter_source(const dm_input_arbiter *arbiter) {
    return arbiter ? arbiter->source : DM_INPUT_SOURCE_NONE;
}

dm_input_layout dm_input_arbiter_layout(const dm_input_arbiter *arbiter) {
    return arbiter ? arbiter->layout : DM_INPUT_LAYOUT_TOUCH_HUD;
}

int dm_input_arbiter_requires_fresh(const dm_input_arbiter *arbiter) {
    return arbiter ? arbiter->fresh_required : 0;
}

int dm_input_arbiter_connect(dm_input_arbiter *arbiter, uint32_t device_id,
                             uint32_t generation) {
    if (!arbiter || !valid_device(device_id, generation)) return 0;
    if (arbiter->connected && arbiter->device_id == device_id &&
        arbiter->generation == generation)
        return 1;
    /* A connection is not input. It may invalidate an old gamepad source, but
       must not cancel or replace a touch gesture. */
    if (arbiter->source == DM_INPUT_SOURCE_GAMEPAD)
        (void)invalidate(arbiter);
    else
        clear_gamepad(arbiter);
    arbiter->device_id = device_id;
    arbiter->generation = generation;
    arbiter->connected = 1;
    return 1;
}

int dm_input_arbiter_disconnect(dm_input_arbiter *arbiter, uint32_t device_id,
                                uint32_t generation) {
    if (!arbiter || !valid_device(device_id, generation) ||
        !arbiter->connected || arbiter->device_id != device_id ||
        arbiter->generation != generation)
        return 0;
    arbiter->connected = 0;
    clear_gamepad(arbiter);
    if (arbiter->source == DM_INPUT_SOURCE_GAMEPAD)
        (void)invalidate(arbiter);
    return 1;
}

int dm_input_arbiter_gamepad(dm_input_arbiter *arbiter, uint32_t device_id,
                             uint32_t generation, const dm_pad_sample *sample) {
    if (!arbiter || !sample || !sample->connected || !arbiter->focused ||
        arbiter->paused || !arbiter->connected ||
        arbiter->device_id != device_id || arbiter->generation != generation)
        return 0;
    if (arbiter->source != DM_INPUT_SOURCE_GAMEPAD && !gamepad_active(sample))
        return 1;   /* a neutral sample is valid but does not take ownership */
    if (arbiter->source != DM_INPUT_SOURCE_GAMEPAD)
        claim_source(arbiter, DM_INPUT_SOURCE_GAMEPAD);
    dm_android_pad_publish(arbiter->gamepad, sample);
    return 1;
}

int dm_input_arbiter_touch(dm_input_arbiter *arbiter, const dm_touch_event *event) {
    if (!arbiter || !event || !arbiter->focused || arbiter->paused) return 0;
    if (arbiter->source != DM_INPUT_SOURCE_TOUCH) {
        /* Only a fresh touch-down may take ownership back from gamepad. Late
           MOVE/UP events cannot resurrect a canceled touch stream. Commit only
           after the staged real bridge accepts the complete event. */
        if (event->action != DM_TOUCH_ACTION_DOWN &&
            event->action != DM_TOUCH_ACTION_POINTER_DOWN)
            return 0;
        if (!touch_shape_valid(event)) return 0;
        return take_touch(arbiter, event);
    }
    if (!dm_touch_bridge_event(arbiter->touch, event)) return 0;
    arbiter->last_time_ns = event->time_ns;
    return 1;
}

int dm_input_arbiter_focus(dm_input_arbiter *arbiter, int focused) {
    if (!arbiter) return 0;
    arbiter->focused = focused != 0;
    if (!arbiter->focused) return invalidate(arbiter);
    return 1;
}

int dm_input_arbiter_pause(dm_input_arbiter *arbiter, int paused) {
    if (!arbiter) return 0;
    arbiter->paused = paused != 0;
    if (arbiter->paused) return invalidate(arbiter);
    return 1;
}

int dm_input_arbiter_resume(dm_input_arbiter *arbiter) {
    if (!arbiter) return 0;
    arbiter->paused = 0;
    /* Do not clear fresh_required or publish held state: the next valid input
       must claim the source. */
    return 1;
}

int dm_input_arbiter_queue_changed(dm_input_arbiter *arbiter) {
    return invalidate(arbiter);
}

int dm_input_arbiter_read(dm_input_arbiter *arbiter, dm_npad_state *out) {
    if (!arbiter || !out) return 0;
    if (arbiter->pending_neutral) {
        arbiter->pending_neutral = 0;
        *out = neutral_state(arbiter);
        return 1;
    }
    if (arbiter->source == DM_INPUT_SOURCE_TOUCH) {
        *out = dm_touch_bridge_read(arbiter->touch);
        return 1;
    }
    if (arbiter->source == DM_INPUT_SOURCE_GAMEPAD) {
        *out = dm_android_pad_read(arbiter->gamepad);
        return 1;
    }
    *out = neutral_state(arbiter);
    return 1;
}

int dm_input_arbiter_observe(const dm_input_arbiter *arbiter,
                             dm_android_touch_observation *out) {
    if (!arbiter || !arbiter->touch || !out) return 0;
    return dm_touch_bridge_observe(arbiter->touch, arbiter->density, out);
}

int dm_input_arbiter_diag(const dm_input_arbiter *arbiter, dm_touch_diag *out) {
    if (!arbiter || !arbiter->touch || !out) return 0;
    const dm_touch_diag *diag = dm_touch_bridge_diag(arbiter->touch);
    if (!diag) return 0;
    *out = *diag;
    return 1;
}

static void restore_geometry(dm_touch_bridge *bridge, float width_pt,
                             float height_pt, float density, int64_t time_ns) {
    if (!bridge) return;
    (void)dm_touch_bridge_resize(bridge, width_pt, height_pt, time_ns);
    (void)dm_touch_bridge_set_density(bridge, density);
}

static int apply_geometry(dm_input_arbiter *arbiter, float width_pt,
                          float height_pt, float density, int64_t time_ns) {
    if (!dm_touch_bridge_resize(arbiter->touch, width_pt, height_pt, time_ns))
        return 0;
    if (!dm_touch_bridge_set_density(arbiter->touch, density)) {
        restore_geometry(arbiter->touch, arbiter->width_pt, arbiter->height_pt,
                         arbiter->density, time_ns);
        return 0;
    }
    if (!dm_touch_bridge_resize(arbiter->probe, width_pt, height_pt, time_ns)) {
        restore_geometry(arbiter->touch, arbiter->width_pt, arbiter->height_pt,
                         arbiter->density, time_ns);
        return 0;
    }
    if (!dm_touch_bridge_set_density(arbiter->probe, density)) {
        restore_geometry(arbiter->probe, arbiter->width_pt, arbiter->height_pt,
                         arbiter->density, time_ns);
        restore_geometry(arbiter->touch, arbiter->width_pt, arbiter->height_pt,
                         arbiter->density, time_ns);
        return 0;
    }
    return 1;
}

int dm_input_arbiter_set_geometry(dm_input_arbiter *arbiter, float width_pt,
                                  float height_pt, float density,
                                  int64_t time_ns) {
    if (!arbiter || !arbiter->touch || !arbiter->probe ||
        !isfinite(width_pt) || !isfinite(height_pt) || !isfinite(density) ||
        width_pt <= 0.0f || height_pt <= 0.0f || density <= 0.0f ||
        time_ns < 0)
        return 0;
    if (arbiter->width_pt == width_pt && arbiter->height_pt == height_pt &&
        arbiter->density == density)
        return 1;
    if (!apply_geometry(arbiter, width_pt, height_pt, density, time_ns))
        return 0;
    arbiter->width_pt = width_pt;
    arbiter->height_pt = height_pt;
    arbiter->density = density;
    arbiter->last_time_ns = time_ns;
    return invalidate(arbiter);
}

int dm_input_arbiter_tick(dm_input_arbiter *arbiter, int64_t time_ns) {
    if (!arbiter || time_ns < 0 || !arbiter->focused || arbiter->paused) return 0;
    arbiter->last_time_ns = time_ns;
    if (arbiter->source == DM_INPUT_SOURCE_TOUCH)
        return dm_touch_bridge_tick(arbiter->touch, time_ns);
    return 1;
}

#ifdef DM_INPUT_TEST
dm_touch_bridge *dm_input_arbiter_test_touch(dm_input_arbiter *arbiter) {
    return arbiter ? arbiter->touch : NULL;
}

int dm_input_arbiter_test_probe_geometry(const dm_input_arbiter *arbiter,
                                         float *width_pt, float *height_pt,
                                         float *density) {
    if (!arbiter || !arbiter->probe || !width_pt || !height_pt || !density)
        return 0;
    dm_android_touch_observation o;
    if (!dm_touch_bridge_observe(arbiter->probe,
                                 dm_touch_bridge_density(arbiter->probe), &o))
        return 0;
    *width_pt = o.width_pt;
    *height_pt = o.height_pt;
    *density = o.density;
    return 1;
}
#endif
