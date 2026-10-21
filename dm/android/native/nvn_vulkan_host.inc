// Host drawing-switch handoff for the reference Vulkan backend.
// Requests are written on the game thread and latched when a submit is
// enqueued; the translation thread applies the latched value before replay.
// This file only owns queue-state handoff, not freeze/extrapolation drawing.
#pragma once
#include <stdatomic.h>
#include <stdint.h>

typedef struct {
    _Atomic int frz_req;
    _Atomic int force_req;
    _Atomic int frz_applied;
    _Atomic int force_applied;
} dm_nvn_host_state;

static inline void dm_nvn_host_state_init(dm_nvn_host_state *s) {
    atomic_store(&s->frz_req, -1);
    atomic_store(&s->force_req, 0);
    atomic_store(&s->frz_applied, -1);
    atomic_store(&s->force_applied, 0);
}

static inline void dm_nvn_host_state_frozen(dm_nvn_host_state *s, int on) {
    atomic_store_explicit(&s->frz_req, on != 0, memory_order_release);
}

static inline void dm_nvn_host_state_force_full(dm_nvn_host_state *s, int n) {
    atomic_store_explicit(&s->force_req, n, memory_order_release);
}

static inline uint64_t dm_nvn_host_state_latch(dm_nvn_host_state *s) {
    uint32_t frz = (uint32_t)(atomic_load_explicit(&s->frz_req, memory_order_acquire) + 1);
    uint32_t force = (uint32_t)atomic_exchange_explicit(&s->force_req, 0, memory_order_acq_rel);
    return (uint64_t)frz | ((uint64_t)force << 32);
}

static inline void dm_nvn_host_state_apply(dm_nvn_host_state *s, uint64_t value,
                                           int *frz, int *force) {
    int f = (int)(uint32_t)value - 1;
    int n = (int)(uint32_t)(value >> 32);
    atomic_store_explicit(&s->frz_applied, f, memory_order_release);
    atomic_store_explicit(&s->force_applied, n, memory_order_release);
    if (frz) *frz = f;
    if (force) *force = n;
}
