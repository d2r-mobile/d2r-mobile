// #361 production layout seam: Android touch_swap_above setting -> the single
// dm_touch_controls layout used for drawing and hit testing. The fixture also
// switches while a finger owns the main button: that ownership must be retired,
// not left on the old swap position.
#include "../native/live_touch.c"

#include <stdio.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); return 1; } } while (0)

static int event(dm_live_touch *t, int action, int id, float x, float y, int64_t at) {
    int32_t pointer = id;
    int w = (int)t->width, h = (int)t->height;
    return dm_live_touch_event(t, action, 0, 1, &pointer, &x, &y,
                               w, h, 0, 0, w, h, at);
}

int main(void) {
    dm_tc_set_swap_above(0);
    dm_live_touch *t = dm_live_touch_create(667, 375);
    CHECK(t != NULL);
    dm_live_touch_set_context(t, 0, (1u << DM_TC_COUNT) - 1, 1, 1, 1, 0);

    dm_tc_layout base;
    dm_tc_layout_make(&base, 667, 375);
    CHECK(t->state.layout.swap_above == 0);
    CHECK(t->state.layout.swap.x == base.swap.x && t->state.layout.swap.y == base.swap.y);
    CHECK(dm_tc_hit(&base, base.swap.x, base.swap.y) == DM_TC_SWAP);

    // A real finger owns the main button while the setting changes.
    CHECK(event(t, DM_LIVE_ACTION_DOWN, 71, base.target[DM_TC_MAIN].x,
                base.target[DM_TC_MAIN].y, 1000000000));
    CHECK(dm_live_touch_buttons(t) != 0);
    CHECK(dm_live_touch_set_swap_above(t, 1) == 1);
    CHECK(t->state.layout.swap_above == 1);
    CHECK(t->state.buttons == 0 && t->state.finger[0].active == 0);
    CHECK(t->state.layout.swap.x != base.swap.x || t->state.layout.swap.y != base.swap.y);

    dm_tc_layout above;
    dm_tc_layout_make(&above, 667, 375);   // global was set by the production setter
    CHECK(t->state.layout.swap.x == above.swap.x && t->state.layout.swap.y == above.swap.y);
    CHECK(dm_tc_hit(&t->state.layout, t->state.layout.swap.x, t->state.layout.swap.y) == DM_TC_SWAP);

    // Re-pressing after the cancel uses the new layout.
    dm_live_touch_set_context(t, 0, (1u << DM_TC_COUNT) - 1, 1, 1, 1, 0);
    CHECK(event(t, DM_LIVE_ACTION_DOWN, 72, above.target[DM_TC_MAIN].x,
                above.target[DM_TC_MAIN].y, 1100000000));
    CHECK(dm_live_touch_buttons(t) != 0);

    // Repeating the same setting must not retire a live finger or emit a second resize.
    CHECK(dm_live_touch_set_swap_above(t, 1) == 0);
    CHECK(dm_live_touch_buttons(t) != 0);

    // Switching back while that finger is active retires it and restores the base position.
    CHECK(dm_live_touch_set_swap_above(t, 0) == 1);
    CHECK(t->state.layout.swap_above == 0);
    CHECK(t->state.buttons == 0 && t->state.finger[0].active == 0);
    CHECK(t->state.layout.swap.x == base.swap.x && t->state.layout.swap.y == base.swap.y);

    // A new finger after the switch is accepted and does not inherit the old owner.
    dm_live_touch_set_context(t, 0, (1u << DM_TC_COUNT) - 1, 1, 1, 1, 0);
    CHECK(event(t, DM_LIVE_ACTION_DOWN, 73, base.target[DM_TC_MAIN].x,
                base.target[DM_TC_MAIN].y, 1200000000));
    CHECK(dm_live_touch_buttons(t) != 0);

    dm_live_touch_destroy(t);
    puts("PASS: touch_swap_above applies both production positions and retires an active gesture on switch");
    return 0;
}
