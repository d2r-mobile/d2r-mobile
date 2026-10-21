#include "live_touch_items.h"
#define LIVE_ITEM_MAX_POINTER 16

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define ITEM_ANSWER_TIMEOUT_NS 250000000LL

struct dm_live_touch_items {
    dm_live_touch *touch;
    dm_live_touch_items_host host;
    dm_live_touch_item_snapshot last;
    uint64_t scene_epoch, world_epoch, ui_identity;
    int have_context, navigation;
    int active;
    int32_t pointer_id;
    int slot;
    int item_known, item_kind;
    unsigned item_seq;
    int drop_known, drop_kind;
    unsigned drop_seq;
    int pending_release;
    int64_t release_ns;
    float release_nx, release_ny;
    int32_t quarantine[LIVE_ITEM_MAX_POINTER];
    int quarantine_count;
};

static int quarantined(const dm_live_touch_items *items, int32_t id) {
    for (int i = 0; i < items->quarantine_count; ++i)
        if (items->quarantine[i] == id) return 1;
    return 0;
}
static void quarantine_add(dm_live_touch_items *items, int32_t id) {
    if (id < 0 || quarantined(items, id) || items->quarantine_count >= LIVE_ITEM_MAX_POINTER) return;
    items->quarantine[items->quarantine_count++] = id;
}
static void quarantine_remove(dm_live_touch_items *items, int32_t id) {
    for (int i = 0; i < items->quarantine_count; ++i) {
        if (items->quarantine[i] != id) continue;
        items->quarantine[i] = items->quarantine[--items->quarantine_count];
        return;
    }
}

static int valid_host(const dm_live_touch_items_host *host) {
    return host && host->item_query && host->item_result &&
           host->drop_query && host->drop_result && host->drop_last &&
           host->drop_point && host->origin_point;
}

dm_live_touch_items *dm_live_touch_items_create(dm_live_touch *touch,
                                                const dm_live_touch_items_host *host) {
    if (!touch || !valid_host(host)) return NULL;
    dm_live_touch_items *items = calloc(1, sizeof(*items));
    if (!items) return NULL;
    items->touch = touch;
    items->host = *host;
    items->slot = -1;
    items->pointer_id = -1;
    dm_live_touch_item_snapshot_get(touch, &items->last);
    return items;
}

void dm_live_touch_items_destroy(dm_live_touch_items *items) {
    free(items);
}

static void discard_pending(dm_live_touch_items *items) {
    items->active = 0;
    items->pending_release = 0;
    items->pointer_id = -1;
    items->slot = -1;
    items->item_known = 0;
    items->drop_known = 0;
    items->item_seq = 0;
    items->drop_seq = 0;
    items->quarantine_count = 0;
}

int dm_live_touch_items_set_context(dm_live_touch_items *items,
                                    uint64_t scene_epoch, uint64_t world_epoch,
                                    uint64_t ui_identity, int navigation) {
    if (!items) return 0;
    int changed = items->have_context &&
        (items->scene_epoch != scene_epoch || items->world_epoch != world_epoch ||
         items->ui_identity != ui_identity || items->navigation != !!navigation);
    if (changed) discard_pending(items);
    items->have_context = 1;
    items->scene_epoch = scene_epoch;
    items->world_epoch = world_epoch;
    items->ui_identity = ui_identity;
    items->navigation = !!navigation;
    return changed;
}

static float clamp_norm(float v) {
    if (!isfinite(v) || v < 0) return 0;
    if (v > 0.999f) return 0.999f;
    return v;
}

static int normalize_item_kind(int kind);

static void issue_item_query(dm_live_touch_items *items, float nx, float ny) {
    items->item_seq = items->host.item_query(items->host.context, nx, ny);
    items->item_known = 0;
    items->item_kind = 0;
}

static void poll_item_hit(dm_live_touch_items *items) {
    if (!items->active || items->pending_release || items->item_known) return;
    int result = items->host.item_result(items->host.context, items->item_seq);
    if (result < 0) return;
    items->item_kind = normalize_item_kind(result);
    items->item_known = 1;
    dm_live_touch_apply_item_hit(items->touch, items->slot, items->item_kind);
}

static void issue_drop_query(dm_live_touch_items *items, float nx, float ny, float finger_ny) {
    float lift = dm_live_touch_pointer_lift_norm(items->touch);
    float lifted = ny > lift ? ny - lift : 0;
    items->drop_seq = items->host.drop_query(items->host.context, nx, lifted, finger_ny);
    items->drop_known = 0;
    items->drop_kind = 0;
}

