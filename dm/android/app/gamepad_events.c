#include "gamepad_events.h"
#include "dm_input.h"

#include <math.h>
#include <stddef.h>

#ifndef __ANDROID__
/* Host-test boundary: these mirror the installed NDK r29 android/input.h and
   android/keycodes.h values used below. Host tests supply the getter stubs. */
#define AINPUT_EVENT_TYPE_KEY 1
#define AINPUT_EVENT_TYPE_MOTION 2
#define AINPUT_SOURCE_GAMEPAD 0x00000401
#define AINPUT_SOURCE_JOYSTICK 0x01000010
#define AKEY_EVENT_ACTION_DOWN 0
#define AKEY_EVENT_ACTION_UP 1
#define AKEY_EVENT_ACTION_MULTIPLE 2
#define AMOTION_EVENT_ACTION_MASK 0xff
#define AMOTION_EVENT_ACTION_DOWN 0
#define AMOTION_EVENT_ACTION_UP 1
#define AMOTION_EVENT_ACTION_MOVE 2
#define AMOTION_EVENT_ACTION_CANCEL 3
#define AKEYCODE_DPAD_UP 19
#define AKEYCODE_DPAD_DOWN 20
#define AKEYCODE_DPAD_LEFT 21
#define AKEYCODE_DPAD_RIGHT 22
#define AKEYCODE_BUTTON_A 96
#define AKEYCODE_BUTTON_B 97
#define AKEYCODE_BUTTON_X 99
#define AKEYCODE_BUTTON_Y 100
#define AKEYCODE_BUTTON_L1 102
#define AKEYCODE_BUTTON_R1 103
#define AKEYCODE_BUTTON_L2 104
#define AKEYCODE_BUTTON_R2 105
#define AKEYCODE_BUTTON_THUMBL 106
#define AKEYCODE_BUTTON_THUMBR 107
#define AKEYCODE_BUTTON_START 108
#define AKEYCODE_BUTTON_SELECT 109
#define AKEYCODE_DPAD_UP_LEFT 268
#define AKEYCODE_DPAD_DOWN_LEFT 269
#define AKEYCODE_DPAD_UP_RIGHT 270
#define AKEYCODE_DPAD_DOWN_RIGHT 271
#define AMOTION_EVENT_AXIS_X 0
#define AMOTION_EVENT_AXIS_Y 1
#define AMOTION_EVENT_AXIS_Z 11
#define AMOTION_EVENT_AXIS_RZ 14
#define AMOTION_EVENT_AXIS_HAT_X 15
#define AMOTION_EVENT_AXIS_HAT_Y 16
#define AMOTION_EVENT_AXIS_LTRIGGER 17
#define AMOTION_EVENT_AXIS_RTRIGGER 18
int32_t AInputEvent_getType(const AInputEvent *event);
int32_t AInputEvent_getSource(const AInputEvent *event);
int32_t AKeyEvent_getAction(const AInputEvent *event);
int32_t AKeyEvent_getKeyCode(const AInputEvent *event);
int32_t AKeyEvent_getRepeatCount(const AInputEvent *event);
int32_t AMotionEvent_getAction(const AInputEvent *event);
float AMotionEvent_getAxisValue(const AInputEvent *event, int32_t axis, size_t pointer_index);
#endif

#define DM_GAMEPAD_AXIS_DEADZONE 0.10f
#define DM_GAMEPAD_TRIGGER_THRESHOLD 0.50f
#define DM_GAMEPAD_DPAD_BITS \
    (DM_BTN_LEFT | DM_BTN_UP | DM_BTN_RIGHT | DM_BTN_DOWN)
#define DM_GAMEPAD_TRIGGER_BITS (DM_BTN_ZL | DM_BTN_ZR)

static int recognized_source(int32_t source) {
    return dm_gamepad_source_is_gamepad(source);
}

