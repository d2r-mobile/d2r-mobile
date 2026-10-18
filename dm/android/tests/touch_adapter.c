#include "touch_adapter.h"
#include "input.h"
#include "dm_touch_controls.h"
#include "dm_input.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int checks, cases;
#define CHECK(expr) do { ++checks; if (!(expr)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); } } while (0)
#define RUN(f) do { int before = checks; f(); ++cases; printf("PASS %s (%d checks)\n", #f, checks - before); } while (0)
#define CASE(f) do { fixture a = make_fixture(); f(&a); drop_fixture(&a); ++cases; printf("PASS %s (%d checks)\n", #f, checks); } while (0)

typedef struct {
    dm_android_pad *pad;
    dm_android_touch *touch;
    dm_tc_layout layout;
} fixture;

static fixture make_fixture(void) {
    fixture f;
    f.pad = dm_android_pad_create();
    CHECK(f.pad);
    f.touch = dm_android_touch_create(f.pad, 667, 375);
    CHECK(f.touch);
    dm_tc_layout_make(&f.layout, 667, 375);
    return f;
}

static void drop_fixture(fixture *f) {
    dm_android_touch_destroy(f->touch);
    dm_android_pad_destroy(f->pad);
}

static dm_npad_state read_pad(fixture *f) {
    dm_npad_state s = dm_android_touch_read(f->touch);
    CHECK(s.attributes == 1);
    return s;
}

static void belt_upper_point(const dm_tc_layout *l, int col, int row, float *x, float *y) {
    const dm_tc_target *t = &l->target[DM_TC_BELT1 + col];
    float u = l->height / 1320.0f;
    *x = t->x;
    *y = t->y - (165.0f + 162.0f * (row - 1)) * u;
}

static void tap(fixture *f, int32_t id, float x, float y, int64_t t) {
    CHECK(dm_android_touch_down(f->touch, id, x, y, t));
    CHECK(dm_android_touch_up(f->touch, id, t + 1000000));
}

static void two_ids_live(fixture *f) {
    dm_npad_state s;
    CHECK(dm_android_touch_down(f->touch, 7, 90, 275, 1000000000));
    CHECK(dm_android_touch_move(f->touch, 7, 150, 275, 1010000000));
    s = read_pad(f); CHECK(s.lx == DM_STICK_MAX && s.ly == 0 && s.buttons == 0);
    CHECK(dm_android_touch_down(f->touch, 3, f->layout.target[DM_TC_MAIN].x,
                                f->layout.target[DM_TC_MAIN].y, 1020000000));
    s = read_pad(f); CHECK(s.lx == DM_STICK_MAX && s.buttons == DM_BTN_A);
    CHECK(dm_android_touch_up(f->touch, 7, 1030000000));
    s = read_pad(f); CHECK(s.lx == 0 && s.ly == 0 && s.buttons == DM_BTN_A);
    CHECK(dm_android_touch_up(f->touch, 3, 1040000000));
    s = read_pad(f); CHECK(s.buttons == 0 && s.lx == 0 && s.ly == 0);
}

/* IDs are opaque and can be large, reordered, and reused. If an implementation
   used pointer_id as an array index or assumed event order, these calls fail. */
static void ids_are_not_slots(fixture *f) {
    dm_npad_state s;
    CHECK(dm_android_touch_down(f->touch, 100000, f->layout.target[DM_TC_MAIN].x,
                                f->layout.target[DM_TC_MAIN].y, 2000000000));
    CHECK(dm_android_touch_down(f->touch, 42, f->layout.target[DM_TC_SKILL1].x,
                                f->layout.target[DM_TC_SKILL1].y, 2010000000));
    CHECK(dm_android_touch_tick(f->touch, 2110000000));
    s = read_pad(f); CHECK(s.buttons == (DM_BTN_A | DM_BTN_B));
    CHECK(dm_android_touch_move(f->touch, 42,
                                f->layout.target[DM_TC_SKILL1].x - 50,
                                f->layout.target[DM_TC_SKILL1].y - 50, 2120000000));
    s = read_pad(f); CHECK(s.buttons == (DM_BTN_A | DM_BTN_B) && s.rx != 0 && s.ry != 0);
    CHECK(dm_android_touch_up(f->touch, 100000, 2130000000));
    s = read_pad(f); CHECK(s.buttons == DM_BTN_B);
    CHECK(dm_android_touch_up(f->touch, 42, 2160000000));   /* > DM_TC_KEY_MIN_S after key-down */
    s = read_pad(f); CHECK(s.buttons == 0);
    /* reuse ID 42: it must get a fresh bounded slot, not a stale core finger */
    CHECK(dm_android_touch_down(f->touch, 42, 90, 275, 2170000000));
    s = read_pad(f); CHECK(s.lx == 0 && s.ly == 0 && s.buttons == 0);
    CHECK(dm_android_touch_move(f->touch, 42, 150, 275, 2180000000));
    s = read_pad(f); CHECK(s.lx == DM_STICK_MAX);
    CHECK(dm_android_touch_up(f->touch, 42, 2190000000));
}

static void duplicate_and_invalid(fixture *f) {
    dm_npad_state s;
    CHECK(!dm_android_touch_down(f->touch, -1, 100, 100, 3000000000));
    CHECK(!dm_android_touch_down(f->touch, 1, NAN, 100, 3010000000));
    CHECK(!dm_android_touch_move(f->touch, 99, 100, 100, 3020000000));
    CHECK(!dm_android_touch_up(f->touch, 99, 3030000000));
    CHECK(dm_android_touch_down(f->touch, 8, 90, 275, 3040000000));
    CHECK(!dm_android_touch_down(f->touch, 8, 250, 100, 3050000000));   /* duplicate keeps first owner */
    CHECK(dm_android_touch_move(f->touch, 8, 150, 275, 3060000000));
    s = read_pad(f); CHECK(s.lx == DM_STICK_MAX);
    CHECK(dm_android_touch_up(f->touch, 8, 3070000000));
}

