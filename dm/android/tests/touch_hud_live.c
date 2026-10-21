// #121 production seam: actual bridge MotionEvent -> read-only observation ->
// HUD snapshot/geometry/pixels. Actual owned host Vulkan; no game/SDK/device.
#include "touch_bridge.h"
#include "touch_hud.h"
#include "dm_touch_controls.h"
#include "vk_host.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks;
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c); return 1; } checks++; } while (0)

static int event(dm_touch_bridge *b, dm_touch_action action, int32_t id, float x_px, float y_px, int64_t t) {
    dm_touch_event e; memset(&e, 0, sizeof e);
    e.action = action; e.action_index = 0; e.pointer_count = 1;
    e.points[0] = (dm_touch_point){id, x_px, y_px}; e.time_ns = t;
    return dm_touch_bridge_event(b, &e);
}

static uint32_t pixel(const dm_vk_pixels *p, float x, float y) {
    uint32_t px = (uint32_t)x, py = (uint32_t)y;
    if (px >= p->width || py >= p->height) return 0;
    return ((uint32_t *)p->bytes)[(size_t)py * p->width + px];
}

int main(int argc, char **argv) {
    if (argc < 3) return 2;
    dm_touch_bridge *b = dm_touch_bridge_create(667, 375, 2);
    CHECK(b);
    dm_android_touch_observation o;
    dm_touch_hud_snapshot h;
    dm_tc_layout layout; dm_tc_layout_make(&layout, 667, 375);
    const float main_x = layout.target[DM_TC_MAIN].x * 2, main_y = layout.target[DM_TC_MAIN].y * 2;
    CHECK(event(b, DM_TOUCH_ACTION_DOWN, 7, main_x, main_y, 20000000000));
    CHECK(dm_touch_bridge_observe(b, 2, &o));
    CHECK(o.pressed_mask == (1u << DM_TC_MAIN));
    CHECK(o.width_pt == 667 && o.height_pt == 375 && o.density == 2);
    CHECK(o.available_mask == 0 && !o.joystick_active);
    CHECK(dm_touch_hud_snapshot_from_observation(&o, 1334, 750, &h));
    dm_touch_hud_geometry g[DM_TC_COUNT]; dm_touch_hud_joy_geometry joy;
    CHECK(dm_touch_hud_build_geometry(&h, g, &joy));
    CHECK(g[DM_TC_MAIN].center_x_px == main_x && g[DM_TC_MAIN].center_y_px == main_y);
    CHECK(dm_tc_hit(&layout, g[DM_TC_MAIN].center_x_pt, g[DM_TC_MAIN].center_y_pt) == DM_TC_MAIN);

    dm_vk vk; if (dm_vk_create(&vk) != VK_SUCCESS) { fprintf(stderr,"VULKAN UNAVAILABLE\n"); return 77; }
    dm_vk_pixels px = {0}; px.width = 1334; px.height = 750; px.capacity = 1334u*750u*4u; px.bytes = malloc(px.capacity);
    CHECK(px.bytes);
    CHECK(dm_touch_hud_render(&vk, &h, &px, 0, argv[1], argv[2]));
    uint32_t pressed = pixel(&px, main_x, main_y);
    CHECK(dm_touch_bridge_cancel_all(b, 20001000000));
    CHECK(dm_touch_bridge_observe(b, 2, &o));
    CHECK(o.pressed_mask == 0 && !o.joystick_active);
    CHECK(dm_touch_hud_snapshot_from_observation(&o, 1334, 750, &h));
    CHECK(dm_touch_hud_render(&vk, &h, &px, 0, argv[1], argv[2]));
    uint32_t normal = pixel(&px, main_x, main_y);
    CHECK(normal != pressed);   // no stale pressed frame after cancel

    /* Joystick: actual down/move observation drives origin/current, not layout guess. */
    CHECK(event(b, DM_TOUCH_ACTION_DOWN, 8, 180, 550, 20002000000));
    CHECK(dm_touch_bridge_observe(b, 2, &o));
    CHECK(o.joystick_active && o.joy_origin_x == 90 && o.joy_origin_y == 275);
    CHECK(o.joy_current_x == 90 && o.joy_current_y == 275);
    CHECK(dm_touch_hud_snapshot_from_observation(&o, 1334, 750, &h));
    CHECK(dm_touch_hud_render(&vk, &h, &px, 0, argv[1], argv[2]));
    uint32_t joy_at_rest_thumb = pixel(&px, 180, 550);
    CHECK(event(b, DM_TOUCH_ACTION_MOVE, 8, 300, 550, 20003000000));
    CHECK(dm_touch_bridge_observe(b, 2, &o));
    CHECK(o.joy_current_x == 150 && o.joy_current_y == 275);
    CHECK(dm_touch_hud_snapshot_from_observation(&o, 1334, 750, &h));
    CHECK(dm_touch_hud_render(&vk, &h, &px, 0, argv[1], argv[2]));
    uint32_t joy_moved_thumb = pixel(&px, 300, 550);
    CHECK(joy_moved_thumb != joy_at_rest_thumb);   // live thumb moved, no duplicated hit rules

    /* Observation does not consume a pending skill pulse. */
    CHECK(dm_touch_bridge_cancel_all(b, 20004000000));
    CHECK(event(b, DM_TOUCH_ACTION_DOWN, 9, layout.target[DM_TC_SKILL1].x*2, layout.target[DM_TC_SKILL1].y*2, 20005000000));
    CHECK(event(b, DM_TOUCH_ACTION_UP, 9, layout.target[DM_TC_SKILL1].x*2, layout.target[DM_TC_SKILL1].y*2, 20005100000));
    CHECK(dm_touch_bridge_observe(b, 2, &o));
    CHECK(dm_touch_bridge_observe(b, 2, &o));
    dm_npad_state s = dm_touch_bridge_read(b);
    CHECK(s.buttons == layout.target[DM_TC_SKILL1].button);
    CHECK(dm_touch_bridge_read(b).buttons == 0);

    /* Pending world-tap action survives repeated observations. */
    CHECK(event(b, DM_TOUCH_ACTION_DOWN, 10, 200, 400, 20006000000));
    CHECK(event(b, DM_TOUCH_ACTION_UP, 10, 200, 400, 20006100000));
    CHECK(dm_touch_bridge_observe(b, 2, &o));
    CHECK(dm_touch_bridge_observe(b, 2, &o));
    dm_android_touch_action a;
    CHECK(dm_touch_bridge_take_action(b, &a));
    CHECK(a.kind == DM_ANDROID_ACTION_WORLD_TAP);

    /* Resize: actual layout extent changes, pressed state is cleared. */
    CHECK(dm_touch_bridge_resize(b, 812, 375, 20007000000));
    CHECK(dm_touch_bridge_observe(b, 2, &o));
    CHECK(o.width_pt == 812 && o.height_pt == 375 && o.pressed_mask == 0 && !o.joystick_active);
    dm_tc_layout wide; dm_tc_layout_make(&wide, 812, 375);
    CHECK(wide.target[DM_TC_MAIN].x != layout.target[DM_TC_MAIN].x);

    /* Invalid conversion must not mutate caller output. */
    dm_touch_hud_snapshot sentinel; memset(&sentinel, 0x5a, sizeof sentinel);
    dm_touch_hud_snapshot before = sentinel;
    CHECK(!dm_touch_hud_snapshot_from_observation(&o, 1, 1, &sentinel));
    CHECK(memcmp(&sentinel, &before, sizeof sentinel) == 0);

    free(px.bytes); dm_vk_destroy(&vk); dm_touch_bridge_destroy(b, NULL);
    printf("touch HUD live seam: %d checks\n", checks);
    return 0;
}
