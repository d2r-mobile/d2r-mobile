#!/bin/bash
# devlock.sh 跨语言环境自测（不碰手机）：ps -o lstart 的字符串跟语言环境走，持有者与抢锁者环境不同时，活着的持有者不能被当无主锁拆掉。
# 三格：① 中文环境持锁、C 环境来抢 → 等持有者放锁；② 修正前在中文环境写的旧锁（lstart 是中文串）、持有者活着 → 等；③ 持有者已死 → 收回。
# Cross-locale self-test for devlock.sh (does not touch the phone): the ps -o lstart string follows the locale; when holder and grabber have different locales, a live holder must not be torn down as an ownerless lock.
# Three cells: ① Chinese-locale holder, C-locale grabber → wait for the holder to release; ② an old lock written in a Chinese locale before the fix (lstart is a Chinese string), holder alive → wait; ③ holder dead → reclaim.
set -u; H=$(cd "$(dirname "$0")" && pwd); T=$(mktemp -d); export DM_DEVLOCK_DIR=$T/device.lock DM_DEVLOCK_QUEUE=$T/device.queue; bad=0
ok(){ echo "  ✓ $1"; }; no(){ echo "  🔴 $1"; bad=1; }
# ① 中文环境持锁 4 秒，1 秒后 C 环境来抢：抢到的时刻必须在持有者放锁之后
# ① A Chinese-locale process holds the lock for 4 seconds and a C-locale one comes to grab 1 second later: the moment it gets the lock must be after the holder releases
( unset DM_DEVLOCK_HELD; export LC_ALL=zh_CN.UTF-8; source $H/devlock.sh; devlock_acquire 甲 2>/dev/null; sleep 4; date +%s > $T/a_rel; devlock_release ) &
sleep 1
( unset DM_DEVLOCK_HELD; export LC_ALL=C; source $H/devlock.sh; devlock_acquire 乙 2>/dev/null; date +%s > $T/b_got; devlock_release )
wait; [ "$(cat $T/b_got)" -ge "$(cat $T/a_rel)" ] && ok "① 中文环境持锁时 C 环境的等到放锁才拿到" || no "① C 环境的在中文环境持有者还活着时拿到了锁"
# ② 旧锁：持有者活着、lstart 是中文环境写的串（修正前的写法），C 环境来抢 → 不能拆
# ② Old lock: holder alive, lstart is a string written in a Chinese locale (the pre-fix way), a C-locale process comes to grab → must not be torn down
sleep 30 & P=$!; mkdir -p $DM_DEVLOCK_DIR; echo $P > $DM_DEVLOCK_DIR/pid; echo 旧锁 > $DM_DEVLOCK_DIR/owner; date +%s > $DM_DEVLOCK_DIR/started
LC_ALL=zh_CN.UTF-8 ps -o lstart= -p $P | sed 's/^[[:space:]]*//; s/[[:space:]]*$//' > $DM_DEVLOCK_DIR/lstart
( sleep 3; date +%s > $T/p_kill; kill $P ) &
( unset DM_DEVLOCK_HELD; export LC_ALL=C; source $H/devlock.sh; devlock_acquire 丙 2>/dev/null; date +%s > $T/c_got; devlock_release )
wait; [ "$(cat $T/c_got)" -ge "$(cat $T/p_kill)" ] && ok "② 中文串旧锁的持有者活着时没被拆，死了才收回" || no "② 中文串旧锁在持有者活着时被拆了"
# ③ 持有者已死（pid 不在）→ 立即收回
# ③ Holder is dead (pid gone) → reclaim immediately
mkdir -p $DM_DEVLOCK_DIR; echo 999999 > $DM_DEVLOCK_DIR/pid; echo 死锁 > $DM_DEVLOCK_DIR/owner; echo "Thu Jan  1 00:00:00 2026" > $DM_DEVLOCK_DIR/lstart
t0=$(date +%s); ( unset DM_DEVLOCK_HELD; source $H/devlock.sh; devlock_acquire 丁 2>/dev/null; devlock_release ); [ $(( $(date +%s) - t0 )) -le 3 ] && ok "③ 死持有者的锁 3 秒内收回" || no "③ 死持有者的锁没及时收回"
rm -rf $T; [ $bad = 0 ] && echo "devlock_locale_test 通过 3/3" || { echo "🔴 devlock_locale_test 有不过的"; exit 1; }