static void cancellation_has_no_cast(fixture *f) {
    dm_npad_state s;
    CHECK(dm_android_touch_down(f->touch, 5, f->layout.target[DM_TC_SKILL1].x,
                                f->layout.target[DM_TC_SKILL1].y, 4000000000));
    CHECK(dm_android_touch_cancel(f->touch, 5, 4010000000));
    s = read_pad(f); CHECK(s.buttons == 0 && s.rx == 0 && s.ry == 0);
    CHECK(dm_android_touch_tick(f->touch, 4500000000));
    s = read_pad(f); CHECK(s.buttons == 0);
    CHECK(!dm_android_touch_move(f->touch, 5, 100, 100, 4510000000));
}

static void outside_move_and_up(fixture *f) {
    dm_npad_state s;
    CHECK(dm_android_touch_down(f->touch, 6, f->layout.target[DM_TC_MAIN].x,
                                f->layout.target[DM_TC_MAIN].y, 5000000000));
    CHECK(dm_android_touch_move(f->touch, 6, 2000, 2000, 5010000000));
    s = read_pad(f); CHECK(s.buttons == DM_BTN_A);
    CHECK(dm_android_touch_up(f->touch, 6, 5020000000));
    s = read_pad(f); CHECK(s.buttons == 0 && s.lx == 0 && s.ly == 0 && s.rx == 0 && s.ry == 0);
}

static void focus_pause_resize(fixture *f) {
    dm_npad_state s;
    CHECK(dm_android_touch_down(f->touch, 1, 90, 275, 6000000000));
    CHECK(dm_android_touch_down(f->touch, 2, f->layout.target[DM_TC_MAIN].x,
                                f->layout.target[DM_TC_MAIN].y, 6010000000));
    CHECK(dm_android_touch_focus_lost(f->touch, 6020000000));
    s = read_pad(f); CHECK(s.buttons == 0 && s.lx == 0 && s.ly == 0);
    CHECK(!dm_android_touch_down(f->touch, 3, 90, 275, 6030000000));
    CHECK(dm_android_touch_resume(f->touch, 6040000000));
    CHECK(dm_android_touch_down(f->touch, 3, 90, 275, 6050000000));
    CHECK(dm_android_touch_pause(f->touch, 6060000000));
    s = read_pad(f); CHECK(s.buttons == 0 && s.lx == 0);
    CHECK(dm_android_touch_resume(f->touch, 6070000000));
    CHECK(dm_android_touch_down(f->touch, 4, f->layout.target[DM_TC_MAIN].x,
                                f->layout.target[DM_TC_MAIN].y, 6080000000));
    CHECK(dm_android_touch_resize(f->touch, 812, 375, 6090000000));
    s = read_pad(f); CHECK(s.buttons == 0 && s.lx == 0);
    CHECK(!dm_android_touch_move(f->touch, 4, 100, 100, 6100000000));
}

static void clock_regression(fixture *f) {
    dm_npad_state s;
    CHECK(!dm_android_touch_down(f->touch, 9, 90, 275, -1));
    CHECK(dm_android_touch_down(f->touch, 9, 90, 275, 7000000000));
    CHECK(dm_android_touch_move(f->touch, 9, 150, 275, 6990000000));   /* clamped, not rewound */
    s = read_pad(f); CHECK(s.lx == DM_STICK_MAX);
    CHECK(dm_android_touch_up(f->touch, 9, 6980000000));
    s = read_pad(f); CHECK(s.lx == 0 && s.buttons == 0);
    CHECK(!dm_android_touch_tick(f->touch, INT64_MIN));
    CHECK(dm_android_touch_tick(f->touch, INT64_MAX));   /* maximum timestamp is accepted without overflow */
}

static void release_to_cast_pulse(fixture *f) {
    dm_npad_state s;
    CHECK(dm_android_touch_down(f->touch, 11, f->layout.target[DM_TC_SKILL1].x,
                                f->layout.target[DM_TC_SKILL1].y, 8000000000));
    CHECK(dm_android_touch_up(f->touch, 11, 8010000000));   /* shorter than settle + key minimum */
    s = read_pad(f); CHECK(s.buttons == DM_BTN_B);
    s = read_pad(f); CHECK(s.buttons == 0);   /* at most once, already neutral */
    CHECK(dm_android_touch_tick(f->touch, 8100000000));
    s = read_pad(f); CHECK(s.buttons == 0);   /* pulse withdrawn, no stuck key */
}

/* The first consumer read may be delayed across ticks and unrelated events; the
   pulse must keep its captured aim until that read. */
static void delayed_pulse_keeps_aim(fixture *f) {
    dm_npad_state s;
    float x = f->layout.target[DM_TC_SKILL1].x, y = f->layout.target[DM_TC_SKILL1].y;
    CHECK(dm_android_touch_down(f->touch, 12, x, y, 8200000000));
    CHECK(dm_android_touch_move(f->touch, 12, x - 50, y - 50, 8201000000));
    CHECK(dm_android_touch_up(f->touch, 12, 8202000000));
    CHECK(dm_android_touch_tick(f->touch, 8300000000));
    CHECK(dm_android_touch_down(f->touch, 13, 90, 275, 8301000000));
    CHECK(dm_android_touch_up(f->touch, 13, 8302000000));
    s = read_pad(f); CHECK(s.buttons == DM_BTN_B && s.rx < 0 && s.ry > 0);
    s = read_pad(f); CHECK(s.buttons == 0 && s.rx == 0 && s.ry == 0);
}

/* Canceling the joystick must not erase another pointer's unread skill pulse. */
static void cancel_isolates_unread_pulse(fixture *f) {
    dm_npad_state s;
    CHECK(dm_android_touch_down(f->touch, 1, 90, 275, 8400000000));
    tap(f, 2, f->layout.target[DM_TC_SKILL1].x, f->layout.target[DM_TC_SKILL1].y, 8410000000);
    CHECK(dm_android_touch_cancel(f->touch, 1, 8420000000));
    s = read_pad(f); CHECK(s.buttons == DM_BTN_B && s.lx == 0 && s.ly == 0);
    s = read_pad(f); CHECK(s.buttons == 0 && s.lx == 0 && s.ly == 0);
}

