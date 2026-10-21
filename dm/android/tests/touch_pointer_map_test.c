// #239 owned pointer-identity adapter test: 0/16, reorder, release/reuse, cancel/clear, overflow.
#include "../native/touch_pointer_map.h"
#include <stdio.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)
int main(void) {
    dm_touch_pointer_map m;
    dm_touch_pointer_clear(&m);
    CHECK(dm_touch_pointer_slot(&m, 0) == -1 && dm_touch_pointer_slot(&m, 16) == -1);
    int a = dm_touch_pointer_acquire(&m, 0);
    int b = dm_touch_pointer_acquire(&m, 16);
    CHECK(a == 0 && b == 1 && a != b);                 // distinct platform IDs never alias
    CHECK(dm_touch_pointer_acquire(&m, 16) == b);      // reorder/duplicate keeps identity
    CHECK(dm_touch_pointer_acquire(&m, 0) == a);
    dm_touch_pointer_release(&m, 0);
    CHECK(dm_touch_pointer_slot(&m, 0) == -1);
    CHECK(dm_touch_pointer_slot(&m, 16) == b);
    CHECK(dm_touch_pointer_acquire(&m, 32) == a);      // released slot is reusable
    CHECK(dm_touch_pointer_acquire(&m, 16) == b);      // reuse did not disturb other finger

    dm_touch_pointer_clear(&m);                        // cancel/input-destroy path
    CHECK(dm_touch_pointer_slot(&m, 16) == -1 && dm_touch_pointer_slot(&m, 32) == -1);

    for (int i = 0; i < DM_TOUCH_POINTER_SLOTS; ++i) CHECK(dm_touch_pointer_acquire(&m, 100 + i) == i);
    CHECK(dm_touch_pointer_acquire(&m, 200) == -1);    // 17th active finger: explicit drop
    CHECK(m.overflow == 1);
    for (int i = 0; i < DM_TOUCH_POINTER_SLOTS; ++i) CHECK(dm_touch_pointer_slot(&m, 100 + i) == i);
    CHECK(dm_touch_pointer_slot(&m, 200) == -1);       // never aliased into an active slot
    dm_touch_pointer_release(&m, 100);
    CHECK(dm_touch_pointer_acquire(&m, 200) == 0);     // capacity freed, deterministic reuse
    puts("PASS: pointer identity stable for 0/16, reorder, release/reuse, cancel and overflow");
    return 0;
}
