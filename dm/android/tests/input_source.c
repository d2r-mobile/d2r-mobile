#include "../app/input_source.h"
#include "../app/touch_bridge.h"
#include "dm_input.h"
#include "dm_touch_controls.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks;
#define CHECK(expr) do { ++checks; if (!(expr)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); return 1; } } while (0)

static dm_touch_event touch_event(dm_touch_action action, int32_t id,
                                  int64_t time_ns, int32_t index) {
    dm_tc_layout layout;
    dm_touch_event event;
    dm_tc_layout_make(&layout, 480, 300);
    memset(&event, 0, sizeof event);
    event.action = action;
    event.action_index = index;
    event.pointer_count = 1;
    event.time_ns = time_ns;
    event.points[0].id = id;
    event.points[0].x_px = layout.target[DM_TC_MAIN].x * 3.0f;
    event.points[0].y_px = layout.target[DM_TC_MAIN].y * 3.0f;
    return event;
}

static dm_pad_sample gamepad_sample(uint64_t buttons, int32_t lx, int32_t ly) {
    dm_pad_sample sample;
    memset(&sample, 0, sizeof sample);
    sample.connected = 1;
    sample.buttons = buttons;
    sample.lx = lx;
    sample.ly = ly;
    return sample;
}

static int rejected_takeover_cases(void) {
    dm_input_arbiter *arbiter = dm_input_arbiter_create(480, 300, 3.0f);
    CHECK(arbiter != NULL);
    CHECK(dm_input_arbiter_connect(arbiter, 2, 10));
    dm_pad_sample held;
    memset(&held, 0, sizeof held);
    held.connected = 1;
    held.buttons = DM_BTN_A;
    held.lx = 20000;
    CHECK(dm_input_arbiter_gamepad(arbiter, 2, 10, &held));
    dm_npad_state state;
    CHECK(dm_input_arbiter_read(arbiter, &state) && state.buttons == 0);
    CHECK(dm_input_arbiter_read(arbiter, &state) && state.buttons == DM_BTN_A);

    dm_touch_event malformed;
    memset(&malformed, 0, sizeof malformed);
    malformed.action = DM_TOUCH_ACTION_POINTER_DOWN;
    malformed.action_index = 1;
    malformed.pointer_count = 2;
    malformed.time_ns = 1000000000;
    malformed.points[0] = (dm_touch_point){.id = 7, .x_px = 100, .y_px = 100};
    malformed.points[1] = (dm_touch_point){.id = 8, .x_px = 200, .y_px = 100};
    CHECK(!dm_input_arbiter_touch(arbiter, &malformed));
    CHECK(dm_input_arbiter_source(arbiter) == DM_INPUT_SOURCE_GAMEPAD);
    CHECK(dm_input_arbiter_layout(arbiter) == DM_INPUT_LAYOUT_CONTROLLER);
    CHECK(dm_input_arbiter_read(arbiter, &state));
    CHECK(state.buttons == DM_BTN_A && state.lx == 20000);

    /* A malformed multi-pointer DOWN must not steal ownership either. */
    malformed.action = DM_TOUCH_ACTION_DOWN;
    malformed.action_index = 0;
    CHECK(!dm_input_arbiter_touch(arbiter, &malformed));
    CHECK(dm_input_arbiter_source(arbiter) == DM_INPUT_SOURCE_GAMEPAD);
    CHECK(dm_input_arbiter_read(arbiter, &state));
    CHECK(state.buttons == DM_BTN_A && state.lx == 20000);

    /* Downstream scaled-coordinate rejection occurs after shape validation and
       inside the staged real bridge; the held gamepad sample must survive. */
    dm_touch_event scaled;
    memset(&scaled, 0, sizeof scaled);
    scaled.action = DM_TOUCH_ACTION_DOWN;
    scaled.action_index = 0;
    scaled.pointer_count = 1;
    scaled.time_ns = 1000000001;
    scaled.points[0] = (dm_touch_point){.id = 9, .x_px = 1.0e38f, .y_px = 100};
    dm_input_arbiter *small = dm_input_arbiter_create(480, 300, 1.0e-38f);
    CHECK(small != NULL);
    CHECK(dm_input_arbiter_connect(small, 3, 20));
    CHECK(dm_input_arbiter_gamepad(small, 3, 20, &held));
    CHECK(dm_input_arbiter_read(small, &state) && state.buttons == 0);
    CHECK(dm_input_arbiter_read(small, &state) && state.buttons == DM_BTN_A);
    CHECK(!dm_input_arbiter_touch(small, &scaled));
    CHECK(dm_input_arbiter_source(small) == DM_INPUT_SOURCE_GAMEPAD);
    CHECK(dm_input_arbiter_read(small, &state) && state.buttons == DM_BTN_A);
    dm_input_arbiter_destroy(small, NULL);

    /* NONE source remains NONE after malformed/non-owning takeovers. */
    dm_input_arbiter *empty = dm_input_arbiter_create(480, 300, 3.0f);
    CHECK(empty != NULL);
    CHECK(!dm_input_arbiter_touch(empty, &malformed));
    CHECK(dm_input_arbiter_source(empty) == DM_INPUT_SOURCE_NONE);
    CHECK(dm_input_arbiter_read(empty, &state) && state.buttons == 0);
    dm_touch_event valid = touch_event(DM_TOUCH_ACTION_DOWN, 7, 1000000002, 0);
    CHECK(dm_input_arbiter_touch(empty, &valid));
    CHECK(dm_input_arbiter_source(empty) == DM_INPUT_SOURCE_TOUCH);
    CHECK(dm_input_arbiter_read(empty, &state) && state.buttons == 0);
    CHECK(dm_input_arbiter_read(empty, &state) && state.buttons == DM_BTN_A);
    dm_input_arbiter_destroy(empty, NULL);

    dm_input_arbiter_destroy(arbiter, NULL);
    return 0;
}

