#include "../app/touch_bridge.h"
#include "dm_touch_controls.h"
#include "dm_input.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks;
#define CHECK(expr) do { ++checks; if (!(expr)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); } } while (0)
#define CASE(f) do { f(); printf("PASS %s (%d checks)\n", #f, checks); } while (0)

static void point(dm_touch_event *event, int32_t id, float x_pt, float y_pt, float density) {
    CHECK(event->pointer_count < DM_TOUCH_MAX_POINTS);
    dm_touch_point *p = &event->points[event->pointer_count++];
    p->id = id;
    p->x_px = x_pt * density;
    p->y_px = y_pt * density;
}

static dm_touch_event event(dm_touch_action action, int32_t index, int64_t time_ns) {
    dm_touch_event e;
    memset(&e, 0, sizeof(e));
    e.action = action;
    e.action_index = index;
    e.time_ns = time_ns;
    return e;
}

static dm_npad_state read_state(dm_touch_bridge *bridge) {
    dm_npad_state state = dm_touch_bridge_read(bridge);
    CHECK(state.attributes == 1);
    return state;
}

static void down(dm_touch_bridge *bridge, int32_t id, float x, float y, int64_t time_ns) {
    dm_touch_event e = event(DM_TOUCH_ACTION_DOWN, 0, time_ns);
    point(&e, id, x, y, 2.0f);
    CHECK(dm_touch_bridge_event(bridge, &e));
}
static void up(dm_touch_bridge *bridge, int32_t id, float x, float y, int64_t time_ns) {
    dm_touch_event e = event(DM_TOUCH_ACTION_UP, 0, time_ns);
    point(&e, id, x, y, 2.0f);
    CHECK(dm_touch_bridge_event(bridge, &e));
}
static void tap(dm_touch_bridge *bridge, int32_t id, float x, float y, int64_t time_ns) {
    down(bridge, id, x, y, time_ns);
    up(bridge, id, x, y, time_ns + 1000000);
}

static void density_and_noncontiguous_ids(void) {
    dm_touch_bridge *b = dm_touch_bridge_create(667, 375, 2.0f);
    dm_tc_layout layout;
    CHECK(b != NULL);
    dm_tc_layout_make(&layout, 667, 375);

    /* Physical pixels at density 2 must land on the point-space target. */
    down(b, 100000, layout.target[DM_TC_MAIN].x, layout.target[DM_TC_MAIN].y, 1000000000);
    dm_npad_state s = read_state(b);
    CHECK(s.buttons == DM_BTN_A);
    up(b, 100000, layout.target[DM_TC_MAIN].x, layout.target[DM_TC_MAIN].y, 1010000000);
    s = read_state(b);
    CHECK(s.buttons == 0);
    dm_touch_bridge_destroy(b, NULL);
}

static void action_index_multifinger(void) {
    dm_touch_bridge *b = dm_touch_bridge_create(667, 375, 2.0f);
    dm_tc_layout layout;
    dm_touch_event e;
    CHECK(b != NULL);
    dm_tc_layout_make(&layout, 667, 375);

    down(b, 42, layout.target[DM_TC_MAIN].x, layout.target[DM_TC_MAIN].y, 2000000000);
    (void)read_state(b);
    e = event(DM_TOUCH_ACTION_POINTER_DOWN, 1, 2010000000);
    point(&e, 42, layout.target[DM_TC_MAIN].x, layout.target[DM_TC_MAIN].y, 2.0f);
    point(&e, 9001, 90, 275, 2.0f);
    CHECK(dm_touch_bridge_event(b, &e));
    e = event(DM_TOUCH_ACTION_MOVE, 0, 2020000000);
    point(&e, 42, layout.target[DM_TC_MAIN].x, layout.target[DM_TC_MAIN].y, 2.0f);
    point(&e, 9001, 150, 275, 2.0f);
    CHECK(dm_touch_bridge_event(b, &e));
    dm_npad_state s = read_state(b);
    CHECK(s.buttons == DM_BTN_A);
    CHECK(s.lx == DM_STICK_MAX);

    /* ACTION_POINTER_UP index 1 lifts the joystick, not array slot 0. */
    e = event(DM_TOUCH_ACTION_POINTER_UP, 1, 2030000000);
    point(&e, 42, layout.target[DM_TC_MAIN].x, layout.target[DM_TC_MAIN].y, 2.0f);
    point(&e, 9001, 150, 275, 2.0f);
    CHECK(dm_touch_bridge_event(b, &e));
    s = read_state(b);
    CHECK(s.buttons == DM_BTN_A);
    CHECK(s.lx == 0 && s.ly == 0);
    up(b, 42, layout.target[DM_TC_MAIN].x, layout.target[DM_TC_MAIN].y, 2040000000);
    s = read_state(b);
    CHECK(s.buttons == 0);
    dm_touch_bridge_destroy(b, NULL);
}

static void resize_cancels_and_reuses_id(void) {
    dm_touch_bridge *b = dm_touch_bridge_create(667, 375, 2.0f);
    dm_tc_layout layout;
    CHECK(b != NULL);
    dm_tc_layout_make(&layout, 667, 375);
    down(b, 7, layout.target[DM_TC_MAIN].x, layout.target[DM_TC_MAIN].y, 7000000000);
    CHECK(read_state(b).buttons == DM_BTN_A);
    CHECK(dm_touch_bridge_resize(b, 600, 400, 7010000000));
    CHECK(read_state(b).buttons == 0);
    dm_tc_layout_make(&layout, 600, 400);
    down(b, 7, layout.target[DM_TC_MAIN].x, layout.target[DM_TC_MAIN].y, 7020000000);
    CHECK(read_state(b).buttons == DM_BTN_A); /* bridge active[] followed core reset */
    up(b, 7, layout.target[DM_TC_MAIN].x, layout.target[DM_TC_MAIN].y, 7030000000);
    dm_touch_bridge_destroy(b, NULL);
}

