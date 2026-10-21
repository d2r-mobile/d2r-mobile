// Live Android touch bridge to the original D2R gesture core.
// This module only translates NativeActivity MotionEvent lifecycle and core
// output; it does not own game scene/UI state and does not synthesize actions.
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dm_live_touch dm_live_touch;

enum {
    DM_LIVE_ACTION_DOWN = 0,
    DM_LIVE_ACTION_UP = 1,
    DM_LIVE_ACTION_MOVE = 2,
    DM_LIVE_ACTION_CANCEL = 3,
    DM_LIVE_ACTION_POINTER_DOWN = 5,
    DM_LIVE_ACTION_POINTER_UP = 6
};

dm_live_touch *dm_live_touch_create(float width_pt, float height_pt);
void dm_live_touch_destroy(dm_live_touch *touch);
// The real picture can change between centered front-end 16:9 and in-run
// phone aspect. Resizing cancels core ownership and the pointer table.
void dm_live_touch_resize(dm_live_touch *touch, float width_pt, float height_pt);
// navigation = menu/front-end/panel state supplied by the real host.
// mask is the core target mask; callers use dm_tc_nav_mask/game mode mask.
void dm_live_touch_set_mode(dm_live_touch *touch, int navigation, uint32_t mask);
// Apply one validated provider context. Any scene/world/UI identity change
// cancels the old core ownership before the new mode is installed, so an old
// pointer or release is never delivered into the new scene/character.
void dm_live_touch_set_context(dm_live_touch *touch, int navigation, uint32_t mask,
                               uint64_t scene_epoch, uint64_t world_epoch,
                               uint64_t ui_identity, uint64_t object_generation);

// Android MotionEvent coordinates are window pixels. The viewport conversion
// maps the game picture into the core's point layout; out-of-picture points are
// clamped rather than aliased to another pointer.
int dm_live_touch_event(dm_live_touch *touch, int action, int action_index,
                        int pointer_count, const int32_t *ids,
                        const float *x_px, const float *y_px,
                        int viewport_width, int viewport_height,
                        float picture_left, float picture_top,
                        float picture_width, float picture_height,
                        int64_t time_ns);
int dm_live_touch_cancel_all(dm_live_touch *touch, int64_t time_ns);
// Shared pause/focus/input-destruction boundary: cancel any outstanding UI
// press callback and the core ownership together. The callback is the caller's
// actual dm_ui_touch phase-3 adapter; null is allowed when no UI source exists.
void dm_live_touch_cancel_sources(dm_live_touch *touch, int64_t time_ns,
                                  void (*ui_cancel)(void));
void dm_live_touch_tick(dm_live_touch *touch, int64_t time_ns);

uint64_t dm_live_touch_buttons(const dm_live_touch *touch);
int32_t dm_live_touch_lx(const dm_live_touch *touch);
int32_t dm_live_touch_ly(const dm_live_touch *touch);
int32_t dm_live_touch_rx(const dm_live_touch *touch);
int32_t dm_live_touch_ry(const dm_live_touch *touch);
int dm_live_touch_has_pointer(const dm_live_touch *touch, int32_t id);
int dm_live_touch_active(const dm_live_touch *touch);
int dm_live_touch_pointer_long(const dm_live_touch *touch);
// True while the core still owns physical pointers or has deferred/belt/pulse
// work that the owner loop must advance. Not a claim that the game consumed it.
int dm_live_touch_needs_work(const dm_live_touch *touch);
// One owner-thread scheduler step: advance core timers, bound-release a pulse
// after its core hold plus a fixed small margin (no game ack is fabricated),
// and return whether more owner work remains.
int dm_live_touch_scheduler_step(dm_live_touch *touch, int64_t time_ns);
// End a short zero-hold core pulse after it has been published once. The host
// press-edge latch is what makes the short A tap visible to the game; this is
// not an acknowledgement that the game consumed an action.
int dm_live_touch_end_released_pulse(dm_live_touch *touch);

#ifdef __cplusplus
}
#endif
