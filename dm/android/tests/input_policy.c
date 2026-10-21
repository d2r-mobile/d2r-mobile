#include "../app/input_policy.h"
#include "../app/touch_bridge.h"
#include "dm_input.h"
#include "dm_touch_controls.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks;
#define CHECK(expr) do { ++checks; if (!(expr)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); } } while (0)

static dm_touch_event main_down(int64_t time_ns) {
    dm_tc_layout layout;
    dm_touch_event event;
    dm_tc_layout_make(&layout, 480, 300);
    memset(&event, 0, sizeof(event));
    event.action = DM_TOUCH_ACTION_DOWN;
    event.action_index = 0;
    event.pointer_count = 1;
    event.time_ns = time_ns;
    event.points[0].id = 7;
    event.points[0].x_px = layout.target[DM_TC_MAIN].x * 3.0f;
    event.points[0].y_px = layout.target[DM_TC_MAIN].y * 3.0f;
    return event;
}

int main(void) {
    dm_input_policy gate;
    dm_input_policy_init(&gate);
    CHECK(!gate.resumed && !gate.focused);
    CHECK(!dm_input_policy_ready(&gate));
    CHECK(!dm_input_policy_allows(&gate));
    dm_input_policy_set_resumed(&gate, 1);
    dm_input_policy_set_focused(&gate, 1);
    dm_input_policy_set_queue(&gate, 1);
    dm_input_policy_set_window(&gate, 1);
    dm_input_policy_set_touch(&gate, 1);
    CHECK(dm_input_policy_ready(&gate));
    /* Timer startup is part of acceptance: readiness without a clock is not. */
    CHECK(!dm_input_policy_allows(&gate));
    dm_input_policy_set_timer(&gate, 1);
    CHECK(dm_input_policy_allows(&gate));

    dm_touch_bridge *bridge = dm_touch_bridge_create(480, 300, 3.0f);
    CHECK(bridge != NULL);
    dm_touch_event down = main_down(1000000000);
    CHECK(dm_touch_bridge_event(bridge, &down));
    CHECK(dm_touch_bridge_read(bridge).buttons == DM_BTN_A);

    /* A paused activity must reject queued DOWN/MOVE without resurrecting. */
    dm_input_policy_set_resumed(&gate, 0);
    dm_input_policy_set_timer(&gate, 0);
    CHECK(!dm_input_policy_allows(&gate));
    CHECK(dm_touch_bridge_cancel_all(bridge, 1000000001));
    CHECK(dm_touch_bridge_read(bridge).buttons == 0);
    if (dm_input_policy_allows(&gate)) CHECK(dm_touch_bridge_event(bridge, &down));
    CHECK(dm_touch_bridge_snapshot(bridge).buttons == 0);

    /* Resume requires a deliberately restarted clock before a fresh DOWN. */
    dm_input_policy_set_resumed(&gate, 1);
    CHECK(dm_input_policy_ready(&gate));
    CHECK(!dm_input_policy_allows(&gate));
    dm_input_policy_set_timer(&gate, 1);
    CHECK(dm_input_policy_allows(&gate));
    dm_touch_event fresh = main_down(2000000000);
    fresh.points[0].id = 9;
    CHECK(dm_touch_bridge_event(bridge, &fresh));
    CHECK(dm_touch_bridge_read(bridge).buttons == DM_BTN_A);

    /* Fatal timer loss gates queued DOWN until a successful retry. */
    dm_input_policy_set_timer(&gate, 0);
    CHECK(!dm_input_policy_allows(&gate));
    CHECK(dm_touch_bridge_cancel_all(bridge, 2000000001));
    CHECK(dm_touch_bridge_read(bridge).buttons == 0);
    if (dm_input_policy_allows(&gate)) CHECK(dm_touch_bridge_event(bridge, &fresh));
    CHECK(dm_touch_bridge_snapshot(bridge).buttons == 0);
    dm_input_policy_set_timer(&gate, 1);
    CHECK(dm_input_policy_allows(&gate));
    fresh.time_ns = 3000000000;
    CHECK(dm_touch_bridge_event(bridge, &fresh));
    CHECK(dm_touch_bridge_read(bridge).buttons == DM_BTN_A);

    dm_input_policy_set_focused(&gate, 0);
    CHECK(!dm_input_policy_allows(&gate));
    dm_input_policy_set_focused(&gate, 1);
    dm_input_policy_set_queue(&gate, 0);
    CHECK(!dm_input_policy_allows(&gate));
    dm_input_policy_set_queue(&gate, 1);
    dm_input_policy_set_window(&gate, 0);
    CHECK(!dm_input_policy_allows(&gate));
    dm_input_policy_set_window(&gate, 1);
    dm_input_policy_set_touch(&gate, 0);
    CHECK(!dm_input_policy_allows(&gate));
    dm_input_policy_set_touch(&gate, 1);
    CHECK(dm_input_policy_allows(&gate));

    dm_touch_bridge_destroy(bridge, NULL);
    printf("PASS input policy: %d checks\n", checks);
    return 0;
}