static void density_change_cancels_held_gesture(void) {
    dm_touch_bridge *b = dm_touch_bridge_create(667, 375, 2.0f);
    dm_tc_layout layout;
    CHECK(b != NULL);
    dm_tc_layout_make(&layout, 667, 375);
    down(b, 11, layout.target[DM_TC_MAIN].x, layout.target[DM_TC_MAIN].y, 7100000000);
    CHECK(read_state(b).buttons == DM_BTN_A);
    CHECK(dm_touch_bridge_set_density(b, 3.0f));
    CHECK(read_state(b).buttons == 0); /* held gesture cancelled, not rebased */
    dm_touch_event e = event(DM_TOUCH_ACTION_DOWN, 0, 7110000000);
    point(&e, 11, layout.target[DM_TC_MAIN].x, layout.target[DM_TC_MAIN].y, 3.0f);
    CHECK(dm_touch_bridge_event(b, &e));
    CHECK(read_state(b).buttons == DM_BTN_A);
    dm_touch_bridge_destroy(b, NULL);
}

static void cancel_without_points(void) {
    dm_touch_bridge *b = dm_touch_bridge_create(667, 375, 2.0f);
    dm_tc_layout layout;
    CHECK(b != NULL);
    dm_tc_layout_make(&layout, 667, 375);
    down(b, 12, layout.target[DM_TC_MAIN].x, layout.target[DM_TC_MAIN].y, 7200000000);
    (void)read_state(b);
    dm_touch_event e = event(DM_TOUCH_ACTION_CANCEL, 0, 7210000000);
    CHECK(dm_touch_bridge_event(b, &e)); /* zero pointers still cancels */
    CHECK(dm_touch_bridge_diag(b)->state.buttons == 0);
    CHECK(read_state(b).buttons == 0);
    dm_touch_bridge_destroy(b, NULL);
}

static void malformed_event_sets(void) {
    dm_touch_bridge *b = dm_touch_bridge_create(667, 375, 2.0f);
    dm_tc_layout layout;
    dm_touch_event e;
    CHECK(b != NULL);
    dm_tc_layout_make(&layout, 667, 375);

    e = event(DM_TOUCH_ACTION_DOWN, 0, 7300000000);
    point(&e, 20, layout.target[DM_TC_MAIN].x, layout.target[DM_TC_MAIN].y, 2.0f);
    point(&e, 21, 90, 275, 2.0f);
    CHECK(!dm_touch_bridge_event(b, &e)); /* non-owner point in DOWN frame */
    down(b, 20, layout.target[DM_TC_MAIN].x, layout.target[DM_TC_MAIN].y, 7310000000);
    e = event(DM_TOUCH_ACTION_UP, 0, 7320000000);
    point(&e, 20, layout.target[DM_TC_MAIN].x, layout.target[DM_TC_MAIN].y, 2.0f);
    point(&e, 21, 90, 275, 2.0f);
    CHECK(!dm_touch_bridge_event(b, &e)); /* non-owner point in UP frame */
    CHECK(read_state(b).buttons == DM_BTN_A); /* malformed UP did not stick/clear */
    dm_touch_bridge_destroy(b, NULL);
}

static void invalid_events(void) {
    dm_touch_bridge *b = dm_touch_bridge_create(667, 375, 2.0f);
    dm_touch_event e;
    CHECK(b != NULL);
    CHECK(dm_touch_bridge_create(667, 375, 0) == NULL);
    CHECK(!dm_touch_bridge_set_density(b, 0));

    e = event(DM_TOUCH_ACTION_DOWN, 0, -1);
    point(&e, 1, 100, 100, 2.0f);
    CHECK(!dm_touch_bridge_event(b, &e));
    e = event(DM_TOUCH_ACTION_DOWN, 0, 1);
    CHECK(!dm_touch_bridge_event(b, &e)); /* zero pointers */
    e = event(DM_TOUCH_ACTION_DOWN, 0, 2);
    point(&e, 1, 100, 100, 2.0f);
    point(&e, 1, 110, 110, 2.0f);
    CHECK(!dm_touch_bridge_event(b, &e)); /* duplicate pointer id */
    e = event(DM_TOUCH_ACTION_DOWN, 5, 3);
    point(&e, 1, 100, 100, 2.0f);
    CHECK(!dm_touch_bridge_event(b, &e)); /* invalid action index */
    e = event(DM_TOUCH_ACTION_DOWN, 0, 4);
    point(&e, 1, NAN, 100, 2.0f);
    CHECK(!dm_touch_bridge_event(b, &e)); /* non-finite coordinate */
    e = event(DM_TOUCH_ACTION_UP, 0, 5);
    point(&e, 77, 100, 100, 2.0f);
    CHECK(!dm_touch_bridge_event(b, &e)); /* unknown pointer id */
    CHECK(dm_touch_bridge_diag(b)->invalid_events >= 6);
    dm_touch_bridge_destroy(b, NULL);
}

static void timer_without_touch(void) {
    dm_touch_bridge *b = dm_touch_bridge_create(667, 375, 2.0f);
    dm_tc_layout layout;
    CHECK(b != NULL);
    dm_tc_layout_make(&layout, 667, 375);
    down(b, 1, layout.target[DM_TC_SKILL1].x, layout.target[DM_TC_SKILL1].y, 3000000000);
    (void)read_state(b);
    CHECK(dm_touch_bridge_tick(b, 3100000000));
    dm_npad_state s = read_state(b);
    CHECK(s.buttons == DM_BTN_B);
    CHECK(dm_touch_bridge_tick(b, 3110000000));
    s = read_state(b);
    CHECK(s.buttons == DM_BTN_B);
    up(b, 1, layout.target[DM_TC_SKILL1].x, layout.target[DM_TC_SKILL1].y, 3200000000);
    s = read_state(b);
    CHECK(s.buttons == 0);
    dm_touch_bridge_destroy(b, NULL);
}

