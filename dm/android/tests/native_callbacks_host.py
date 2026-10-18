#!/usr/bin/env python3
"""Host callback regression for actual dm/android/app/native.c.

The test extracts named static production functions from native.c (fail closed
if missing/ambiguous), compiles that actual text against owned Android/WSI
boundary shims and the real wsi/lifecycle sources, and runs callback assertions.
No implementation is copied into the fixture. A private mutant is compiled from
a temporary copy and must fail; the repository is never modified.
"""
from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

FUNCTIONS = ["wsi_report_now", "wsi_report_progress", "wsi_report_snapshot", "wsi_report_fail",
             "host_stop",
             "touch_refresh_hud", "touch_consume", "touch_cancel", "touch_resize_window",
             "window_ready", "window_resized", "window_destroyed",
             "pause_app", "resume_app", "focus_changed", "configuration_changed",
             "input_created", "input_destroyed",
             "draw_diagnostic", "frame_timer", "input_timer",
             "motion_action", "handle_motion", "input_ready"]


def extract_function(text: str, name: str) -> str:
    matches = list(re.finditer(rf"(?m)^static\s+[^\n]*\b{re.escape(name)}\s*\(", text))
    if len(matches) != 1:
        raise SystemExit(f"fail closed: expected exactly one static {name}, found {len(matches)}")
    start = matches[0].start()
    brace = text.find("{", matches[0].end())
    if brace < 0:
        raise SystemExit(f"fail closed: no body for {name}")
    depth = 0
    in_string = False
    in_char = False
    escaped = False
    in_line = False
    in_block = False
    i = brace
    while i < len(text):
        ch = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""
        if in_line:
            if ch == "\n": in_line = False
        elif in_block:
            if ch == "*" and nxt == "/":
                in_block = False; i += 1
        elif in_string:
            if escaped: escaped = False
            elif ch == "\\": escaped = True
            elif ch == '"': in_string = False
        elif in_char:
            if escaped: escaped = False
            elif ch == "\\": escaped = True
            elif ch == "'": in_char = False
        else:
            if ch == "/" and nxt == "/": in_line = True; i += 1
            elif ch == "/" and nxt == "*": in_block = True; i += 1
            elif ch == '"': in_string = True
            elif ch == "'": in_char = True
            elif ch == "{": depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0:
                    return text[start:i + 1]
        i += 1
    raise SystemExit(f"fail closed: unbalanced body for {name}")


