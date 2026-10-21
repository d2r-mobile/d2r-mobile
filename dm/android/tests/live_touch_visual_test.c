// Read-only visual snapshot fixture for the draw-only Android overlay.
#include "../native/live_touch.c"

#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); return 1; } } while (0)

static int event(dm_live_touch *t, int action, int id, float x, float y, int64_t at) {
    int32_t pointer = id;
    int w = (int)t->width, h = (int)t->height;
    return dm_live_touch_event(t, action, 0, 1, &pointer, &x, &y,
                               w, h, 0, 0, w, h, at);
}

int main(void) {
    dm_tc_layout layout;
    dm_tc_layout_make(&layout, 667, 600);
    dm_live_touch *t = dm_live_touch_create(667, 600);
    CHECK(t != NULL);
    dm_live_touch_set_context(t, 0, (1u << DM_TC_COUNT) - 1, 1, 1, 1, 0);

    dm_live_touch_visual v;
    CHECK(dm_live_touch_visual_snapshot(t, &v) == 1 && v.valid);
    CHECK(v.width == 667 && v.height == 600);
    CHECK(v.joy_radius > 0 && v.swap_radius > 0 && v.diff_radius > 0);
    CHECK(v.enabled_targets == (1u << DM_TC_COUNT) - 1);
    CHECK(v.target_x[DM_TC_MAIN] == layout.target[DM_TC_MAIN].x);
    CHECK(v.target_y[DM_TC_MAIN] == layout.target[DM_TC_MAIN].y);
    CHECK(dm_tc_hit(&layout, v.target_x[DM_TC_MAIN], v.target_y[DM_TC_MAIN]) == DM_TC_MAIN);
    CHECK(dm_tc_hit(&layout, v.target_x[DM_TC_SKILL1], v.target_y[DM_TC_SKILL1]) == DM_TC_SKILL1);

    // Empty/destroyed snapshot is never a fake visible layout.
    CHECK(dm_live_touch_visual_snapshot(NULL, &v) == 0);

    // Actual selector open publishes the pick cells; no fake static menu.
    CHECK(event(t, DM_LIVE_ACTION_DOWN, 10, layout.diff.x, layout.diff.y, 1000000000));
    CHECK(dm_live_touch_visual_snapshot(t, &v) == 1 && v.diff_open == 1);
    CHECK(v.pick_x[0] == layout.pick[0].x && v.pick_r[7] == layout.pick[7].r);
    (void)event(t, DM_LIVE_ACTION_UP, 10, layout.diff.x, layout.diff.y, 1010000000);

    // Resize and cancel boundaries: snapshot tracks the actual core, with no buttons.
    dm_live_touch_resize(t, 800, 400);
    CHECK(dm_live_touch_visual_snapshot(t, &v) == 1 && v.width == 800 && v.height == 400);
    CHECK(dm_live_touch_cancel_all(t, 1020000000));
    CHECK(dm_live_touch_visual_snapshot(t, &v) == 1 && v.buttons == 0);

    // Floating joystick: core DOWN at (70,240) has zero axes but must still publish active origin.
    dm_live_touch *jt = dm_live_touch_create(667, 375);
    CHECK(jt != NULL);
    dm_live_touch_set_context(jt, 0, (1u << DM_TC_COUNT) - 1, 1, 1, 1, 0);
    CHECK(event(jt, DM_LIVE_ACTION_DOWN, 21, 70, 240, 1030000000));
    CHECK(dm_live_touch_visual_snapshot(jt, &v) == 1 && v.joy_active == 1);
    CHECK(v.joy_origin_x == 70 && v.joy_origin_y == 240);
    CHECK(v.joy_x == 70 && v.joy_y == 240);
    CHECK(v.lx == 0 && v.ly == 0);
    CHECK(event(jt, DM_LIVE_ACTION_MOVE, 21, 130, 240, 1040000000));
    CHECK(dm_live_touch_visual_snapshot(jt, &v) == 1 && v.joy_active == 1);
    CHECK(v.joy_origin_x == 70 && v.joy_origin_y == 240);
    CHECK(v.lx > 0 && v.ly == 0);
    CHECK(event(jt, DM_LIVE_ACTION_UP, 21, 130, 240, 1050000000));
    CHECK(dm_live_touch_visual_snapshot(jt, &v) == 1 && v.joy_active == 0);
    CHECK(v.joy_x == v.joy_rest_x && v.joy_y == v.joy_rest_y);
    dm_live_touch_destroy(jt);

    // Actual navigation state suppresses gameplay-only entries; legal targets stay in the mask.
    uint32_t nav_mask = dm_tc_nav_mask(1) | (1u << DM_TC_MENU);
    dm_live_touch_set_context(t, 1, nav_mask, 2, 2, 2, 0);
    CHECK(dm_live_touch_visual_snapshot(t, &v) == 1 && v.navigation_mode == 1);
    CHECK(v.enabled_targets == nav_mask);
    CHECK((v.enabled_targets & (1u << DM_TC_PORTAL)) == 0);
    CHECK((v.enabled_targets & (1u << DM_TC_BELT1)) == 0);
    CHECK((v.enabled_targets & (1u << DM_TC_MENU)) != 0);
    CHECK((v.enabled_targets & (1u << DM_TC_MAIN)) != 0);

    // Direct production signature: mask-only change must invalidate; identical snapshot must not.
    dm_live_touch_visual sig_a = {0}, sig_b = {0};
    dm_live_touch_geometry sig_g = {0};
    sig_a.enabled_targets = 1; sig_b = sig_a;
    CHECK(dm_live_touch_visual_signature(&sig_a, &sig_g) ==
          dm_live_touch_visual_signature(&sig_b, &sig_g));
    sig_b.enabled_targets = 0;
    CHECK(dm_live_touch_visual_signature(&sig_a, &sig_g) !=
          dm_live_touch_visual_signature(&sig_b, &sig_g));
    sig_b.enabled_targets = 1; sig_b.frontend = 1;
    CHECK(dm_live_touch_visual_signature(&sig_a, &sig_g) !=
          dm_live_touch_visual_signature(&sig_b, &sig_g));

    // Scene/panel transitions must change the visual snapshot/signature with no
    // touch event at all; an unchanged repeat must not invalidate.
    dm_live_touch *at = dm_live_touch_create(667, 375);
    CHECK(at != NULL);
    dm_live_touch_set_context(at, 1, dm_tc_nav_mask(1), 10, 20, 30, 0);
    dm_live_touch_visual front = {0}, camp = {0}, panel = {0};
    CHECK(dm_live_touch_visual_snapshot(at, &front) == 1 && front.navigation_mode == 1);
    dm_live_touch_geometry ag = {0};
    uint64_t front_sig = dm_live_touch_visual_signature(&front, &ag);
    CHECK(dm_live_touch_visual_snapshot(at, &camp) == 1);
    CHECK(front_sig == dm_live_touch_visual_signature(&camp, &ag)); // unchanged repeat
    dm_live_touch_set_context(at, 0, ((1u << DM_TC_COUNT) - 1) & ~(1u << DM_TC_MAP), 10, 20, 30, 0);
    CHECK(dm_live_touch_visual_snapshot(at, &camp) == 1 && camp.navigation_mode == 0);
    CHECK(front_sig != dm_live_touch_visual_signature(&camp, &ag));
    dm_live_touch_set_context(at, 1, dm_tc_nav_mask(1), 10, 20, 30, 0);
    CHECK(dm_live_touch_visual_snapshot(at, &panel) == 1 && panel.navigation_mode == 1);
    CHECK(dm_live_touch_visual_signature(&panel, &ag) !=
          dm_live_touch_visual_signature(&camp, &ag));
    dm_live_touch_destroy(at);

    // Destroyed owner cannot expose a snapshot (caller must drop the pointer).
    dm_live_touch_destroy(t);
    CHECK(dm_live_touch_visual_snapshot(NULL, &v) == 0);
    puts("PASS: live visual layout snapshot, target centers, difficulty pick, resize, cancel, empty");
    return 0;
}
