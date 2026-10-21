// #255 caller geometry fixture: actual output aspect (explicit 16:9, phone,
// capped ultrawide), centered front-end UI and invalid surfaces.
#include "../native/live_touch_geometry.h"
#include <stdio.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

int main(void) {
    // Phone-aspect output fills a 20:9 surface.
    dm_live_touch_geometry g = dm_live_touch_geometry_for(3200, 1440, 1600, 720, 1);
    CHECK(g.valid && g.left == 0 && g.top == 0 && g.width == 3200 && g.height == 1440);
    CHECK(g.logical_width == 667.0f);
    CHECK(g.logical_height > 299.0f && g.logical_height < 301.0f);

    // A finger on the right/bottom edge reaches the logical edge.
    float nx = 3199.0f / g.width, ny = 1439.0f / g.height;
    CHECK(nx * g.logical_width > 666.0f && ny * g.logical_height > 299.0f);

    // Explicit 1600x900 compatibility output must stay centered 16:9 even in-run.
    g = dm_live_touch_geometry_for(3200, 1440, 1600, 900, 1);
    CHECK(g.valid && g.left == 320 && g.top == 0 && g.width == 2560 && g.height == 1440);
    CHECK(g.logical_height > 374.0f && g.logical_height < 376.0f);

    // Capped ultrawide output is wider than the surface: fit vertically.
    g = dm_live_touch_geometry_for(3200, 1440, 1920, 720, 1);
    CHECK(g.valid && g.left == 0 && g.top == 120 && g.width == 3200 && g.height == 1200);
    CHECK(g.logical_height > 249.0f && g.logical_height < 251.0f);

    // Front-end UI remains the original centered 16:9 rectangle.
    g = dm_live_touch_geometry_for(3200, 1440, 1600, 720, 0);
    CHECK(g.valid && g.left == 320 && g.top == 0 && g.width == 2560 && g.height == 1440);
    g = dm_live_touch_geometry_for(1920, 1080, 1600, 900, 0);
    CHECK(g.valid && g.left == 0 && g.top == 0 && g.width == 1920 && g.height == 1080);

    CHECK(!dm_live_touch_geometry_for(0, 0, 1600, 720, 1).valid);
    CHECK(!dm_live_touch_geometry_for(319, 240, 1600, 720, 0).valid);
    CHECK(!dm_live_touch_geometry_for(320, 239, 1600, 720, 0).valid);
    CHECK(!dm_live_touch_geometry_for(16385, 720, 1600, 720, 1).valid);
    puts("PASS: caller geometry fits actual output aspect, keeps front-end 16:9 and invalid surfaces neutral");
    return 0;
}