/* Two distinct skills tapped before either read must both survive in order. */
static void two_distinct_pulses(fixture *f) {
    dm_npad_state a, b, c;
    tap(f, 1, f->layout.target[DM_TC_SKILL1].x, f->layout.target[DM_TC_SKILL1].y, 8250000000);
    tap(f, 2, f->layout.target[DM_TC_SKILL2].x, f->layout.target[DM_TC_SKILL2].y, 8270000000);
    a = read_pad(f); b = read_pad(f); c = read_pad(f);
    CHECK(a.buttons == DM_BTN_B);
    CHECK(b.buttons == DM_BTN_X);
    CHECK(c.buttons == 0);
}

/* A new pulse arriving during the release must queue, not replace the release or
   the older same-button pulse; FIFO button order remains B, B, X. */
static void same_button_release_with_new_pulse(fixture *f) {
    dm_npad_state a, b, c, d, e;
    tap(f, 1, f->layout.target[DM_TC_SKILL1].x, f->layout.target[DM_TC_SKILL1].y, 9300000000);
    tap(f, 2, f->layout.target[DM_TC_SKILL1].x, f->layout.target[DM_TC_SKILL1].y, 9320000000);
    a = read_pad(f); CHECK(a.buttons == DM_BTN_B);
    tap(f, 3, f->layout.target[DM_TC_SKILL2].x, f->layout.target[DM_TC_SKILL2].y, 9340000000);
    CHECK(dm_android_touch_tick(f->touch, 9350000000));
    b = read_pad(f); CHECK(b.buttons == 0);
    CHECK(dm_android_touch_tick(f->touch, 9360000000));
    c = read_pad(f); CHECK(c.buttons == DM_BTN_B);
    d = read_pad(f); CHECK(d.buttons == DM_BTN_X);
    e = read_pad(f); CHECK(e.buttons == 0);
}

/* Same-button repeats need a consumer-visible release between edges. */
static void repeated_same_button(fixture *f) {
    dm_npad_state a, b, c, d;
    tap(f, 1, f->layout.target[DM_TC_SKILL1].x, f->layout.target[DM_TC_SKILL1].y, 8350000000);
    tap(f, 2, f->layout.target[DM_TC_SKILL1].x, f->layout.target[DM_TC_SKILL1].y, 8370000000);
    a = read_pad(f); b = read_pad(f); c = read_pad(f); d = read_pad(f);
    CHECK(a.buttons == DM_BTN_B);
    CHECK(b.buttons == 0);
    CHECK(c.buttons == DM_BTN_B);
    CHECK(d.buttons == 0);
}

/* The release is a read-boundary invariant: a tick before every read must not
   republish the pending same-button pulse over the required release. */
static void same_button_release_across_ticks(fixture *f) {
    dm_npad_state a, b, c, d;
    tap(f, 1, f->layout.target[DM_TC_SKILL1].x, f->layout.target[DM_TC_SKILL1].y, 9100000000);
    tap(f, 2, f->layout.target[DM_TC_SKILL1].x, f->layout.target[DM_TC_SKILL1].y, 9120000000);
    CHECK(dm_android_touch_tick(f->touch, 9130000000));
    a = read_pad(f); CHECK(a.buttons == DM_BTN_B);
    CHECK(dm_android_touch_tick(f->touch, 9140000000));
    b = read_pad(f); CHECK(b.buttons == 0);
    CHECK(dm_android_touch_tick(f->touch, 9150000000));
    c = read_pad(f); CHECK(c.buttons == DM_BTN_B);
    CHECK(dm_android_touch_tick(f->touch, 9160000000));
    d = read_pad(f); CHECK(d.buttons == 0);
}

/* An unrelated active joystick must remain held through the neutral release and
   the following same-button pulse. */
static void same_button_release_with_unrelated_move(fixture *f) {
    dm_npad_state a, b, c;
    tap(f, 1, f->layout.target[DM_TC_SKILL1].x, f->layout.target[DM_TC_SKILL1].y, 9200000000);
    tap(f, 2, f->layout.target[DM_TC_SKILL1].x, f->layout.target[DM_TC_SKILL1].y, 9220000000);
    a = read_pad(f); CHECK(a.buttons == DM_BTN_B);
    CHECK(dm_android_touch_down(f->touch, 9, 90, 275, 9230000000));
    CHECK(dm_android_touch_move(f->touch, 9, 150, 275, 9231000000));
    CHECK(dm_android_touch_tick(f->touch, 9232000000));
    b = read_pad(f); CHECK(b.buttons == 0 && b.lx == DM_STICK_MAX);
    CHECK(dm_android_touch_tick(f->touch, 9233000000));
    c = read_pad(f); CHECK(c.buttons == DM_BTN_B && c.lx == DM_STICK_MAX);
    CHECK(dm_android_touch_up(f->touch, 9, 9234000000));
}

/* Distinct pulses keep their own aim directions, not an OR/collapsed sample. */
static void distinct_aims(fixture *f) {
    dm_npad_state a, b, c;
    float x1 = f->layout.target[DM_TC_SKILL1].x, y1 = f->layout.target[DM_TC_SKILL1].y;
    float x2 = f->layout.target[DM_TC_SKILL2].x, y2 = f->layout.target[DM_TC_SKILL2].y;
    CHECK(dm_android_touch_down(f->touch, 1, x1, y1, 8600000000));
    CHECK(dm_android_touch_move(f->touch, 1, x1 - 50, y1 - 50, 8601000000));
    CHECK(dm_android_touch_up(f->touch, 1, 8602000000));
    CHECK(dm_android_touch_down(f->touch, 2, x2, y2, 8610000000));
    CHECK(dm_android_touch_move(f->touch, 2, x2 + 50, y2 + 50, 8611000000));
    CHECK(dm_android_touch_up(f->touch, 2, 8612000000));
    a = read_pad(f); b = read_pad(f); c = read_pad(f);
    CHECK(a.buttons == DM_BTN_B && a.rx < 0 && a.ry > 0);
    CHECK(b.buttons == DM_BTN_X && b.rx > 0 && b.ry < 0);
    CHECK(c.buttons == 0 && c.rx == 0 && c.ry == 0);
}

/* Full bounded pulse storage backpressures without partial mutation; global
   cancel clears the queued pulses. */
