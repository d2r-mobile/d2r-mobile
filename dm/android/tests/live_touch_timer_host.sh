#!/usr/bin/env bash
# Exercise the actual timer programmer with an owned timerfd boundary.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
source=${1:-"$here/../native/frontend_activity.c"}
work=$(mktemp -d "${TMPDIR:-/tmp}/live-touch-timer.XXXXXX")
trap 'rm -rf "$work"' EXIT
cat > "$work/test.c" <<'C'
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "live_owner_timer.h"
typedef struct { int touch_timer_fd, touch_timer_active; } game_app;
struct itimerspec { struct { long tv_sec, tv_nsec; } it_interval, it_value; };
static int calls, fail;
static long deadline;
static int timerfd_settime(int fd, int flags, const struct itimerspec *value, void *old) {
    assert(fd == 3 && flags == 0 && !old);
    assert(value->it_interval.tv_nsec == value->it_value.tv_nsec);
    calls++; deadline = value->it_value.tv_nsec;
    return fail ? -1 : 0;
}
#define LOG(...) ((void)0)
C
sed -n '/^static void touch_timer_set_period(/,/^}/p' "$source" >> "$work/test.c"
cat >> "$work/test.c" <<'C'
int main(void) {
    game_app app = {3, 0};
    touch_timer_set_period(&app, 0);
    assert(calls == 0);
    // 120Hz MOVE events must not keep postponing the 16ms owner deadline.
    for (int i = 0; i < 100; ++i) touch_timer_set_period(&app, 1);
    assert(calls == 1 && app.touch_timer_active == 1 && deadline == 16000000);
    touch_timer_set_period(&app, 0);
    assert(calls == 2 && deadline == 100000000);
    fail = 1;
    touch_timer_set_period(&app, 1);
    assert(calls == 3 && app.touch_timer_active == 0);
    fail = 0;
    touch_timer_set_period(&app, 1);
    assert(calls == 4 && app.touch_timer_active == 1);
    app.touch_timer_fd = -1;
    touch_timer_set_period(&app, 0);
    assert(calls == 4);
    puts("PASS: continuous MOVE preserves owner deadline; mode changes and timer failure remain explicit");
}
C
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I "$here/../native" "$work/test.c" -o "$work/test"
"$work/test"