static void ring_full_up_retry(void) {
    dm_touch_bridge *b = dm_touch_bridge_create(667, 375, 2.0f);
    dm_tc_layout layout;
    CHECK(b != NULL);
    dm_tc_layout_make(&layout, 667, 375);
    const float sx = layout.target[DM_TC_SKILL1].x;
    const float sy = layout.target[DM_TC_SKILL1].y;
    const float mx = layout.target[DM_TC_MAIN].x;
    const float my = layout.target[DM_TC_MAIN].y;

    down(b, 1, mx, my, 4000000000); /* held while the pulse ring fills */
    for (int i = 0; i < 16; i++) {
        tap(b, 100, sx, sy, 4010000000 + (int64_t)i * 2000000);
    }
    up(b, 1, mx, my, 4400000000);
    CHECK(dm_touch_bridge_diag(b)->up_retries == 1);
    CHECK(dm_touch_bridge_diag(b)->rejected_events == 0);
    CHECK(dm_touch_bridge_diag(b)->cancel_fallbacks == 0);
    for (int i = 0; i < 16; i++) (void)read_state(b);
    dm_npad_state s = read_state(b);
    CHECK(s.buttons == 0 && s.lx == 0);
    dm_touch_bridge_destroy(b, NULL);
}

static void cancel_clears_queued_pulses(void) {
    dm_touch_bridge *b = dm_touch_bridge_create(667, 375, 2.0f);
    dm_tc_layout layout;
    CHECK(b != NULL);
    dm_tc_layout_make(&layout, 667, 375);
    for (int i = 0; i < 16; i++) {
        tap(b, 100, layout.target[DM_TC_SKILL1].x, layout.target[DM_TC_SKILL1].y,
            5000000000 + (int64_t)i * 2000000);
    }
    CHECK(dm_touch_bridge_cancel_all(b, 5400000000));
    dm_npad_state s = read_state(b);
    CHECK(s.buttons == 0);
    CHECK(dm_touch_bridge_diag(b)->state.buttons == 0);
    dm_touch_bridge_destroy(b, NULL);
}

static void resize_density_and_neutral_destroy(void) {
    dm_touch_bridge *b = dm_touch_bridge_create(667, 375, 2.0f);
    dm_npad_state neutral;
    dm_tc_layout layout;
    CHECK(b != NULL);
    CHECK(dm_touch_bridge_set_density(b, 3.0f));
    CHECK(dm_touch_bridge_density(b) == 3.0f);
    CHECK(dm_touch_bridge_resize(b, 600, 400, 6000000000));
    dm_tc_layout_make(&layout, 600, 400);
    dm_touch_event e = event(DM_TOUCH_ACTION_DOWN, 0, 6001000000);
    point(&e, 8, layout.target[DM_TC_SKILL1].x, layout.target[DM_TC_SKILL1].y, 3.0f);
    CHECK(dm_touch_bridge_event(b, &e));
    CHECK(dm_touch_bridge_tick(b, 6101000000));
    dm_npad_state s = read_state(b);
    CHECK(s.buttons == DM_BTN_B);
    dm_touch_bridge_destroy(b, &neutral);
    CHECK(neutral.attributes == 1);
    CHECK(neutral.buttons == 0 && neutral.lx == 0 && neutral.ly == 0);
}

static void belt_context_change_cancels_outer_and_reuses_id(void) {
    dm_touch_bridge *b = dm_touch_bridge_create(667, 375, 2.0f);
    dm_tc_layout layout;
    dm_touch_event e;
    dm_npad_state s;
    CHECK(b != NULL);
    dm_tc_layout_make(&layout, 667, 375);
    down(b, 7, layout.target[DM_TC_MAIN].x, layout.target[DM_TC_MAIN].y, 8000000000);
    s = read_state(b); CHECK(s.buttons == DM_BTN_A);
    CHECK(dm_touch_bridge_set_belt_context(b, 4, 0));   /* real change */
    s = read_state(b); CHECK(s.buttons == 0);
    /* The adapter released the pointer; the outer table must agree, or these
       stale events would be accepted and the reused DOWN rejected. */
    e = event(DM_TOUCH_ACTION_MOVE, 0, 8010000000);
    point(&e, 7, layout.target[DM_TC_MAIN].x, layout.target[DM_TC_MAIN].y, 2.0f);
    CHECK(!dm_touch_bridge_event(b, &e));
    e = event(DM_TOUCH_ACTION_UP, 0, 8020000000);
    point(&e, 7, layout.target[DM_TC_MAIN].x, layout.target[DM_TC_MAIN].y, 2.0f);
    CHECK(!dm_touch_bridge_event(b, &e));
    down(b, 7, layout.target[DM_TC_MAIN].x, layout.target[DM_TC_MAIN].y, 8030000000);
    s = read_state(b); CHECK(s.buttons == DM_BTN_A);   /* same ID accepted again */
    up(b, 7, layout.target[DM_TC_MAIN].x, layout.target[DM_TC_MAIN].y, 8040000000);
    s = read_state(b); CHECK(s.buttons == 0);
    dm_touch_bridge_destroy(b, NULL);
}

static void belt_context_invalid_and_same_preserve_ownership(void) {
    dm_touch_bridge *b = dm_touch_bridge_create(667, 375, 2.0f);
    dm_tc_layout layout;
    dm_touch_event e;
    dm_npad_state s;
    CHECK(b != NULL);
    dm_tc_layout_make(&layout, 667, 375);
    down(b, 9, layout.target[DM_TC_MAIN].x, layout.target[DM_TC_MAIN].y, 8100000000);
    s = read_state(b); CHECK(s.buttons == DM_BTN_A);
    CHECK(!dm_touch_bridge_set_belt_context(NULL, 4, 0));
    CHECK(!dm_touch_bridge_set_belt_context(b, -1, 0));
    CHECK(!dm_touch_bridge_set_belt_context(b, 5, 0));
    CHECK(!dm_touch_bridge_set_belt_context(b, 4, 2));
    CHECK(!dm_touch_bridge_set_belt_context(b, 4, -1));
    /* Rejected updates changed neither layer: the held pointer still moves. */
    e = event(DM_TOUCH_ACTION_MOVE, 0, 8101000000);
    point(&e, 9, layout.target[DM_TC_MAIN].x, layout.target[DM_TC_MAIN].y, 2.0f);
    CHECK(dm_touch_bridge_event(b, &e));
    s = read_state(b); CHECK(s.buttons == DM_BTN_A);
    /* Same snapshot (bridge default 0/0) is a no-op: no cancel, no consume. */
    CHECK(dm_touch_bridge_set_belt_context(b, 0, 0));
    s = read_state(b); CHECK(s.buttons == DM_BTN_A);
    e = event(DM_TOUCH_ACTION_MOVE, 0, 8102000000);
    point(&e, 9, layout.target[DM_TC_MAIN].x, layout.target[DM_TC_MAIN].y, 2.0f);
    CHECK(dm_touch_bridge_event(b, &e));
    up(b, 9, layout.target[DM_TC_MAIN].x, layout.target[DM_TC_MAIN].y, 8103000000);
    s = read_state(b); CHECK(s.buttons == 0);
    dm_touch_bridge_destroy(b, NULL);
}

