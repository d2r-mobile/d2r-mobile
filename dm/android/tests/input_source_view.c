// #165 focused regression: arbiter read-only view + atomic geometry over the
// SAME actual bridge, including takeover-probe cache and old-pointer retirement.
#include "input_source.h"
#include "touch_bridge.h"
#include "dm_touch_controls.h"
#include "dm_input.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks;
#define CHECK(expr) do { ++checks; if (!(expr)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); } } while (0)

static dm_touch_event ev(dm_touch_action action, int32_t id, float x_pt, float y_pt,
                         float density, int64_t t) {
    dm_touch_event e;
    memset(&e, 0, sizeof e);
    e.action = action;
    e.action_index = 0;
    e.pointer_count = 1;
    e.time_ns = t;
    e.points[0].id = id;
    e.points[0].x_px = x_pt * density;
    e.points[0].y_px = y_pt * density;
    return e;
}

static int send(dm_input_arbiter *a, dm_touch_action action, int32_t id,
                float x, float y, float density, int64_t t) {
    dm_touch_event e = ev(action, id, x, y, density, t);
    return dm_input_arbiter_touch(a, &e);
}

static void upper_point(const dm_tc_layout *l, int col, int row, float *x, float *y) {
    const dm_tc_target *t = &l->target[DM_TC_BELT1 + col];
    float u = l->height / 1320.0f;
    *x = t->x;
    *y = t->y - (165.0f + 162.0f * (row - 1)) * u;
}

static void observation_is_read_only(dm_input_arbiter *a, dm_touch_bridge *b,
                                     const dm_tc_layout *l) {
    dm_android_touch_observation o;
    dm_touch_diag d0, d1;
    dm_android_touch_action action;
    dm_npad_state s;
    float x, y;
    CHECK(dm_input_arbiter_observe(a, &o));
    CHECK(o.width_pt == 480 && o.height_pt == 300 && o.density == 3);
    CHECK(dm_input_arbiter_diag(a, &d0));
    CHECK(!dm_input_arbiter_observe(NULL, &o));
    CHECK(!dm_input_arbiter_observe(a, NULL));
    CHECK(!dm_input_arbiter_diag(NULL, &d1));
    CHECK(!dm_input_arbiter_diag(a, NULL));

    /* Repeated views must not consume the pending belt action. */
    CHECK(dm_touch_bridge_set_belt_context(b, 4, 1));
    upper_point(l, 1, 1, &x, &y);
    CHECK(send(a, DM_TOUCH_ACTION_DOWN, 11, x, y, 3, 1000000000));
    CHECK(dm_input_arbiter_diag(a, &d0));
    for (int i = 0; i < 3; ++i) {
        CHECK(dm_input_arbiter_observe(a, &o));
        CHECK(dm_input_arbiter_diag(a, &d1));
    }
    CHECK(d1.diagnostic_reads == d0.diagnostic_reads);
    CHECK(dm_touch_bridge_take_action(b, &action));
    CHECK(action.kind == DM_ANDROID_ACTION_BELT && (int)action.x == (0x100 | 1 << 4 | 1));
    CHECK(send(a, DM_TOUCH_ACTION_UP, 11, x, y, 3, 1010000000));

    /* Clear the takeover neutral boundary before checking the pulse. */
    s = dm_input_arbiter_read(a, &s) ? s : s;
    CHECK(s.buttons == 0);
    /* Repeated views must not consume a pending bottom-row pulse. */
    CHECK(send(a, DM_TOUCH_ACTION_DOWN, 12, l->target[DM_TC_BELT2].x,
               l->target[DM_TC_BELT2].y, 3, 1020000000));
    CHECK(send(a, DM_TOUCH_ACTION_UP, 12, l->target[DM_TC_BELT2].x,
               l->target[DM_TC_BELT2].y, 3, 1030000000));
    CHECK(dm_input_arbiter_observe(a, &o));
    CHECK(dm_input_arbiter_diag(a, &d1));
    s = dm_input_arbiter_read(a, &s) ? s : s;
    CHECK(s.buttons == DM_BTN_UP);
}

