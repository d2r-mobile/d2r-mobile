// Physical controller vs touchscreen layout routing for the Android front end.
// Pure classification only: callers still own host transaction requests and
// input-state release. This is shared by the real input_ready path and the
// synthetic routing fixture so source classification cannot drift.
#pragma once

enum {
    DM_PADMODE_ROUTE_NONE = -1,
    DM_PADMODE_ROUTE_TOUCH = 0,
    DM_PADMODE_ROUTE_PAD = 1
};

static inline int dm_padmode_is_pad_source(int source) {
    // Match the whole source class mask: keyboards/touchscreens carry the
    // shared CLASS_BUTTON bit, so an "any overlap" test misclassifies them.
    return (((source & 0x00000401) == 0x00000401) ||
            ((source & 0x01000010) == 0x01000010)) &&
           (source & 0x00001002) == 0;
}

static inline int dm_padmode_is_touch_source(int source) {
    return (source & 0x00001002) == 0x00001002 && !dm_padmode_is_pad_source(source);
}

static inline int dm_padmode_route_event(int is_motion, int is_key, int source,
                                         int action, int key_is_pad,
                                         int pad_active) {
    if (is_motion && dm_padmode_is_touch_source(source) &&
        (action == 0 || action == 5)) return DM_PADMODE_ROUTE_TOUCH;   // DOWN / POINTER_DOWN
    if (is_motion && dm_padmode_is_pad_source(source) && action == 3)
        return DM_PADMODE_ROUTE_TOUCH;                                  // controller motion cancel / removal
    if (is_key && dm_padmode_is_pad_source(source) && key_is_pad)
        return DM_PADMODE_ROUTE_PAD;
    if (is_motion && dm_padmode_is_pad_source(source) && pad_active)
        return DM_PADMODE_ROUTE_PAD;
    return DM_PADMODE_ROUTE_NONE;
}
