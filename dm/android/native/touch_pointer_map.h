// Android pointer-id → existing D2R touch slot mapping.
// Stable for the life of a gesture; never aliases distinct platform IDs and never reuses a live slot.
#pragma once
#include <stdint.h>

#define DM_TOUCH_POINTER_SLOTS 16

typedef struct {
    int32_t platform[DM_TOUCH_POINTER_SLOTS];   // -1 = free
    int overflow;                               // a deterministic drop happened; cleared by clear()
} dm_touch_pointer_map;

static inline void dm_touch_pointer_clear(dm_touch_pointer_map *m) {
    for (int i = 0; i < DM_TOUCH_POINTER_SLOTS; ++i) m->platform[i] = -1;
    m->overflow = 0;
}

static inline int dm_touch_pointer_slot(const dm_touch_pointer_map *m, int32_t id) {
    for (int i = 0; i < DM_TOUCH_POINTER_SLOTS; ++i) if (m->platform[i] == id) return i;
    return -1;
}

// Returns the stable slot for id, allocating the first free one. -1 means the bounded table is full:
// the caller must drop this new finger, never alias it to an active slot.
static inline int dm_touch_pointer_acquire(dm_touch_pointer_map *m, int32_t id) {
    int slot = dm_touch_pointer_slot(m, id);
    if (slot >= 0) return slot;
    for (int i = 0; i < DM_TOUCH_POINTER_SLOTS; ++i) if (m->platform[i] < 0) {
        m->platform[i] = id;
        return i;
    }
    m->overflow = 1;
    return -1;
}

static inline void dm_touch_pointer_release(dm_touch_pointer_map *m, int32_t id) {
    int slot = dm_touch_pointer_slot(m, id);
    if (slot >= 0) m->platform[slot] = -1;
}
