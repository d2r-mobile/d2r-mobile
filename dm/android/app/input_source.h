// Touch/gamepad input-source arbitration.
//
// This module owns only the arbitration policy between the accepted touch
// bridge and a gamepad snapshot. It is host-testable and does not extract
// Android MotionEvents, publish guest HID, or choose a UI layout. The active
// source is always explicit; two sources are never OR-ed into one sample.
#pragma once

#include <stdint.h>
#include "input.h"
#include "touch_bridge.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DM_INPUT_MAX_DEVICE_ID 15u
#define DM_INPUT_GAMEPAD_DEADZONE 8000

typedef enum dm_input_source {
    DM_INPUT_SOURCE_NONE = 0,
    DM_INPUT_SOURCE_TOUCH,
    DM_INPUT_SOURCE_GAMEPAD
} dm_input_source;

typedef enum dm_input_layout {
    DM_INPUT_LAYOUT_TOUCH_HUD = 0,
    DM_INPUT_LAYOUT_CONTROLLER
} dm_input_layout;

typedef enum dm_input_invalidate_reason {
    DM_INPUT_INVALIDATE_FOCUS_LOST = 1,
    DM_INPUT_INVALIDATE_PAUSE,
    DM_INPUT_INVALIDATE_QUEUE_CHANGED
} dm_input_invalidate_reason;

typedef struct dm_input_arbiter dm_input_arbiter;

// width_pt/height_pt/density match dm_touch_bridge_create. Returns NULL for
// non-finite or non-positive geometry.
dm_input_arbiter *dm_input_arbiter_create(float width_pt, float height_pt, float density);
// Cancels both sources, consumes a neutral boundary, destroys the real bridge
// and pad. neutral_out, when non-NULL, receives the neutral boundary.
void dm_input_arbiter_destroy(dm_input_arbiter *arbiter, dm_npad_state *neutral_out);

dm_input_source dm_input_arbiter_source(const dm_input_arbiter *arbiter);
dm_input_layout dm_input_arbiter_layout(const dm_input_arbiter *arbiter);
// True after focus loss/pause/queue replacement/disconnect until a fresh
// active input claims a source. A neutral gamepad snapshot does not clear it.
int dm_input_arbiter_requires_fresh(const dm_input_arbiter *arbiter);

// A device connection alone never changes the active source or cancels touch.
// id > DM_INPUT_MAX_DEVICE_ID and generation == 0 are rejected. Reconnecting a
// different id/generation invalidates an old gamepad source, but leaves a
// touch source alone; old samples are rejected by id/generation.
int dm_input_arbiter_connect(dm_input_arbiter *arbiter, uint32_t device_id,
                             uint32_t generation);
// Disconnect requires the exact current id/generation. A late sample after
// disconnect is rejected. Disconnecting the active gamepad source invalidates
// it; disconnecting a non-active gamepad does not cancel touch.
int dm_input_arbiter_disconnect(dm_input_arbiter *arbiter, uint32_t device_id,
                                uint32_t generation);

// Accepts one gamepad sample. A connected but neutral sample (all buttons zero
// and both axes inside DM_INPUT_GAMEPAD_DEADZONE) is valid but never steals
// ownership. A valid active sample switches to gamepad, after canceling touch
// pointers/queued gestures and publishing a neutral boundary. Samples from a
// stale id/generation or disconnected device are rejected.
int dm_input_arbiter_gamepad(dm_input_arbiter *arbiter, uint32_t device_id,
                             uint32_t generation, const dm_pad_sample *sample);

// Accepts one MotionEvent-shaped touch event. While gamepad owns the source,
// only a valid DOWN/POINTER_DOWN can switch back to touch; late MOVE/UP are
// rejected. The switch cancels the old touch stream, clears gamepad buttons and
// axes, and queues a neutral boundary before the new touch state.
int dm_input_arbiter_touch(dm_input_arbiter *arbiter, const dm_touch_event *event);

// Focus loss, pause and queue replacement invalidate both sources and require
// fresh input. Setting focus/pause back does not restore held state.
int dm_input_arbiter_focus(dm_input_arbiter *arbiter, int focused);
int dm_input_arbiter_pause(dm_input_arbiter *arbiter, int paused);
int dm_input_arbiter_resume(dm_input_arbiter *arbiter);
int dm_input_arbiter_queue_changed(dm_input_arbiter *arbiter);

// One output reader. After a source switch or invalidation the first call
// returns the neutral boundary; the next call returns the selected source's
// coherent snapshot. While source is NONE, reads stay neutral. The caller must
// keep all calls on the input thread.
int dm_input_arbiter_read(dm_input_arbiter *arbiter, dm_npad_state *out);
// Advances touch-owned timers only while touch is active. Returns 0 for invalid
// time or rejected tick.
int dm_input_arbiter_tick(dm_input_arbiter *arbiter, int64_t time_ns);

#ifdef DM_INPUT_TEST
// Test build only: expose the actual bridge so an arbiter invalidation test can
// stage a real pending belt/portal request and verify it was retired.
dm_touch_bridge *dm_input_arbiter_test_touch(dm_input_arbiter *arbiter);
#endif

#ifdef __cplusplus
}
#endif
