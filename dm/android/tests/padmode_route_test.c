#include "padmode_route.h"
#include <stdio.h>

static int failures;
#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #cond); failures++; } } while (0)

int main(void) {
    // Touch down asks for touch layout; move/up must not re-request it.
    CHECK(dm_padmode_route_event(1, 0, 0x00001002, 0, 0, 0) == DM_PADMODE_ROUTE_TOUCH);
    CHECK(dm_padmode_route_event(1, 0, 0x00001002, 5, 0, 0) == DM_PADMODE_ROUTE_TOUCH);
    CHECK(dm_padmode_route_event(1, 0, 0x00001002, 2, 0, 0) == DM_PADMODE_ROUTE_NONE);
    CHECK(dm_padmode_route_event(1, 0, 0x00001002, 1, 0, 0) == DM_PADMODE_ROUTE_NONE);

    // Only actual controller sources ask for the controller layout.
    CHECK(dm_padmode_route_event(0, 1, 0x00000401, 0, 1, 0) == DM_PADMODE_ROUTE_PAD);
    CHECK(dm_padmode_route_event(1, 0, 0x01000010, 2, 0, 1) == DM_PADMODE_ROUTE_PAD);
    CHECK(dm_padmode_route_event(1, 0, 0x01000010, 2, 0, 0) == DM_PADMODE_ROUTE_NONE);
    CHECK(dm_padmode_route_event(0, 1, 0x00000101, 0, 1, 0) == DM_PADMODE_ROUTE_NONE);
    CHECK(dm_padmode_route_event(1, 0, 0x00002002, 2, 0, 1) == DM_PADMODE_ROUTE_NONE);

    // Controller removal/cancel returns to touch; a touchscreen is never a pad.
    CHECK(dm_padmode_route_event(1, 0, 0x00000401, 3, 0, 0) == DM_PADMODE_ROUTE_TOUCH);
    CHECK(!dm_padmode_is_pad_source(0x00001002));
    CHECK(dm_padmode_is_touch_source(0x00001002));
    CHECK(dm_padmode_is_pad_source(0x00000401));
    CHECK(dm_padmode_is_pad_source(0x01000010));

    if (failures) return 1;
    puts("PASS padmode route: touch down/up, controller key/joystick, cancel");
    return 0;
}