PRELUDE = r'''
#include "lifecycle.h"
#include "wsi_diagnostic.h"
#include "wsi_bridge.h"
#include "wsi_report.h"
#include "view_policy.h"
#include "present_policy.h"
#include "diagnostic_scene.h"
#include "input_policy.h"
#include "input_source.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef int VkResult;
#define VK_SUCCESS 0
#define DM_DIAG_TARGET_EXTENT 256u
#define DM_VK_INDEXED_TEXTURED_VERTEX_STRIDE 16u
#define WINDOW_FORMAT_RGBA_8888 1
#define ALOOPER_EVENT_ERROR 1
#define ALOOPER_EVENT_HANGUP 2
#define AINPUT_EVENT_TYPE_KEY 1
#define AINPUT_EVENT_TYPE_MOTION 2
#define AINPUT_SOURCE_TOUCHSCREEN 0x00001002
#define AINPUT_SOURCE_JOYSTICK 0x01000010
#define AINPUT_SOURCE_MOUSE 0x00002002
#define AINPUT_SOURCE_UNKNOWN 0
#define AKEY_EVENT_ACTION_DOWN 0
#define AKEY_EVENT_ACTION_UP 1
#define AKEYCODE_BACK 4
#define AMOTION_EVENT_ACTION_MASK 0xff
#define AMOTION_EVENT_ACTION_DOWN 0
#define AMOTION_EVENT_ACTION_MOVE 2
#define AMOTION_EVENT_ACTION_UP 1
#define AMOTION_EVENT_ACTION_CANCEL 3
#define AMOTION_EVENT_ACTION_POINTER_DOWN 5
#define AMOTION_EVENT_ACTION_POINTER_UP 6
#define AMOTION_EVENT_ACTION_POINTER_INDEX_MASK 0xff00
#define AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT 8
typedef struct AInputEvent {
    int type, source, keycode, action;
    int64_t time_ns;
    uint32_t pointer_count;
    int32_t ids[DM_TOUCH_MAX_POINTS];
    float xs[DM_TOUCH_MAX_POINTS], ys[DM_TOUCH_MAX_POINTS];
} AInputEvent;
typedef struct AInputQueue {
    AInputEvent event;
    int has_event, predispatch;
    int finish_calls, last_handled, pre_calls;
} AInputQueue;
typedef struct { int width, height, refs; } ANativeWindow;
typedef struct { void *instance; float density; } ANativeActivity;
typedef struct { int unused; } ALooper;
typedef struct { void *bits; int width, height, stride, format; } ANativeWindow_Buffer;
typedef struct { int device; } dm_vk;
static void dm_vk_destroy(dm_vk *vk) { if (vk) vk->device = 0; }
typedef struct { uint32_t width, height; uint8_t *bytes; size_t capacity; } dm_vk_pixels;
typedef struct { float position[2], uv[2]; } dm_vk_indexed_textured_vertex;
typedef struct {
    const dm_vk_indexed_textured_vertex *vertices;
    uint32_t vertex_count, vertex_stride;
    const uint16_t *indices; uint32_t index_count;
    const uint8_t *texture_pixels; size_t texture_size;
    uint32_t texture_width, texture_height;
    const char *vertex_spv, *fragment_spv;
} dm_vk_indexed_textured_draw;
typedef struct { int dirty; } dm_touch_hud_frame;

typedef struct {
    ANativeActivity *activity;
    dm_app_lifecycle *lifecycle;
    dm_wsi wsi;
    dm_wsi_bridge wsi_bridge;
    dm_wsi_report report;
    ANativeWindow *window;
    int resumed;
    int focused;
    int frame_fd;
    int wsi_last_logged;
    int touch_allowed;
    dm_input_arbiter *arbiter;
    AInputQueue *input;
    ALooper *looper;
    float density;
    float touch_width_pt, touch_height_pt, touch_density;
    dm_input_policy gate;
    dm_touch_hud_frame hud_frame;
    dm_android_touch_observation hud_obs;
    int hud_obs_valid;
    uint64_t last_touch_buttons;
    int32_t last_touch_lx, last_touch_ly, last_touch_rx, last_touch_ry;
    int64_t last_touch_log_ns;
    dm_view_policy view;
    dm_vk vk;
    char vertex[8], fragment[8];
} app_state;

static int touch_resize_calls, bad_touch_reads, update_calls, stop_calls, start_calls;
static int lock_calls, frame_calls, fail_open, fail_close, fail_start, frame_status = DM_WSI_OK;
static int scene_ready = 1;
static ANativeWindow *last_touch;
static dm_diag_scene_vertex scene_vertices[DM_DIAG_SCENE_VERTEX_COUNT];
static dm_diag_scene scene = {.vertices = scene_vertices,
    .vertex_count = DM_DIAG_SCENE_VERTEX_COUNT};
static dm_diag_scene_mismatch mismatch;

static void update_touch_state(app_state *s) { (void)s; update_calls++; }
static int input_ready(int fd, int events, void *data);
static void stop_frame_timer(app_state *s) { if (s && s->frame_fd >= 0) { s->frame_fd = -1; stop_calls++; } }
static int start_frame_timer(app_state *s) {
    start_calls++;
    if (fail_start) return 0;
    s->frame_fd = 1; return 1;
}
static void dm_touch_hud_frame_dirty(dm_touch_hud_frame *f) { (void)f; }
static void dm_touch_hud_frame_suppress(dm_touch_hud_frame *f) { (void)f; }
static int dm_touch_hud_frame_begin(dm_touch_hud_frame *f, int a, int b, int c, int d) {
    (void)f; (void)a; (void)b; (void)c; (void)d; return 1;
}
static int dm_touch_hud_frame_timer_ready(const dm_touch_hud_frame *f, int ready) {
    (void)f; return ready;
}
static int touch_consume_calls, input_bridge_calls, activity_finish_calls;
static int input_bridge_last_action, input_bridge_last_count;
static float activity_density(ANativeActivity *activity) {
    return activity && activity->density > 0.0f ? activity->density : 1.0f;
}
static ALooper looper_stub;
static ALooper *ALooper_forThread(void) { return &looper_stub; }
static ALooper *ALooper_prepare(int opts) { (void)opts; return &looper_stub; }
static void AInputQueue_attachLooper(AInputQueue *queue, ALooper *looper, int ident,
                                     void *callback, void *data) {
    (void)queue; (void)looper; (void)ident; (void)callback; (void)data;
}
static void AInputQueue_detachLooper(AInputQueue *queue) { (void)queue; }
static void ANativeActivity_finish(ANativeActivity *activity) { (void)activity; activity_finish_calls++; }
static int32_t AInputEvent_getType(const AInputEvent *event) { return event->type; }
static int32_t AInputEvent_getSource(const AInputEvent *event) { return event->source; }
static int32_t AKeyEvent_getAction(const AInputEvent *event) { return event->action; }
static int32_t AKeyEvent_getKeyCode(const AInputEvent *event) { return event->keycode; }
static int32_t AMotionEvent_getAction(const AInputEvent *event) { return event->action; }
static int64_t AMotionEvent_getEventTime(const AInputEvent *event) { return event->time_ns; }
static size_t AMotionEvent_getPointerCount(const AInputEvent *event) { return event->pointer_count; }
static int32_t AMotionEvent_getPointerId(const AInputEvent *event, size_t index) {
    return index < event->pointer_count ? event->ids[index] : -1;
}
static float AMotionEvent_getX(const AInputEvent *event, size_t index) {
    return index < event->pointer_count ? event->xs[index] : 0.0f;
}
static float AMotionEvent_getY(const AInputEvent *event, size_t index) {
    return index < event->pointer_count ? event->ys[index] : 0.0f;
}
static int32_t AInputQueue_getEvent(AInputQueue *queue, AInputEvent **event) {
    if (!queue->has_event) return -1;
    queue->has_event = 0;
    *event = &queue->event;
    return 0;
}
static int32_t AInputQueue_preDispatchEvent(AInputQueue *queue, AInputEvent *event) {
    (void)event;
    queue->pre_calls++;
    return queue->predispatch;
}
static void AInputQueue_finishEvent(AInputQueue *queue, AInputEvent *event, int32_t handled) {
    (void)event;
    queue->finish_calls++;
    queue->last_handled = handled;
}
void dm_input_policy_set_timer(dm_input_policy *gate, int enabled) { (void)gate; (void)enabled; }
static void stop_input_timer(app_state *s) { (void)s; }
static void dm_touch_hud_frame_composed(dm_touch_hud_frame *f, int ok) { (void)f; (void)ok; }
static int dm_touch_hud_frame_complete(dm_touch_hud_frame *f, int ok) { (void)f; (void)ok; return 1; }
static int draw_hud_overlay(app_state *s, ANativeWindow_Buffer *b) { (void)s; (void)b; return 0; }
#define LOG(...) ((void)0)
static int64_t monotonic_ns(void) { return 1000000000LL; }
static void draw_diagnostic(ANativeActivity *a, ANativeWindow *w, int external);
int dm_diag_scene_ready(const dm_app_lifecycle *l, const void *w) {
    (void)l; (void)w; return scene_ready;
}
const dm_diag_scene *dm_diag_scene_owned(void) { return &scene; }
static VkResult dm_vk_create(dm_vk *vk) { vk->device = 1; return VK_SUCCESS; }
static VkResult dm_vk_draw_indexed_textured_offscreen(dm_vk *vk,
        const dm_vk_indexed_textured_draw *d, dm_vk_pixels *p) {
    (void)vk; (void)d; (void)p; return VK_SUCCESS;
}
uint32_t dm_diag_scene_check_pixels(const dm_diag_scene *s, const uint8_t *p,
        uint32_t w, uint32_t h, size_t c, dm_diag_scene_mismatch *m) {
    (void)s; (void)p; (void)w; (void)h; (void)c; (void)m; return 1;
}
static int ANativeWindow_getWidth(ANativeWindow *w) { return w->width; }
static int ANativeWindow_getHeight(ANativeWindow *w) { return w->height; }
static int ANativeWindow_lock(ANativeWindow *w, ANativeWindow_Buffer *b, void *r) {
    (void)w; (void)b; (void)r; lock_calls++; return 0;
}
static int ANativeWindow_unlockAndPost(ANativeWindow *w) { (void)w; return 0; }
int dm_present_copy(const dm_present_source *s, const dm_present_target *t, dm_present_rect *r) {
    (void)s; (void)t; if (r) *r = (dm_present_rect){0}; return 1;
}

static int acquire(void *ctx, void *w) { (void)ctx; ((ANativeWindow *)w)->refs++; return 0; }
static void release(void *ctx, void *w) {
    (void)ctx; ANativeWindow *win = w; if (win->refs > 0) win->refs--;
}
static int load(void *ctx) { (void)ctx; return 0; }
static int stop(void *ctx) { (void)ctx; return 0; }
static int opened(void *ctx, void *w, uint32_t ww, uint32_t hh) {
    (void)ctx; (void)w; (void)ww; (void)hh; return fail_open ? DM_WSI_ERROR : 0;
}
static int closed(void *ctx) { (void)ctx; return fail_close ? 1 : 0; }
static int framed(void *ctx) { (void)ctx; frame_calls++; return frame_status; }
static int created(void *ctx, void *w) { return dm_app_lifecycle_surface_created(ctx, w); }
static int destroyed(void *ctx, void *w) { return dm_app_lifecycle_surface_destroyed(ctx, w); }
/* Mirror of the production observer: attempts are recorded only at the real open boundary. */
static void opened_observed(void *ctx, uint32_t w, uint32_t h) {
    app_state *st = ctx; dm_wsi_report_attempt(&st->report, w, h);
}

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)
'''


