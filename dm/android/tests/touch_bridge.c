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
    printf("PASS touch bridge: %d checks\n", checks);
    return 0;
}
