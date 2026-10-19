// Android live-owner polling policy. The owner must observe provider/scene/
// panel changes even when no touch event or deferred gesture work exists.
#pragma once

#define DM_LIVE_OWNER_ACTIVE_MS 16
#define DM_LIVE_OWNER_IDLE_MS 100

static inline int dm_live_owner_interval_ms(int needs_work) {
    return needs_work ? DM_LIVE_OWNER_ACTIVE_MS : DM_LIVE_OWNER_IDLE_MS;
}

static inline int dm_live_owner_should_stop(int live_present, int mode_enabled,
                                             int input_attached, int activity_resumed,
                                             int window_present, int destroyed) {
    return !live_present || !mode_enabled || !input_attached || !activity_resumed ||
           !window_present || destroyed;
}

static inline int dm_live_owner_can_start(int live_present, int mode_enabled,
                                           int input_attached, int activity_resumed,
                                           int window_present, int destroyed,
                                           int geometry_valid) {
    return !dm_live_owner_should_stop(live_present, mode_enabled, input_attached,
                                      activity_resumed, window_present, destroyed) &&
           geometry_valid;
}
