// Owned, bounded native Vulkan HUD geometry over the frozen r2d touch layout.
// This is an offscreen render slice: it reuses dm_tc_layout point geometry and
// dm_tc_hit for the same transform as input, and does not implement a game
// presenter, swapchain, glyph atlas or live binding/cooldown state.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "dm_touch_controls.h"
#include "touch_adapter.h"
#include "vk_host.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DM_TOUCH_HUD_VERTEX_CAP 2048

typedef struct {
    float width_pt, height_pt;
    float density;                 /* pixels per point; width_pt*density == pixel_width */
    uint32_t pixel_width, pixel_height;
    uint32_t available_mask;       /* target has a proven binding/resource */
    uint32_t enabled_mask;         /* target is shown in this context */
    uint32_t pressed_mask;         /* diagnostic pressed state; drawn for enabled targets even when availability is unknown */
    int joystick_active;           /* live observed joystick, not a guess */
    float joy_origin_x_pt, joy_origin_y_pt;
    float joy_current_x_pt, joy_current_y_pt;
} dm_touch_hud_snapshot;

// Convert an adapter observation into HUD input without inventing bindings or
// consuming anything. pixel_width/height must match width_pt/height_pt*density
// within one pixel; invalid input returns 0 and leaves *out untouched.
int dm_touch_hud_snapshot_from_observation(const dm_android_touch_observation *observation,
                                           uint32_t pixel_width, uint32_t pixel_height,
                                           dm_touch_hud_snapshot *out);

typedef struct {
    int target;                    /* DM_TC_* */
    float center_x_pt, center_y_pt, radius_pt;
    float center_x_px, center_y_px, radius_px;
} dm_touch_hud_geometry;

typedef struct {
    float x, y;
    float r, g, b, a;
} dm_touch_hud_vertex;

// Build the accepted HUD geometry into a caller buffer without any Vulkan
// object/allocation. Returns 0 on invalid snapshot/overlay or insufficient cap;
// the existing offscreen renderer uses the same builder.
int dm_touch_hud_build_vertices(const dm_touch_hud_snapshot *snapshot, int overlay,
                                dm_touch_hud_vertex *out, uint32_t capacity,
                                uint32_t *out_count);

typedef struct {
    float center_x_pt, center_y_pt, radius_pt;
    float center_x_px, center_y_px, radius_px;
} dm_touch_hud_joy_geometry;

// Explicit resource boundary: no licensed glyph atlas is shipped in this slice.
const char *dm_touch_hud_art_status(void);

// Pure preflight: no Vulkan object, allocation or output mutation.
int dm_touch_hud_validate(const dm_touch_hud_snapshot *snapshot);

// Geometry derived only from dm_tc_layout_make(width_pt,height_pt) and the
// snapshot density. Returns 0 on invalid input. out may hold DM_TC_COUNT.
int dm_touch_hud_build_geometry(const dm_touch_hud_snapshot *snapshot,
                          dm_touch_hud_geometry out[DM_TC_COUNT],
                          dm_touch_hud_joy_geometry *joy);

// Offscreen RGBA8 render. overlay=0 draws the HUD; overlay=1 draws target
// bounds/crosses and the joystick zone for hit-region review. Validation runs
// before any GPU allocation; on failure pixels and vk are untouched.
int dm_touch_hud_render(dm_vk *vk, const dm_touch_hud_snapshot *snapshot,
                        dm_vk_pixels *pixels, int overlay,
                        const char *vertex_spv, const char *fragment_spv);

#ifdef __cplusplus
}
#endif