static void bounded_pulses_and_overflow(fixture *f) {
    dm_npad_state s;
    for (int i = 0; i < DM_ANDROID_TOUCH_PULSE_CAPACITY; ++i)
        tap(f, i, f->layout.target[DM_TC_SKILL1].x, f->layout.target[DM_TC_SKILL1].y,
            8700000000 + (int64_t)i * 1000000);
    CHECK(!dm_android_touch_down(f->touch, 99, f->layout.target[DM_TC_SKILL1].x,
                                 f->layout.target[DM_TC_SKILL1].y, 8800000000));
    CHECK(!dm_android_touch_tick(f->touch, 8801000000));
    CHECK(dm_android_touch_cancel_all(f->touch, 8802000000));
    s = read_pad(f); CHECK(s.buttons == 0);
}

/* Global cancellation must clear a queue of pending pulses, not only the
   deferred single-slot case. */
static void global_cancel_clears_pulses(fixture *f) {
    dm_npad_state s;
    tap(f, 1, f->layout.target[DM_TC_SKILL1].x, f->layout.target[DM_TC_SKILL1].y, 8900000000);
    tap(f, 2, f->layout.target[DM_TC_SKILL2].x, f->layout.target[DM_TC_SKILL2].y, 8902000000);
    CHECK(dm_android_touch_focus_lost(f->touch, 8903000000));
    s = read_pad(f); CHECK(s.buttons == 0);
    s = read_pad(f); CHECK(s.buttons == 0);
}

/* Unknown/duplicate/full events must not advance the accepted clock. */
static void invalid_event_no_timer_mutation(fixture *f) {
    dm_npad_state s;
    tap(f, 30, f->layout.swap.x, f->layout.swap.y, 8500000000);
    CHECK(dm_android_touch_down(f->touch, 31, f->layout.target[DM_TC_SKILL1].x,
                                f->layout.target[DM_TC_SKILL1].y, 8510000000));
    CHECK(dm_android_touch_up(f->touch, 31, 8510100000));
    CHECK(!dm_android_touch_move(f->touch, 999, 0, 0, INT64_MAX));
    CHECK(dm_android_touch_tick(f->touch, 8515000000));
    s = read_pad(f); CHECK(s.buttons == DM_BTN_ZL);   /* still before the ZL lead deadline */
    CHECK(dm_android_touch_tick(f->touch, 8680000000));   /* 0.17 s after press at 8.51 */
    s = read_pad(f); CHECK(s.buttons == (DM_BTN_ZL | DM_BTN_B));
}

/* Destroy with a surviving pad must leave no stuck button for the next reader. */
static void destroy_leaves_pad_neutral(void) {
    dm_android_pad *pad = dm_android_pad_create();
    CHECK(pad);
    dm_android_touch *touch = dm_android_touch_create(pad, 667, 375);
    CHECK(touch);
    dm_tc_layout l;
    dm_tc_layout_make(&l, 667, 375);
    CHECK(dm_android_touch_down(touch, 1, l.target[DM_TC_MAIN].x, l.target[DM_TC_MAIN].y, 9000000000));
    dm_android_touch_destroy(touch);
    dm_npad_state s = dm_android_pad_read(pad);
    CHECK(s.buttons == 0 && s.lx == 0 && s.ly == 0 && s.attributes == 1);
    dm_android_pad_destroy(pad);
}

static void second_set_zl_and_deferred(fixture *f) {
    dm_npad_state s;
    tap(f, 20, f->layout.swap.x, f->layout.swap.y, 9000000000);
    s = read_pad(f); CHECK(s.buttons == 0);
    CHECK(dm_android_touch_down(f->touch, 21, f->layout.target[DM_TC_SKILL1].x,
                                f->layout.target[DM_TC_SKILL1].y, 9100000000));
    CHECK(dm_android_touch_up(f->touch, 21, 9101000000));
    s = read_pad(f); CHECK(s.buttons == DM_BTN_ZL);   /* ZL leads, skill is deferred */
    CHECK(dm_android_touch_tick(f->touch, 9150000000));
    s = read_pad(f); CHECK(s.buttons == DM_BTN_ZL);
    CHECK(dm_android_touch_tick(f->touch, 9270000000));   /* DM_TC_ZL_LEAD_S after press at 9.10 */
    s = read_pad(f); CHECK(s.buttons == (DM_BTN_ZL | DM_BTN_B));
    CHECK(dm_android_touch_tick(f->touch, 9300000000));
    s = read_pad(f); CHECK(s.buttons == 0);
}

static void cancel_clears_deferred(fixture *f) {
    dm_npad_state s;
    tap(f, 30, f->layout.swap.x, f->layout.swap.y, 10000000000);
    CHECK(dm_android_touch_down(f->touch, 31, f->layout.target[DM_TC_SKILL1].x,
                                f->layout.target[DM_TC_SKILL1].y, 10100000000));
    CHECK(dm_android_touch_up(f->touch, 31, 10101000000));
    (void)read_pad(f);
    CHECK(dm_android_touch_cancel_all(f->touch, 10102000000));
    s = read_pad(f); CHECK(s.buttons == 0);
    CHECK(dm_android_touch_tick(f->touch, 10300000000));
    s = read_pad(f); CHECK(s.buttons == 0);
}

static void full_pointer_table(fixture *f) {
    dm_npad_state s;
    for (int i = 0; i < DM_TC_MAX_TOUCH; ++i)
        CHECK(dm_android_touch_down(f->touch, 1000 + i, 400, 100, 11000000000 + i));
    CHECK(!dm_android_touch_down(f->touch, 2000, 400, 100, 11100000000));
    s = read_pad(f); CHECK(s.buttons == 0 && s.lx == 0);
    CHECK(dm_android_touch_cancel_all(f->touch, 11200000000));
}

/* Port of upstream t_touch_controls.c:joystick_plus_skills through the Android
   MotionEvent adapter: one movement pointer plus five button pointers, each
   with its own opaque ID, consumed as one pad snapshot. Releasing one skill
   must drop only that button; the stick keeps its axes until its own pointer
   ends. A port that keyed input by slot, collapsed owners, or cleared the
   stick on any button release fails here. */
