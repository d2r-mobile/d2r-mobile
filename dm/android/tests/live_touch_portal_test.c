// #255 portal action fixture: real live_touch core plus a counting host
// request callback. No game files or host execution.
#include "../native/live_touch.c"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); return 1; } } while (0)

typedef struct {
    int calls;
    int released;
} portal_fixture;

static void on_portal(void *ctx) {
    portal_fixture *f = ctx;
    f->calls++;
}

static int event(dm_live_touch *t, int action, int id, float x, float y, int64_t at) {
    int32_t pointer = id;
    return dm_live_touch_event(t, action, 0, 1, &pointer, &x, &y,
                               667, 600, 0, 0, 667, 600, at);
}

int main(void) {
    dm_tc_layout layout;
    dm_tc_layout_make(&layout, 667, 600);
    dm_live_touch *t = dm_live_touch_create(667, 600);
    CHECK(t != NULL);
    dm_live_touch_set_context(t, 0, (1u << DM_TC_COUNT) - 1, 1, 1, 1, 0);

    portal_fixture calls;
    memset(&calls, 0, sizeof(calls));
    dm_live_touch_portal_actions actions = {&calls, on_portal};
    float px = layout.target[DM_TC_PORTAL].x;
    float py = layout.target[DM_TC_PORTAL].y;

    // A fresh valid portal-target down reaches the host once.
    CHECK(event(t, DM_LIVE_ACTION_DOWN, 10, px, py, 1000000000));
    CHECK(t->portal_request == 1);
    CHECK(dm_live_touch_consume_portal_request(t, 1, 1, 1, &actions));
    CHECK(calls.calls == 1);
    CHECK(!dm_live_touch_consume_portal_request(t, 1, 1, 1, &actions));
    CHECK(event(t, DM_LIVE_ACTION_MOVE, 10, px, py, 1001000000));
    CHECK(event(t, DM_LIVE_ACTION_UP, 10, px, py, 1002000000));
    (void)dm_live_touch_scheduler_step(t, 1003000000);
    CHECK(!dm_live_touch_consume_portal_request(t, 1, 1, 1, &actions));
    CHECK(calls.calls == 1);

    // Menu/panel/death navigation mode cannot request a portal.
    dm_live_touch_set_context(t, 1, dm_tc_nav_mask(1), 2, 2, 2, 0);
    (void)event(t, DM_LIVE_ACTION_DOWN, 11, px, py, 1010000000);
    CHECK(t->portal_request == 0);
    CHECK(!dm_live_touch_consume_portal_request(t, 2, 2, 2, &actions));
    (void)event(t, DM_LIVE_ACTION_UP, 11, px, py, 1011000000);
    CHECK(calls.calls == 1);

    // Already-produced request and the in-flight pointer do not survive context change.
    dm_live_touch_set_context(t, 0, (1u << DM_TC_COUNT) - 1, 3, 3, 3, 0);
    CHECK(event(t, DM_LIVE_ACTION_DOWN, 12, px, py, 1020000000));
    CHECK(t->portal_request == 1);
    dm_live_touch_set_context(t, 0, (1u << DM_TC_COUNT) - 1, 4, 4, 4, 0);
    CHECK(!dm_live_touch_consume_portal_request(t, 4, 4, 4, &actions));
    CHECK(!event(t, DM_LIVE_ACTION_UP, 12, px, py, 1021000000));
    CHECK(calls.calls == 1);

    // Focus/cancel clears the request.
    dm_live_touch_set_context(t, 0, (1u << DM_TC_COUNT) - 1, 5, 5, 5, 0);
    CHECK(event(t, DM_LIVE_ACTION_DOWN, 13, px, py, 1030000000));
    CHECK(t->portal_request == 1);
    CHECK(dm_live_touch_cancel_all(t, 1031000000));
    CHECK(!dm_live_touch_consume_portal_request(t, 5, 5, 5, &actions));
    CHECK(calls.calls == 1);

    // Unknown context cannot consume even an already-produced request.
    t->portal_request = 1;
    CHECK(!dm_live_touch_consume_portal_request(t, 5, 5, 5, &actions));
    CHECK(calls.calls == 1);

    // Recovery: a fresh valid portal down works again.
    dm_live_touch_set_context(t, 0, (1u << DM_TC_COUNT) - 1, 6, 6, 6, 0);
    CHECK(event(t, DM_LIVE_ACTION_DOWN, 14, px, py, 1040000000));
    CHECK(dm_live_touch_consume_portal_request(t, 6, 6, 6, &actions));
    CHECK(calls.calls == 2);
    CHECK(!dm_live_touch_consume_portal_request(t, 6, 6, 6, &actions));
    CHECK(event(t, DM_LIVE_ACTION_UP, 14, px, py, 1041000000));

    dm_live_touch_destroy(t);
    puts("PASS: actual core portal hit routes one host request, rejects navigation/context/cancel/unknown and recovers");
    return 0;
}
