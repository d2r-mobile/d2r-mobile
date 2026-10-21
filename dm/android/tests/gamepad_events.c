#include "../app/gamepad_events.h"
#include "dm_input.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct AInputEvent {
    int type;
    int source;
    int action;
    int keycode;
    int repeat;
    float axis[19];
};

int32_t AInputEvent_getType(const AInputEvent *event) { return event->type; }
int32_t AInputEvent_getSource(const AInputEvent *event) { return event->source; }
int32_t AKeyEvent_getAction(const AInputEvent *event) { return event->action; }
int32_t AKeyEvent_getKeyCode(const AInputEvent *event) { return event->keycode; }
int32_t AKeyEvent_getRepeatCount(const AInputEvent *event) { return event->repeat; }
int32_t AMotionEvent_getAction(const AInputEvent *event) { return event->action; }
float AMotionEvent_getAxisValue(const AInputEvent *event, int32_t axis, size_t pointer_index) {
    (void)pointer_index;
    return axis >= 0 && axis < 19 ? event->axis[axis] : 0.0f;
}

enum {
    EVENT_KEY = 1, EVENT_MOTION = 2, EVENT_FOCUS = 3,
    SOURCE_GAMEPAD = 0x00000401, SOURCE_JOYSTICK = 0x01000010,
    SOURCE_TOUCHSCREEN = 0x00001002,
    KEY_DOWN = 0, KEY_UP = 1, KEY_MULTIPLE = 2,
    MOTION_DOWN = 0, MOTION_UP = 1, MOTION_MOVE = 2, MOTION_CANCEL = 3,
    KEY_DPAD_UP = 19, KEY_DPAD_DOWN = 20, KEY_DPAD_LEFT = 21, KEY_DPAD_RIGHT = 22,
    KEY_A = 96, KEY_B = 97, KEY_X = 99, KEY_Y = 100,
    KEY_L1 = 102, KEY_R1 = 103, KEY_L2 = 104, KEY_R2 = 105,
    KEY_THUMBL = 106, KEY_THUMBR = 107, KEY_START = 108, KEY_SELECT = 109,
    KEY_UP_LEFT = 268, KEY_DOWN_LEFT = 269, KEY_UP_RIGHT = 270, KEY_DOWN_RIGHT = 271,
    AXIS_X = 0, AXIS_Y = 1, AXIS_Z = 11, AXIS_RZ = 14,
    AXIS_HAT_X = 15, AXIS_HAT_Y = 16, AXIS_LTRIGGER = 17, AXIS_RTRIGGER = 18,
};

static int checks;
#define CHECK(expr) do { ++checks; if (!(expr)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); return 1; } } while (0)

static struct AInputEvent key_event(int source, int action, int keycode, int repeat) {
    struct AInputEvent event;
    memset(&event, 0, sizeof event);
    event.type = EVENT_KEY;
    event.source = source;
    event.action = action;
    event.keycode = keycode;
    event.repeat = repeat;
    return event;
}

static struct AInputEvent motion_event(int source, int action) {
    struct AInputEvent event;
    memset(&event, 0, sizeof event);
    event.type = EVENT_MOTION;
    event.source = source;
    event.action = action;
    return event;
}

