// Android item-panel routing over the existing dm_live_touch gesture core.
// The original host decides hit/drop legality and the original gesture core
// decides single/double/long/drag; this module only defers the release while
// the async host answer is outstanding and forwards one-shot host actions.
#pragma once
#include <stdint.h>
#include "live_touch.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dm_live_touch_items dm_live_touch_items;

typedef struct {
    void *context;
    unsigned (*item_query)(void *context, float nx, float ny);
    int (*item_result)(void *context, unsigned seq);
    unsigned (*drop_query)(void *context, float nx, float ny_lifted, float ny_finger);
    int (*drop_result)(void *context, unsigned seq);
    int (*drop_last)(void *context);
    int (*drop_point)(void *context, float *nx, float *ny);
    int (*origin_point)(void *context, float *nx, float *ny);
    int shop_supported;
} dm_live_touch_items_host;

typedef struct {
    void *context;
    void (*item_tap)(void *context);
    void (*identify)(void *context);
    void (*use)(void *context);
    void (*long_press)(void *context);
    void (*tab_go)(void *context);
    void (*stack_take)(void *context);
    void (*mouse_click)(void *context, float nx, float ny);
    void (*shop_drop)(void *context, int origin_kind, int drop_kind, float nx, float ny);
} dm_live_touch_items_actions;

dm_live_touch_items *dm_live_touch_items_create(dm_live_touch *touch,
                                                const dm_live_touch_items_host *host);
void dm_live_touch_items_destroy(dm_live_touch_items *items);
// Any scene/world/UI identity change discards an outstanding answer/release
// without ending it into the new context; the caller's live core is cancelled
// by dm_live_touch_set_context independently.
int dm_live_touch_items_set_context(dm_live_touch_items *items,
                                    uint64_t scene_epoch, uint64_t world_epoch,
                                    uint64_t ui_identity, int navigation);
// Wraps dm_live_touch_event. DOWN/MOVE pass through; a navigation-pointer UP
// is held until the host's item/drop answer is available (bounded timeout).
int dm_live_touch_items_event(dm_live_touch_items *items,
                              int action, int action_index,
                              int pointer_count, const int32_t *ids,
                              const float *x_px, const float *y_px,
                              int viewport_width, int viewport_height,
                              float picture_left, float picture_top,
                              float picture_width, float picture_height,
                              int64_t time_ns);
// Advances a held release and resolves its one-shot host answer. No action is
// called here; consume() is the single callback boundary.
void dm_live_touch_items_tick(dm_live_touch_items *items, int64_t time_ns);
int dm_live_touch_items_pointer_ready(const dm_live_touch_items *items);
// Forwards only newly produced original-core sequences. Context must still
// match; stale sequences are discarded.
int dm_live_touch_items_consume(dm_live_touch_items *items,
                                uint64_t scene_epoch, uint64_t world_epoch,
                                uint64_t ui_identity,
                                const dm_live_touch_items_actions *actions);
// Focus/cancel/input destruction: discard pending work, never click or finish
// a gesture into a changed context.
void dm_live_touch_items_cancel(dm_live_touch_items *items);

#ifdef __cplusplus
}
#endif
