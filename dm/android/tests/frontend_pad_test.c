// Owned boundary checks for the actual Android HAT/trigger mapping.
// Neutral, diagonal, and exact threshold values are exercised directly; no
// duplicate/mirror implementation is used.
#include "../native/frontend_pad.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    assert(dm_pad_hat_buttons(0, 0) == 0);
    assert(dm_pad_hat_buttons(0.4f, -0.4f) == 0);
    assert(dm_pad_hat_buttons(-1, -1) == (DM_BTN_LEFT | DM_BTN_UP));
    assert(dm_pad_hat_buttons(1, 1) == (DM_BTN_RIGHT | DM_BTN_DOWN));
    assert(dm_pad_hat_buttons(-0.5f, 0) == DM_BTN_LEFT);
    assert(dm_pad_hat_buttons(0.5f, 0) == DM_BTN_RIGHT);
    assert(dm_pad_hat_buttons(0, -0.5f) == DM_BTN_UP);
    assert(dm_pad_hat_buttons(0, 0.5f) == DM_BTN_DOWN);
    assert(dm_pad_trigger_buttons(0, 0) == 0);
    assert(dm_pad_trigger_buttons(0.49f, 0.49f) == 0);
    assert(dm_pad_trigger_buttons(0.5f, 0) == DM_BTN_ZL);
    assert(dm_pad_trigger_buttons(0, 0.5f) == DM_BTN_ZR);
    assert(dm_pad_trigger_buttons(0.5f, 0.5f) == (DM_BTN_ZL | DM_BTN_ZR));
    puts("frontend_pad: neutral/diagonal/boundary PASS");
    return 0;
}
