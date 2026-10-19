#pragma once

// Pure Android pad-axis mappings shared by the NativeActivity frontend and its
// owned boundary test. The current host dm_input.h has the same button bits but
// not these two helpers, so keep the minimal reference mapping here.
#include <stdint.h>
#include "../../host/dm_input.h"

static inline uint64_t dm_pad_hat_buttons(float hat_x, float hat_y) {
    uint64_t buttons = 0;
    if (hat_x <= -0.5f) buttons |= DM_BTN_LEFT; else if (hat_x >= 0.5f) buttons |= DM_BTN_RIGHT;
    if (hat_y <= -0.5f) buttons |= DM_BTN_UP; else if (hat_y >= 0.5f) buttons |= DM_BTN_DOWN;
    return buttons;
}

static inline uint64_t dm_pad_trigger_buttons(float left, float right) {
    uint64_t buttons = 0;
    if (left >= 0.5f) buttons |= DM_BTN_ZL;
    if (right >= 0.5f) buttons |= DM_BTN_ZR;
    return buttons;
}