static int channel_interleave_cases(void) {
    dm_gamepad_context ctx;
    dm_npad_state state;
    struct AInputEvent key;
    struct AInputEvent motion;

    /* A stick MOVE with neutral HAT must not clear held digital D-pad. */
    dm_gamepad_context_init(&ctx);
    memset(&state, 0, sizeof state);
    key = key_event(SOURCE_GAMEPAD, KEY_DOWN, KEY_DPAD_UP, 0);
    CHECK(dm_gamepad_event_apply(&ctx, &key, &state));
    motion = motion_event(SOURCE_JOYSTICK, MOTION_MOVE);
    motion.axis[AXIS_X] = 0.5f;
    CHECK(dm_gamepad_event_apply(&ctx, &motion, &state));
    CHECK((state.buttons & DM_BTN_UP) != 0);

    /* A stick MOVE must not clear a held digital trigger. */
    dm_gamepad_context_init(&ctx);
    memset(&state, 0, sizeof state);
    key = key_event(SOURCE_GAMEPAD, KEY_DOWN, KEY_L2, 0);
    CHECK(dm_gamepad_event_apply(&ctx, &key, &state));
    CHECK(dm_gamepad_event_apply(&ctx, &motion, &state));
    CHECK((state.buttons & DM_BTN_ZL) != 0);

    /* Digital LEFT UP must not clear held analog HAT_LEFT. */
    dm_gamepad_context_init(&ctx);
    memset(&state, 0, sizeof state);
    motion = motion_event(SOURCE_JOYSTICK, MOTION_MOVE);
    motion.axis[AXIS_HAT_X] = -1.0f;
    CHECK(dm_gamepad_event_apply(&ctx, &motion, &state));
    key = key_event(SOURCE_GAMEPAD, KEY_UP, KEY_DPAD_LEFT, 0);
    CHECK(dm_gamepad_event_apply(&ctx, &key, &state));
    CHECK((state.buttons & DM_BTN_LEFT) != 0);

    /* Reverse release order: digital release leaves analog held; neutral
       motion then releases the analog contribution. */
    dm_gamepad_context_init(&ctx);
    memset(&state, 0, sizeof state);
    key = key_event(SOURCE_GAMEPAD, KEY_DOWN, KEY_DPAD_LEFT, 0);
    CHECK(dm_gamepad_event_apply(&ctx, &key, &state));
    CHECK(dm_gamepad_event_apply(&ctx, &motion, &state));
    key = key_event(SOURCE_GAMEPAD, KEY_UP, KEY_DPAD_LEFT, 0);
    CHECK(dm_gamepad_event_apply(&ctx, &key, &state));
    CHECK((state.buttons & DM_BTN_LEFT) != 0);
    motion = motion_event(SOURCE_JOYSTICK, MOTION_MOVE);
    CHECK(dm_gamepad_event_apply(&ctx, &motion, &state));
    CHECK((state.buttons & DM_BTN_LEFT) == 0);

    /* Trigger reverse order: analog held survives digital release, and the
       digital release does not clear the analog contribution. */
    dm_gamepad_context_init(&ctx);
    memset(&state, 0, sizeof state);
    motion = motion_event(SOURCE_JOYSTICK, MOTION_MOVE);
    motion.axis[AXIS_LTRIGGER] = 0.75f;
    CHECK(dm_gamepad_event_apply(&ctx, &motion, &state));
    key = key_event(SOURCE_GAMEPAD, KEY_UP, KEY_L2, 0);
    CHECK(dm_gamepad_event_apply(&ctx, &key, &state));
    CHECK((state.buttons & DM_BTN_ZL) != 0);
    key = key_event(SOURCE_GAMEPAD, KEY_DOWN, KEY_L2, 0);
    CHECK(dm_gamepad_event_apply(&ctx, &key, &state));
    key = key_event(SOURCE_GAMEPAD, KEY_UP, KEY_L2, 0);
    CHECK(dm_gamepad_event_apply(&ctx, &key, &state));
    CHECK((state.buttons & DM_BTN_ZL) != 0);
    motion.axis[AXIS_LTRIGGER] = 0.0f;
    CHECK(dm_gamepad_event_apply(&ctx, &motion, &state));
    CHECK((state.buttons & DM_BTN_ZL) == 0);

    /* CANCEL resets both contributions. */
    dm_gamepad_context_init(&ctx);
    memset(&state, 0, sizeof state);
    key = key_event(SOURCE_GAMEPAD, KEY_DOWN, KEY_DPAD_LEFT, 0);
    CHECK(dm_gamepad_event_apply(&ctx, &key, &state));
    motion = motion_event(SOURCE_JOYSTICK, MOTION_MOVE);
    motion.axis[AXIS_HAT_X] = -1.0f;
    motion.axis[AXIS_LTRIGGER] = 0.75f;
    CHECK(dm_gamepad_event_apply(&ctx, &motion, &state));
    motion = motion_event(SOURCE_JOYSTICK, MOTION_CANCEL);
    CHECK(dm_gamepad_event_apply(&ctx, &motion, &state));
    CHECK(state.buttons == 0 && state.lx == 0 && state.ly == 0 &&
          state.rx == 0 && state.ry == 0);
    CHECK(ctx.digital_buttons == 0 && ctx.analog_buttons == 0);

    /* Independent caller-owned contexts do not leak contributions. */
    dm_gamepad_context one, two;
    dm_npad_state state_one, state_two;
    dm_gamepad_context_init(&one);
    dm_gamepad_context_init(&two);
    memset(&state_one, 0, sizeof state_one);
    memset(&state_two, 0, sizeof state_two);
    key = key_event(SOURCE_GAMEPAD, KEY_DOWN, KEY_A, 0);
    CHECK(dm_gamepad_event_apply(&one, &key, &state_one));
    key = key_event(SOURCE_GAMEPAD, KEY_DOWN, KEY_B, 0);
    CHECK(dm_gamepad_event_apply(&two, &key, &state_two));
    CHECK(state_one.buttons == DM_BTN_A && state_two.buttons == DM_BTN_B);
    motion = motion_event(SOURCE_JOYSTICK, MOTION_MOVE);
    motion.axis[AXIS_HAT_X] = -1.0f;
    CHECK(dm_gamepad_event_apply(&one, &motion, &state_one));
    CHECK((state_one.buttons & DM_BTN_LEFT) != 0);
    CHECK(state_two.buttons == DM_BTN_B);
    return 0;
}