int main(void) {
    if (rejected_takeover_cases()) return 1;
    dm_input_arbiter *arbiter = dm_input_arbiter_create(480, 300, 3.0f);
    CHECK(arbiter != NULL);
    CHECK(dm_input_arbiter_source(arbiter) == DM_INPUT_SOURCE_NONE);
    CHECK(dm_input_arbiter_layout(arbiter) == DM_INPUT_LAYOUT_TOUCH_HUD);
    CHECK(!dm_input_arbiter_requires_fresh(arbiter));

    dm_npad_state state;
    CHECK(dm_input_arbiter_read(arbiter, &state));
    CHECK(state.buttons == 0 && state.lx == 0 && state.attributes == 0);

    /* Connection and a neutral sample are not ownership. */
    CHECK(dm_input_arbiter_connect(arbiter, 2, 10));
    CHECK(dm_input_arbiter_source(arbiter) == DM_INPUT_SOURCE_NONE);
    dm_pad_sample neutral = gamepad_sample(0, 0, 0);
    CHECK(dm_input_arbiter_gamepad(arbiter, 2, 10, &neutral));
    CHECK(dm_input_arbiter_source(arbiter) == DM_INPUT_SOURCE_NONE);
    CHECK(dm_input_arbiter_read(arbiter, &state) && state.buttons == 0);
    dm_pad_sample edge = gamepad_sample(0, DM_INPUT_GAMEPAD_DEADZONE, 0);
    CHECK(dm_input_arbiter_gamepad(arbiter, 2, 10, &edge));
    CHECK(dm_input_arbiter_source(arbiter) == DM_INPUT_SOURCE_NONE);
    dm_pad_sample axis = gamepad_sample(0, DM_INPUT_GAMEPAD_DEADZONE + 1, 0);
    CHECK(dm_input_arbiter_gamepad(arbiter, 2, 10, &axis));
    CHECK(dm_input_arbiter_source(arbiter) == DM_INPUT_SOURCE_GAMEPAD);
    CHECK(dm_input_arbiter_read(arbiter, &state) && state.buttons == 0);
    CHECK(dm_input_arbiter_read(arbiter, &state) &&
          state.lx == DM_INPUT_GAMEPAD_DEADZONE + 1);

    /* A valid touch down claims touch and emits a neutral boundary first. */
    dm_touch_event down = touch_event(DM_TOUCH_ACTION_DOWN, 7, 1000000000, 0);
    CHECK(dm_input_arbiter_touch(arbiter, &down));
    CHECK(dm_input_arbiter_source(arbiter) == DM_INPUT_SOURCE_TOUCH);
    CHECK(dm_input_arbiter_layout(arbiter) == DM_INPUT_LAYOUT_TOUCH_HUD);
    CHECK(dm_input_arbiter_read(arbiter, &state) && state.buttons == 0);
    CHECK(dm_input_arbiter_read(arbiter, &state) && state.buttons == DM_BTN_A);
    dm_touch_event held_move = touch_event(DM_TOUCH_ACTION_MOVE, 7, 1000000001, 0);
    CHECK(dm_input_arbiter_touch(arbiter, &held_move));

    /* Held touch -> active gamepad: touch pointers/queued gesture are canceled,
       gamepad owns output, and no touch MOVE/UP can resurrect. */
    dm_pad_sample pad_a = gamepad_sample(DM_BTN_A, 20000, 0);
    CHECK(dm_input_arbiter_gamepad(arbiter, 2, 10, &pad_a));
    CHECK(dm_input_arbiter_source(arbiter) == DM_INPUT_SOURCE_GAMEPAD);
    CHECK(dm_input_arbiter_layout(arbiter) == DM_INPUT_LAYOUT_CONTROLLER);
    CHECK(dm_input_arbiter_read(arbiter, &state) && state.buttons == 0);
    CHECK(dm_input_arbiter_read(arbiter, &state) && state.buttons == DM_BTN_A);
    dm_touch_event late_move = touch_event(DM_TOUCH_ACTION_MOVE, 7, 1000000001, 0);
    dm_touch_event late_up = touch_event(DM_TOUCH_ACTION_UP, 7, 1000000002, 0);
    CHECK(!dm_input_arbiter_touch(arbiter, &late_move));
    CHECK(!dm_input_arbiter_touch(arbiter, &late_up));
    CHECK(dm_input_arbiter_source(arbiter) == DM_INPUT_SOURCE_GAMEPAD);

    /* Gamepad -> touch with the same pointer id must work only if the old
       touch stream was canceled and neutralized. */
    dm_touch_event down_again = touch_event(DM_TOUCH_ACTION_DOWN, 7, 1000000003, 0);
    CHECK(dm_input_arbiter_touch(arbiter, &down_again));
    CHECK(dm_input_arbiter_source(arbiter) == DM_INPUT_SOURCE_TOUCH);
    CHECK(dm_input_arbiter_read(arbiter, &state) && state.buttons == 0);
    CHECK(dm_input_arbiter_read(arbiter, &state) && state.buttons == DM_BTN_A);

    /* Gamepad release and disconnect invalidate the gamepad source. */
    CHECK(dm_input_arbiter_gamepad(arbiter, 2, 10, &pad_a));
    dm_pad_sample release = gamepad_sample(0, 0, 0);
    CHECK(dm_input_arbiter_gamepad(arbiter, 2, 10, &release));
    CHECK(dm_input_arbiter_disconnect(arbiter, 2, 10));
    CHECK(dm_input_arbiter_source(arbiter) == DM_INPUT_SOURCE_NONE);
    CHECK(dm_input_arbiter_requires_fresh(arbiter));
    CHECK(dm_input_arbiter_read(arbiter, &state) && state.buttons == 0);
    CHECK(!dm_input_arbiter_gamepad(arbiter, 2, 10, &pad_a));

    /* Reused id with a new generation: old generation is rejected, connection
       alone does not take over, fresh active input does. */
    CHECK(dm_input_arbiter_connect(arbiter, 2, 11));
    CHECK(dm_input_arbiter_source(arbiter) == DM_INPUT_SOURCE_NONE);
    CHECK(!dm_input_arbiter_gamepad(arbiter, 2, 10, &pad_a));
    CHECK(dm_input_arbiter_source(arbiter) == DM_INPUT_SOURCE_NONE);
    CHECK(dm_input_arbiter_gamepad(arbiter, 2, 11, &pad_a));
    CHECK(dm_input_arbiter_source(arbiter) == DM_INPUT_SOURCE_GAMEPAD);
    CHECK(dm_input_arbiter_read(arbiter, &state) && state.buttons == 0);
    CHECK(dm_input_arbiter_read(arbiter, &state) && state.buttons == DM_BTN_A);

    /* Focus loss invalidates; late input is rejected even after focus returns. */
    CHECK(dm_input_arbiter_focus(arbiter, 0));
    CHECK(dm_input_arbiter_source(arbiter) == DM_INPUT_SOURCE_NONE);
    CHECK(dm_input_arbiter_requires_fresh(arbiter));
    CHECK(dm_input_arbiter_read(arbiter, &state) && state.buttons == 0);
    CHECK(!dm_input_arbiter_gamepad(arbiter, 2, 11, &pad_a));
    CHECK(dm_input_arbiter_focus(arbiter, 1));
    CHECK(dm_input_arbiter_requires_fresh(arbiter));
    CHECK(dm_input_arbiter_source(arbiter) == DM_INPUT_SOURCE_NONE);
    CHECK(!dm_input_arbiter_touch(arbiter, &late_move));
    CHECK(dm_input_arbiter_touch(arbiter, &down_again));
    CHECK(dm_input_arbiter_source(arbiter) == DM_INPUT_SOURCE_TOUCH);
    CHECK(!dm_input_arbiter_requires_fresh(arbiter));

    /* Queue replacement and pause are explicit invalidations. */
    CHECK(dm_input_arbiter_queue_changed(arbiter));
    CHECK(dm_input_arbiter_source(arbiter) == DM_INPUT_SOURCE_NONE);
    CHECK(dm_input_arbiter_read(arbiter, &state) && state.buttons == 0);
    CHECK(!dm_input_arbiter_touch(arbiter, &late_move));
    CHECK(dm_input_arbiter_pause(arbiter, 1));
    CHECK(dm_input_arbiter_source(arbiter) == DM_INPUT_SOURCE_NONE);
    CHECK(dm_input_arbiter_read(arbiter, &state) && state.buttons == 0);
    CHECK(!dm_input_arbiter_gamepad(arbiter, 2, 11, &pad_a));
    CHECK(dm_input_arbiter_pause(arbiter, 0));
    CHECK(dm_input_arbiter_requires_fresh(arbiter));
    CHECK(dm_input_arbiter_touch(arbiter, &down_again));
    CHECK(dm_input_arbiter_source(arbiter) == DM_INPUT_SOURCE_TOUCH);

    /* Bounds and repeated lifecycle events do not invent a device. */
    CHECK(!dm_input_arbiter_connect(arbiter, DM_INPUT_MAX_DEVICE_ID + 1, 12));
    CHECK(!dm_input_arbiter_connect(arbiter, 2, 0));
    CHECK(!dm_input_arbiter_disconnect(arbiter, 2, 12));
    CHECK(dm_input_arbiter_queue_changed(arbiter));
    CHECK(dm_input_arbiter_read(arbiter, &state) && state.buttons == 0);
    CHECK(dm_input_arbiter_queue_changed(arbiter));
    CHECK(dm_input_arbiter_read(arbiter, &state) && state.buttons == 0);

    /* A second touch pointer and a gamepad switch preserve one coherent source
       without OR-ing. */
    dm_touch_event pointer_down = touch_event(DM_TOUCH_ACTION_DOWN, 8, 2000000000, 0);
    CHECK(dm_input_arbiter_touch(arbiter, &pointer_down));
    CHECK(dm_input_arbiter_read(arbiter, &state) && state.buttons == 0);
    CHECK(dm_input_arbiter_gamepad(arbiter, 2, 11, &pad_a));
    CHECK(dm_input_arbiter_source(arbiter) == DM_INPUT_SOURCE_GAMEPAD);
    CHECK(dm_input_arbiter_read(arbiter, &state) && state.buttons == 0);
    CHECK(dm_input_arbiter_read(arbiter, &state) && state.buttons == DM_BTN_A);

    /* A conflicting second id does not take over by connection; the old id is
       rejected and the new id needs its own active input. */
    CHECK(dm_input_arbiter_connect(arbiter, 3, 20));
    CHECK(dm_input_arbiter_source(arbiter) == DM_INPUT_SOURCE_NONE);
    CHECK(!dm_input_arbiter_gamepad(arbiter, 2, 11, &pad_a));
    CHECK(dm_input_arbiter_gamepad(arbiter, 3, 20, &pad_a));
    CHECK(dm_input_arbiter_source(arbiter) == DM_INPUT_SOURCE_GAMEPAD);
    CHECK(dm_input_arbiter_read(arbiter, &state) && state.buttons == 0);
    CHECK(dm_input_arbiter_read(arbiter, &state) && state.buttons == DM_BTN_A);

    dm_npad_state final_neutral;
    dm_input_arbiter_destroy(arbiter, &final_neutral);
    CHECK(final_neutral.buttons == 0 && final_neutral.lx == 0 && final_neutral.ly == 0 &&
          final_neutral.rx == 0 && final_neutral.ry == 0);
    printf("PASS input source: %d checks\n", checks);
    return 0;
}