int dm_gamepad_source_is_gamepad(int32_t source) {
    return (source & AINPUT_SOURCE_GAMEPAD) == AINPUT_SOURCE_GAMEPAD ||
           (source & AINPUT_SOURCE_JOYSTICK) == AINPUT_SOURCE_JOYSTICK;
}

static uint64_t key_button(int32_t keycode) {
    switch (keycode) {
    case AKEYCODE_BUTTON_A: return DM_BTN_A;
    case AKEYCODE_BUTTON_B: return DM_BTN_B;
    case AKEYCODE_BUTTON_X: return DM_BTN_X;
    case AKEYCODE_BUTTON_Y: return DM_BTN_Y;
    case AKEYCODE_BUTTON_L1: return DM_BTN_L;
    case AKEYCODE_BUTTON_R1: return DM_BTN_R;
    case AKEYCODE_BUTTON_L2: return DM_BTN_ZL;
    case AKEYCODE_BUTTON_R2: return DM_BTN_ZR;
    case AKEYCODE_BUTTON_THUMBL: return DM_BTN_LS;
    case AKEYCODE_BUTTON_THUMBR: return DM_BTN_RS;
    case AKEYCODE_BUTTON_START: return DM_BTN_PLUS;
    case AKEYCODE_BUTTON_SELECT: return DM_BTN_MINUS;
    case AKEYCODE_DPAD_UP: return DM_BTN_UP;
    case AKEYCODE_DPAD_DOWN: return DM_BTN_DOWN;
    case AKEYCODE_DPAD_LEFT: return DM_BTN_LEFT;
    case AKEYCODE_DPAD_RIGHT: return DM_BTN_RIGHT;
    case AKEYCODE_DPAD_UP_LEFT: return DM_BTN_UP | DM_BTN_LEFT;
    case AKEYCODE_DPAD_DOWN_LEFT: return DM_BTN_DOWN | DM_BTN_LEFT;
    case AKEYCODE_DPAD_UP_RIGHT: return DM_BTN_UP | DM_BTN_RIGHT;
    case AKEYCODE_DPAD_DOWN_RIGHT: return DM_BTN_DOWN | DM_BTN_RIGHT;
    default: return 0;
    }
}

static int32_t stick_value(float value) {
    float scaled = value * (float)DM_STICK_MAX;
    if (scaled > (float)DM_STICK_MAX) scaled = (float)DM_STICK_MAX;
    if (scaled < -(float)DM_STICK_MAX) scaled = -(float)DM_STICK_MAX;
    return (int32_t)lroundf(scaled);
}

static int32_t deadzone_value(float value) {
    if (fabsf(value) <= DM_GAMEPAD_AXIS_DEADZONE) return 0;
    return stick_value(value);
}

static int apply_key(dm_gamepad_context *context, const AInputEvent *event,
                     dm_npad_state *state) {
    int32_t action = AKeyEvent_getAction(event);
    int32_t repeat = AKeyEvent_getRepeatCount(event);
    uint64_t mask = key_button(AKeyEvent_getKeyCode(event));
    if (!mask || (action != AKEY_EVENT_ACTION_DOWN && action != AKEY_EVENT_ACTION_UP) ||
        repeat < 0)
        return 0;
    dm_gamepad_context next_context = *context;
    if (action == AKEY_EVENT_ACTION_DOWN)
        next_context.digital_buttons |= mask;   /* repeated DOWN is idempotent */
    else
        next_context.digital_buttons &= ~mask;
    dm_npad_state next = *state;
    uint64_t old = context->digital_buttons | context->analog_buttons;
    next.buttons = (state->buttons & ~old) |
                   next_context.digital_buttons | next_context.analog_buttons;
    *context = next_context;
    *state = next;
    return 1;
}

static int axis_in_range(float value, float low, float high) {
    return isfinite(value) && value >= low && value <= high;
}

