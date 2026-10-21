// Platform-neutral CPU window overlay and coalesced redraw scheduler for #121.
// This is the seam NativeActivity actually calls: no Android, Vulkan, timer or
// gesture state. Geometry comes from the accepted frozen HUD layout.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "touch_hud.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dm_touch_hud_schedule {
    int dirty;
    unsigned draws;
    unsigned retries;
} dm_touch_hud_schedule;

// Small explicit frame orchestration seam used by NativeActivity. Dirtiness
// starts timer-initiated frames; it never removes HUD content from a mandatory
// external redraw. Completion is acknowledged only after a successful post.
typedef struct dm_touch_hud_frame {
    dm_touch_hud_schedule schedule;
    int open;
    int composed_ok;
    int external;
    unsigned frames;
} dm_touch_hud_frame;

void dm_touch_hud_schedule_init(dm_touch_hud_schedule *schedule);
void dm_touch_hud_schedule_dirty(dm_touch_hud_schedule *schedule);
int dm_touch_hud_schedule_should_draw(const dm_touch_hud_schedule *schedule, int ready);
// success clears dirty; failure keeps it so a later tick retries.
void dm_touch_hud_schedule_draw_done(dm_touch_hud_schedule *schedule, int success);
// permanent unavailability (wrong buffer format/extent) drops pending work
// rather than retrying every timer tick.
void dm_touch_hud_schedule_suppress(dm_touch_hud_schedule *schedule);

void dm_touch_hud_frame_init(dm_touch_hud_frame *frame);
void dm_touch_hud_frame_dirty(dm_touch_hud_frame *frame);
void dm_touch_hud_frame_suppress(dm_touch_hud_frame *frame);
int dm_touch_hud_frame_timer_ready(const dm_touch_hud_frame *frame, int ready);
// external_redraw composes valid HUD regardless of dirty; timer_tick may start
// only when dirty. ready and hud_valid gate actual frame dispatch.
int dm_touch_hud_frame_begin(dm_touch_hud_frame *frame, int external_redraw,
                             int timer_tick, int ready, int hud_valid);
void dm_touch_hud_frame_composed(dm_touch_hud_frame *frame, int success);
// Returns 1 only when composed HUD and post both succeeded. A successful post
// clears dirty exactly once; failed compose/post keeps a pending retry.
int dm_touch_hud_frame_complete(dm_touch_hud_frame *frame, int post_success);

// Draw the bounded geometric overlay into RGBA8 bytes. Visible extent is
// width x height; stride is in pixels and may include padding (left untouched).
// capacity is the full byte capacity of bits. Invalid/nonfinite geometry,
// dimension/stride/capacity overflow and NULL input return 0 before any byte
// is written. Legitimate finite out-of-view centres are clipped in double
// space, never cast to int while unbounded.
int dm_touch_hud_overlay_draw(const dm_touch_hud_snapshot *snapshot,
                              uint8_t *bits, uint32_t width, uint32_t height,
                              uint32_t stride, size_t capacity);

#ifdef __cplusplus
}
#endif
