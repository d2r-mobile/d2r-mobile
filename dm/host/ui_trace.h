// Opt-in, bounded front-end touch tracing. This is diagnostic-only: it never
// changes touch phases, coordinates or host mouse delivery.
#pragma once
#include <stdatomic.h>
#include <stdint.h>

typedef struct {
    atomic_int enabled;
    atomic_uint emitted[4];
} dm_ui_trace_state;

enum {
    DM_UI_TRACE_PRODUCER = 0,
    DM_UI_TRACE_TOUCH,
    DM_UI_TRACE_PUMP,
    DM_UI_TRACE_CANCEL,
    DM_UI_TRACE_STAGES
};

#define DM_UI_TRACE_LIMIT 32u

static inline void dm_ui_trace_reset(dm_ui_trace_state *s, int on) {
    atomic_store(&s->enabled, on ? 1 : 0);
    for (int i = 0; i < DM_UI_TRACE_STAGES; i++) atomic_store(&s->emitted[i], 0);
}

static inline int dm_ui_trace_take_stage(dm_ui_trace_state *s, int stage) {
    if (!atomic_load(&s->enabled) || stage < 0 || stage >= DM_UI_TRACE_STAGES) return 0;
    unsigned n = atomic_fetch_add(&s->emitted[stage], 1);
    return n < DM_UI_TRACE_LIMIT;
}

static inline int dm_ui_trace_take(dm_ui_trace_state *s) {
    return dm_ui_trace_take_stage(s, DM_UI_TRACE_PRODUCER);
}

// Touch phase 1 is movement while the finger is held; it is intentionally not
// logged so a long hold cannot consume the terminal down/up receipts.
static inline int dm_ui_trace_touch_take(dm_ui_trace_state *s, int phase) {
    if (phase == 1) return 0;
    return dm_ui_trace_take_stage(s, DM_UI_TRACE_TOUCH);
}

// Pump tracing is transition-based; held frames do not consume the stage budget.
static inline int dm_ui_trace_pump_take(dm_ui_trace_state *s, int *last_phase, int phase) {
    int trace = (phase != *last_phase) && dm_ui_trace_take_stage(s, DM_UI_TRACE_PUMP);
    *last_phase = phase;
    return trace;
}
