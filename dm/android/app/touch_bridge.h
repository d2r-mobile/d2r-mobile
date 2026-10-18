// Host-testable NativeActivity MotionEvent translation and diagnostic consumer.
// Android-specific event extraction stays in app/native.c; this module owns the
// mapping to the existing dm_android_touch adapter, bounded backpressure and the
// single consumer snapshot. It has no Android/UI dependencies.
#pragma once

#include <stdint.h>
#include "touch_adapter.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DM_TOUCH_MAX_POINTS 16

typedef enum dm_touch_action {
    DM_TOUCH_ACTION_DOWN = 0,
    DM_TOUCH_ACTION_MOVE,
    DM_TOUCH_ACTION_UP,
    DM_TOUCH_ACTION_CANCEL,
    DM_TOUCH_ACTION_POINTER_DOWN,
    DM_TOUCH_ACTION_POINTER_UP
} dm_touch_action;

typedef struct dm_touch_point {
    int32_t id;
    float x_px;
    float y_px;
} dm_touch_point;

// One MotionEvent-shaped event. Coordinates are physical pixels; the bridge
// divides by density before calling the adapter. Time is non-negative
// CLOCK_MONOTONIC nanoseconds. action_index selects the pointer for
// DOWN/POINTER_DOWN/UP/POINTER_UP and is ignored for MOVE/CANCEL.
typedef struct dm_touch_event {
    dm_touch_action action;
    int32_t action_index;
    uint32_t pointer_count;
    dm_touch_point points[DM_TOUCH_MAX_POINTS];
    int64_t time_ns;
} dm_touch_event;

typedef struct dm_touch_diag {
    dm_npad_state state;
    uint64_t accepted_events;
    uint64_t invalid_events;
    uint64_t rejected_events;
    uint64_t drained_pulses;
    uint64_t up_retries;
    uint64_t cancel_fallbacks;
    uint64_t diagnostic_reads;  /* consumed by this diagnostic bridge, not a guest HID */
} dm_touch_diag;

typedef struct dm_touch_bridge dm_touch_bridge;

// width_pt/height_pt are logical points (physical pixels / density), matching
// the existing adapter contract. density must be positive.
dm_touch_bridge *dm_touch_bridge_create(float width_pt, float height_pt, float density);
// Cancels active pointers/queued pulses, consumes a neutral snapshot into
// neutral_out when non-NULL, then destroys the adapter and pad exactly once.
void dm_touch_bridge_destroy(dm_touch_bridge *bridge, dm_npad_state *neutral_out);

int dm_touch_bridge_set_density(dm_touch_bridge *bridge, float density);
float dm_touch_bridge_density(const dm_touch_bridge *bridge);
int dm_touch_bridge_resize(dm_touch_bridge *bridge, float width_pt, float height_pt,
                           int64_t time_ns);

// Applies one MotionEvent-shaped event. Returns 1 when the event was consumed
// (including a safe UP/CANCEL fallback) and 0 for malformed input or bounded
// backpressure. Does not consume the diagnostic snapshot; call read separately.
int dm_touch_bridge_event(dm_touch_bridge *bridge, const dm_touch_event *event);
// Advances adapter timers without requiring another touch event.
int dm_touch_bridge_tick(dm_touch_bridge *bridge, int64_t time_ns);
// Applies ACTION_CANCEL/focus-loss/pause semantics and queues a neutral state.
int dm_touch_bridge_cancel_all(dm_touch_bridge *bridge, int64_t time_ns);

// Single diagnostic consumer. Consumes one ordered pulse (or returns the
// current neutral/held snapshot), stores it, and returns it. All bridge calls,
// including this one and tick, must stay on the same input/core thread.
dm_npad_state dm_touch_bridge_read(dm_touch_bridge *bridge);
dm_npad_state dm_touch_bridge_snapshot(const dm_touch_bridge *bridge);
const dm_touch_diag *dm_touch_bridge_diag(const dm_touch_bridge *bridge);

#ifdef __cplusplus
}
#endif
