#pragma once

// Native live-touch readiness transition boundary. Only a real availability
// change may notify Java; duplicate observations and a destroyed activity do not.
typedef struct {
    int available;
    int destroyed;
    unsigned changes;
} dm_live_touch_readiness;

static inline int dm_live_touch_readiness_update(dm_live_touch_readiness *state, int available) {
    if (!state || state->destroyed) return 0;
    available = !!available;
    if (state->available == available) return 0;
    state->available = available;
    state->changes++;
    return 1;
}

static inline void dm_live_touch_readiness_destroy(dm_live_touch_readiness *state) {
    if (!state) return;
    state->destroyed = 1;
    state->available = 0;
}
