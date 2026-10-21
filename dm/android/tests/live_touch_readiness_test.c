#include "live_touch_readiness.h"

#include <stdio.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

int main(void) {
    dm_live_touch_readiness state = {0};
    CHECK(!dm_live_touch_readiness_update(&state, 0));   // startup false: no notification
    CHECK(dm_live_touch_readiness_update(&state, 1));    // first real true: notify once
    CHECK(state.changes == 1);
    CHECK(!dm_live_touch_readiness_update(&state, 1));   // duplicate true: no storm
    CHECK(dm_live_touch_readiness_update(&state, 0));    // provider loss: notify fallback
    CHECK(state.changes == 2);
    CHECK(!dm_live_touch_readiness_update(&state, 0));
    dm_live_touch_readiness_destroy(&state);
    CHECK(state.destroyed && !state.available);
    CHECK(!dm_live_touch_readiness_update(&state, 1));   // stale/destroyed instance cannot revive
    puts("PASS: live readiness notifies only actual transitions and stays destroyed");
    return 0;
}
