#include "../native/live_owner_timer.h"
#include <stdio.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

int main(void) {
    // Initial observation is allowed with no gesture work; 100ms bound.
    CHECK(dm_live_owner_interval_ms(0) == 100);
    // Active gesture/deferred work keeps the existing 16ms cadence.
    CHECK(dm_live_owner_interval_ms(1) == 16);
    CHECK(dm_live_owner_interval_ms(0) == 100); // after release, back to idle
    // Pause/input detach/window/live teardown cancels observation.
    CHECK(dm_live_owner_should_stop(0, 1, 1, 1, 1, 0));
    CHECK(dm_live_owner_should_stop(1, 0, 1, 1, 1, 0));
    CHECK(dm_live_owner_should_stop(1, 1, 0, 1, 1, 0));
    CHECK(dm_live_owner_should_stop(1, 1, 1, 0, 1, 0));
    CHECK(dm_live_owner_should_stop(1, 1, 1, 1, 0, 0));
    CHECK(dm_live_owner_should_stop(1, 1, 1, 1, 1, 1));
    CHECK(!dm_live_owner_should_stop(1, 1, 1, 1, 1, 0));
    // Actual start decision: callback order may be incomplete, and geometry
    // must be current; only the fully valid owner may create the timer.
    CHECK(!dm_live_owner_can_start(1, 1, 0, 1, 1, 0, 1)); // input first? not yet
    CHECK(!dm_live_owner_can_start(1, 1, 1, 0, 1, 0, 1)); // focus gain while paused
    CHECK(!dm_live_owner_can_start(1, 1, 1, 1, 0, 0, 1)); // no window
    CHECK(!dm_live_owner_can_start(1, 1, 1, 1, 1, 0, 0)); // no valid geometry
    CHECK(!dm_live_owner_can_start(1, 1, 1, 1, 1, 1, 1)); // destroyed
    CHECK(dm_live_owner_can_start(1, 1, 1, 1, 1, 0, 1));  // all callbacks arrived, once
    puts("PASS: idle owner observes at 100ms, active work stays 16ms, teardown stops");
    return 0;
}