static void belt_context_survives_cancel_and_resize(void) {
    dm_touch_bridge *b = dm_touch_bridge_create(667, 375, 2.0f);
    dm_tc_layout layout;
    dm_touch_event e;
    dm_npad_state s;
    CHECK(b != NULL);
    dm_tc_layout_make(&layout, 667, 375);
    CHECK(dm_touch_bridge_set_belt_context(b, 4, 0));
    CHECK(dm_touch_bridge_cancel_all(b, 8190000000));   /* context is host state, not a gesture */
    down(b, 5, layout.target[DM_TC_MAIN].x, layout.target[DM_TC_MAIN].y, 8200000000);
    s = read_state(b); CHECK(s.buttons == DM_BTN_A);
    CHECK(dm_touch_bridge_set_belt_context(b, 4, 0));   /* retained: same values, no cancel */
    s = read_state(b); CHECK(s.buttons == DM_BTN_A);
    up(b, 5, layout.target[DM_TC_MAIN].x, layout.target[DM_TC_MAIN].y, 8201000000);
    s = read_state(b); CHECK(s.buttons == 0);
    /* Resize cancels pointers but keeps the host snapshot; a same-value update
       after resize must not cancel a newly held finger. */
    CHECK(dm_touch_bridge_resize(b, 812, 375, 8210000000));
    dm_tc_layout_make(&layout, 812, 375);
    down(b, 6, layout.target[DM_TC_MAIN].x, layout.target[DM_TC_MAIN].y, 8211000000);
    s = read_state(b); CHECK(s.buttons == DM_BTN_A);
    CHECK(dm_touch_bridge_set_belt_context(b, 4, 0));
    s = read_state(b); CHECK(s.buttons == DM_BTN_A);
    up(b, 6, layout.target[DM_TC_MAIN].x, layout.target[DM_TC_MAIN].y, 8212000000);
    s = read_state(b); CHECK(s.buttons == 0);
    /* A real change after resize still invalidates both layers. */
    down(b, 7, layout.target[DM_TC_MAIN].x, layout.target[DM_TC_MAIN].y, 8220000000);
    s = read_state(b); CHECK(s.buttons == DM_BTN_A);
    CHECK(dm_touch_bridge_set_belt_context(b, 4, 1));
    s = read_state(b); CHECK(s.buttons == 0);
    e = event(DM_TOUCH_ACTION_MOVE, 0, 8221000000);
    point(&e, 7, layout.target[DM_TC_MAIN].x, layout.target[DM_TC_MAIN].y, 2.0f);
    CHECK(!dm_touch_bridge_event(b, &e));
    /* The same ID is accepted under the new (expanded) context; a bottom-row
       cell still drinks, so the pointer is genuinely live. */
    down(b, 7, layout.target[DM_TC_BELT2].x, layout.target[DM_TC_BELT2].y, 8222000000);
    s = read_state(b); CHECK(s.buttons == 0);
    up(b, 7, layout.target[DM_TC_BELT2].x, layout.target[DM_TC_BELT2].y, 8223000000);
    s = read_state(b); CHECK(s.buttons == DM_BTN_UP);
    s = read_state(b); CHECK(s.buttons == 0);
    dm_touch_bridge_destroy(b, NULL);
}

static void belt_upper_point(const dm_tc_layout *l, int col, int row, float *x, float *y) {
    const dm_tc_target *t = &l->target[DM_TC_BELT1 + col];
    float u = l->height / 1320.0f;
    *x = t->x;
    *y = t->y - (165.0f + 162.0f * (row - 1)) * u;
}

static void belt_action_expanded_cell_one_shot(void) {
    dm_touch_bridge *b = dm_touch_bridge_create(667, 375, 2.0f);
    dm_tc_layout layout;
    dm_android_touch_action a;
    dm_npad_state s;
    float x, y;
    CHECK(b != NULL);
    dm_tc_layout_make(&layout, 667, 375);
    CHECK(dm_touch_bridge_set_belt_context(b, 4, 1));
    /* Expanded row 1, column 1: the core request is 0x100 | row<<4 | col =
       0x111 (player drink via dm_belt_use). The merc-only host code for the
       same cell would be 0x106; the existing action enum has one BELT kind, so
       no merc action is fabricated here. */
    belt_upper_point(&layout, 1, 1, &x, &y);
    tap(b, 31, x, y, 8300000000);
    CHECK(dm_touch_bridge_take_action(b, &a));
    CHECK(a.kind == DM_ANDROID_ACTION_BELT);
    CHECK((int)a.x == (0x100 | 1 << 4 | 1));
    CHECK(!dm_touch_bridge_take_action(b, &a));   /* exactly once */
    belt_upper_point(&layout, 3, 2, &x, &y);      /* row 2, column 3 */
    tap(b, 32, x, y, 8310000000);
    CHECK(dm_touch_bridge_take_action(b, &a));
    CHECK(a.kind == DM_ANDROID_ACTION_BELT && (int)a.x == (0x100 | 2 << 4 | 3));
    CHECK(!dm_touch_bridge_take_action(b, &a));
    /* Bottom-row tap is still a pad key pulse, not an action. */
    tap(b, 33, layout.target[DM_TC_BELT2].x, layout.target[DM_TC_BELT2].y, 8320000000);
    s = read_state(b); CHECK(s.buttons == DM_BTN_UP);
    s = read_state(b); CHECK(s.buttons == 0);
    CHECK(!dm_touch_bridge_take_action(b, &a));
    dm_touch_bridge_destroy(b, NULL);
}

