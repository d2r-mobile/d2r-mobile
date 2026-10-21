// Narrow Android physical-gamepad event conversion.
//
// The caller owns device admission/generation and supplies the prior npad
// state for one admitted device. This module only converts KeyEvent/MotionEvent
// getters into that state; it does not arbitrate sources or touch input.
#pragma once

#include <stdint.h>
#include "input.h"

#ifdef __ANDROID__
#include <android/input.h>
#else
typedef struct AInputEvent AInputEvent;
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dm_gamepad_context {
    uint64_t digital_buttons;   /* KeyEvent-owned face/shoulder/dpad/trigger bits */
    uint64_t analog_buttons;    /* MotionEvent-owned HAT/trigger bits */
} dm_gamepad_context;

// Initializes one caller-owned context per admitted device. Reinitialize on
// disconnect/replacement so no contribution leaks across devices.
void dm_gamepad_context_init(dm_gamepad_context *context);

// Returns 1 when the Android source contains complete GAMEPAD or JOYSTICK
// membership (InputEvent.isFromSource semantics for those two classes),
// including combined source masks. Pure mouse/keyboard/touch classes return 0;
// this is not a generic source-class test.
int dm_gamepad_source_is_gamepad(int32_t source);

// Returns 1 and atomically updates *state for a recognized gamepad/joystick
// KeyEvent or MotionEvent. Digital KeyEvent and analog MotionEvent button
// contributions are composed, so releasing one channel does not clear the
// other. Returns 0 and leaves both *context and *state byte-for-byte unchanged
// for unknown sources/actions/keys, malformed repeats, or non-finite/out-of-
// range axes. Unrelated fields and the caller's sampling/attributes are
// preserved.
int dm_gamepad_event_apply(dm_gamepad_context *context, const AInputEvent *event,
                           dm_npad_state *state);

#ifdef __cplusplus
}
#endif
