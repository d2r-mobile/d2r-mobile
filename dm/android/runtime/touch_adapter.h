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
//
// A core pulse with a non-zero pulse_hold_s (inventory hold-Y, belt hold, etc.)
// is queued with its hold duration. The standalone adapter starts that hold
// clock on the first dm_android_touch_read of the pulse, using the last accepted
// timestamp; ticks before the first read cannot expire it. While the deadline
// has not passed, repeated reads keep returning the held button; a tick at or
// after the deadline releases it (with a neutral read before a same-button
// successor). This is a standalone consumption boundary, not an acknowledgement
// of real guest/cursor delivery, and no such acknowledgement is invented.
// When the ring is full, a tick may still expire an already observed timed head
// so the held key can release and the queue can drain; an unobserved head stays
// queued for its full first-read hold. New mutating input remains rejected while
// the ring is full.
// Zero-hold release-to-cast pulses keep their existing one-read ordering and
// same-button neutral separation.
#define DM_ANDROID_TOUCH_PULSE_CAPACITY 16

typedef struct dm_android_touch dm_android_touch;

// One core-produced action after a gesture. The bridge/provider layer must
// acknowledge real external consumption; no success is implied here.
enum dm_android_touch_action_kind {
    DM_ANDROID_ACTION_WORLD_TAP = 1,
    DM_ANDROID_ACTION_IDENTIFY,
    DM_ANDROID_ACTION_USE,
    DM_ANDROID_ACTION_LONG_PRESS,
    DM_ANDROID_ACTION_STACK_TAKE,
    DM_ANDROID_ACTION_TAB_TAP,
    DM_ANDROID_ACTION_SHOP_DROP,
    DM_ANDROID_ACTION_UI_CLICK,
    DM_ANDROID_ACTION_DIFFICULTY,
    DM_ANDROID_ACTION_BELT
};

typedef struct dm_android_touch_action {
    uint32_t kind;
    float x, y;
    uint64_t sequence;
    int32_t source_pointer;   /* pointer id that produced an object action; -1 otherwise */
    uint64_t source_token;    /* immutable gesture token for that pointer; 0 if ambiguous */
} dm_android_touch_action;

// One-shot observation of an armed town-portal gesture. The fields are copied
// from the core state at the moment the portal target is pressed; the request
// is not reissued while the same gesture epoch stays armed.
typedef struct dm_android_portal_request {
    uint64_t gesture_epoch;
    uint64_t binding_identity;
    uint64_t scene_epoch;
    uint64_t mask;
    int phase;   /* core portal_phase: 1 = modifier-pending, 2 = armed */
} dm_android_portal_request;

// Game context is supplied by the provider bridge. availability=0 gates all
// new touch events and publishes a neutral sample; context changes cancel
// deferred/queued actions when the navigation mode or scene epoch changes.
int dm_android_touch_set_game_available(dm_android_touch *touch, int available);
int dm_android_touch_set_game_context(dm_android_touch *touch, int navigation_mode,
                                      uint32_t panel_bits, uint64_t scene_epoch,
                                      uint64_t world_epoch, uint64_t ui_identity,
                                      uint64_t object_generation);
// Host-supplied belt snapshot, mirroring dm_belt_rows()/expanded in
// dm/host/dm_actions.h. rows is the worn belt row count: 1-4 per the host
// contract, 0 when the host cell reading is empty (the same value the core
// initializes to and dm_actions.c's (cells+3)/4 formula can produce). open is
// the expanded bit (0/1). Values are validated before any mutation; invalid
// input returns 0 and changes nothing (no publish, no gesture cancel). Same
// values are a no-op returning 1. A real change cancels owned pointers and
// queued pulses with the core's cancel-all semantics, so a stale gesture
// cannot be resolved under the new rows/open context. The snapshot itself
// survives cancel/focus/pause/resize, like the core's host-fed belt fields.
// The adapter default (0 rows, not open) keeps the pre-context single-row
// behavior until a host reading is supplied. Belt requests (1 expand, 2
// collapse, 0x100 | row<<4 | col) are still consumed once through
// dm_android_touch_take_action.
int dm_android_touch_set_belt_context(dm_android_touch *touch, int rows, int open);
// Host-supplied town-portal context over the existing core portal state
// (dm_tc_set_portal_binding / dm_tc_portal_ack). available must be 0/1. An
// explicit available=0 disables the portal. available=1 with mask 0 is invalid
// input: it returns 0 before any mutation and leaves the previous context in
// place (it is not an implicit disable). A ready context needs a non-zero
// host-verified mask; no original-game key is invented here. Same values are a
// no-op returning 1. A real mask, identity or scene change cancels the core
// portal arm (releases the portal finger, drops its adapter pointer ownership
// and invalidates the gesture epoch), so a stale request cannot be acked.
// Defaults are unavailable (0/0/0/0).
int dm_android_touch_set_portal_context(dm_android_touch *touch, int available,
                                        uint64_t mask, uint64_t binding_identity,
                                        uint64_t scene_epoch);
