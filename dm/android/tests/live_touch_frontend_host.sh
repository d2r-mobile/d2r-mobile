#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/live-frontend.XXXXXX")
trap 'rm -rf "$work"' EXIT
source=${1:-"$here/../native/frontend_activity.c"}
sed -n '/uint32_t mask=navigation?/,+1p' "$source" > "$work/frontend_mask_actual.inc"
sed -n '/^static void shop_ghost_contact_event(game_app \*s,int action,int32_t id) {/,/^}/p' "$source" > "$work/frontend_actual.inc"
sed -n '/^static void shop_ghost_sync(game_app \*s,const dm_tc_state \*c,const dm_tc_finger \*f) {/,/^}/p' "$source" >> "$work/frontend_actual.inc"
sed -n '/^static void live_touch_frontend_tick(game_app \*s) {/,/^}/p' "$source" >> "$work/frontend_actual.inc"
sed -n '/^static void live_frontend_touch(game_app \*s,int phase,int id,float nx,float ny){/,/^}/p' "$source" >> "$work/frontend_actual.inc"
"${CC:-cc}" -std=c11 -fsanitize=address,undefined -g -I "$work" -I "$here/../native" \
  -I "$here/../../ios/Sources" -I "$here/../../host" "$here/live_touch_frontend_host.c" \
  "$here/../native/live_touch.c" "$here/../../ios/Sources/dm_touch_controls.c" -lm -o "$work/test"
"$work/test"
sed -n '/^static _Atomic int g_map_hidden_req/,/^}/p' "$here/../../host/dm_cursor.c" > "$work/map_visibility_actual.inc"
"${CC:-cc}" -std=c11 -fsanitize=address,undefined -g -I "$work" "$here/map_visibility_host.c" -o "$work/map_visibility"
"$work/map_visibility"
sed -n '/^static pthread_mutex_t g_map_title_lock/,/^\/\/ 真全屏/{ /^\/\/ 真全屏/!p; }' "$here/../../host/dm_cursor.c" > "$work/map_title_actual.inc"
"${CC:-cc}" -std=c11 -fsanitize=address,undefined -g -I "$work" "$here/map_title_host.c" -pthread -o "$work/map_title"
"$work/map_title"
for test in live_touch_map_test live_touch_items_test live_touch_scheduler_test; do
    # The scheduler fixture includes live_touch.c itself.
    sources=("$here/../native/live_touch_items.c")
    if [[ "$test" != live_touch_scheduler_test ]]; then sources+=("$here/../native/live_touch.c"); fi
    "${CC:-cc}" -std=c11 -fsanitize=address,undefined -g -I "$here/../native" \
      -I "$here/../../ios/Sources" -I "$here/../../host" "$here/$test.c" \
      "${sources[@]}" "$here/../../ios/Sources/dm_touch_controls.c" -lm -o "$work/$test"
    "$work/$test"
done