MAIN = r'''
static int input_cases(void) {
    app_state s;
    memset(&s, 0, sizeof s);
    s.density = 1.0f;
    s.touch_allowed = 1;
    s.arbiter = dm_input_arbiter_create(667, 375, 1.0f);
    CHECK(s.arbiter != NULL);
    ANativeActivity activity = {&s, 1.0f};
    s.activity = &activity;
    dm_app_host_ops host = {.ctx = &s, .load = load, .stop = stop,
        .surface_acquire = acquire, .surface_release = release};
    s.lifecycle = dm_app_lifecycle_create(&host, NULL, NULL);
    CHECK(s.lifecycle != NULL);
    CHECK(dm_app_lifecycle_start(s.lifecycle));
    AInputQueue q;
    memset(&q, 0, sizeof q);
    s.input = &q;
    dm_touch_diag d0, d1;
    CHECK(dm_input_arbiter_diag(s.arbiter, &d0));
    uint64_t accepted0 = d0.accepted_events;

    /* The real native entry, real arbiter, real bridge and real adapter: a
       touch DOWN must be accepted, and the first neutral boundary must be
       consumed by touch_consume so the actual output is not lost. */
    q.has_event = 1;
    q.event.type = AINPUT_EVENT_TYPE_MOTION;
    q.event.source = AINPUT_SOURCE_TOUCHSCREEN;
    q.event.action = AMOTION_EVENT_ACTION_DOWN;
    q.event.time_ns = 1;
    q.event.pointer_count = 1;
    q.event.ids[0] = 7;
    q.event.xs[0] = 100;
    q.event.ys[0] = 120;
    CHECK(input_ready(0, 0, &activity) == 1);
    CHECK(q.finish_calls == 1 && q.last_handled == 1);
    CHECK(dm_input_arbiter_source(s.arbiter) == DM_INPUT_SOURCE_TOUCH);
    CHECK(dm_input_arbiter_diag(s.arbiter, &d1) && d1.accepted_events == accepted0 + 1);
    dm_npad_state state;
    CHECK(dm_input_arbiter_read(s.arbiter, &state) && state.attributes == 1);

    /* Read-only HUD observe must not consume the held/actual output. */
    dm_touch_diag before_observe;
    CHECK(dm_input_arbiter_diag(s.arbiter, &before_observe));
    dm_android_touch_observation observation;
    for (int i = 0; i < 3; i++) {
        CHECK(dm_input_arbiter_observe(s.arbiter, &observation));
        CHECK(dm_input_arbiter_diag(s.arbiter, &d1));
    }
    CHECK(d1.accepted_events == before_observe.accepted_events);
    CHECK(d1.diagnostic_reads == before_observe.diagnostic_reads);
    CHECK(d1.state.buttons == before_observe.state.buttons);
    CHECK(dm_input_arbiter_read(s.arbiter, &state) && state.attributes == 1);

    q.finish_calls = q.last_handled = 0;
    q.has_event = 1;
    q.event.action = AMOTION_EVENT_ACTION_MOVE;
    q.event.xs[0] = 110;
    CHECK(input_ready(0, 0, &activity) == 1);
    CHECK(q.finish_calls == 1 && q.last_handled == 1);
    CHECK(dm_input_arbiter_diag(s.arbiter, &d1) && d1.accepted_events == accepted0 + 2);
    CHECK(dm_input_arbiter_read(s.arbiter, &state) && state.attributes == 1);

    q.finish_calls = q.last_handled = 0;
    q.has_event = 1;
    q.event.action = AMOTION_EVENT_ACTION_UP;
    CHECK(input_ready(0, 0, &activity) == 1);
    CHECK(q.finish_calls == 1 && q.last_handled == 1);
    CHECK(dm_input_arbiter_diag(s.arbiter, &d1) && d1.accepted_events == accepted0 + 3);

    /* Non-touch sources are filtered by the real callback before the arbiter. */
    const int rejected_sources[] = {AINPUT_SOURCE_JOYSTICK, AINPUT_SOURCE_MOUSE,
                                    AINPUT_SOURCE_UNKNOWN};
    for (size_t i = 0; i < sizeof rejected_sources / sizeof rejected_sources[0]; i++) {
        uint64_t before = d1.accepted_events;
        q.finish_calls = q.last_handled = 0;
        q.has_event = 1;
        q.event.action = AMOTION_EVENT_ACTION_DOWN;
        q.event.source = rejected_sources[i];
        CHECK(input_ready(0, 0, &activity) == 1);
        CHECK(q.finish_calls == 1 && q.last_handled == 0);
        CHECK(dm_input_arbiter_diag(s.arbiter, &d1) && d1.accepted_events == before);
    }

    /* preDispatch still bypasses finishEvent and the arbiter. */
    uint64_t before_pre = d1.accepted_events;
    q.finish_calls = q.pre_calls = q.last_handled = 0;
    q.has_event = 1;
    q.predispatch = 1;
    q.event.source = AINPUT_SOURCE_TOUCHSCREEN;
    q.event.action = AMOTION_EVENT_ACTION_DOWN;
    CHECK(input_ready(0, 0, &activity) == 1);
    CHECK(q.pre_calls == 1 && q.finish_calls == 0);
    CHECK(dm_input_arbiter_diag(s.arbiter, &d1) && d1.accepted_events == before_pre);
    q.predispatch = 0;

    /* BACK finishes exactly once and is never sent to touch. */
    q.finish_calls = q.last_handled = 0;
    q.has_event = 1;
    q.event.type = AINPUT_EVENT_TYPE_KEY;
    q.event.action = AKEY_EVENT_ACTION_UP;
    q.event.keycode = AKEYCODE_BACK;
    q.event.source = 0;
    int finish_before = activity_finish_calls;
    CHECK(input_ready(0, 0, &activity) == 1);
    CHECK(q.finish_calls == 1 && q.last_handled == 1);
    CHECK(activity_finish_calls == finish_before + 1);

    q.finish_calls = q.last_handled = 0;
    q.has_event = 1;
    q.event.action = AKEY_EVENT_ACTION_DOWN;
    finish_before = activity_finish_calls;
    CHECK(input_ready(0, 0, &activity) == 1);
    CHECK(q.finish_calls == 1 && q.last_handled == 1);
    CHECK(activity_finish_calls == finish_before);

    q.finish_calls = q.last_handled = 0;
    q.has_event = 1;
    q.event.keycode = 999;
    CHECK(input_ready(0, 0, &activity) == 1);
    CHECK(q.finish_calls == 1 && q.last_handled == 0);

    /* Lifecycle gate still consumes the queue event but cannot mutate input. */
    s.touch_allowed = 0;
    q.finish_calls = q.last_handled = 0;
    q.has_event = 1;
    q.event.type = AINPUT_EVENT_TYPE_MOTION;
    q.event.source = AINPUT_SOURCE_TOUCHSCREEN;
    q.event.action = AMOTION_EVENT_ACTION_DOWN;
    before_pre = d1.accepted_events;
    CHECK(input_ready(0, 0, &activity) == 1);
    CHECK(q.finish_calls == 1 && q.last_handled == 1);
    CHECK(dm_input_arbiter_diag(s.arbiter, &d1) && d1.accepted_events == before_pre);
    s.touch_allowed = 1;

    /* A new queue invalidates the old source without destroying/re-creating a
       second bridge. Late MOVE/UP cannot resurrect; a fresh DOWN succeeds. */
    AInputQueue q2;
    memset(&q2, 0, sizeof q2);
    input_created(&activity, &q2);
    CHECK(s.arbiter != NULL);
    CHECK(s.input == &q2);
    CHECK(dm_input_arbiter_source(s.arbiter) == DM_INPUT_SOURCE_NONE);
    CHECK(dm_input_arbiter_requires_fresh(s.arbiter));
    q2.finish_calls = q2.last_handled = 0;
    q2.has_event = 1;
    q2.event.type = AINPUT_EVENT_TYPE_MOTION;
    q2.event.source = AINPUT_SOURCE_TOUCHSCREEN;
    q2.event.action = AMOTION_EVENT_ACTION_MOVE;
    q2.event.time_ns = 2;
    q2.event.pointer_count = 1;
    q2.event.ids[0] = 7;
    q2.event.xs[0] = 120;
    q2.event.ys[0] = 130;
    CHECK(input_ready(0, 0, &activity) == 1);
    CHECK(dm_input_arbiter_source(s.arbiter) == DM_INPUT_SOURCE_NONE);
    q2.finish_calls = q2.last_handled = 0;
    q2.has_event = 1;
    q2.event.action = AMOTION_EVENT_ACTION_DOWN;
    CHECK(input_ready(0, 0, &activity) == 1);
    CHECK(dm_input_arbiter_source(s.arbiter) == DM_INPUT_SOURCE_TOUCH);

    /* Pause invalidates held touch; late MOVE/UP and a paused DOWN are rejected;
       resume requires a new DOWN. */
    q2.finish_calls = q2.last_handled = 0;
    q2.has_event = 1;
    q2.event.action = AMOTION_EVENT_ACTION_UP;
    CHECK(input_ready(0, 0, &activity) == 1);
    pause_app(&activity);
    CHECK(dm_input_arbiter_source(s.arbiter) == DM_INPUT_SOURCE_NONE);
    CHECK(dm_input_arbiter_requires_fresh(s.arbiter));
    q2.finish_calls = q2.last_handled = 0;
    q2.has_event = 1;
    q2.event.action = AMOTION_EVENT_ACTION_MOVE;
    CHECK(input_ready(0, 0, &activity) == 1);
    CHECK(dm_input_arbiter_source(s.arbiter) == DM_INPUT_SOURCE_NONE);
    q2.finish_calls = q2.last_handled = 0;
    q2.has_event = 1;
    q2.event.action = AMOTION_EVENT_ACTION_DOWN;
    CHECK(input_ready(0, 0, &activity) == 1);
    CHECK(dm_input_arbiter_source(s.arbiter) == DM_INPUT_SOURCE_NONE);
    resume_app(&activity);
    q2.finish_calls = q2.last_handled = 0;
    q2.has_event = 1;
    CHECK(input_ready(0, 0, &activity) == 1);
    CHECK(dm_input_arbiter_source(s.arbiter) == DM_INPUT_SOURCE_TOUCH);

    /* Geometry change uses the atomic arbiter path and requires fresh DOWN. */
    ANativeWindow B = {800, 600, 0};
    s.window = &B;
    touch_resize_window(&s, &B);
    CHECK(dm_input_arbiter_source(s.arbiter) == DM_INPUT_SOURCE_NONE);
    CHECK(dm_input_arbiter_requires_fresh(s.arbiter));
    CHECK(dm_input_arbiter_observe(s.arbiter, &observation));
    CHECK(observation.width_pt > 799.0f && observation.width_pt < 801.0f);
    q2.finish_calls = q2.last_handled = 0;
    q2.has_event = 1;
    q2.event.action = AMOTION_EVENT_ACTION_MOVE;
    CHECK(input_ready(0, 0, &activity) == 1);
    CHECK(dm_input_arbiter_source(s.arbiter) == DM_INPUT_SOURCE_NONE);
    q2.finish_calls = q2.last_handled = 0;
    q2.has_event = 1;
    q2.event.action = AMOTION_EVENT_ACTION_DOWN;
    CHECK(input_ready(0, 0, &activity) == 1);
    CHECK(dm_input_arbiter_source(s.arbiter) == DM_INPUT_SOURCE_TOUCH);

    /* Configuration density change is also an atomic geometry change; the
       observation and fresh requirement use the same arbiter state. */
    activity.density = 2.0f;
    configuration_changed(&activity);
    CHECK(dm_input_arbiter_source(s.arbiter) == DM_INPUT_SOURCE_NONE);
    CHECK(dm_input_arbiter_requires_fresh(s.arbiter));
    CHECK(dm_input_arbiter_observe(s.arbiter, &observation));
    CHECK(observation.density == 2.0f);
    CHECK(observation.width_pt > 399.0f && observation.width_pt < 401.0f);

    /* Queue loss keeps the owned arbiter alive and invalidates it; recreating
       the queue reuses that same arbiter. */
    dm_input_arbiter *same = s.arbiter;
    input_destroyed(&activity, &q2);
    CHECK(s.arbiter == same);
    CHECK(s.input == NULL);
    CHECK(dm_input_arbiter_source(s.arbiter) == DM_INPUT_SOURCE_NONE);
    input_created(&activity, &q2);
    CHECK(s.arbiter == same);
    CHECK(dm_input_arbiter_source(s.arbiter) == DM_INPUT_SOURCE_NONE);
    CHECK(dm_input_arbiter_requires_fresh(s.arbiter));

    dm_npad_state neutral;
    dm_input_arbiter_destroy(s.arbiter, &neutral);
    s.arbiter = NULL;
    dm_app_lifecycle_stop(s.lifecycle);
    dm_app_lifecycle_destroy(s.lifecycle);
    return 0;
}

int main(void) {
    if (input_cases()) return 1;
    app_state s;
    memset(&s, 0, sizeof s);
    s.frame_fd = -1;
    ANativeActivity activity = {&s};
    s.activity = &activity;
    dm_app_host_ops host = {.ctx = &s, .load = load, .stop = stop,
        .surface_acquire = acquire, .surface_release = release};
    s.lifecycle = dm_app_lifecycle_create(&host, NULL, NULL);
    CHECK(s.lifecycle != NULL);
    CHECK(dm_app_lifecycle_start(s.lifecycle));
    dm_wsi_ops wops = {.ctx = &s, .open = opened, .close = closed, .frame = framed};
    dm_wsi_init(&s.wsi, &wops, 1);
    dm_wsi_set_open_observer(&s.wsi, opened_observed, &s);
    dm_wsi_lifecycle_ops lops = {.ctx = s.lifecycle, .surface_created = created,
                                 .surface_destroyed = destroyed, .trace = NULL};
    dm_wsi_bridge_init(&s.wsi_bridge, &s.wsi, &lops);
    s.resumed = 1;
    s.density = 1.0f;
    s.arbiter = dm_input_arbiter_create(667, 375, s.density);
    CHECK(s.arbiter != NULL);
    dm_android_touch_observation obs;
    ANativeWindow A = {640, 480, 0}, B = {800, 600, 0}, D = {700, 500, 0};

    window_ready(&activity, &A);
    CHECK(A.refs == 1);
    CHECK(s.window == dm_app_lifecycle_surface(s.lifecycle));
    CHECK(s.frame_fd == 1);
    CHECK(dm_input_arbiter_observe(s.arbiter, &obs));
    CHECK(obs.width_pt == 640.0f && obs.height_pt == 480.0f);

    fail_open = 1;
    window_ready(&activity, &B);
    CHECK(B.refs == 1);
    CHECK(s.window == dm_app_lifecycle_surface(s.lifecycle));
    CHECK(s.window != &A);
    fail_open = 0;

    /* Report evidence on the real callback path: one attempt for this window, one honest failure,
       requested size recorded, no invented VkResult, no queued frame, exactly one final snapshot. */
    CHECK(s.report.open_failures == 1);
    CHECK(s.report.errors == 1);
    CHECK(s.report.unsupported == 0);
    CHECK(s.report.queued_presents == 0);
    CHECK(s.report.config.requested_width == 800 && s.report.config.requested_height == 600);
    CHECK(s.report.config.extent_width == 0 && s.report.config.format == -1);
    CHECK(s.report.config.stale == 1);
    CHECK(s.report.last_stage == DM_WSI_STAGE_OPEN);
    CHECK(s.report.has_code == 0 && s.report.last_code == 0);
    CHECK(s.report.snapshots == 1);

    /* Successfully reopen active B with an armed frame timer. Stale A
       callbacks must leave B's timer, size and touch geometry untouched. */
    window_ready(&activity, &B);
    CHECK(B.refs == 1);
    CHECK(s.window == &B);
    CHECK(s.frame_fd == 1);
    int devnull = open("/dev/null", O_RDONLY);
    CHECK(devnull >= 0);

    /* Actual frame_timer: successful frame keeps timer armed; sticky error stops it. */
    int frames_before = s.wsi.frames;
    frame_status = DM_WSI_OK;
    CHECK(frame_timer(devnull, 0, &s) == 1);
    CHECK(s.wsi.frames == frames_before + 1);
    CHECK(s.frame_fd == 1);
    int frame_calls_before = frame_calls;
    s.wsi.error = DM_WSI_ERROR;
    frame_timer(devnull, 0, &s);
    CHECK(frame_calls == frame_calls_before);
    CHECK(s.frame_fd == -1);
    s.wsi.error = 0;
    s.frame_fd = 1;

    int stops = stop_calls;
    uint32_t b_width = s.wsi.width;
    int frame_fd_before = s.frame_fd;
    window_destroyed(&activity, &A);
    window_resized(&activity, &A);
    CHECK(s.frame_fd == frame_fd_before);
    CHECK(stop_calls == stops);
    CHECK(dm_input_arbiter_observe(s.arbiter, &obs));
    CHECK(obs.width_pt == 800.0f && obs.height_pt == 600.0f);
    CHECK(s.wsi.width == b_width);
    CHECK(s.frame_fd == 1);
    CHECK(s.window == &B);

    /* Actual touch/HUD timer path: WSI skips CPU lock; default CPU locks. */
    s.touch_allowed = 1;
    lock_calls = 0;
    s.wsi.enabled = 1;
    CHECK(input_timer(devnull, 0, &s) == 1);
    CHECK(lock_calls == 0);
    s.wsi.enabled = 0;
    s.vertex[0] = 'v'; s.fragment[0] = 'f';
    CHECK(input_timer(devnull, 0, &s) == 1);
    CHECK(lock_calls == 1);

    /* Close failure keeps the lifecycle reference and cached live window. */
    s.wsi.enabled = 1;
    fail_close = 1;
    window_destroyed(&activity, &B);
    CHECK(B.refs == 1);
    CHECK(s.window == &B);
    fail_close = 0;

    /* Pause/pre-resume/windowless/sticky/surface-loss/error paths do not arm. */
    stop_calls = start_calls = frame_calls = 0;
    s.resumed = 0;
    window_ready(&activity, &B);
    CHECK(start_calls == 0);
    CHECK(s.frame_fd == -1);
    pause_app(&activity);
    CHECK(s.frame_fd == -1);
    CHECK(start_calls == 0);
    /* Duplicate pause callbacks during the same paused period are not counted again. */
    uint64_t paused_once = s.report.paused;
    uint64_t snapshots_once = s.report.snapshots;
    pause_app(&activity);
    CHECK(s.report.paused == paused_once);
    CHECK(s.report.snapshots == snapshots_once);
    s.resumed = 1;
    s.wsi.error = DM_WSI_ERROR;
    resume_app(&activity);
    CHECK(s.frame_fd == -1);
    CHECK(start_calls == 0);
    s.wsi.error = 0;
    pause_app(&activity);
    frame_status = DM_WSI_SURFACE_LOST;
    resume_app(&activity);
    CHECK(start_calls == 0);
    frame_status = DM_WSI_OK;
    s.wsi.error = 0;
    window_resized(&activity, &B);
    CHECK(start_calls == 0);
    frame_status = DM_WSI_ERROR;
    window_ready(&activity, &B);
    CHECK(start_calls == 0);
    frame_status = DM_WSI_OK;
    fail_start = 1;
    window_ready(&activity, &B);
    CHECK(s.frame_fd == -1);
    CHECK(start_calls > 0);
    fail_start = 0;

    /* Successfully destroyed window: a later resize is windowless and cannot arm. */
    window_ready(&activity, &D);
    CHECK(D.refs == 1);
    int d_starts = start_calls;
    window_destroyed(&activity, &D);
    CHECK(D.refs == 0);
    window_resized(&activity, &D);
    CHECK(start_calls == d_starts);

    /* Actual default CPU redraw/resize callback still reaches the lock path. */
    ANativeWindow E = {700, 500, 0};
    s.wsi.enabled = 0;
    int locks_before = lock_calls;
    window_resized(&activity, &E);
    CHECK(lock_calls == locks_before + 1);
    s.wsi.enabled = 1;

    /* WSI render entry never reaches CPU lock; default CPU path does. */
    lock_calls = 0;
    s.wsi.enabled = 1;
    draw_diagnostic(&activity, &B, 1);
    CHECK(lock_calls == 0);
    s.wsi.enabled = 0;
    s.hud_obs_valid = 0;
    s.vertex[0] = 'v'; s.fragment[0] = 'f';
    memset(&s.view, 0, sizeof s.view);
    draw_diagnostic(&activity, &B, 1);
    CHECK(lock_calls == 1);

    /* #147 repair: open attempts belong to real opens, not to redraw callbacks. */
    s.wsi.enabled = 1;
    s.wsi.error = 0;
    s.wsi.paused = 0;
    s.resumed = 1;
    ANativeWindow F = {900, 700, 0};
    window_ready(&activity, &F);
    CHECK(F.refs == 1);
    uint64_t attempts_after_open = s.report.open_attempts;
    CHECK(attempts_after_open >= 2);
    window_ready(&activity, &F);
    CHECK(s.report.open_attempts == attempts_after_open);   /* repeated redraw adds no attempt */

    /* A same-window dimension change is a legitimate REBUILD: no open failure, timer stays armed, and the
       rebuild performs exactly one real open with the new requested extent. */
    int failures_before_rebuild = (int)s.report.open_failures;
    int fd_before_rebuild = s.frame_fd;
    uint64_t attempts_before_rebuild = s.report.open_attempts;
    F.width = 1024; F.height = 768;
    window_resized(&activity, &F);
    CHECK(s.report.open_failures == failures_before_rebuild);
    CHECK(s.frame_fd != -1);
    frame_status = DM_WSI_OK;
    frame_timer(devnull, 0, &s);
    CHECK(s.report.open_attempts == attempts_before_rebuild + 1);
    CHECK(s.report.config.requested_width == 1024 && s.report.config.requested_height == 768);
    F.width = 900; F.height = 700;

    /* Present DEVICE_LOST during a rebuild: the callback fallback must not double count or erase the code. */
    s.report.attempt_failed = 0;
    dm_wsi_report_note_stage(&s.report, DM_WSI_STAGE_PRESENT, -4);
    dm_wsi_report_result(&s.report, DM_WSI_ERROR);
    int errors_before_fallback = (int)s.report.errors;
    fail_close = 1;
    s.wsi.needs_rebuild = 1;
    frame_status = DM_WSI_OK;
    window_resized(&activity, &F);
    CHECK(s.report.errors == errors_before_fallback);
    CHECK(s.report.last_stage == DM_WSI_STAGE_PRESENT);
    CHECK(s.report.has_code == 1 && s.report.last_code == -4);
    fail_close = 0;
    s.wsi.needs_rebuild = 0;

    /* A timer failure is recorded, but never counted as an open attempt or open failure. */
    s.report.attempt_failed = 0;
    uint64_t attempts_before_timer = s.report.open_attempts;
    int failures_before_timer = (int)s.report.open_failures;
    int errors_before_timer = (int)s.report.errors;
    frame_timer(devnull, ALOOPER_EVENT_ERROR, &s);
    CHECK(s.report.errors == errors_before_timer + 1);
    CHECK(s.report.open_attempts == attempts_before_timer);
    CHECK(s.report.open_failures == failures_before_timer);

    fail_close = 0;
    window_destroyed(&activity, &B);
    /* One shutdown: destroy and the lifecycle host stop are the same stop, counted once, one snapshot. */
    s.wsi.enabled = 1;
    int snapshots_before_stop = (int)s.report.snapshots;
    CHECK(host_stop(&s) == 0);
    CHECK(s.report.stops == 1);
    CHECK(host_stop(&s) == 0);
    CHECK(s.report.stops == 1);
    CHECK(s.report.snapshots == snapshots_before_stop + 1);
    dm_wsi_stop(&s.wsi);
    dm_app_lifecycle_stop(s.lifecycle);
    dm_app_lifecycle_destroy(s.lifecycle);
    dm_npad_state neutral;
    dm_input_arbiter_destroy(s.arbiter, &neutral);
    printf("native callbacks: OK (frames=%d lock=%d)\n", frame_calls, lock_calls);
    return 0;
}
'''