static void multi_finger_movement_and_skill_group(fixture *f) {
    dm_npad_state s;
    uint64_t group = DM_BTN_A | DM_BTN_B | DM_BTN_X | DM_BTN_Y | DM_BTN_R;
    CHECK(dm_android_touch_down(f->touch, 10, 90, 275, 12000000000));
    CHECK(dm_android_touch_move(f->touch, 10, 150, 275, 12010000000));
    CHECK(dm_android_touch_down(f->touch, 11, f->layout.target[DM_TC_MAIN].x,
                                f->layout.target[DM_TC_MAIN].y, 12020000000));
    CHECK(dm_android_touch_down(f->touch, 12, f->layout.target[DM_TC_SKILL1].x,
                                f->layout.target[DM_TC_SKILL1].y, 12030000000));
    CHECK(dm_android_touch_down(f->touch, 13, f->layout.target[DM_TC_SKILL2].x,
                                f->layout.target[DM_TC_SKILL2].y, 12040000000));
    CHECK(dm_android_touch_down(f->touch, 14, f->layout.target[DM_TC_SKILL3].x,
                                f->layout.target[DM_TC_SKILL3].y, 12050000000));
    CHECK(dm_android_touch_down(f->touch, 15, f->layout.target[DM_TC_SKILL4].x,
                                f->layout.target[DM_TC_SKILL4].y, 12060000000));
    CHECK(dm_android_touch_tick(f->touch, 12200000000));   /* past every skill's settle window */
    s = read_pad(f);
    CHECK(s.buttons == group && s.lx == DM_STICK_MAX && s.ly == 0 && s.rx == 0 && s.ry == 0);
    CHECK(dm_android_touch_up(f->touch, 13, 12300000000));   /* held past the pulse minimum */
    s = read_pad(f);
    CHECK(s.buttons == (group & ~DM_BTN_X) && s.lx == DM_STICK_MAX && s.ly == 0);
    CHECK(dm_android_touch_up(f->touch, 10, 12400000000));   /* stick ends, skills stay held */
    s = read_pad(f);
    CHECK(s.buttons == (group & ~DM_BTN_X) && s.lx == 0 && s.ly == 0);
    CHECK(dm_android_touch_up(f->touch, 11, 12500000000));
    CHECK(dm_android_touch_up(f->touch, 12, 12510000000));
    CHECK(dm_android_touch_up(f->touch, 14, 12520000000));
    CHECK(dm_android_touch_up(f->touch, 15, 12530000000));
    s = read_pad(f);
    CHECK(s.buttons == 0 && s.lx == 0 && s.ly == 0 && s.rx == 0 && s.ry == 0);
}

/* Port of upstream t_touch_controls.c:joystick_deadzone cancel/resume,
   including the timing source: ACTION_CANCEL's own timestamp is the start of
   the 0.35 s resume window, not the timestamp of the last core event. The
   stick is held still for 0.9 s (no move) and then cancelled, so an adapter
   that leaves core last_time_s at the last move rejects a valid resume at
   0.2 s and starts a fresh stick at the new point instead (axes 0, then the
   opposite direction). */
static void joystick_cancel_resume_uses_cancel_time(fixture *f) {
    dm_npad_state s;
    CHECK(dm_android_touch_down(f->touch, 30, 90, 275, 70000000000));
    CHECK(dm_android_touch_move(f->touch, 30, 260, 160, 70100000000));
    s = read_pad(f); CHECK(s.lx > 0 && s.ly > 0);
    CHECK(dm_android_touch_cancel(f->touch, 30, 71000000000));   /* 0.9 s after the last move */
    s = read_pad(f); CHECK(s.lx == 0 && s.ly == 0 && s.buttons == 0);
    CHECK(dm_android_touch_down(f->touch, 31, 236, 128, 71200000000));   /* 0.2 s after cancel, outside the start zone */
    s = read_pad(f); CHECK(s.lx > 0 && s.ly > 0);   /* resumed: old touch point, not a fresh zero */
    CHECK(dm_android_touch_move(f->touch, 31, 150, 275, 71300000000));   /* 60 pt right of the old touch point */
    s = read_pad(f); CHECK(s.lx == DM_STICK_MAX && s.ly == 0);
    CHECK(dm_android_touch_cancel(f->touch, 31, 71400000000));
    (void)read_pad(f);
    CHECK(dm_android_touch_down(f->touch, 32, 236, 128, 71800000000));   /* 0.4 s after cancel: window closed */
    s = read_pad(f); CHECK(s.lx == 0 && s.ly == 0);
    CHECK(dm_android_touch_up(f->touch, 32, 71810000000));
}

/* Port of upstream t_touch_controls.c:cancel_and_resize through the adapter:
   one pointer's ACTION_CANCEL releases only what it owns, a short skill tap
   already queued survives an unrelated cancel, and a resize neutralizes every
   owner, drops the unread pulse and invalidates the old pointer table. */
