// #318 authoritative touch layout: Android MAP moves to the top-left while
// iOS keeps its original top-right position. Hit testing uses the same layout.
#include "dm_touch_controls.h"
#include "dm_input.h"

#include <math.h>
#include <stdio.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); return 1; } } while (0)

int main(void) {
    dm_tc_layout l;
    dm_tc_layout_make(&l, 667, 600);
    float k = dm_tc_scale_for_size(667, 600);
    const dm_tc_target *map = &l.target[DM_TC_MAP];
    CHECK(map->button == DM_BTN_MINUS);
    CHECK(map->r == 22 * k);
    CHECK(dm_tc_hit(&l, map->x, map->y) == DM_TC_MAP);
    CHECK(map->x - map->r >= 0 && map->y - map->r >= 0);
    CHECK(map->x + map->r <= l.width && map->y + map->r <= l.height);

#ifdef __ANDROID__
    CHECK(map->x == 30 * k && map->y == 30 * k);
    CHECK(map->x < l.width / 2);
    // The original top-right minimap/title entry must no longer overlap our MAP.
    CHECK(dm_tc_hit(&l, l.width - 30 * k, 30 * k) != DM_TC_MAP);
    // Do not introduce an overlap with another top-left control.
    for (int i = 0; i < DM_TC_COUNT; ++i) {
        if (i == DM_TC_MAP) continue;
        float dx = map->x - l.target[i].x, dy = map->y - l.target[i].y;
        CHECK(dx * dx + dy * dy > (map->r + l.target[i].r) * (map->r + l.target[i].r));
    }
    puts("PASS: Android MAP is top-left, remains hittable, clear of top-right entry and other controls");
#else
    CHECK(map->x == l.width - 30 * k && map->y == 30 * k);
    CHECK(map->x > l.width / 2);
    puts("PASS: iOS MAP layout remains top-right");
#endif
    return 0;
}
