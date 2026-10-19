// Opt-in, bounded front-end touch tracing. This is diagnostic-only: it never
// changes touch phases, coordinates or host mouse delivery.
#pragma once
#include <stdatomic.h>
#include <stdint.h>

typedef struct {
    atomic_int enabled;
    atomic_uint generation;
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
    atomic_store(&s->generation, 0);
    for (int i = 0; i < DM_UI_TRACE_STAGES; i++) atomic_store(&s->emitted[i], 0);
}

static inline int dm_ui_trace_arm(dm_ui_trace_state *s, unsigned generation) {
    if (!generation) { atomic_store(&s->enabled, 0); atomic_store(&s->generation, 0); return 0; }
    if (atomic_load(&s->generation) == generation) return 0;
    atomic_store(&s->generation, generation);
    for (int i = 0; i < DM_UI_TRACE_STAGES; i++) atomic_store(&s->emitted[i], 0);
    atomic_store(&s->enabled, 1);
    return 1;
}

static inline unsigned dm_ui_trace_generation(dm_ui_trace_state *s) {
    return atomic_load(&s->generation);
}

static inline int dm_ui_trace_rearm_generation(unsigned *current, unsigned generation) {
    if (generation == *current) return 0;
    *current = generation;
    return 1;
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
static inline int dm_ui_trace_pump_take(dm_ui_trace_state *s, _Atomic int *last_phase, int phase) {
    int last = atomic_load(last_phase);
    int trace = (phase != last) && dm_ui_trace_take_stage(s, DM_UI_TRACE_PUMP);
    atomic_store(last_phase, phase);
    return trace;
}

// Only a current successful snap may contribute a widget name. ui_snap can
// return 0/6 and leave g_click_name from an earlier tap; that is not provenance.
static inline const char *dm_ui_trace_snap_name(int result, const char *name) {
    return ((result == 1 || result == 2) && name && name[0]) ? name : "unknown";
}
