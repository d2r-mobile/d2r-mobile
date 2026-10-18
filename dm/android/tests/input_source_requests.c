// #159 focused regression: actual arbiter invalidation must retire pending
// Android belt/portal requests, not just held pointers. Test build only accessor
// exposes the real bridge; all gesture/request production is the real chain.
#include "input_source.h"
#include "touch_bridge.h"
#include "dm_touch_controls.h"
#include "dm_input.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks;
#define CHECK(expr) do { ++checks; if (!(expr)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); } } while (0)

static dm_touch_event ev(dm_touch_action action, int32_t id, float x_pt, float y_pt,
                         int64_t t) {
    dm_touch_event e;
    memset(&e, 0, sizeof e);
    e.action = action;
    e.action_index = 0;
    e.pointer_count = 1;
    e.time_ns = t;
    e.points[0].id = id;
    e.points[0].x_px = x_pt * 2.0f;
    e.points[0].y_px = y_pt * 2.0f;
    return e;
}

static void upper_point(const dm_tc_layout *l, int col, int row, float *x, float *y) {
    const dm_tc_target *t = &l->target[DM_TC_BELT1 + col];
    float u = l->height / 1320.0f;
    *x = t->x;
    *y = t->y - (165.0f + 162.0f * (row - 1)) * u;
}

static int send(dm_input_arbiter *a, dm_touch_action action, int32_t id,
                float x, float y, int64_t t) {
    dm_touch_event e = ev(action, id, x, y, t);
    return dm_input_arbiter_touch(a, &e);
}

static void retired_belt_then_new_gesture(dm_input_arbiter *a, dm_touch_bridge *b,
                                          const dm_tc_layout *l) {
    dm_android_touch_action action;
    float x, y;
    upper_point(l, 1, 1, &x, &y);
    CHECK(dm_touch_bridge_set_belt_context(b, 4, 1));
    CHECK(send(a, DM_TOUCH_ACTION_DOWN, 101, x, y, 1000000000));
    CHECK(dm_touch_bridge_take_action(b, &action));
    CHECK(action.kind == DM_ANDROID_ACTION_BELT && (int)action.x == (0x100 | 1 << 4 | 1));
    CHECK(send(a, DM_TOUCH_ACTION_UP, 101, x, y, 1010000000));
    /* Leave a second request unconsumed; invalidation must retire it. */
    CHECK(send(a, DM_TOUCH_ACTION_DOWN, 102, x, y, 1020000000));
    CHECK(dm_input_arbiter_queue_changed(a));
    CHECK(dm_input_arbiter_source(a) == DM_INPUT_SOURCE_NONE);
    CHECK(!dm_touch_bridge_take_action(b, &action));
    /* Late MOVE from the retired pointer cannot resurrect the gesture. */
    CHECK(!send(a, DM_TOUCH_ACTION_MOVE, 102, x, y, 1025000000));
    /* Host context is preserved; a new legitimate gesture works under it. */
    CHECK(dm_touch_bridge_set_belt_context(b, 4, 1));
    CHECK(send(a, DM_TOUCH_ACTION_DOWN, 103, x, y, 1030000000));
    CHECK(dm_touch_bridge_take_action(b, &action));
    CHECK(action.kind == DM_ANDROID_ACTION_BELT && (int)action.x == (0x100 | 1 << 4 | 1));
    CHECK(send(a, DM_TOUCH_ACTION_UP, 103, x, y, 1040000000));
}

static void retired_portal_then_new_gesture(dm_input_arbiter *a, dm_touch_bridge *b,
                                            const dm_tc_layout *l) {
    dm_android_portal_request r;
    uint64_t old_epoch;
    float px = l->target[DM_TC_PORTAL].x, py = l->target[DM_TC_PORTAL].y;
    /* Leave the expanded belt rows, which overlap the portal hit area, before
       exercising the portal request path. */
    CHECK(dm_touch_bridge_set_belt_context(b, 0, 0));
    CHECK(dm_touch_bridge_set_portal_context(b, 1, DM_BTN_ZL | DM_BTN_B, 77, 3));
    CHECK(send(a, DM_TOUCH_ACTION_DOWN, 201, px, py, 2000000000));
    CHECK(dm_touch_bridge_take_portal_request(b, &r));
    old_epoch = r.gesture_epoch;
    CHECK(old_epoch != 0);
    /* Retire the still-armed core portal state and its unread request path. */
    CHECK(dm_input_arbiter_queue_changed(a));
    CHECK(!dm_touch_bridge_take_portal_request(b, &r));
    CHECK(!dm_touch_bridge_portal_ack(b, old_epoch, 77, 3));
    /* A late event from the retired portal pointer is rejected. */
    CHECK(!send(a, DM_TOUCH_ACTION_MOVE, 201, px, py, 2005000000));
    /* Context survives; a fresh portal gesture arms a new epoch. */
    CHECK(dm_touch_bridge_set_portal_context(b, 1, DM_BTN_ZL | DM_BTN_B, 77, 3));
    CHECK(send(a, DM_TOUCH_ACTION_DOWN, 202, px, py, 2010000000));
    CHECK(dm_touch_bridge_take_portal_request(b, &r));
    CHECK(r.gesture_epoch != old_epoch && r.binding_identity == 77 && r.scene_epoch == 3);
    CHECK(send(a, DM_TOUCH_ACTION_UP, 202, px, py, 2020000000));
}

int main(void) {
    dm_input_arbiter *a = dm_input_arbiter_create(667, 375, 2.0f);
    CHECK(a != NULL);
    dm_touch_bridge *b = dm_input_arbiter_test_touch(a);
    CHECK(b != NULL);
    dm_tc_layout layout;
    dm_tc_layout_make(&layout, 667, 375);
    retired_belt_then_new_gesture(a, b, &layout);
    retired_portal_then_new_gesture(a, b, &layout);
    /* Idempotent invalidation: repeated calls do not recreate stale requests. */
    CHECK(dm_input_arbiter_queue_changed(a));
    CHECK(dm_input_arbiter_queue_changed(a));
    CHECK(dm_input_arbiter_source(a) == DM_INPUT_SOURCE_NONE);
    dm_input_arbiter_destroy(a, NULL);
    printf("PASS input source requests: %d checks\n", checks);
    return 0;
}
