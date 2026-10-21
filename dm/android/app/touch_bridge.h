// Host-testable NativeActivity MotionEvent translation and diagnostic consumer.
// Android-specific event extraction stays in app/native.c; this module owns the
// mapping to the existing dm_android_touch adapter, bounded backpressure and the
// single consumer snapshot. It has no Android/UI dependencies.
#pragma once

#include <stdint.h>
#include "touch_adapter.h"
#include "touch_game_provider.h"

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

typedef enum dm_touch_invalid_reason {
    DM_TOUCH_INVALID_NONE = 0,
    DM_TOUCH_INVALID_TIME_NEGATIVE,
    DM_TOUCH_INVALID_ACTION_RANGE,
    DM_TOUCH_INVALID_COUNT_RANGE,
    DM_TOUCH_INVALID_ACTION_INDEX,
    DM_TOUCH_INVALID_POINT_ID_NEGATIVE,
    DM_TOUCH_INVALID_COORDINATE_NONFINITE,
    DM_TOUCH_INVALID_DUPLICATE_ID,
    DM_TOUCH_INVALID_UNKNOWN_POINTER,
    DM_TOUCH_INVALID_POINTER_ALREADY_OWNED,
    DM_TOUCH_INVALID_POINTER_CAPACITY,
    DM_TOUCH_INVALID_COUNT
} dm_touch_invalid_reason;

typedef struct dm_touch_diag {
    dm_npad_state state;
    uint64_t accepted_events;
    uint64_t invalid_events;
    uint64_t rejected_events;
    uint64_t drained_pulses;
    uint64_t up_retries;
    uint64_t cancel_fallbacks;
    uint64_t diagnostic_reads;  /* consumed by this diagnostic bridge, not a guest HID */
    uint64_t invalid_reason[DM_TOUCH_INVALID_COUNT]; /* fixed bounded counters */
    dm_touch_invalid_reason first_invalid_reason;
    dm_touch_invalid_reason last_invalid_reason;
} dm_touch_diag;

const char *dm_touch_invalid_reason_name(dm_touch_invalid_reason reason);

typedef struct dm_touch_bridge dm_touch_bridge;

// width_pt/height_pt are logical points (physical pixels / density), matching
// the existing adapter contract. density must be positive.
dm_touch_bridge *dm_touch_bridge_create(float width_pt, float height_pt, float density);
// Cancels active pointers/queued pulses, consumes a neutral snapshot into
// neutral_out when non-NULL, then destroys the adapter and pad exactly once.
void dm_touch_bridge_destroy(dm_touch_bridge *bridge, dm_npad_state *neutral_out);

int dm_touch_bridge_set_density(dm_touch_bridge *bridge, float density);
float dm_touch_bridge_density(const dm_touch_bridge *bridge);
// Optional #33 game provider boundary. Attaching replaces any previous provider
// and starts unavailable until a validated snapshot arrives. Without attachment
// the accepted #32 bridge remains a generic diagnostic path.
int dm_touch_bridge_attach_game(dm_touch_bridge *bridge, const dm_tgp_provider *provider);
int dm_touch_bridge_resize(dm_touch_bridge *bridge, float width_pt, float height_pt,
                           int64_t time_ns);

// Host-supplied belt snapshot for the adapter's existing belt context (rows
// 0-4, open 0/1; dm_belt_rows). The adapter validates before mutating. Invalid
// input returns 0 and changes neither the adapter nor the bridge's active
// pointer table. Same values return 1 without cancelling or consuming. A real
// change cancels inner pointers/queued pulses through the adapter's belt
// context path, then clears the bridge's active table and consumes the neutral
// snapshot, so stale MOVE/UP are rejected and a reused ID can be pressed
// again. The snapshot survives bridge cancel/resize like the core fields; an
// unconsumed belt_request is never read or cleared here (one-shot ownership
// stays in dm_android_touch_take_action). No game/HUD context is synthesized.
int dm_touch_bridge_set_belt_context(dm_touch_bridge *bridge, int rows, int open);