// One-shot observation of a portal arm produced by an actual portal press.
// Returns 1 and fills *out for the first caller; later calls for the same arm
// return 0. out == NULL never consumes. A binding/scene/availability change,
// full cancel or resize drops an untaken request. Ack success also clears it.
int dm_android_touch_take_portal_request(dm_android_touch *touch,
                                         dm_android_portal_request *out);
// Confirms the portal arm with the core rules: a pending modifier arm, the
// exact gesture epoch, binding identity and scene epoch, and a still-held
// portal finger. Wrong/stale/ended arms return 0 and leave the request alone;
// success clears the pending state and publishes the armed mask.
int dm_android_touch_portal_ack(dm_android_touch *touch, uint64_t gesture_epoch,
                                uint64_t binding_identity, uint64_t scene_epoch);
// Read-only ownership query: 1 when pointer_id currently maps to an active
// core finger, 0 for NULL/negative/unknown. Used by the outer bridge to drop
// only IDs the adapter/core canceled; it does not mutate ownership.
int dm_android_touch_has_pointer(const dm_android_touch *touch, int32_t pointer_id);
// Immutable per-down gesture token for an active pointer. The game bridge
// retains object identity by token, so a later DOWN or reused pointer id cannot
// overwrite a pending object action. Returns 0 for unknown/NULL.
int dm_android_touch_pointer_token(const dm_android_touch *touch, int32_t pointer_id,
                                   uint64_t *out_token);
// 1 with the token of the single unread USE action; 0 when no USE is pending.
// A second USE produced before the first is consumed makes both ambiguous and
// this returns 0, so consumers reject rather than retarget either action.
int dm_android_touch_pending_use_token(const dm_android_touch *touch,
                                       uint64_t *out_token);
int dm_android_touch_token_active(const dm_android_touch *touch, uint64_t token);

// Read-only visual observation of the actual adapter/core gesture state.
//
// Same-thread, non-consuming and immutable from the caller's perspective: this
// does not read the pad, advance timers, drain actions/pulses/portal requests,
// mutate pointer ownership or retain borrowed pointers. The output is a value
// copy and remains caller-owned.
//
// width_pt/height_pt are the frozen layout's logical dimensions. density is a
// caller-supplied view metric (pixels per point), not adapter gesture state; it
// is validated and copied so a HUD can do its own pixel conversion. available_mask
// is deliberately conservative: only a host-proven town-portal binding is
// reported here. Main/skill/belt/menu/map bindings, glyphs/art, cooldown and mana
// are not proven by this seam and stay unavailable/unknown (0 bits), never
// guessed from emitted gamepad buttons. enabled_mask is the core's enabled
// target mask; pressed_mask contains only active fingers whose target is enabled.
typedef struct dm_android_touch_observation {
    float width_pt, height_pt;
    float density;
    uint32_t enabled_mask;
    uint32_t pressed_mask;
    uint32_t available_mask;
    int joystick_active;
    float joy_origin_x, joy_origin_y;
    float joy_touch_x, joy_touch_y;
    float joy_current_x, joy_current_y;
    float joy_radius;
    int suspended;
    int game_available;
} dm_android_touch_observation;

// Returns 1 and writes *out on success. Rejects NULL, non-finite/non-positive
// density or layout geometry, and invalid target/joystick geometry before any
// output byte is changed (returns 0, *out untouched). Cancel/pause/resize and
// context changes are visible immediately on the next observation; no stale
// pressed value is cached.
int dm_android_touch_observe(const dm_android_touch *touch, float density,
                             dm_android_touch_observation *out);
int dm_android_touch_pointer_hit(dm_android_touch *touch, int32_t pointer_id, int kind);
int dm_android_touch_pointer_release(dm_android_touch *touch, int32_t pointer_id,
                                     int drop_kind);
// Consume at most one core action sequence that has not been returned before.
int dm_android_touch_take_action(dm_android_touch *touch, dm_android_touch_action *out);

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