static void belt_action_null_and_context_retention(void) {
    dm_touch_bridge *b = dm_touch_bridge_create(667, 375, 2.0f);
    dm_tc_layout layout;
    dm_android_touch_action a;
    float x, y;
    CHECK(b != NULL);
    dm_tc_layout_make(&layout, 667, 375);
    CHECK(dm_touch_bridge_set_belt_context(b, 4, 1));
    belt_upper_point(&layout, 1, 1, &x, &y);
    tap(b, 41, x, y, 8400000000);
    (void)read_state(b);                           /* pad reads do not consume actions */
    CHECK(!dm_touch_bridge_take_action(b, NULL));  /* null output never consumes */
    CHECK(dm_touch_bridge_take_action(b, &a));
    CHECK(a.kind == DM_ANDROID_ACTION_BELT && (int)a.x == (0x100 | 1 << 4 | 1));
    CHECK(!dm_touch_bridge_take_action(b, &a));
    /* Same-value context update preserves a pending request. */
    tap(b, 42, x, y, 8410000000);
    CHECK(dm_touch_bridge_set_belt_context(b, 4, 1));
    CHECK(dm_touch_bridge_take_action(b, &a));
    CHECK((int)a.x == (0x100 | 1 << 4 | 1));
    /* Invalid updates change neither context nor request. */
    tap(b, 43, x, y, 8420000000);
    CHECK(!dm_touch_bridge_set_belt_context(b, 5, 0));
    CHECK(!dm_touch_bridge_set_belt_context(b, 4, 2));
    CHECK(dm_touch_bridge_take_action(b, &a));
    CHECK((int)a.x == (0x100 | 1 << 4 | 1));
    /* A real context change cancels gestures but the adapter's cancel-all
       retains belt_request: the already-requested cell stays one-shot. */
    tap(b, 44, x, y, 8430000000);
    CHECK(dm_touch_bridge_set_belt_context(b, 4, 0));
    CHECK(dm_touch_bridge_take_action(b, &a));
    CHECK((int)a.x == (0x100 | 1 << 4 | 1));
    /* Reopen, then cancel-all and resize both retain the pending request. */
    CHECK(dm_touch_bridge_set_belt_context(b, 4, 1));
    tap(b, 45, x, y, 8440000000);
    CHECK(dm_touch_bridge_cancel_all(b, 8441000000));
    CHECK(dm_touch_bridge_take_action(b, &a));
    CHECK((int)a.x == (0x100 | 1 << 4 | 1));
    tap(b, 46, x, y, 8450000000);
    CHECK(dm_touch_bridge_resize(b, 812, 375, 8451000000));
    CHECK(dm_touch_bridge_take_action(b, &a));
    CHECK((int)a.x == (0x100 | 1 << 4 | 1));
    CHECK(!dm_touch_bridge_take_action(b, &a));
    dm_touch_bridge_destroy(b, NULL);
}

typedef struct {
    uint64_t now;
    uint64_t scene_epoch;
    uint64_t world_epoch;
    uint64_t object_generation;
    uint64_t generation;
    uint64_t ui_identity;
    uint32_t module_id;
    int scene;
    int available;
    int action_result;
    int fail_on_call;
    int snap_calls;
    int actions;
    int acked;
    int rejected_action;
    int last_kind;
    float last_x;
    float last_y;
    uint64_t last_scene;
    uint64_t last_generation;
} provider_fixture;

static int provider_snap(void *context, dm_tgp_snapshot *out) {
    provider_fixture *f = context;
    memset(out, 0, sizeof(*out));
    f->snap_calls++;
    if (f->fail_on_call > 0 && f->snap_calls == f->fail_on_call) return 0;
    out->abi_version = DM_TGP_ABI_VERSION;
    out->module_id = f->module_id;
    out->generation = f->generation;
    out->scene_epoch = f->scene_epoch;
    out->world_epoch = f->world_epoch;
    out->object_generation = f->object_generation;
    out->captured_ns = f->now;
    out->ui_identity = f->ui_identity;
    out->available = f->available;
    out->scene = f->scene;
    out->dialogue_only = f->scene == DM_TGP_SCENE_DIALOG;
    out->dead = f->scene == DM_TGP_SCENE_DEAD;
    return 1;
}

static int provider_hit(void *context, float x, float y, dm_tgp_hit *out) {
    provider_fixture *f = context;
    (void)x; (void)y;
    memset(out, 0, sizeof(*out));
    out->valid = 1;
    out->kind = DM_TGP_HIT_NONE;
    out->object_generation = f->object_generation;
    out->scene_epoch = f->scene_epoch;
    return 1;
}

static int provider_action(void *context, const dm_tgp_action *action, uint64_t now_ns) {
    provider_fixture *f = context;
    (void)now_ns;
    f->actions++;
    f->last_kind = action->kind;
    f->last_x = action->x;
    f->last_y = action->y;
    f->last_scene = action->scene_epoch;
    f->last_generation = action->generation;
    if (!f->action_result) { f->rejected_action++; return 0; }
    f->acked++;
    return 1;
}

static dm_tgp_provider provider_from(provider_fixture *f) {
    dm_tgp_provider p;
    memset(&p, 0, sizeof(p));
    p.abi_version = DM_TGP_ABI_VERSION;
    p.module_id = f->module_id;
    p.generation = f->generation;
    p.module_base = 0x10000;
    p.module_size = 0x1000;
    p.object_range_base = 0x20000;
    p.object_range_size = 0x1000;
    p.context = f;
    p.snapshot = provider_snap;
    p.hit = provider_hit;
    p.action = provider_action;
    return p;
}