static void geometry_validation_and_transition(void) {
    dm_input_arbiter *a = dm_input_arbiter_create(480, 300, 3);
    dm_touch_bridge *b = dm_input_arbiter_test_touch(a);
    dm_tc_layout old_layout, new_layout;
    dm_android_touch_observation o;
    dm_android_touch_action action;
    dm_npad_state s;
    float px, py, pw, ph, pd;
    CHECK(a && b);
    dm_tc_layout_make(&old_layout, 480, 300);

    /* Invalid arguments must not change source, pending request, or geometry. */
    CHECK(dm_touch_bridge_set_belt_context(b, 4, 1));
    upper_point(&old_layout, 1, 1, &px, &py);
    CHECK(send(a, DM_TOUCH_ACTION_DOWN, 21, px, py, 3, 2000000000));
    CHECK(dm_input_arbiter_source(a) == DM_INPUT_SOURCE_TOUCH);
    CHECK(!dm_input_arbiter_set_geometry(a, NAN, 300, 3, 2001000000));
    CHECK(!dm_input_arbiter_set_geometry(a, 480, 0, 3, 2001000000));
    CHECK(!dm_input_arbiter_set_geometry(a, 480, 300, -1, 2001000000));
    CHECK(!dm_input_arbiter_set_geometry(a, 480, 300, 3, -1));
    CHECK(dm_input_arbiter_source(a) == DM_INPUT_SOURCE_TOUCH);
    CHECK(dm_input_arbiter_observe(a, &o));
    CHECK(o.width_pt == 480 && o.height_pt == 300 && o.density == 3);
    CHECK(dm_touch_bridge_take_action(b, &action));   /* pending survives invalid calls */
    CHECK(action.kind == DM_ANDROID_ACTION_BELT);
    CHECK(send(a, DM_TOUCH_ACTION_UP, 21, px, py, 3, 2002000000));

    /* Identical geometry is a no-op, including held input and pending request. */
    CHECK(send(a, DM_TOUCH_ACTION_DOWN, 22, old_layout.target[DM_TC_MAIN].x,
               old_layout.target[DM_TC_MAIN].y, 3, 2010000000));
    CHECK(dm_input_arbiter_set_geometry(a, 480, 300, 3, 2011000000));
    CHECK(dm_input_arbiter_source(a) == DM_INPUT_SOURCE_TOUCH);
    dm_touch_event held = ev(DM_TOUCH_ACTION_MOVE, 22, old_layout.target[DM_TC_MAIN].x,
                             old_layout.target[DM_TC_MAIN].y, 3, 2010500000);
    CHECK(dm_input_arbiter_touch(a, &held));
    CHECK(dm_input_arbiter_diag(a, &(dm_touch_diag){0}));
    CHECK(send(a, DM_TOUCH_ACTION_UP, 22, old_layout.target[DM_TC_MAIN].x,
               old_layout.target[DM_TC_MAIN].y, 3, 2012000000));

    CHECK(dm_touch_bridge_set_belt_context(b, 4, 1));
    upper_point(&old_layout, 1, 1, &px, &py);
    CHECK(send(a, DM_TOUCH_ACTION_DOWN, 23, px, py, 3, 2020000000));
    CHECK(dm_input_arbiter_set_geometry(a, 480, 300, 3, 2021000000));
    CHECK(dm_touch_bridge_take_action(b, &action));
    CHECK(send(a, DM_TOUCH_ACTION_UP, 23, px, py, 3, 2022000000));

    /* Real change retires old pointer, updates actual bridge and probe cache. */
    CHECK(send(a, DM_TOUCH_ACTION_DOWN, 24, old_layout.target[DM_TC_MAIN].x,
               old_layout.target[DM_TC_MAIN].y, 3, 2030000000));
    CHECK(dm_input_arbiter_set_geometry(a, 667, 375, 2, 2031000000));
    CHECK(dm_input_arbiter_source(a) == DM_INPUT_SOURCE_NONE);
    CHECK(dm_input_arbiter_requires_fresh(a));
    CHECK(dm_input_arbiter_observe(a, &o));
    CHECK(o.width_pt == 667 && o.height_pt == 375 && o.density == 2);
    CHECK(dm_input_arbiter_test_probe_geometry(a, &pw, &ph, &pd));
    CHECK(pw == 667 && ph == 375 && pd == 2);
    CHECK(!send(a, DM_TOUCH_ACTION_MOVE, 24, old_layout.target[DM_TC_MAIN].x,
                old_layout.target[DM_TC_MAIN].y, 3, 2032000000));
    CHECK(!send(a, DM_TOUCH_ACTION_UP, 24, old_layout.target[DM_TC_MAIN].x,
                old_layout.target[DM_TC_MAIN].y, 3, 2033000000));
    dm_tc_layout_make(&new_layout, 667, 375);
    CHECK(send(a, DM_TOUCH_ACTION_DOWN, 25, new_layout.target[DM_TC_MAIN].x,
               new_layout.target[DM_TC_MAIN].y, 2, 2034000000));
    CHECK(dm_input_arbiter_source(a) == DM_INPUT_SOURCE_TOUCH);
    CHECK(!dm_input_arbiter_requires_fresh(a));
    s = dm_input_arbiter_read(a, &s) ? s : s;
    CHECK(s.buttons == 0); /* neutral boundary */
    s = dm_input_arbiter_read(a, &s) ? s : s;
    CHECK(s.attributes == 1);
    CHECK(send(a, DM_TOUCH_ACTION_UP, 25, new_layout.target[DM_TC_MAIN].x,
               new_layout.target[DM_TC_MAIN].y, 2, 2035000000));
    dm_input_arbiter_destroy(a, NULL);
}

int main(void) {
    dm_input_arbiter *a = dm_input_arbiter_create(480, 300, 3);
    dm_touch_bridge *b = dm_input_arbiter_test_touch(a);
    dm_tc_layout layout;
    CHECK(a && b);
    dm_tc_layout_make(&layout, 480, 300);
    observation_is_read_only(a, b, &layout);
    /* Pending request consumed above; re-establish a clean arbiter for geometry. */
    dm_input_arbiter_destroy(a, NULL);
    geometry_validation_and_transition();
    printf("PASS input source view: %d checks\n", checks);
    return 0;
}
