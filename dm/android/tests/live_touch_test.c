// #255 owned live-touch fixture: real core lifecycle through the Android
// pointer-id adapter, including menu tap, movement and cancellation.
#include "../native/live_touch.h"
#include "../../ios/Sources/dm_touch_controls.h"
#include "../../host/dm_input.h"
#include <stdio.h>
#include <stdint.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)
#define W 667.0f
#define H 375.0f

static int event(dm_live_touch *t, int action, int index, int count,
                 const int32_t *ids, const float *xs, const float *ys, int64_t ns) {
    return dm_live_touch_event(t, action, index, count, ids, xs, ys,
                              (int)W, (int)H, 0, 0, W, H, ns);
}

static int ui_cancel_count;
static void ui_cancel(void) { ++ui_cancel_count; }

int main(void) {
    dm_live_touch *t = dm_live_touch_create(W, H);
    CHECK(t != NULL);
    int32_t id = 42;
    float x = W * 0.5f, y = H * 0.5f;

    // Navigation/menu path: a tap goes through the original core's pointer
    // pulse (host press-edge latch publishes A), not a synthetic button.
    dm_live_touch_set_mode(t, 1, dm_tc_nav_mask(1));
    CHECK(event(t, DM_LIVE_ACTION_DOWN, 0, 1, &id, &x, &y, 1000000000) == 1);
    CHECK(dm_live_touch_has_pointer(t, id));
    CHECK(dm_live_touch_active(t) == 1);
    CHECK(event(t, DM_LIVE_ACTION_UP, 0, 1, &id, &x, &y, 1010000000) == 1);
    CHECK(!dm_live_touch_has_pointer(t, id));
    CHECK((dm_live_touch_buttons(t) & DM_BTN_A) != 0);
    CHECK(dm_live_touch_end_released_pulse(t) == 0);
    dm_live_touch_scheduler_step(t, 1020000000, 10, 0);
    dm_live_touch_scheduler_step(t, 1030000000, 11, 0);
    dm_live_touch_scheduler_step(t, 1160000000, 14, 0);
    CHECK(dm_live_touch_buttons(t) == 0);

    // No further MotionEvent: the owner tick must advance the core's stationary
    // long-hold timer.
    CHECK(event(t, DM_LIVE_ACTION_DOWN, 0, 1, &id, &x, &y, 2000000000) == 1);
    CHECK(dm_live_touch_active(t) == 1);
    CHECK(dm_live_touch_pointer_long(t) == 0);
    dm_live_touch_tick(t, 3000000000);
    CHECK(dm_live_touch_pointer_long(t) == 1);
    CHECK(event(t, DM_LIVE_ACTION_UP, 0, 1, &id, &x, &y, 3010000000) == 1);
    CHECK(!dm_live_touch_has_pointer(t, id));

    // Game path: the original core's floating stick publishes a real axis.
    dm_live_touch_set_mode(t, 0, ((1u << DM_TC_COUNT) - 1) & ~(1u << DM_TC_MAP));
    x = 80; y = 300;
    CHECK(event(t, DM_LIVE_ACTION_DOWN, 0, 1, &id, &x, &y, 2000000000) == 1);
    x = 120; y = 300;
    CHECK(event(t, DM_LIVE_ACTION_MOVE, -1, 1, &id, &x, &y, 2010000000) == 1);
    CHECK(dm_live_touch_lx(t) > 0);
    CHECK(event(t, DM_LIVE_ACTION_CANCEL, 0, 1, &id, &x, &y, 2020000000) == 1);
    CHECK(!dm_live_touch_has_pointer(t, id));
    CHECK(dm_live_touch_lx(t) == 0 && dm_live_touch_ly(t) == 0);
    CHECK(dm_live_touch_buttons(t) == 0);

    // Actual pause/focus/input-destruction boundary: UI phase-3 cancellation
    // and core ownership are invoked together; touch-only cancel does not
    // publish a physical-controller mask.
    x = 100; y = 300;
    CHECK(event(t, DM_LIVE_ACTION_DOWN, 0, 1, &id, &x, &y, 2500000000) == 1);
    dm_live_touch_cancel_sources(t, 2510000000, ui_cancel);
    CHECK(ui_cancel_count == 1);
    CHECK(!dm_live_touch_active(t));
    CHECK(dm_live_touch_buttons(t) == 0);

    // Pointer identity is retained: the same numeric id is usable after a
    // complete up and cannot be confused with a slot index.
    x = 100; y = 300;
    CHECK(event(t, DM_LIVE_ACTION_DOWN, 0, 1, &id, &x, &y, 3000000000) == 1);
    CHECK(event(t, DM_LIVE_ACTION_UP, 0, 1, &id, &x, &y, 3010000000) == 1);
    CHECK(event(t, DM_LIVE_ACTION_DOWN, 0, 1, &id, &x, &y, 3020000000) == 1);
    CHECK(event(t, DM_LIVE_ACTION_UP, 0, 1, &id, &x, &y, 3030000000) == 1);

    dm_live_touch_destroy(t);
    puts("PASS: live touch menu A pulse, stationary tick/long-hold, movement axis and cancel reached the real core");
    return 0;
}