def build_source(native: Path) -> str:
    text = native.read_text(encoding="utf-8")
    return PRELUDE + "\n".join(extract_function(text, name) for name in FUNCTIONS) + MAIN


def compile_and_run(cc: str, app: Path, source: str, work: Path, label: str,
                    quiet: bool = False) -> tuple[str, int, str]:
    c_file = work / f"{label}.c"
    exe = work / label
    c_file.write_text(source, encoding="utf-8")
    compile_command = [cc, "-std=gnu11", "-g", "-fsanitize=address,undefined",
                       f"-I{app}",                       f"-I{app.parent / 'runtime'}",
                       f"-I{app.parent.parent / 'ios' / 'Sources'}", f"-I{app.parent / 'host'}",
                       str(c_file),
                       str(app / "wsi_bridge.c"), str(app / "wsi_diagnostic.c"),
                       str(app / "wsi_report.c"),
                       str(app / "input_source.c"), str(app / "touch_bridge.c"),
                       str(app / "view_policy.c"),
                       str(app.parent / "runtime" / "touch_game_bridge.c"),
                       str(app.parent / "runtime" / "touch_adapter.c"),
                       str(app.parent / "runtime" / "input.c"),
                       str(app.parent.parent / "ios" / "Sources" / "dm_touch_controls.c"),
                       str(app / "lifecycle.c"), "-o", str(exe)]
    built = subprocess.run(compile_command, text=True, capture_output=True)
    if built.returncode != 0:
        if not quiet:
            print(f"{label}: compile failed\n{built.stderr}", file=sys.stderr)
        return "build_failed", built.returncode, built.stderr
    environment = os.environ.copy()
    environment["ASAN_OPTIONS"] = "detect_leaks=0"
    environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
    run = subprocess.run([str(exe)], text=True, capture_output=True, env=environment)
    if not quiet:
        print(f"{label}: exit={run.returncode}")
        if run.stdout: print(run.stdout, end="")
        if run.stderr: print(run.stderr, end="", file=sys.stderr)
    text = run.stdout + run.stderr
    sanitizer = ("runtime error:" in text or "ERROR: AddressSanitizer" in text or
                 "SUMMARY: UndefinedBehaviorSanitizer" in text or
                 "SUMMARY: AddressSanitizer" in text)
    if sanitizer:
        return "sanitizer", run.returncode, text
    if run.returncode != 0:
        return "assertion", run.returncode, text
    return "ok", 0, text