static void provider_reset(provider_fixture *f) {
    memset(f, 0, sizeof(*f));
    f->scene_epoch = 1;
    f->world_epoch = 1;
    f->object_generation = 10;
    f->generation = 10;
    f->ui_identity = 5;
    f->module_id = 7;
    f->scene = DM_TGP_SCENE_GAME;
    f->available = 1;
    f->action_result = 1;
}

static void provider_belt_action_exactly_once(void) {
    provider_fixture f;
    provider_reset(&f);
    dm_tgp_provider p = provider_from(&f);
    dm_touch_bridge *b = dm_touch_bridge_create(667, 375, 2.0f);
    dm_tc_layout layout;
    dm_android_touch_action a;
    dm_npad_state s;
    float x, y;
    CHECK(b != NULL);
    CHECK(dm_touch_bridge_attach_game(b, &p));
    CHECK(dm_touch_bridge_set_belt_context(b, 4, 1));
    dm_tc_layout_make(&layout, 667, 375);
    belt_upper_point(&layout, 1, 1, &x, &y);
    f.now = 9000000000;
    down(b, 51, x, y, 9000000000);
    /* Actual outer event -> core belt request -> attached provider callback,
       exactly once, with the player expanded-cell encoding and scene tag. */
    CHECK(f.actions == 1 && f.acked == 1 && f.rejected_action == 0);
    CHECK(f.last_kind == DM_TGP_ACTION_BELT && (int)f.last_x == (0x100 | 1 << 4 | 1));
    CHECK(f.last_scene == 1 && f.last_generation == 10);
    /* The standalone outlet defers to the attached provider; it cannot steal. */
    CHECK(!dm_touch_bridge_take_action(b, &a));
    f.now = 9001000000;
    CHECK(dm_touch_bridge_tick(b, 9001000000));
    CHECK(f.actions == 1 && f.acked == 1);   /* no duplicate dispatch */
    up(b, 51, x, y, 9002000000);
    /* Bottom row remains a pad pulse and never becomes a belt action. */
    f.now = 9003000000;
    tap(b, 52, layout.target[DM_TC_BELT2].x, layout.target[DM_TC_BELT2].y, 9003000000);
    s = read_state(b); CHECK(s.buttons == DM_BTN_UP);
    CHECK(f.actions == 1);
    dm_touch_bridge_destroy(b, NULL);
}

static void provider_action_reject_not_acked(void) {
    provider_fixture f;
    provider_reset(&f);
    f.action_result = 0;
    dm_tgp_provider p = provider_from(&f);
    dm_touch_bridge *b = dm_touch_bridge_create(667, 375, 2.0f);
    dm_tc_layout layout;
    float x, y;
    CHECK(b != NULL);
    CHECK(dm_touch_bridge_attach_game(b, &p));
    CHECK(dm_touch_bridge_set_belt_context(b, 4, 1));
    dm_tc_layout_make(&layout, 667, 375);
    belt_upper_point(&layout, 1, 1, &x, &y);
    f.now = 9020000000;
    down(b, 53, x, y, 9020000000);
    CHECK(f.actions == 1 && f.acked == 0 && f.rejected_action == 1);
    f.now = 9021000000;
    CHECK(dm_touch_bridge_tick(b, 9021000000));
    CHECK(f.actions == 1);   /* a rejected action is never retried or acked */
    dm_touch_bridge_destroy(b, NULL);
}

static void provider_stale_action_not_retagged(void) {
    provider_fixture f;
    provider_reset(&f);
    dm_tgp_provider p = provider_from(&f);
    dm_touch_bridge *b = dm_touch_bridge_create(667, 375, 2.0f);
    dm_tc_layout layout;
    dm_touch_event e;
    float x, y;
    CHECK(b != NULL);
    CHECK(dm_touch_bridge_attach_game(b, &p));
    CHECK(dm_touch_bridge_set_belt_context(b, 4, 1));
    dm_tc_layout_make(&layout, 667, 375);
    belt_upper_point(&layout, 1, 1, &x, &y);
    /* The belt request is produced while the tick's snapshot refresh fails,
       so it stays pending in the adapter. */
    f.now = 9100000000;
    f.fail_on_call = 3;
    down(b, 55, x, y, 9100000000);
    CHECK(f.actions == 0 && f.acked == 0);
    /* A later scene must not receive that old action retagged with its epoch. */
    f.fail_on_call = 0;
    f.scene_epoch = 2;
    f.object_generation = 11;
    f.now = 9101000000;
    CHECK(dm_touch_bridge_tick(b, 9101000000));
    CHECK(f.actions == 0 && f.acked == 0);   /* pre-fix: one call tagged scene 2 */
    /* Unavailable and identity-mismatched snapshots fail closed before input. */
    f.available = 0;
    f.now = 9110000000;
    e = event(DM_TOUCH_ACTION_DOWN, 0, 9110000000);
    point(&e, 56, x, y, 2.0f);
    CHECK(!dm_touch_bridge_event(b, &e));
    f.available = 1;
    f.module_id = 8;   /* snapshot identity no longer matches the provider */
    f.now = 9111000000;
    e = event(DM_TOUCH_ACTION_DOWN, 0, 9111000000);
    point(&e, 57, x, y, 2.0f);
    CHECK(!dm_touch_bridge_event(b, &e));
    /* Stale snapshot (older than DM_TGP_MAX_AGE_NS) also fails closed. */
    f.module_id = 7;
    f.now = 8900000000;   /* 300 ms older than the event: beyond DM_TGP_MAX_AGE_NS */
    e = event(DM_TOUCH_ACTION_DOWN, 0, 9200000000);
    point(&e, 58, x, y, 2.0f);
    CHECK(!dm_touch_bridge_event(b, &e));
    CHECK(f.actions == 0);
    dm_touch_bridge_destroy(b, NULL);
}

