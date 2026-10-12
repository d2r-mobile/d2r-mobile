#!/bin/bash
export DM_DEVBATTERY=0 DEVBATTERY_DIR=$(mktemp -d)   # 锁单测不过电量闸、不写真电量日志（一次挂住的锁单测往真告警文件写了 18 条「读不到」） | Lock unit tests do not go through the battery gate and do not write the real battery log (a hung lock unit test once wrote 18 「读不到」(unreadable) entries into the real alert file)
# devlock.sh 排队次序自测（不碰手机）：持锁者在跑时，先到的测量号、后到的冒烟号、再后到的旧版无类别号——放锁后的拿锁次序应为 冒烟 → 旧版 → 测量。
# Queue-order self-test for devlock.sh (does not touch the phone): while a holder is running, a measurement ticket arrives first, then a smoke test ticket, then an old-style classless ticket - after release the lock should be obtained in the order smoke test → old-style → measurement.
set -u; H=$(cd "$(dirname "$0")" && pwd); T=$(mktemp -d); export DM_DEVLOCK_DIR=$T/device.lock DM_DEVLOCK_QUEUE=$T/device.queue
w(){ ( unset DM_DEVLOCK_HELD; [ "$2" = measure ] && export DM_DEVLOCK_CLASS=measure; source $H/devlock.sh; devlock_acquire "$1" 2>/dev/null; echo "$1" >> $T/order; sleep 0.3; devlock_release ) & }
( source $H/devlock.sh; devlock_acquire 持锁者 2>/dev/null; sleep 4; devlock_release ) &
sleep 1; w 测量 measure; sleep 0.5; w 冒烟 smoke; sleep 0.5
( mkdir -p $DM_DEVLOCK_QUEUE; sleep 30 & P=$!; K=$DM_DEVLOCK_QUEUE/$(python3 -c 'import time;print(time.time_ns())')_$P; mkdir $K; echo 旧版 > $K/owner
  until [ "$(ls $DM_DEVLOCK_QUEUE | LC_ALL=C sort | head -1 | sed 's/.*_//')" = $P ] && [ ! -d $DM_DEVLOCK_DIR ]; do sleep 0.1; done; echo 旧版 >> $T/order; kill $P; rm -rf $DM_DEVLOCK_QUEUE/*_$P ) &
wait; got=$(tr '\n' ' ' < $T/order); rm -rf $T
[ "$got" = "冒烟 旧版 测量 " ] && echo "devlock_test 通过：$got" || { echo "🔴 devlock_test 次序不对：期望「冒烟 旧版 测量」，实际「${got}」"; exit 1; }
