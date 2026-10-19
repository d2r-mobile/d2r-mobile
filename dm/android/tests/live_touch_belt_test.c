// #33 belt request adapter fixture: real live_touch core plus a counting host
// action consumer. No game files or host action execution.
#include "live_touch.h"
#include "../../ios/Sources/dm_touch_controls.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); return 1; } } while (0)

typedef struct {
    int expand_calls, use_calls;
    int last_on, last_col, last_row;
} actions_fixture;

static void on_expand(void *ctx, int on) {
    actions_fixture *f = ctx;
    f->expand_calls++;
    f->last_on = on;
}
static void on_use(void *ctx, int col, int row) {
    actions_fixture *f = ctx;
    f->use_calls++;
    f->last_col = col;
    f->last_row = row;
}

static int event(dm_live_touch *t, int action, int id, float x, float y, int64_t at) {
    int32_t pointer = id;
    return dm_live_touch_event(t, action, 0, 1, &pointer, &x, &y,
                               667, 600, 0, 0, 667, 600, at);
}

static void belt_xy(const dm_tc_layout *layout, float *x, float *y) {
    *x = layout->target[DM_TC_BELT1].x;
    *y = layout->target[DM_TC_BELT1].y;
}

static int swipe(dm_live_touch *t, float x, float y, float dy, int id, int64_t at) {
    if (!event(t, DM_LIVE_ACTION_DOWN, id, x, y, at)) return 0;
    if (!event(t, DM_LIVE_ACTION_MOVE, id, x, y + dy, at + 1000000)) return 0;
    if (!event(t, DM_LIVE_ACTION_UP, id, x, y + dy, at + 2000000)) return 0;
    return 1;
}