static void cancel_and_resize_neutralize_owned_gestures(fixture *f) {
    dm_npad_state s;
    dm_tc_layout wide;
    CHECK(dm_android_touch_down(f->touch, 20, 90, 275, 13000000000));
    CHECK(dm_android_touch_move(f->touch, 20, 150, 275, 13010000000));
    CHECK(dm_android_touch_down(f->touch, 21, f->layout.target[DM_TC_MAIN].x,
                                f->layout.target[DM_TC_MAIN].y, 13020000000));
    tap(f, 22, f->layout.target[DM_TC_SKILL1].x, f->layout.target[DM_TC_SKILL1].y, 13030000000);
    CHECK(dm_android_touch_cancel(f->touch, 20, 13040000000));   /* cancel the stick only */
    s = read_pad(f); CHECK(s.buttons == (DM_BTN_A | DM_BTN_B) && s.lx == 0 && s.ly == 0);
    CHECK(dm_android_touch_down(f->touch, 23, f->layout.target[DM_TC_SKILL2].x,
                                f->layout.target[DM_TC_SKILL2].y, 13050000000));
    CHECK(dm_android_touch_cancel(f->touch, 23, 13060000000));   /* before settle: no cast, no pulse */
    s = read_pad(f); CHECK(s.buttons == DM_BTN_A);
    CHECK(dm_android_touch_tick(f->touch, 13200000000));
    s = read_pad(f); CHECK(s.buttons == DM_BTN_A);   /* cancelled skill never surfaces later */
    tap(f, 24, f->layout.target[DM_TC_SKILL1].x, f->layout.target[DM_TC_SKILL1].y, 13210000000);
    CHECK(dm_android_touch_resize(f->touch, 812, 375, 13220000000));   /* unread pulse queued */
    s = read_pad(f); CHECK(s.buttons == 0 && s.lx == 0 && s.ly == 0 && s.rx == 0 && s.ry == 0);
    CHECK(dm_android_touch_tick(f->touch, 13500000000));
    s = read_pad(f); CHECK(s.buttons == 0);   /* dropped, not delivered late */
    CHECK(!dm_android_touch_move(f->touch, 21, 200, 200, 13510000000));
    CHECK(!dm_android_touch_up(f->touch, 21, 13520000000));
    dm_tc_layout_make(&wide, 812, 375);
    CHECK(dm_android_touch_down(f->touch, 25, wide.target[DM_TC_MAIN].x,
                                wide.target[DM_TC_MAIN].y, 13530000000));
    s = read_pad(f); CHECK(s.buttons == DM_BTN_A);
    CHECK(dm_android_touch_up(f->touch, 25, 13531000000));
    s = read_pad(f); CHECK(s.buttons == 0);
}

/* Port of upstream t_touch_controls.c:belt_single_double through the Android
   adapter. A belt cell sends no key while held and exactly one key on release;
   two quick taps queued before the first read are two distinct edges, never a
   merged press; the second skill set's belt pulse carries no ZL (ZL + belt =
   feed the mercenary in the original game). Column mapping is part of the
   expectation, not read back from the adapter. */
static void belt_tap_release_and_double_tap(fixture *f) {
    dm_npad_state s;
    float bx = f->layout.target[DM_TC_BELT2].x, by = f->layout.target[DM_TC_BELT2].y;
    uint64_t k = f->layout.target[DM_TC_BELT2].button;
    CHECK(k == DM_BTN_UP);
    CHECK(f->layout.target[DM_TC_BELT3].button == DM_BTN_DOWN);
    CHECK(dm_android_touch_down(f->touch, 40, bx, by, 16000000000));
    s = read_pad(f); CHECK(s.buttons == 0);   /* no key while the belt is held */
    CHECK(dm_android_touch_up(f->touch, 40, 16080000000));
    s = read_pad(f); CHECK(s.buttons == k);   /* drinks on release */
    s = read_pad(f); CHECK(s.buttons == 0);
    /* Two taps before either pulse is consumed: two rising edges with a neutral
       read between them. A same-button merge yields one edge and fails. */
    CHECK(dm_android_touch_down(f->touch, 41, bx, by, 16100000000));
    CHECK(dm_android_touch_up(f->touch, 41, 16108000000));
    CHECK(dm_android_touch_down(f->touch, 42, bx, by, 16110000000));
    CHECK(dm_android_touch_up(f->touch, 42, 16118000000));
    int edges = 0;
    uint64_t prev = 0;
    for (int i = 0; i < 6; ++i) {
        s = read_pad(f);
        CHECK(!(s.buttons & DM_BTN_ZL));
        if ((s.buttons & k) && !(prev & k)) ++edges;
        prev = s.buttons & k;
    }
    CHECK(edges == 2);
    /* Second skill set: the belt pulse must not carry ZL. */
    tap(f, 43, f->layout.swap.x, f->layout.swap.y, 16200000000);
    s = read_pad(f); CHECK(s.buttons == 0);
    CHECK(dm_android_touch_down(f->touch, 44, bx, by, 16210000000));
    s = read_pad(f); CHECK(s.buttons == 0);
    CHECK(dm_android_touch_up(f->touch, 44, 16218000000));
    s = read_pad(f); CHECK(s.buttons == k);
    s = read_pad(f); CHECK(s.buttons == 0);
    tap(f, 45, f->layout.target[DM_TC_BELT3].x, f->layout.target[DM_TC_BELT3].y, 16300000000);
    s = read_pad(f); CHECK(s.buttons == DM_BTN_DOWN);
    s = read_pad(f); CHECK(s.buttons == 0);
}

/* Port of upstream t_touch_controls.c:held_skill2_vs_belt through the adapter:
   one pointer holds a second-set skill while a second pointer taps the belt.
   Expected timeline, computed from the rule: ZL and the skill key drop at the
   belt release; the belt key comes only after a full DM_TC_ZL_LEAD_S with no
   ZL and no skill key; then ZL returns first and the held skill key returns a
   full lead later. A port that keeps ZL during the belt pulse (feeds the
   mercenary), keeps the skill key alone (casts the first set), or drops the
   held finger fails. */