def run_mutant(cc: str, app: Path, text: str, work: Path, label: str, expected: str) -> int:
    status, returncode, output = compile_and_run(cc, app, build_source_from_text(text), work, label)
    if status != "assertion" or returncode == 0:
        print(f"FAIL: mutant {label} did not produce an intended assertion failure (status={status})",
              file=sys.stderr)
        return 1
    if expected not in output:
        print(f"FAIL: mutant {label} failed outside expected assertion {expected!r}", file=sys.stderr)
        return 1
    print(f"private native mutant {label} failed as required")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", required=True)
    parser.add_argument("--app", required=True)
    parser.add_argument("--cc", default="cc")
    args = parser.parse_args()
    native = Path(args.native)
    app = Path(args.app)
    normal = build_source(native)
    source_text = native.read_text(encoding="utf-8")
    mutants = [
        ("cached-window", "s->window=dm_app_lifecycle_surface(s->lifecycle);",
         "/* mutant: omit cached-window synchronization */", "s.window"),
        ("stale-timer",
         'if(!dm_wsi_bridge_owns_window(&s->wsi_bridge,window)){LOG("wsi stale window destroy ignored");return;}\n        stop_frame_timer(s);',
         'stop_frame_timer(s);\n        if(!dm_wsi_bridge_owns_window(&s->wsi_bridge,window)){LOG("wsi stale window destroy ignored");return;}',
         "s.frame_fd"),
        ("omit-source-guard",
         "&&\n                 (AInputEvent_getSource(event)&AINPUT_SOURCE_TOUCHSCREEN)==AINPUT_SOURCE_TOUCHSCREEN",
         "",
         "q.finish_calls == 1 && q.last_handled == 0"),
        ("omit-queue-invalidation",
         "if(s->arbiter&&!dm_input_arbiter_queue_changed(s->arbiter))\n            LOG(\"arbiter queue-change invalidation rejected\");",
         "/* mutant: queue change does not retire arbiter sources */",
         "dm_input_arbiter_source(s.arbiter) == DM_INPUT_SOURCE_NONE"),
    ]
    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        status, rc, output = compile_and_run(args.cc, app, normal, work, "native-callbacks")
        if status != "ok" or rc != 0:
            print("FAIL: production callback fixture did not compile/run cleanly", file=sys.stderr)
            return 1
        # Harness self-controls: compile failure and sanitizer/UB are not accepted
        # as intended mutant assertion failures.
        status, _, _ = compile_and_run(args.cc, app, "int main(void){ this is invalid", work,
                                       "control-compile-error", quiet=True)
        if status != "build_failed":
            print("FAIL: compile-error control was not classified as build_failed", file=sys.stderr)
            return 1
        status, _, _ = compile_and_run(args.cc, app,
                                       "int main(void){ volatile int *p=0; return *p; }",
                                       work, "control-ub", quiet=True)
        if status != "sanitizer":
            print("FAIL: UB control was not rejected as sanitizer", file=sys.stderr)
            return 1
        for name, old, new, expected in mutants:
            if old not in source_text:
                print(f"fail closed: mutant anchor missing for {name}", file=sys.stderr)
                return 1
            if run_mutant(args.cc, app, source_text.replace(old, new, 1), work,
                          f"native-callbacks-mutant-{name}", expected) != 0:
                return 1
    return 0


def build_source_from_text(text: str) -> str:
    return PRELUDE + "\n".join(extract_function(text, name) for name in FUNCTIONS) + MAIN


if __name__ == "__main__":
    raise SystemExit(main())