int main(void) {
    dm_tc_layout layout;
    dm_tc_layout_make(&layout, 667, 600);
    float bx, by;
    belt_xy(&layout, &bx, &by);

    dm_live_touch *t = dm_live_touch_create(667, 600);
    CHECK(t != NULL);
    dm_live_touch_set_context(t, 0, (1u << DM_TC_COUNT) - 1, 1, 1, 1, 0);
    CHECK(dm_live_touch_set_belt(t, 2, 0));

    actions_fixture calls;
    memset(&calls, 0, sizeof(calls));
    dm_live_touch_belt_actions actions = {&calls, on_expand, on_use};

    // Expand: actual core swipe on a real belt target.
    CHECK(swipe(t, bx, by, -40, 11, 1000000000));
    CHECK(dm_live_touch_consume_belt_request(t, 1, 1, 1, &actions));
    CHECK(calls.expand_calls == 1 && calls.last_on == 1 && calls.use_calls == 0);
    CHECK(!dm_live_touch_consume_belt_request(t, 1, 1, 1, &actions)); // one-shot

    // Collapse: same real core, now expanded.
    CHECK(dm_live_touch_set_belt(t, 2, 1));
    CHECK(swipe(t, bx, by, 40, 12, 1100000000));
    CHECK(dm_live_touch_consume_belt_request(t, 1, 1, 1, &actions));
    CHECK(calls.expand_calls == 2 && calls.last_on == 0);

    // Upper row tap: actual core hit-test point, row 1 column 0.
    CHECK(dm_live_touch_set_belt(t, 2, 1));
    float u = layout.height / 1320.0f;
    float ux = layout.target[DM_TC_BELT1].x;
    float uy = layout.target[DM_TC_BELT1].y - 165.0f * u;
    CHECK(event(t, DM_LIVE_ACTION_DOWN, 13, ux, uy, 1200000000));
    CHECK(dm_live_touch_consume_belt_request(t, 1, 1, 1, &actions));
    CHECK(calls.use_calls == 1 && calls.last_col == 0 && calls.last_row == 1);
    CHECK(!dm_live_touch_consume_belt_request(t, 1, 1, 1, &actions));
    (void)event(t, DM_LIVE_ACTION_UP, 13, ux, uy, 1201000000);

    // New request with changed context must not execute.
    CHECK(swipe(t, bx, by, -40, 14, 1300000000));
    dm_live_touch_set_context(t, 0, (1u << DM_TC_COUNT) - 1, 2, 2, 2, 0);
    CHECK(!dm_live_touch_consume_belt_request(t, 2, 2, 2, &actions));
    CHECK(calls.expand_calls == 2 && calls.use_calls == 1);

    // Focus/cancel clears a request that was already produced.
    dm_live_touch_set_context(t, 0, (1u << DM_TC_COUNT) - 1, 3, 3, 3, 0);
    CHECK(dm_live_touch_set_belt(t, 2, 0));
    CHECK(swipe(t, bx, by, -40, 15, 1400000000));
    CHECK(dm_live_touch_cancel_all(t, 1500000000));
    CHECK(!dm_live_touch_consume_belt_request(t, 3, 3, 3, &actions));
    CHECK(calls.expand_calls == 2 && calls.use_calls == 1);

    // Unknown host HUD clears a pending request instead of replaying it later.
    CHECK(dm_live_touch_set_belt(t, 2, 0));
    CHECK(swipe(t, bx, by, -40, 16, 1600000000));
    CHECK(dm_live_touch_set_belt(t, 0, 0));
    CHECK(dm_live_touch_set_belt(t, 2, 0));
    CHECK(!dm_live_touch_consume_belt_request(t, 3, 3, 3, &actions));
    CHECK(calls.expand_calls == 2 && calls.use_calls == 1);

    // Invalid host belt state is rejected and clears stale request.
    CHECK(swipe(t, bx, by, -40, 17, 1700000000));
    CHECK(!dm_live_touch_set_belt(t, -1, 0));
    CHECK(!dm_live_touch_consume_belt_request(t, 3, 3, 3, &actions));
    CHECK(calls.expand_calls == 2 && calls.use_calls == 1);
    CHECK(!dm_live_touch_set_belt(t, 0, 1));

    // Unknown before the gesture: a belt touch is not allowed to become a
    // controller belt pulse while the host HUD has no valid state.
    CHECK(dm_live_touch_set_belt(t, 0, 0));
    CHECK(!event(t, DM_LIVE_ACTION_DOWN, 25, bx, by, 1750000000));
    CHECK(!dm_live_touch_has_pointer(t, 25));
    CHECK(dm_live_touch_buttons(t) == 0);
    CHECK(dm_live_touch_set_belt(t, 2, 0));

    // Unknown during a held belt gesture: abort that gesture, not other fingers.
    CHECK(dm_live_touch_set_belt(t, 2, 0));
    float mx = layout.target[DM_TC_MAIN].x;
    float my = layout.target[DM_TC_MAIN].y;
    CHECK(event(t, DM_LIVE_ACTION_DOWN, 30, mx, my, 1790000000));
    CHECK(dm_live_touch_buttons(t) & layout.target[DM_TC_MAIN].button);
    CHECK(event(t, DM_LIVE_ACTION_DOWN, 20, bx, by, 1800000000));
    CHECK(event(t, DM_LIVE_ACTION_MOVE, 20, bx, by - 40, 1801000000));
    CHECK(dm_live_touch_set_belt(t, 0, 0));
    CHECK(dm_live_touch_set_belt(t, 2, 0));
    CHECK(!dm_live_touch_has_pointer(t, 20));
    CHECK(dm_live_touch_has_pointer(t, 30));
    CHECK(dm_live_touch_buttons(t) & layout.target[DM_TC_MAIN].button);
    CHECK(!event(t, DM_LIVE_ACTION_UP, 20, bx, by - 40, 1802000000));
    CHECK(dm_live_touch_buttons(t) & layout.target[DM_TC_MAIN].button);
    CHECK(event(t, DM_LIVE_ACTION_UP, 30, mx, my, 1803000000));
    CHECK(!dm_live_touch_consume_belt_request(t, 3, 3, 3, &actions));
    CHECK(calls.expand_calls == 2 && calls.use_calls == 1);
    CHECK(dm_live_touch_buttons(t) == 0);

    // Recovery: a fresh valid gesture is not quarantined.
    dm_live_touch_set_context(t, 0, (1u << DM_TC_COUNT) - 1, 3, 3, 3, 0);
    CHECK(swipe(t, bx, by, -40, 21, 1810000000));
    CHECK(dm_live_touch_consume_belt_request(t, 3, 3, 3, &actions));
    CHECK(calls.expand_calls == 3 && calls.last_on == 1);

    // Invalid during an upper-row held gesture: no use and no belt pulse.
    CHECK(dm_live_touch_set_belt(t, 2, 1));
    float ux2 = layout.target[DM_TC_BELT1].x;
    float uy2 = layout.target[DM_TC_BELT1].y - 165.0f * (layout.height / 1320.0f);
    CHECK(event(t, DM_LIVE_ACTION_DOWN, 22, ux2, uy2, 1820000000));
    CHECK(!dm_live_touch_set_belt(t, -1, 0));
    CHECK(dm_live_touch_set_belt(t, 2, 1));
    CHECK(!dm_live_touch_has_pointer(t, 22));
    CHECK(!event(t, DM_LIVE_ACTION_UP, 22, ux2, uy2, 1821000000));
    CHECK(!dm_live_touch_consume_belt_request(t, 3, 3, 3, &actions));
    CHECK(calls.expand_calls == 3 && calls.use_calls == 1);
    CHECK(dm_live_touch_buttons(t) == 0);

    // Fresh upper-row gesture after invalid recovery still reaches host use.
    CHECK(event(t, DM_LIVE_ACTION_DOWN, 23, ux2, uy2, 1830000000));
    CHECK(dm_live_touch_consume_belt_request(t, 3, 3, 3, &actions));
    CHECK(calls.use_calls == 2 && calls.last_col == 0 && calls.last_row == 1);
    (void)event(t, DM_LIVE_ACTION_UP, 23, ux2, uy2, 1831000000);

    dm_live_touch_destroy(t);
    puts("PASS: real core belt expand/collapse/upper-row requests consume once through the host action adapter and unknown/invalid held gestures are quarantined without replay");
    return 0;
}
