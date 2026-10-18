// Android 触控事件 → 复用 r2d 手势核心的适配层。
// Android touch-event adapter over the reused r2d gesture core.
//
// This is a gesture/timing adapter, not NativeActivity event delivery. The
// Android app must still translate MotionEvent and call these functions from its
// input path; that lifecycle/event-delivery integration is a separate gap.
//
// Coordinate contract: x_pt / y_pt are logical Android points in the game view,
// i.e. physical pixels divided by DisplayMetrics.density. They are not raw
// MotionEvent pixels. The layout is the same 667x375-point baseline used by the
// portable core.
//
// Time contract: time_ns is a non-negative CLOCK_MONOTONIC timestamp in
// nanoseconds. The adapter converts to seconds and clamps a regressing timestamp
// to the last accepted time; a negative timestamp is rejected. This prevents a
// stale delayed press after a clock rollback.
//
// Pointer contract: pointer_id is Android's opaque MotionEvent pointer ID. The
// adapter owns a 16-entry table and maps each active ID to a bounded core slot;
// slot indices are never treated as pointer identities. Duplicate active IDs,
// unknown moves/ups, negative IDs and a full pointer table are rejected.
//
// Threading/lifetime contract: the adapter is single-producer, single-consumer
// and not internally locked. Call every adapter function, including
// dm_android_touch_read, from one input/core thread. The dm_android_pad must
// outlive the adapter; the caller destroys the pad after the adapter. Destroying
// the adapter publishes a neutral sample and does not free the pad.
#pragma once
#include <stdint.h>
#include "input.h"

#ifdef __cplusplus
extern "C" {
#endif

// Pending release-to-cast pulses are held in a fixed 16-slot ring (the same
// bound as the pointer table). When it is full, mutating input events are
// rejected with 0 before they touch core state; read/cancel operations remain
// available so the consumer can drain. This is explicit backpressure, not an
// unbounded queue and not a silent drop.
#define DM_ANDROID_TOUCH_PULSE_CAPACITY 16

typedef struct dm_android_touch dm_android_touch;

dm_android_touch *dm_android_touch_create(dm_android_pad *pad, float width_pt, float height_pt);
void dm_android_touch_destroy(dm_android_touch *touch);

// Read one coherent pad snapshot. Pending release-to-cast pulses are returned
// one per read in acceptance order, each with its own button/aim/modifier. A
// repeated same-button pulse is separated by one neutral read so the consumer
// sees distinct edges; ticks/moves between the two reads cannot overwrite that
// release. After the last pulse, the next read is neutral/held. When the bounded
// pulse ring is full, callers must drain it and retry the rejected mutating
// event; rejection does not mutate pointer/core/timer state.
// Direct dm_android_pad_read calls bypass this consumption and must not be used.

// Each function returns 1 when the event was accepted and 0 when it was
// rejected. Rejected events publish nothing and do not alter another pointer.
int dm_android_touch_down(dm_android_touch *touch, int32_t pointer_id,
                          float x_pt, float y_pt, int64_t time_ns);
int dm_android_touch_move(dm_android_touch *touch, int32_t pointer_id,
                          float x_pt, float y_pt, int64_t time_ns);
int dm_android_touch_up(dm_android_touch *touch, int32_t pointer_id, int64_t time_ns);
// ACTION_CANCEL for one pointer: release its keys/axes without a tap or a
// release-to-cast pulse.
int dm_android_touch_cancel(dm_android_touch *touch, int32_t pointer_id, int64_t time_ns);
// ACTION_CANCEL for the whole gesture stream, focus loss or pause. Clears all
// pointer ownership, pending pulses, deferred presses, queue state and axes.
int dm_android_touch_cancel_all(dm_android_touch *touch, int64_t time_ns);
int dm_android_touch_focus_lost(dm_android_touch *touch, int64_t time_ns);
int dm_android_touch_pause(dm_android_touch *touch, int64_t time_ns);
int dm_android_touch_resume(dm_android_touch *touch, int64_t time_ns);
int dm_android_touch_resize(dm_android_touch *touch, float width_pt, float height_pt,
                            int64_t time_ns);
// Advance timers (skill settle, ZL lead, deferred release pulse, close repeats)
// and publish one coherent pad snapshot. Call this from the render/input tick.
int dm_android_touch_tick(dm_android_touch *touch, int64_t time_ns);
dm_npad_state dm_android_touch_read(dm_android_touch *touch);

#ifdef __cplusplus
}
#endif