static void portal_bridge_request_ack_and_release(void) {
    dm_touch_bridge *b = dm_touch_bridge_create(667, 375, 2.0f);
    dm_tc_layout layout;
    dm_android_portal_request r;
    dm_touch_event e;
    dm_npad_state s;
    uint64_t mask = DM_BTN_ZL | DM_BTN_B;
    CHECK(b != NULL);
    dm_tc_layout_make(&layout, 667, 375);
    float px = layout.target[DM_TC_PORTAL].x, py = layout.target[DM_TC_PORTAL].y;
    CHECK(dm_touch_bridge_set_portal_context(b, 1, mask, 77, 3));
    down(b, 90, px, py, 20000000000);
    s = read_state(b); CHECK(s.buttons == DM_BTN_ZL);
    CHECK(dm_touch_bridge_take_portal_request(b, &r));
    CHECK(r.binding_identity == 77 && r.scene_epoch == 3 && r.mask == mask && r.phase == 1);
    uint64_t epoch = r.gesture_epoch;
    CHECK(!dm_touch_bridge_take_portal_request(b, &r));
    CHECK(!dm_touch_bridge_take_portal_request(b, NULL));
    /* Unrelated joystick + skill stay owned through the portal arm. */
    down(b, 91, 90, 275, 20010000000);
    e = event(DM_TOUCH_ACTION_MOVE, 0, 20020000000);
    point(&e, 91, 150, 275, 2.0f);
    CHECK(dm_touch_bridge_event(b, &e));
    down(b, 92, layout.target[DM_TC_SKILL2].x, layout.target[DM_TC_SKILL2].y, 20030000000);
    CHECK(dm_touch_bridge_tick(b, 20120000000));
    s = read_state(b); CHECK(s.buttons == (DM_BTN_ZL | DM_BTN_X) && s.lx == DM_STICK_MAX);
    CHECK(!dm_touch_bridge_portal_ack(b, epoch + 1, 77, 3));
    CHECK(!dm_touch_bridge_portal_ack(b, epoch, 78, 3));
    CHECK(!dm_touch_bridge_portal_ack(b, epoch, 77, 4));
    s = read_state(b); CHECK(s.buttons == (DM_BTN_ZL | DM_BTN_X) && s.lx == DM_STICK_MAX);
    CHECK(dm_touch_bridge_portal_ack(b, epoch, 77, 3));
    s = read_state(b); CHECK(s.buttons == (mask | DM_BTN_X) && s.lx == DM_STICK_MAX);
    /* Identity change cancels the portal owner only; outer table must follow. */
    CHECK(dm_touch_bridge_set_portal_context(b, 1, mask, 78, 3));
    s = read_state(b); CHECK(s.buttons == DM_BTN_X && s.lx == DM_STICK_MAX);
    e = event(DM_TOUCH_ACTION_MOVE, 0, 20130000000);
    point(&e, 90, px, py, 2.0f);
    CHECK(!dm_touch_bridge_event(b, &e));           /* stale portal MOVE rejected */
    e = event(DM_TOUCH_ACTION_UP, 0, 20140000000);
    point(&e, 90, px, py, 2.0f);
    CHECK(!dm_touch_bridge_event(b, &e));           /* stale portal UP rejected */
    /* Same ID is free again and arms against the new identity. */
    down(b, 90, px, py, 20150000000);
    s = read_state(b); CHECK(s.buttons == (DM_BTN_ZL | DM_BTN_X) && s.lx == DM_STICK_MAX);
    CHECK(dm_touch_bridge_take_portal_request(b, &r));
    CHECK(r.binding_identity == 78);
    up(b, 90, px, py, 20160000000);                 /* last portal lift cancels */
    s = read_state(b); CHECK(s.buttons == DM_BTN_X && s.lx == DM_STICK_MAX);
    CHECK(!dm_touch_bridge_take_portal_request(b, &r));
    CHECK(!dm_touch_bridge_portal_ack(b, r.gesture_epoch, 78, 3));
    up(b, 92, layout.target[DM_TC_SKILL2].x, layout.target[DM_TC_SKILL2].y, 20170000000);
    up(b, 91, 150, 275, 20180000000);
    s = read_state(b); CHECK(s.buttons == 0 && s.lx == 0 && s.ly == 0);
    dm_touch_bridge_destroy(b, NULL);
}

