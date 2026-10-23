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

    CHECK(map->x == l.width - 30 * k && map->y == 30 * k);
    CHECK(map->x > l.width / 2);
    puts("PASS: shared MAP layout matches upstream top-right geometry");
    return 0;
}
