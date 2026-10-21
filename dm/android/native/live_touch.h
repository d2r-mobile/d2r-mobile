// Live Android touch bridge to the original D2R gesture core.
// This module only translates NativeActivity MotionEvent lifecycle and core
// output; it does not own game scene/UI state and does not synthesize actions.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "live_touch_geometry.h"
#include "../../ios/Sources/dm_touch_controls.h"
#include "../../ios/Sources/dm_touch_game_state.h"

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
// Original iOS tick: an identification scroll is not a dragged item.
void dm_live_touch_set_holding(dm_live_touch *touch, int holding, int targeting);
// iOS frontend map gestures: rect is the host's normalized minimap rectangle.
void dm_live_touch_set_minimap(dm_live_touch *touch, const float rect[4], int full);
// 1 = query world at nx/ny, then open full map only if no target; 2 = return mini.
int dm_live_touch_take_map_request(dm_live_touch *touch, float *nx, float *ny);
// Owner-thread access for the original frontend's continuous gesture hooks.
dm_tc_state *dm_live_touch_controls(dm_live_touch *touch);
// Update the real host belt snapshot used by the original core. rows=0/open=0
// is the explicit unknown/clear form; rows 1..4 and open 0/1 are valid. Invalid
// values clear any old request and do not enable belt gestures.
int dm_live_touch_set_belt(dm_live_touch *touch, int rows, int open);
// One-shot belt request consumer. Context epochs must match the current live
// context; an invalid/mismatched request is cleared without calling a host
// action. The callbacks are the actual host belt expand/use boundary, not an
// acknowledgement or a synthetic action queue.
typedef struct {
    void *context;
    void (*expand)(void *context, int on);
    void (*use)(void *context, int col, int row);
} dm_live_touch_belt_actions;
int dm_live_touch_consume_belt_request(dm_live_touch *touch,
                                       uint64_t scene_epoch, uint64_t world_epoch,
                                       uint64_t ui_identity,
                                       const dm_live_touch_belt_actions *actions);
// One-shot difficulty selector consumer. The core produces diff_request only
// for a selected 1..8 level. Context must still match; unknown/invalid levels
// are cleared without calling the host. The callback is the real
// dm_hud_request_players boundary, never a synthetic acknowledgement.
typedef struct {
    void *context;
    void (*request_players)(void *context, int players);
} dm_live_touch_difficulty_actions;
int dm_live_touch_consume_difficulty_request(dm_live_touch *touch,
                                             uint64_t scene_epoch, uint64_t world_epoch,
                                             uint64_t ui_identity,
                                             const dm_live_touch_difficulty_actions *actions);
// One-shot town-portal request from a fresh valid DM_TC_PORTAL touch down.
// The core's existing modifier/binding gesture is untouched. Context must
// still match; cancel/focus/navigation and context changes discard the request.
typedef struct {
    void *context;
    void (*request_portal)(void *context);
} dm_live_touch_portal_actions;
int dm_live_touch_consume_portal_request(dm_live_touch *touch,
                                         uint64_t scene_epoch, uint64_t world_epoch,
                                         uint64_t ui_identity,
                                         const dm_live_touch_portal_actions *actions);
// Empty-skill-slot tap binding. The front end supplies the real bridge snapshot
// and its scene/world. Only an exact UINT32_MAX raw slot with supported, ready,
// native identity and fresh data is empty; unknown/stale/non-native is never
// treated as empty. A tap outside the core gesture layer becomes one host
// dm_ui_bind_begin(key) request.
typedef struct {
    void *context;
    void (*begin_bind)(void *context, uint64_t key_mask);
} dm_live_touch_skill_bind_actions;
void dm_live_touch_set_skill_snapshot(dm_live_touch *touch,
                                      const dm_tg_snapshot *snapshot,
                                      uint64_t scene_epoch, uint64_t world_epoch);
void dm_live_touch_set_skill_bind_active(dm_live_touch *touch, int active);
int dm_live_touch_consume_skill_bind_request(dm_live_touch *touch,
                                             uint64_t scene_epoch, uint64_t world_epoch,
                                             const dm_live_touch_skill_bind_actions *actions);
// Pre-finish tap classification. Called after the host async item hit is known
// and immediately before the original core ends the navigation pointer.
// mode 0 = leave the original route, 1 = cursor move/select only, 2 = cursor
// move + the original Y button (skill binding). Unknown hits must return 0.
typedef int (*dm_live_touch_tap_hook)(void *context, float *nx, float *ny);
void dm_live_touch_set_tap_hook(dm_live_touch *touch, dm_live_touch_tap_hook hook,
                                void *context);