// Key-bound town-portal context/request/ack over the accepted adapter API. One
// consumer, same dm_android_portal_request type and identity/epoch contract; no
// queue and no raw adapter handle. available=1/mask=0 is invalid and leaves the
// previous context unchanged; available=0 is the explicit disable. Same values
// return 1 without canceling or reconciling. A real change calls the adapter,
// then removes only the outer active IDs the adapter no longer owns (canceled
// portal owners), preserving unrelated joystick/skill pointers, and consumes
// the resulting snapshot. With a #33 game provider attached these return 0:
// the provider path is not a proven portal consumer, so the portal stays
// explicitly unavailable instead of competing for actions. Attaching or
// replacing a provider also disables any binding/request/arm configured
// earlier (and its unread edges), with the outer cache reset to unavailable
// before provider input is accepted.
int dm_touch_bridge_set_portal_context(dm_touch_bridge *bridge, int available,
                                       uint64_t mask, uint64_t binding_identity,
                                       uint64_t scene_epoch);
int dm_touch_bridge_take_portal_request(dm_touch_bridge *bridge,
                                        dm_android_portal_request *out);
int dm_touch_bridge_portal_ack(dm_touch_bridge *bridge, uint64_t gesture_epoch,
                               uint64_t binding_identity, uint64_t scene_epoch);

// Applies one MotionEvent-shaped event. Returns 1 when the event was consumed
// (including a safe UP/CANCEL fallback) and 0 for malformed input or bounded
// backpressure. Does not consume the diagnostic snapshot; call read separately.
int dm_touch_bridge_event(dm_touch_bridge *bridge, const dm_touch_event *event);
// Advances adapter timers without requiring another touch event.
int dm_touch_bridge_tick(dm_touch_bridge *bridge, int64_t time_ns);
// Applies ACTION_CANCEL/focus-loss/pause semantics and queues a neutral state.
int dm_touch_bridge_cancel_all(dm_touch_bridge *bridge, int64_t time_ns);

// 显式输入来源退休：仅 standalone（未 attach #33 provider）支持；取消手指/脉冲/
// 待发动作，并丢弃未消费的腰带请求和传送门请求；保留 host context。provider
// attached 时在 mutation 前返回 0，active pointer/output/provider state 不变。
// Explicit source retirement is standalone-only: with a #33 provider attached it
// returns 0 before mutation, preserving active pointers, output and provider
// state. Standalone cancels pointers/pulses/deferred actions and discards
// unconsumed belt/portal requests while preserving host contexts. Distinct from
// plain cancel_all because upstream dm_tc_cancel_all intentionally preserves
// belt_request; the Android bridge clears it here.
int dm_touch_bridge_retire_requests(dm_touch_bridge *bridge, int64_t time_ns);

// Read-only visual observation forwarded to the adapter. No pad read, action/
// pulse/portal-request consumption, timer advance, ownership mutation or
// retained pointer. density is caller-owned view metric; invalid input returns
// 0 without changing *out.
int dm_touch_bridge_observe(const dm_touch_bridge *bridge, float density,
                            dm_android_touch_observation *out);

// One-shot host action outlet over the adapter's existing action ownership
// (dm_android_touch_take_action, same dm_android_touch_action type/encoding).
// Returns 1 and fills *out when one core action was pending, 0 otherwise.
// out == NULL returns 0 without consuming anything. This is a forward only: no
// duplicate queue and no raw adapter handle. With a #33 game provider attached
// the provider tick is the action consumer, so this returns 0 rather than
// stealing its pending action. Cancels, context changes and resizes keep the
// core's pending-action semantics (the adapter's take_action ownership is
// unchanged; belt_request survives dm_tc_cancel_all/resize).
int dm_touch_bridge_take_action(dm_touch_bridge *bridge, dm_android_touch_action *out);

// Single diagnostic consumer. Consumes one ordered pulse (or returns the
// current neutral/held snapshot), stores it, and returns it. All bridge calls,
// including this one and tick, must stay on the same input/core thread.
dm_npad_state dm_touch_bridge_read(dm_touch_bridge *bridge);
dm_npad_state dm_touch_bridge_snapshot(const dm_touch_bridge *bridge);
const dm_touch_diag *dm_touch_bridge_diag(const dm_touch_bridge *bridge);

#ifdef __cplusplus
}
#endif