static void held_second_set_skill_vs_belt(fixture *f) {
    dm_npad_state s;
    float kx = f->layout.target[DM_TC_SKILL1].x, ky = f->layout.target[DM_TC_SKILL1].y;
    float bx = f->layout.target[DM_TC_BELT1].x, by = f->layout.target[DM_TC_BELT1].y;
    uint64_t bkey = f->layout.target[DM_TC_BELT1].button;
    int64_t belt_up = 15215000000;
    int64_t belt_t = 0, zl_back_t = 0, key_back_t = 0;
    int bad_zl = 0, bad_key = 0, gap_neutral = 1;
    tap(f, 50, f->layout.swap.x, f->layout.swap.y, 15000000000);
    s = read_pad(f); CHECK(s.buttons == 0);
    CHECK(dm_android_touch_down(f->touch, 51, kx, ky, 15010000000));
    for (int i = 1; i <= 20; ++i)
        CHECK(dm_android_touch_tick(f->touch, 15010000000 + (int64_t)i * 10000000));
    s = read_pad(f);
    CHECK((s.buttons & DM_BTN_ZL) && (s.buttons & DM_BTN_B));   /* held second-set skill: ZL + key */
    CHECK(dm_android_touch_down(f->touch, 52, bx, by, 15210000000));   /* belt press sends nothing */
    s = read_pad(f); CHECK(s.buttons == (DM_BTN_ZL | DM_BTN_B));
    CHECK(dm_android_touch_up(f->touch, 52, belt_up));
    s = read_pad(f); CHECK(s.buttons == 0);   /* ZL and skill released before the belt key */
    for (int i = 0; i < 60; ++i) {
        int64_t t = 15220000000 + (int64_t)i * 10000000;
        CHECK(dm_android_touch_tick(f->touch, t));
        s = read_pad(f);
        if (!belt_t) {
            if (s.buttons & bkey) {
                belt_t = t;
                if (s.buttons & DM_BTN_ZL) bad_zl = 1;
                if (s.buttons & DM_BTN_B) bad_key = 1;
            } else if (s.buttons != 0) gap_neutral = 0;
        } else {
            if (!zl_back_t && (s.buttons & DM_BTN_ZL)) { zl_back_t = t; if (s.buttons & DM_BTN_B) bad_key = 1; }
            if (!key_back_t && (s.buttons & DM_BTN_B)) { key_back_t = t; if (!(s.buttons & DM_BTN_ZL)) bad_zl = 1; }
        }
    }
    CHECK(belt_t == 15390000000);
    CHECK(bad_zl == 0 && bad_key == 0 && gap_neutral);
    CHECK(belt_t - belt_up >= 170000000);   /* ZL led a full 0.17 s before the belt key */
    CHECK(zl_back_t > belt_t && zl_back_t - belt_t <= 20000000);   /* ZL back immediately after the drink */
    CHECK(key_back_t > zl_back_t);
    CHECK(key_back_t - zl_back_t >= 150000000 && key_back_t - zl_back_t <= 180000000);   /* full lead for the skill key */
    CHECK(dm_android_touch_up(f->touch, 51, 15820000000));
    s = read_pad(f); CHECK(s.buttons == 0);
}

/* Expanded upper belt rows through the adapter. rows/open come from the host
   snapshot (dm_belt_rows); the request codes are the frozen upstream encoding
   0x100 | row << 4 | col (row 1-3 above the bottom row), computed here from the
   documented rule, not read back from the adapter. A request is consumed once;
   a second take returns nothing. Bottom-row taps still drink, and a tap outside
   the expanded belt asks the host to collapse without a world tap. */
static void belt_context_expanded_rows(fixture *f) {
    dm_npad_state s;
    dm_android_touch_action a;
    float x, y;
    CHECK(dm_android_touch_set_belt_context(f->touch, 4, 1));
    belt_upper_point(&f->layout, 1, 1, &x, &y);   /* row 1, column 1 */
    CHECK(dm_android_touch_down(f->touch, 60, x, y, 17000000000));
    s = read_pad(f); CHECK(s.buttons == 0);
    CHECK(dm_android_touch_take_action(f->touch, &a));
    CHECK(a.kind == DM_ANDROID_ACTION_BELT && (int)a.x == (0x100 | 1 << 4 | 1));
    CHECK(!dm_android_touch_take_action(f->touch, &a));   /* one-shot */
    CHECK(dm_android_touch_up(f->touch, 60, 17005000000));
    CHECK(!dm_android_touch_take_action(f->touch, &a));
    belt_upper_point(&f->layout, 3, 2, &x, &y);   /* row 2, column 3 */
    CHECK(dm_android_touch_down(f->touch, 61, x, y, 17100000000));
    CHECK(dm_android_touch_take_action(f->touch, &a));
    CHECK(a.kind == DM_ANDROID_ACTION_BELT && (int)a.x == (0x100 | 2 << 4 | 3));
    CHECK(!dm_android_touch_take_action(f->touch, &a));
    CHECK(dm_android_touch_up(f->touch, 61, 17105000000));
    /* bottom row while expanded = drink pulse, not a request */
    CHECK(dm_android_touch_down(f->touch, 62, f->layout.target[DM_TC_BELT2].x,
                                f->layout.target[DM_TC_BELT2].y, 17200000000));
    s = read_pad(f); CHECK(s.buttons == 0);
    CHECK(dm_android_touch_up(f->touch, 62, 17208000000));
    s = read_pad(f); CHECK(s.buttons == DM_BTN_UP);
    s = read_pad(f); CHECK(s.buttons == 0);
    CHECK(!dm_android_touch_take_action(f->touch, &a));
    /* tap outside the expanded belt = collapse request, no world tap */
    CHECK(dm_android_touch_down(f->touch, 63, 100, 250, 17300000000));
    CHECK(dm_android_touch_take_action(f->touch, &a));
    CHECK(a.kind == DM_ANDROID_ACTION_BELT && (int)a.x == 2);
    CHECK(dm_android_touch_up(f->touch, 63, 17305000000));
    CHECK(!dm_android_touch_take_action(f->touch, &a));
}

/* Host-requested expand/collapse and context validation through the adapter:
   only rows 1-4 and open 0/1 are accepted, before any mutation; rejected input
   leaves the previous context in force. A real change neutralizes a held
   pointer and its pulse, while cancel/focus/resize keep the host snapshot.
   Expected request codes use the same frozen encoding as above. */