static void portal_bridge_invalid_same_repeat_provider(void) {
    dm_touch_bridge *b = dm_touch_bridge_create(667, 375, 2.0f);
    dm_tc_layout layout;
    dm_android_portal_request r;
    dm_touch_event e;
    dm_npad_state s;
    uint64_t mask = DM_BTN_ZL | DM_BTN_B;
    CHECK(b != NULL);
    dm_tc_layout_make(&layout, 667, 375);
    float px = layout.target[DM_TC_PORTAL].x, py = layout.target[DM_TC_PORTAL].y;
    /* Default unavailable: no key, no request. */
    down(b, 95, px, py, 21000000000);
    s = read_state(b); CHECK(s.buttons == 0);
    CHECK(!dm_touch_bridge_take_portal_request(b, &r));
    CHECK(!dm_touch_bridge_portal_ack(b, 0, 0, 0));
    up(b, 95, px, py, 21001000000);
    CHECK(dm_touch_bridge_set_portal_context(b, 1, mask, 77, 3));
    down(b, 96, px, py, 21010000000);
    s = read_state(b); CHECK(s.buttons == DM_BTN_ZL);
    /* Invalid updates and same values leave ownership/request intact. */
    CHECK(!dm_touch_bridge_set_portal_context(NULL, 1, mask, 77, 3));
    CHECK(!dm_touch_bridge_set_portal_context(b, 2, mask, 77, 3));
    CHECK(!dm_touch_bridge_set_portal_context(b, 1, 0, 77, 3));
    CHECK(dm_touch_bridge_set_portal_context(b, 1, mask, 77, 3));
    CHECK(dm_touch_bridge_take_portal_request(b, &r));
    e = event(DM_TOUCH_ACTION_MOVE, 0, 21011000000);
    point(&e, 96, px, py, 2.0f);
    CHECK(dm_touch_bridge_event(b, &e));            /* owner still held */
    up(b, 96, px, py, 21012000000);
    s = read_state(b); CHECK(s.buttons == 0);
    CHECK(!dm_touch_bridge_take_portal_request(b, &r));
    CHECK(!dm_touch_bridge_portal_ack(b, r.gesture_epoch, 77, 3));
    /* Repeated real changes beyond the 16-slot table must not leak. */
    for (int i = 0; i < 20; ++i) {
        CHECK(dm_touch_bridge_set_portal_context(b, 1, mask, 1000 + (uint64_t)i, 3));
        down(b, 300 + i, px, py, 21100000000 + (int64_t)i * 20000000);
    }
    CHECK(dm_touch_bridge_set_portal_context(b, 1, mask, 2000, 3));
    e = event(DM_TOUCH_ACTION_MOVE, 0, 21200000000);
    point(&e, 319, px, py, 2.0f);
    CHECK(!dm_touch_bridge_event(b, &e));           /* canceled portal ID released */
    down(b, 400, px, py, 21210000000);
    CHECK(dm_touch_bridge_take_portal_request(b, &r));
    CHECK(r.binding_identity == 2000);
    up(b, 400, px, py, 21220000000);
    /* Attached provider: portal surface is explicitly unavailable. */
    provider_fixture f;
    provider_reset(&f);
    dm_tgp_provider p = provider_from(&f);
    CHECK(dm_touch_bridge_attach_game(b, &p));
    CHECK(!dm_touch_bridge_set_portal_context(b, 1, mask, 3000, 3));
    CHECK(!dm_touch_bridge_take_portal_request(b, &r));
    CHECK(!dm_touch_bridge_portal_ack(b, 0, 3000, 3));
    dm_touch_bridge_destroy(b, NULL);
}

static void portal_provider_attach_disables_latent_binding(void) {
    dm_touch_bridge *b = dm_touch_bridge_create(667, 375, 2.0f);
    dm_tc_layout layout;
    dm_android_portal_request r;
    dm_npad_state s;
    provider_fixture f1, f2;
    uint64_t mask = DM_BTN_ZL | DM_BTN_B;
    CHECK(b != NULL);
    dm_tc_layout_make(&layout, 667, 375);
    float px = layout.target[DM_TC_PORTAL].x, py = layout.target[DM_TC_PORTAL].y;
    /* Pending (unacked) arm before attach must not survive into provider input. */
    CHECK(dm_touch_bridge_set_portal_context(b, 1, mask, 7, 1));
    down(b, 50, px, py, 22000000000);
    s = read_state(b); CHECK(s.buttons == DM_BTN_ZL);
    provider_reset(&f1);
    f1.now = 22010000000;
    dm_tgp_provider p1 = provider_from(&f1);
    CHECK(dm_touch_bridge_attach_game(b, &p1));
    down(b, 51, px, py, 22010000000);
    s = read_state(b); CHECK(!(s.buttons & (DM_BTN_ZL | DM_BTN_B)));
    CHECK(!dm_touch_bridge_take_portal_request(b, &r));
    CHECK(!dm_touch_bridge_portal_ack(b, 1, 7, 1));
    dm_touch_bridge_destroy(b, NULL);

    /* Armed/acked before attach, then replacement provider: still unavailable. */
    b = dm_touch_bridge_create(667, 375, 2.0f);
    CHECK(dm_touch_bridge_set_portal_context(b, 1, mask, 9, 2));
    down(b, 60, px, py, 22100000000);
    CHECK(dm_touch_bridge_take_portal_request(b, &r));
    uint64_t epoch = r.gesture_epoch;
    CHECK(dm_touch_bridge_portal_ack(b, epoch, 9, 2));
    s = read_state(b); CHECK(s.buttons == mask);
    provider_reset(&f1);
    f1.now = 22110000000;
    p1 = provider_from(&f1);
    CHECK(dm_touch_bridge_attach_game(b, &p1));
    down(b, 61, px, py, 22110000000);
    s = read_state(b); CHECK(!(s.buttons & (DM_BTN_ZL | DM_BTN_B)));
    CHECK(!dm_touch_bridge_take_portal_request(b, &r));
    CHECK(!dm_touch_bridge_portal_ack(b, epoch, 9, 2));
    provider_reset(&f2);
    f2.now = 22120000000;
    dm_tgp_provider p2 = provider_from(&f2);
    CHECK(dm_touch_bridge_attach_game(b, &p2));   /* replacement */
    down(b, 62, px, py, 22120000000);
    s = read_state(b); CHECK(!(s.buttons & (DM_BTN_ZL | DM_BTN_B)));
    CHECK(!dm_touch_bridge_take_portal_request(b, &r));
    dm_touch_bridge_destroy(b, NULL);
}

int main(void) {
    CASE(density_and_noncontiguous_ids);
    CASE(action_index_multifinger);
    CASE(resize_cancels_and_reuses_id);
    CASE(density_change_cancels_held_gesture);
    CASE(cancel_without_points);
    CASE(malformed_event_sets);
    CASE(invalid_events);
    CASE(timer_without_touch);
    CASE(ring_full_up_retry);
    CASE(cancel_clears_queued_pulses);
    CASE(resize_density_and_neutral_destroy);
    CASE(belt_context_change_cancels_outer_and_reuses_id);
    CASE(belt_context_invalid_and_same_preserve_ownership);
    CASE(belt_context_survives_cancel_and_resize);
    CASE(belt_action_expanded_cell_one_shot);
    CASE(belt_action_null_and_context_retention);
    CASE(portal_bridge_request_ack_and_release);
    CASE(portal_bridge_invalid_same_repeat_provider);
    CASE(portal_provider_attach_disables_latent_binding);
    CASE(provider_belt_action_exactly_once);
    CASE(provider_action_reject_not_acked);
    CASE(provider_stale_action_not_retagged);
    printf("PASS touch bridge: %d checks\n", checks);
    return 0;
}