void dm_live_touch_pointer_move(dm_live_touch *touch, float nx, float ny);
unsigned dm_live_touch_tap_move_seq(const dm_live_touch *touch);
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
int dm_live_touch_skill_set(const dm_live_touch *touch);
uint64_t dm_live_touch_target_button(const dm_live_touch *touch, int target);
int32_t dm_live_touch_lx(const dm_live_touch *touch);
int32_t dm_live_touch_ly(const dm_live_touch *touch);
int32_t dm_live_touch_rx(const dm_live_touch *touch);
int32_t dm_live_touch_ry(const dm_live_touch *touch);
// Read-only snapshot of the actual core layout/state for the Android visual
// overlay. It never mutates gesture state and does not imply a game binding.
typedef struct {
    int valid;
    float width, height;
    float joy_x, joy_y, joy_radius;
    float joy_rest_x, joy_rest_y;
    float joy_origin_x, joy_origin_y;
    int joy_active;
    float swap_x, swap_y, swap_radius;
    float diff_x, diff_y, diff_radius;
    float pick_x[8], pick_y[8], pick_r[8];
    float target_x[DM_TC_COUNT], target_y[DM_TC_COUNT], target_r[DM_TC_COUNT];
    uint32_t enabled_targets;
    uint64_t buttons;
    int skill_set, diff_open, navigation_mode;
    int frontend;
    int32_t lx, ly;
} dm_live_touch_visual;
int dm_live_touch_visual_snapshot(const dm_live_touch *touch, dm_live_touch_visual *out);
// Invalidation key is defined in live_touch.c; the frontend declares it locally
// so the standalone root extraction fixture remains source-compatible.
int dm_live_touch_has_pointer(const dm_live_touch *touch, int32_t id);
int dm_live_touch_active(const dm_live_touch *touch);
int dm_live_touch_pointer_long(const dm_live_touch *touch);
int dm_live_touch_navigation(const dm_live_touch *touch);
int dm_live_touch_pointer_slot_for_id(const dm_live_touch *touch, int32_t id);
int dm_live_touch_pointer_slot(const dm_live_touch *touch);
int dm_live_touch_pointer_dragged_for_id(const dm_live_touch *touch, int32_t id);
float dm_live_touch_pointer_lift_norm(const dm_live_touch *touch);
int dm_live_touch_pointer_position(const dm_live_touch *touch,
                                   float *x, float *y, float *w, float *h);

// Publish the core's current pointer into the host cursor before any host item
// action or pad A pulse. The helper is shared by the event and timer producer
// paths so cursor placement cannot drift between them. It clears the host
// cursor when live routing is off or the core has no valid pointer.
typedef void (*dm_live_touch_cursor_set_fn)(void *context, float x, float y,
                                            float width, float height);
typedef void (*dm_live_touch_cursor_clear_fn)(void *context);
static inline void dm_live_touch_cursor_publish(const dm_live_touch *touch,
                                                int live_on,
                                                dm_live_touch_cursor_set_fn set,
                                                dm_live_touch_cursor_clear_fn clear,
                                                void *context) {
    float x, y, w, h;
    if (live_on && set && dm_live_touch_pointer_position(touch, &x, &y, &w, &h))
        set(context, x, y, w, h);
    else if (clear)
        clear(context);
}
// Item routing seam owned by live_touch_items.c. These call the original
// gesture core directly after a host hit/drop verdict; they do not synthesize
// an item action or acknowledge a host operation.
void dm_live_touch_apply_item_hit(dm_live_touch *touch, int slot, int kind);
void dm_live_touch_apply_drop(dm_live_touch *touch, int kind, float nx, float ny);
int dm_live_touch_finish_pointer(dm_live_touch *touch, int32_t id, int64_t time_ns);
typedef struct {
    unsigned use_seq, ident_seq, item_tap_seq, long_press_seq;
    unsigned stack_take_seq, tab_tap_seq, mouse_click_seq, shop_drop_seq;
    float mouse_click_x, mouse_click_y;
    int pointer_dragged, pointer_shop, pointer_stack;
} dm_live_touch_item_snapshot;
void dm_live_touch_item_snapshot_get(const dm_live_touch *touch,
                                     dm_live_touch_item_snapshot *out);
// True while the core still owns physical pointers or has deferred/belt/pulse
// work that the owner loop must advance. Not a claim that the game consumed it.
int dm_live_touch_needs_work(const dm_live_touch *touch);
// Advance core work using the real input poll count and cursor-withheld A state.
// Return work remaining; -1 cancels an unacknowledged pulse rather than reporting pickup.
int dm_live_touch_scheduler_step(dm_live_touch *touch, int64_t time_ns,
                                  uint64_t input_samples, int cursor_pending);
// End a short zero-hold core pulse after it has been published once. The host
// press-edge latch is what makes the short A tap visible to the game; this is
// not an acknowledgement that the game consumed an action.
// Pointer pickup/drop pulses are excluded: only the receipt scheduler ends them.
int dm_live_touch_end_released_pulse(dm_live_touch *touch);

#ifdef __cplusplus
}
#endif
