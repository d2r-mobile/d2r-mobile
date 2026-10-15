#!/bin/bash
# 窗口独占（mini 多槽回归用）。整表把 4 个槽（macsmoke + macsmoke_reg1–3 锁，各自一份重签包、各自容器）并发跑；Mac 冒烟、Mac App、
# 量测这类「另开一个窗口」的任务只拿 macsmoke 锁，只和槽 0 互斥。新窗口一到前台，窗口服务器就撤掉其余槽包的「App 可见 / 窗口可见」断言，
# 被盖住的包不再收到显示刷新回调，出「显示节拍激活中连续 2.0 秒没回调」并丢帧（查实：Mac App 起窗与 reg1–3
# 三局停帧同一时刻开始；mac.sh 等的那 24 秒就是在排 macsmoke 锁）。
# 做法：开窗任务拿到 macsmoke 锁之后再调 macwin_excl_acquire：建 macwin.excl（记 pid + 启动时刻），等 reg1–3 槽锁全空才返回；
#   槽位局（N>0）取槽锁前调 macwin_wait_free，取到后再核一次（防两边同时进门），见到独占标记就放锁等。
# 依赖 devlock.sh 的 devlock_lstart（判持有者还在不在，按 pid + 启动时刻，不按名字）。
# Window exclusivity (for multi-slot regression on the test Mac). A full table runs 4 slots concurrently (macsmoke + macsmoke_reg1–3 locks, each with its own re-signed app and its own container); tasks that "open another window", such as Mac smoke, Mac App
# and measurements, take only the macsmoke lock and are mutually exclusive only with slot 0. Once a new window comes to the foreground, the window server drops the "app visible / window visible" assertions of the other slots' apps,
# and covered apps stop receiving display refresh callbacks, logging "display link active but no callback for 2.0 s straight" and dropping frames (confirmed: the Mac App opening its window and the frame stalls in the reg1–3
# runs started at the same moment; the 24 s mac.sh waited was spent queuing for the macsmoke lock).
# Approach: a window-opening task calls macwin_excl_acquire after acquiring the macsmoke lock: it creates macwin.excl (recording pid + start time) and returns only once the reg1–3 slot locks are all free;
#   slot runs (N>0) call macwin_wait_free before taking a slot lock and check again after acquiring it (guarding against both sides entering at once); on seeing the exclusivity marker they release the lock and wait.
# Depends on devlock.sh's devlock_lstart (checks whether the holder is alive by pid + start time, not by name).
MACWIN_DIR=${MACWIN_DIR:-${DM_ROOT:-$HOME/dm-work}/run/ios}
macwin_alive() {   # $1 = 锁目录 / 标记目录（里面有 pid、lstart）；持有者还在 = 0 | $1 = lock directory / marker directory (containing pid, lstart); returns 0 if the holder is still alive
  local d=$1 p s; p=$(cat "$d/pid" 2>/dev/null); s=$(cat "$d/lstart" 2>/dev/null)
  [ -n "$p" ] && kill -0 "$p" 2>/dev/null && [ "$(devlock_lstart "$p" "$s")" = "$s" ]
}
macwin_excl_acquire() {   # 已持 macsmoke 锁时调；等槽锁全空，最多 MACWIN_WAIT_MAX 秒（缺省 1800） | Call while holding the macsmoke lock; waits until all slot locks are free, at most MACWIN_WAIT_MAX seconds (default 1800)
  local d=$MACWIN_DIR/macwin.excl s=$SECONDS busy n
  mkdir -p "$d" && printf '%s\n' "$$" > "$d/pid" && devlock_lstart "$$" > "$d/lstart" && printf '%s\n' "${1:-未署名}" > "$d/owner" || return 1
  MACWIN_MINE=1
  while :; do
    busy=; for n in 1 2 3; do macwin_alive "$MACWIN_DIR/macsmoke_reg$n.lock" && busy="$busy reg$n"; done
    [ -z "$busy" ] && return 0
    [ $((SECONDS - s)) -ge "${MACWIN_WAIT_MAX:-1800}" ] && { echo "【红】窗口独占：等了 $((SECONDS - s)) 秒槽锁仍在用（${busy# }），不开窗" >&2; macwin_excl_release; return 1; }
    [ $(((SECONDS - s) % 60)) -lt 5 ] && echo "窗口独占：等整表槽位让出（${busy# }，已等 $((SECONDS - s)) 秒）" >&2
    sleep 5
  done
}
macwin_excl_release() { [ "${MACWIN_MINE:-}" = 1 ] || return 0; [ "$(cat "$MACWIN_DIR/macwin.excl/pid" 2>/dev/null)" = "$$" ] && rm -rf "$MACWIN_DIR/macwin.excl"; MACWIN_MINE=; }
macwin_busy() { macwin_alive "$MACWIN_DIR/macwin.excl"; }   # 有活着的开窗任务独占 = 0 | Returns 0 when a live window-opening task holds exclusivity
macwin_wait_free() { local s=$SECONDS; while macwin_busy; do [ $(((SECONDS - s) % 60)) -lt 5 ] && echo "槽位局：等开窗任务（$(cat "$MACWIN_DIR/macwin.excl/owner" 2>/dev/null)）结束，已等 $((SECONDS - s)) 秒" >&2; sleep 5; done; }