int main(void) {
    if (channel_interleave_cases()) return 1;
    dm_npad_state state = {
        .sampling = 1234, .buttons = DM_BTN_X,
        .lx = 111, .ly = -222, .rx = 333, .ry = -444, .attributes = 7,
    };
    dm_npad_state before;
    struct AInputEvent event;
    dm_gamepad_context ctx;
    dm_gamepad_context_init(&ctx);

    /* Key down/up and repeated down preserve unrelated fields. */
    before = state;
    event = key_event(SOURCE_GAMEPAD, KEY_DOWN, KEY_A, 0);
    CHECK(dm_gamepad_event_apply(&ctx, &event, &state));
    CHECK(state.buttons == (DM_BTN_X | DM_BTN_A));
    CHECK(state.lx == 111 && state.ly == -222 && state.rx == 333 && state.ry == -444);
    CHECK(state.sampling == 1234 && state.attributes == 7);
    event.repeat = 1;
    CHECK(dm_gamepad_event_apply(&ctx, &event, &state));
    CHECK(state.buttons == (DM_BTN_X | DM_BTN_A));
    event = key_event(SOURCE_GAMEPAD, KEY_DOWN, KEY_B, 0);
    CHECK(dm_gamepad_event_apply(&ctx, &event, &state));
    CHECK(state.buttons == (DM_BTN_X | DM_BTN_A | DM_BTN_B));
    event = key_event(SOURCE_GAMEPAD, KEY_UP, KEY_B, 0);
    CHECK(dm_gamepad_event_apply(&ctx, &event, &state));
    CHECK(state.buttons == (DM_BTN_X | DM_BTN_A));
    event = key_event(SOURCE_GAMEPAD, KEY_DOWN, KEY_START, 0);
    CHECK(dm_gamepad_event_apply(&ctx, &event, &state));
    CHECK((state.buttons & DM_BTN_PLUS) != 0);
    event = key_event(SOURCE_GAMEPAD, KEY_DOWN, KEY_SELECT, 0);
    CHECK(dm_gamepad_event_apply(&ctx, &event, &state));
    CHECK((state.buttons & DM_BTN_MINUS) != 0);

    /* D-pad diagonal key codes set/clear both directions. */
    event = key_event(SOURCE_GAMEPAD, KEY_DOWN, KEY_UP_LEFT, 0);
    CHECK(dm_gamepad_event_apply(&ctx, &event, &state));
    CHECK((state.buttons & (DM_BTN_UP | DM_BTN_LEFT)) == (DM_BTN_UP | DM_BTN_LEFT));
    event = key_event(SOURCE_GAMEPAD, KEY_UP, KEY_UP_LEFT, 0);
    CHECK(dm_gamepad_event_apply(&ctx, &event, &state));
    CHECK(!(state.buttons & (DM_BTN_UP | DM_BTN_LEFT)));

    /* Motion sticks/hat/triggers; Y is inverted from Android down-positive. */
    event = motion_event(SOURCE_JOYSTICK, MOTION_MOVE);
    event.axis[AXIS_X] = 0.5f;
    event.axis[AXIS_Y] = -0.5f;
    event.axis[AXIS_Z] = 0.25f;
    event.axis[AXIS_RZ] = 0.75f;
    event.axis[AXIS_HAT_X] = -1.0f;
    event.axis[AXIS_HAT_Y] = 1.0f;
    event.axis[AXIS_LTRIGGER] = 0.75f;
    event.axis[AXIS_RTRIGGER] = 0.75f;
    CHECK(dm_gamepad_event_apply(&ctx, &event, &state));
    CHECK(state.lx == 16384 && state.ly == 16384);
    CHECK(state.rx == 8192 && state.ry == -24575);
    CHECK((state.buttons & (DM_BTN_LEFT | DM_BTN_DOWN)) == (DM_BTN_LEFT | DM_BTN_DOWN));
    CHECK((state.buttons & (DM_BTN_ZL | DM_BTN_ZR)) == (DM_BTN_ZL | DM_BTN_ZR));
    CHECK((state.buttons & (DM_BTN_X | DM_BTN_A)) == (DM_BTN_X | DM_BTN_A));

    /* Full axis extremes preserve the sign convention and clamp exactly. */
    event = motion_event(SOURCE_JOYSTICK, MOTION_MOVE);
    event.axis[AXIS_X] = 1.0f;
    event.axis[AXIS_Y] = -1.0f;
    event.axis[AXIS_Z] = -1.0f;
    event.axis[AXIS_RZ] = 1.0f;
    CHECK(dm_gamepad_event_apply(&ctx, &event, &state));
    CHECK(state.lx == DM_STICK_MAX && state.ly == DM_STICK_MAX);
    CHECK(state.rx == -DM_STICK_MAX && state.ry == -DM_STICK_MAX);

    /* Deadzone and trigger threshold are neutral at the boundary. */
    event = motion_event(SOURCE_GAMEPAD, MOTION_MOVE);
    event.axis[AXIS_X] = 0.05f;
    event.axis[AXIS_Y] = -0.05f;
    event.axis[AXIS_LTRIGGER] = 0.5f;
    event.axis[AXIS_RTRIGGER] = 0.5f;
    CHECK(dm_gamepad_event_apply(&ctx, &event, &state));
    CHECK(state.lx == 0 && state.ly == 0 && state.rx == 0 && state.ry == 0);
    CHECK(!(state.buttons & (DM_BTN_LEFT | DM_BTN_DOWN | DM_BTN_ZL | DM_BTN_ZR)));

    /* Motion UP releases axes/hat/trigger buttons, preserving digital keys. */
    event = motion_event(SOURCE_GAMEPAD, MOTION_MOVE);
    event.axis[AXIS_HAT_X] = -1.0f;
    event.axis[AXIS_LTRIGGER] = 0.75f;
    CHECK(dm_gamepad_event_apply(&ctx, &event, &state));
    CHECK((state.buttons & (DM_BTN_LEFT | DM_BTN_ZL)) == (DM_BTN_LEFT | DM_BTN_ZL));
    state.lx = state.ly = state.rx = state.ry = 100;
    event = motion_event(SOURCE_GAMEPAD, MOTION_UP);
    CHECK(dm_gamepad_event_apply(&ctx, &event, &state));
    CHECK(state.buttons == (DM_BTN_X | DM_BTN_A | DM_BTN_PLUS | DM_BTN_MINUS));
    CHECK(state.lx == 0 && state.ly == 0 && state.rx == 0 && state.ry == 0);
    CHECK(state.sampling == 1234 && state.attributes == 7);

    /* CANCEL is a complete neutral boundary. */
    event = motion_event(SOURCE_GAMEPAD, MOTION_CANCEL);
    CHECK(dm_gamepad_event_apply(&ctx, &event, &state));
    CHECK(state.buttons == DM_BTN_X && state.lx == 0 && state.ly == 0 &&
          state.rx == 0 && state.ry == 0);
    CHECK(state.sampling == 1234 && state.attributes == 7);

    /* Rejections are atomic: exact prior bytes, including unknown source/key. */
    state = before;
    dm_npad_state snapshot = state;
    dm_gamepad_context context_snapshot = ctx;
    event = key_event(SOURCE_TOUCHSCREEN, KEY_DOWN, KEY_A, 0);
    CHECK(!dm_gamepad_event_apply(&ctx, &event, &state));
    CHECK(!memcmp(&state, &snapshot, sizeof state));
    CHECK(!memcmp(&ctx, &context_snapshot, sizeof ctx));
    event = key_event(SOURCE_GAMEPAD, KEY_DOWN, 999, 0);
    CHECK(!dm_gamepad_event_apply(&ctx, &event, &state));
    CHECK(!memcmp(&state, &snapshot, sizeof state));
    event = key_event(SOURCE_GAMEPAD, KEY_MULTIPLE, KEY_A, 0);
    CHECK(!dm_gamepad_event_apply(&ctx, &event, &state));
    CHECK(!memcmp(&state, &snapshot, sizeof state));
    event = key_event(SOURCE_GAMEPAD, KEY_DOWN, KEY_A, -1);
    CHECK(!dm_gamepad_event_apply(&ctx, &event, &state));
    CHECK(!memcmp(&state, &snapshot, sizeof state));
    event = motion_event(SOURCE_GAMEPAD, 5);
    CHECK(!dm_gamepad_event_apply(&ctx, &event, &state));
    CHECK(!memcmp(&state, &snapshot, sizeof state));
    event = motion_event(SOURCE_GAMEPAD, MOTION_MOVE);
    event.axis[AXIS_X] = NAN;
    CHECK(!dm_gamepad_event_apply(&ctx, &event, &state));
    CHECK(!memcmp(&state, &snapshot, sizeof state));
    event = motion_event(SOURCE_GAMEPAD, MOTION_MOVE);
    event.axis[AXIS_Z] = INFINITY;
    CHECK(!dm_gamepad_event_apply(&ctx, &event, &state));
    CHECK(!memcmp(&state, &snapshot, sizeof state));
    event = motion_event(SOURCE_GAMEPAD, MOTION_MOVE);
    event.axis[AXIS_Y] = 1.5f;
    CHECK(!dm_gamepad_event_apply(&ctx, &event, &state));
    CHECK(!memcmp(&state, &snapshot, sizeof state));
    event = motion_event(SOURCE_GAMEPAD, MOTION_MOVE);
    event.axis[AXIS_LTRIGGER] = -0.1f;
    CHECK(!dm_gamepad_event_apply(&ctx, &event, &state));
    CHECK(!memcmp(&state, &snapshot, sizeof state));
    event = motion_event(SOURCE_GAMEPAD, MOTION_MOVE);
    event.axis[AXIS_RTRIGGER] = 1.1f;
    CHECK(!dm_gamepad_event_apply(&ctx, &event, &state));
    CHECK(!memcmp(&state, &snapshot, sizeof state));
    event = key_event(SOURCE_GAMEPAD, KEY_DOWN, KEY_THUMBL, 0);
    CHECK(dm_gamepad_event_apply(&ctx, &event, &state));
    CHECK((state.buttons & (DM_BTN_X | DM_BTN_LS)) == (DM_BTN_X | DM_BTN_LS));
    event = key_event(SOURCE_GAMEPAD, KEY_DOWN, KEY_L2, 0);
    CHECK(dm_gamepad_event_apply(&ctx, &event, &state));
    CHECK((state.buttons & DM_BTN_ZL) != 0);

    printf("PASS gamepad events: %d checks\n", checks);
    return 0;
}