static int normalize_item_kind(int kind) {
    switch (kind) {
    case 0: case 1: case 2: case 3: case 6: case 11:
        // 0 = an ordinary panel widget (including a skill-tree icon). It is a
        // known classification, not unknown; the core must still reach its
        // tap_move/tap_button branch. Only the explicit blocked/unsupported
        // classes below become no-click.
        return kind;
    default:
        // 7/8/10 are real host categories for vendor/stack/blocked branches
        // that this inventory slice does not own. Refuse them as "no click"
        // instead of accidentally buying, selling, or taking a stack.
        // Unknown results/timeouts are also no-click.
        return 6;
    }
}

static void resolve_release(dm_live_touch_items *items, int64_t now_ns) {
    if (!items->pending_release) return;
    int timeout = now_ns - items->release_ns >= ITEM_ANSWER_TIMEOUT_NS;
    if (!items->item_known) {
        int result = items->host.item_result(items->host.context, items->item_seq);
        if (result >= 0) {
            items->item_kind = normalize_item_kind(result);
            items->item_known = 1;
        } else if (timeout) {
            items->item_kind = 6;
            items->item_known = 1;
        }
    }
    if (!items->item_known) return;

    if (dm_live_touch_pointer_dragged_for_id(items->touch, items->pointer_id)) {
        if (!items->drop_known) {
            int result = items->host.drop_result(items->host.context, items->drop_seq);
            if (result >= 0) {
                items->drop_kind = result;
                items->drop_known = 1;
            } else if (timeout) {
                int last = items->host.drop_last(items->host.context);
                items->drop_kind = last >= 0 ? last : 0;
                items->drop_known = 1;
            }
        }
        if (!items->drop_known) return;
        if (items->drop_kind == 5) {
            float nx, ny;
            if (items->host.drop_point(items->host.context, &nx, &ny))
                dm_live_touch_apply_drop(items->touch, 5, clamp_norm(nx), clamp_norm(ny));
            else if (items->host.origin_point(items->host.context, &nx, &ny))
                dm_live_touch_apply_drop(items->touch, 5, clamp_norm(nx), clamp_norm(ny));
            else {
                // No authoritative host point: fail closed instead of using the
                // finger as a legal drop location.
                dm_live_touch_cancel_all(items->touch, items->release_ns);
                discard_pending(items);
                return;
            }
        } else if (items->drop_kind == 0) {
            float nx, ny;
            if (items->host.origin_point(items->host.context, &nx, &ny))
                dm_live_touch_apply_drop(items->touch, 5, clamp_norm(nx), clamp_norm(ny));
            else {
                dm_live_touch_cancel_all(items->touch, items->release_ns);
                discard_pending(items);
                return;
            }
        } else {
            dm_live_touch_apply_drop(items->touch, items->drop_kind, 0, 0);
        }
    }
    dm_live_touch_apply_item_hit(items->touch, items->slot, items->item_kind);
    dm_live_touch_finish_pointer(items->touch, items->pointer_id, items->release_ns);
    discard_pending(items);
}