static void belt_context_requests_validation_and_reset(fixture *f) {
    dm_npad_state s;
    dm_android_touch_action a;
    dm_tc_layout wide;
    float x, y, bx = f->layout.target[DM_TC_BELT1].x, by = f->layout.target[DM_TC_BELT1].y;
    CHECK(dm_android_touch_set_belt_context(f->touch, 0, 0));   /* host reading empty: core default, single row */
    /* invalid snapshots are rejected before mutation */
    CHECK(!dm_android_touch_set_belt_context(NULL, 4, 0));
    CHECK(!dm_android_touch_set_belt_context(f->touch, 5, 0));
    CHECK(!dm_android_touch_set_belt_context(f->touch, -1, 0));
    CHECK(!dm_android_touch_set_belt_context(f->touch, 4, 2));
    CHECK(!dm_android_touch_set_belt_context(f->touch, 4, -1));
    /* rows are still 0: a vertical swipe sends no request and no drink */
    CHECK(dm_android_touch_down(f->touch, 70, bx, by, 17350000000));
    CHECK(dm_android_touch_move(f->touch, 70, bx, by - 30, 17351000000));
    CHECK(dm_android_touch_up(f->touch, 70, 17352000000));
    s = read_pad(f); CHECK(s.buttons == 0);
    CHECK(!dm_android_touch_take_action(f->touch, &a));
    CHECK(dm_android_touch_set_belt_context(f->touch, 1, 0));
    /* 4 rows, closed: swipe up asks the host to expand, exactly once */
    CHECK(dm_android_touch_set_belt_context(f->touch, 4, 0));
    CHECK(dm_android_touch_down(f->touch, 71, bx, by, 17360000000));
    CHECK(dm_android_touch_move(f->touch, 71, bx, by - 30, 17361000000));
    CHECK(dm_android_touch_up(f->touch, 71, 17362000000));
    CHECK(dm_android_touch_take_action(f->touch, &a));
    CHECK(a.kind == DM_ANDROID_ACTION_BELT && (int)a.x == 1);
    CHECK(!dm_android_touch_take_action(f->touch, &a));
    /* invalid open value while closed: an upper-row tap stays a world tap */
    CHECK(!dm_android_touch_set_belt_context(f->touch, 4, 2));
    CHECK(!dm_android_touch_set_belt_context(f->touch, 4, -1));
    belt_upper_point(&f->layout, 1, 1, &x, &y);
    CHECK(dm_android_touch_down(f->touch, 72, x, y, 17370000000));
    CHECK(dm_android_touch_up(f->touch, 72, 17375000000));
    CHECK(dm_android_touch_take_action(f->touch, &a));
    CHECK(a.kind == DM_ANDROID_ACTION_WORLD_TAP);
    CHECK(!dm_android_touch_take_action(f->touch, &a));
    /* host applied the expand: the same tap now requests that exact cell */
    CHECK(dm_android_touch_set_belt_context(f->touch, 4, 1));
    CHECK(dm_android_touch_down(f->touch, 73, x, y, 17380000000));
    CHECK(dm_android_touch_take_action(f->touch, &a));
    CHECK(a.kind == DM_ANDROID_ACTION_BELT && (int)a.x == (0x100 | 1 << 4 | 1));
    CHECK(dm_android_touch_up(f->touch, 73, 17385000000));
    /* swipe down while open asks the host to collapse */
    CHECK(dm_android_touch_down(f->touch, 74, bx, by, 17390000000));
    CHECK(dm_android_touch_move(f->touch, 74, bx, by + 30, 17391000000));
    CHECK(dm_android_touch_up(f->touch, 74, 17392000000));
    CHECK(dm_android_touch_take_action(f->touch, &a));
    CHECK(a.kind == DM_ANDROID_ACTION_BELT && (int)a.x == 2);
    CHECK(!dm_android_touch_take_action(f->touch, &a));
    /* a real context change cancels a held pointer: its up is no longer known */
    CHECK(dm_android_touch_down(f->touch, 75, f->layout.target[DM_TC_BELT2].x,
                                f->layout.target[DM_TC_BELT2].y, 17400000000));
    s = read_pad(f); CHECK(s.buttons == 0);
    CHECK(dm_android_touch_set_belt_context(f->touch, 4, 0));
    CHECK(!dm_android_touch_move(f->touch, 75, bx, by, 17401000000));
    CHECK(!dm_android_touch_up(f->touch, 75, 17402000000));
    s = read_pad(f); CHECK(s.buttons == 0);
    CHECK(!dm_android_touch_take_action(f->touch, &a));
    /* cancel/focus and resize keep the host snapshot */
    CHECK(dm_android_touch_set_belt_context(f->touch, 4, 1));
    CHECK(dm_android_touch_focus_lost(f->touch, 17410000000));
    CHECK(dm_android_touch_resume(f->touch, 17411000000));
    s = read_pad(f); CHECK(s.buttons == 0);
    CHECK(dm_android_touch_set_belt_context(f->touch, 4, 1));   /* same values: no-op */
    CHECK(dm_android_touch_resize(f->touch, 812, 375, 17420000000));
    s = read_pad(f); CHECK(s.buttons == 0 && s.attributes == 1);
    dm_tc_layout_make(&wide, 812, 375);
    belt_upper_point(&wide, 1, 1, &x, &y);
    CHECK(dm_android_touch_down(f->touch, 76, x, y, 17430000000));
    CHECK(dm_android_touch_take_action(f->touch, &a));
    CHECK(a.kind == DM_ANDROID_ACTION_BELT && (int)a.x == (0x100 | 1 << 4 | 1));
    CHECK(dm_android_touch_up(f->touch, 76, 17435000000));
}

int main(void) {
    CASE(two_ids_live);
    CASE(ids_are_not_slots);
    CASE(duplicate_and_invalid);
    CASE(cancellation_has_no_cast);
    CASE(outside_move_and_up);
    CASE(focus_pause_resize);
    CASE(clock_regression);
    CASE(release_to_cast_pulse);
    CASE(delayed_pulse_keeps_aim);
    CASE(cancel_isolates_unread_pulse);
    CASE(two_distinct_pulses);
    CASE(repeated_same_button);
    CASE(same_button_release_with_new_pulse);
    CASE(same_button_release_across_ticks);
    CASE(same_button_release_with_unrelated_move);
    CASE(distinct_aims);
    CASE(bounded_pulses_and_overflow);
    CASE(global_cancel_clears_pulses);
    CASE(invalid_event_no_timer_mutation);
    RUN(destroy_leaves_pad_neutral);
    CASE(second_set_zl_and_deferred);
    CASE(cancel_clears_deferred);
    CASE(full_pointer_table);
    CASE(multi_finger_movement_and_skill_group);
    CASE(joystick_cancel_resume_uses_cancel_time);
    CASE(cancel_and_resize_neutralize_owned_gestures);
    CASE(belt_tap_release_and_double_tap);
    CASE(held_second_set_skill_vs_belt);
    CASE(belt_context_expanded_rows);
    CASE(belt_context_requests_validation_and_reset);
    printf("PASS %d/%d cases; %d/%d checks\n", cases, cases, checks, checks);
    return 0;
}