static int apply_motion(dm_gamepad_context *context, const AInputEvent *event,
                        dm_npad_state *state) {
    int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
    if (action != AMOTION_EVENT_ACTION_DOWN && action != AMOTION_EVENT_ACTION_MOVE &&
        action != AMOTION_EVENT_ACTION_UP && action != AMOTION_EVENT_ACTION_CANCEL)
        return 0;

    dm_gamepad_context next_context = *context;
    dm_npad_state next = *state;
    uint64_t old = context->digital_buttons | context->analog_buttons;
    if (action == AMOTION_EVENT_ACTION_CANCEL) {
        next_context.digital_buttons = 0;
        next_context.analog_buttons = 0;
        next.buttons = state->buttons & ~old;
        next.lx = next.ly = next.rx = next.ry = 0;
        *context = next_context;
        *state = next;
        return 1;
    }
    if (action == AMOTION_EVENT_ACTION_UP) {
        /* Motion UP releases only analog axes/hat/triggers; digital key buttons
           remain owned by the KeyEvent channel. */
        next_context.analog_buttons = 0;
        next.buttons = (state->buttons & ~old) |
                       next_context.digital_buttons | next_context.analog_buttons;
        next.lx = next.ly = next.rx = next.ry = 0;
        *context = next_context;
        *state = next;
        return 1;
    }

    float x = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_X, 0);
    float y = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Y, 0);
    float z = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Z, 0);
    float rz = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RZ, 0);
    float hat_x = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HAT_X, 0);
    float hat_y = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HAT_Y, 0);
    float lt = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_LTRIGGER, 0);
    float rt = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RTRIGGER, 0);
    if (!axis_in_range(x, -1.0f, 1.0f) || !axis_in_range(y, -1.0f, 1.0f) ||
        !axis_in_range(z, -1.0f, 1.0f) || !axis_in_range(rz, -1.0f, 1.0f) ||
        !axis_in_range(hat_x, -1.0f, 1.0f) || !axis_in_range(hat_y, -1.0f, 1.0f) ||
        !axis_in_range(lt, 0.0f, 1.0f) || !axis_in_range(rt, 0.0f, 1.0f))
        return 0;

    /* Android joystick Y is down-positive; npad Y is up-positive. Z/RZ are
       commonly the second stick; HAT is the D-pad. */
    next.lx = deadzone_value(x);
    next.ly = deadzone_value(-y);
    next.rx = deadzone_value(z);
    next.ry = deadzone_value(-rz);
    next_context.analog_buttons = 0;
    if (hat_x <= -0.5f) next_context.analog_buttons |= DM_BTN_LEFT;
    if (hat_x >= 0.5f) next_context.analog_buttons |= DM_BTN_RIGHT;
    if (hat_y <= -0.5f) next_context.analog_buttons |= DM_BTN_UP;
    if (hat_y >= 0.5f) next_context.analog_buttons |= DM_BTN_DOWN;
    if (lt > DM_GAMEPAD_TRIGGER_THRESHOLD) next_context.analog_buttons |= DM_BTN_ZL;
    if (rt > DM_GAMEPAD_TRIGGER_THRESHOLD) next_context.analog_buttons |= DM_BTN_ZR;
    next.buttons = (state->buttons & ~old) |
                   next_context.digital_buttons | next_context.analog_buttons;
    *context = next_context;
    *state = next;
    return 1;
}

void dm_gamepad_context_init(dm_gamepad_context *context) {
    if (context) *context = (dm_gamepad_context){0};
}

int dm_gamepad_event_apply(dm_gamepad_context *context, const AInputEvent *event,
                           dm_npad_state *state) {
    if (!context || !event || !state) return 0;
    int32_t type = AInputEvent_getType(event);
    if (!recognized_source(AInputEvent_getSource(event))) return 0;
    if (type == AINPUT_EVENT_TYPE_KEY) return apply_key(context, event, state);
    if (type == AINPUT_EVENT_TYPE_MOTION) return apply_motion(context, event, state);
    return 0;
}