int dm_live_touch_items_event(dm_live_touch_items *items,
                              int action, int action_index,
                              int pointer_count, const int32_t *ids,
                              const float *x_px, const float *y_px,
                              int viewport_width, int viewport_height,
                              float picture_left, float picture_top,
                              float picture_width, float picture_height,
                              int64_t time_ns) {
    if (!items) return 0;
    dm_live_touch *touch = items->touch;
    if (!touch) return 0;

    if (!items->navigation || !dm_live_touch_navigation(touch)) {
        discard_pending(items);
        return dm_live_touch_event(touch, action, action_index, pointer_count,
                                   ids, x_px, y_px, viewport_width, viewport_height,
                                   picture_left, picture_top, picture_width, picture_height,
                                   time_ns);
    }

    if (action == DM_LIVE_ACTION_DOWN || action == DM_LIVE_ACTION_POINTER_DOWN) {
        if (action_index < 0 || action_index >= pointer_count) return 0;
        int32_t id = ids[action_index];
        if (items->active || items->pending_release) {
            // A second finger must never steal or discard the first pointer's
            // async item/release verdict. Quarantine it until its own UP.
            quarantine_add(items, id);
            return 1;
        }
        if (quarantined(items, id)) quarantine_remove(items, id);
        discard_pending(items);
        int changed = dm_live_touch_event(touch, action, action_index, pointer_count,
                                          ids, x_px, y_px, viewport_width, viewport_height,
                                          picture_left, picture_top, picture_width, picture_height,
                                          time_ns);
        if (!changed || picture_width <= 0 || picture_height <= 0) return changed;
        int slot = dm_live_touch_pointer_slot_for_id(touch, id);
        if (slot < 0 || slot != dm_live_touch_pointer_slot(touch)) return changed;
        float nx = clamp_norm((x_px[action_index] - picture_left) / picture_width);
        float ny = clamp_norm((y_px[action_index] - picture_top) / picture_height);
        items->active = 1;
        items->pointer_id = id;
        items->slot = slot;
        issue_item_query(items, nx, ny);
        return changed;
    }

    if (action == DM_LIVE_ACTION_MOVE) {
        poll_item_hit(items);
        int changed;
        if (items->quarantine_count) {
            int idx = -1;
            for (int i = 0; i < pointer_count; ++i) if (ids[i] == items->pointer_id) { idx = i; break; }
            if (idx < 0) return 1;
            int32_t owner = ids[idx];
            float ox = x_px[idx], oy = y_px[idx];
            changed = dm_live_touch_event(touch, action, -1, 1, &owner, &ox, &oy,
                                          viewport_width, viewport_height, picture_left, picture_top,
                                          picture_width, picture_height, time_ns);
        } else {
            changed = dm_live_touch_event(touch, action, action_index, pointer_count,
                                          ids, x_px, y_px, viewport_width, viewport_height,
                                          picture_left, picture_top, picture_width, picture_height,
                                          time_ns);
        }
        if (!items->active) return changed;
        int idx = -1;
        for (int i = 0; i < pointer_count; ++i) if (ids[i] == items->pointer_id) { idx = i; break; }
        if (idx >= 0 && dm_live_touch_pointer_dragged_for_id(touch, items->pointer_id) &&
            picture_width > 0 && picture_height > 0) {
            float nx = clamp_norm((x_px[idx] - picture_left) / picture_width);
            float ny = clamp_norm((y_px[idx] - picture_top) / picture_height);
            issue_drop_query(items, nx, ny, ny);
        }
        return changed;
    }

    if (action == DM_LIVE_ACTION_UP || action == DM_LIVE_ACTION_POINTER_UP) {
        if (action_index < 0 || action_index >= pointer_count) return 0;
        if (quarantined(items, ids[action_index])) {
            quarantine_remove(items, ids[action_index]);
            return 1;
        }
        if (!items->active || ids[action_index] != items->pointer_id ||
            picture_width <= 0 || picture_height <= 0) {
            return dm_live_touch_event(touch, action, action_index, pointer_count,
                                       ids, x_px, y_px, viewport_width, viewport_height,
                                       picture_left, picture_top, picture_width, picture_height,
                                       time_ns);
        }
        items->release_ns = time_ns;
        items->release_nx = clamp_norm((x_px[action_index] - picture_left) / picture_width);
        items->release_ny = clamp_norm((y_px[action_index] - picture_top) / picture_height);
        if (dm_live_touch_pointer_dragged_for_id(touch, items->pointer_id)) {
            issue_drop_query(items, items->release_nx, items->release_ny, items->release_ny);
        }
        items->pending_release = 1;
        return 1;
    }

    if (action == DM_LIVE_ACTION_CANCEL) {
        discard_pending(items);
        return dm_live_touch_cancel_all(touch, time_ns);
    }
    return dm_live_touch_event(touch, action, action_index, pointer_count,
                               ids, x_px, y_px, viewport_width, viewport_height,
                               picture_left, picture_top, picture_width, picture_height,
                               time_ns);
}

void dm_live_touch_items_tick(dm_live_touch_items *items, int64_t time_ns) {
    if (!items || time_ns < 0) return;
    poll_item_hit(items);
    resolve_release(items, time_ns);
}

int dm_live_touch_items_consume(dm_live_touch_items *items,
                                uint64_t scene_epoch, uint64_t world_epoch,
                                uint64_t ui_identity,
                                const dm_live_touch_items_actions *actions) {
    if (!items || !actions) return 0;
    dm_live_touch_item_snapshot current;
    dm_live_touch_item_snapshot_get(items->touch, &current);
    int matches = items->have_context && items->scene_epoch == scene_epoch &&
                  items->world_epoch == world_epoch && items->ui_identity == ui_identity;
    if (!matches) {
        items->last = current;
        return 0;
    }
    int fired = 0;
    if (current.item_tap_seq != items->last.item_tap_seq && actions->item_tap) {
        actions->item_tap(actions->context); fired++;
    }
    if (current.ident_seq != items->last.ident_seq && actions->identify) {
        actions->identify(actions->context); fired++;
    }
    if (current.use_seq != items->last.use_seq && actions->use) {
        actions->use(actions->context); fired++;
    }
    if (current.long_press_seq != items->last.long_press_seq && actions->long_press) {
        actions->long_press(actions->context); fired++;
    }
    if (current.tab_tap_seq != items->last.tab_tap_seq && actions->tab_go) {
        actions->tab_go(actions->context); fired++;
    }
    if (current.stack_take_seq != items->last.stack_take_seq && actions->stack_take) {
        actions->stack_take(actions->context); fired++;
    }
    if (current.mouse_click_seq != items->last.mouse_click_seq && actions->mouse_click) {
        actions->mouse_click(actions->context, current.mouse_click_x, current.mouse_click_y); fired++;
    }
    items->last = current;
    return fired;
}

void dm_live_touch_items_cancel(dm_live_touch_items *items) {
    if (!items) return;
    discard_pending(items);
}
