#!/bin/bash
export DM_DEVBATTERY=0 DEVBATTERY_DIR=$(mktemp -d)   # 锁单测不过电量闸、不写真电量日志（一次挂住的锁单测往真告警文件写了 18 条「读不到」） | Lock unit tests do not go through the battery gate and do not write the real battery log (a hung lock unit test once wrote 18 「读不到」(unreadable) entries into the real alert file)
# devlock.sh 持锁上限自测（不碰手机，用临时锁目录）：
#  1. 故意不放锁的持锁者（上限 6 秒，里面挂 sleep 子孙）→ 到点被自己的看门狗终止、子孙清干净、锁释放；排在后面的号随后拿到锁
#  2. 正常持锁 3 秒就放（上限 6 秒）→ 不被终止，看门狗随之退出
#  3. 嵌套 acquire（同进程两层 + 子进程沿用）→ 不另起看门狗、不误杀，上限到点前放锁
# Lock hold limit self-test for devlock.sh (does not touch the phone, uses a temporary lock directory):
#  1. A holder that deliberately does not release (limit 6 seconds, with sleep descendants hanging inside) → terminated on time by its own watchdog, descendants cleaned up, lock released; the ticket queued behind then gets the lock
#  2. A normal hold released after 3 seconds (limit 6 seconds) → not terminated, and the watchdog exits along with it
#  3. Nested acquire (two levels in the same process + inherited by a child process) → no extra watchdog, no wrongful kill, lock released before the limit is reached
set -u; H=$(cd "$(dirname "$0")" && pwd); T=$(mktemp -d); export DM_DEVLOCK_DIR=$T/device.lock DM_DEVLOCK_QUEUE=$T/device.queue DM_DEVLOCK_WD_LOG=$T/wd.log
fail=0; ok(){ echo "  通：$*"; }; bad(){ echo "  🔴 不通：$*"; fail=1; }
echo "1. 不放锁的持锁者"
# 持锁者用独立的 bash 进程（devlock 按 $$ 认持锁者：子 shell 的 $$ 是外层脚本，那样看门狗会把整个自测连根终止）
# The holder uses a separate bash process (devlock identifies the holder by $$: a subshell's $$ is the outer script, in which case the watchdog would terminate the whole self-test, root and all)
bash -c 'source $0/devlock.sh; devlock_acquire 赖着不走 6; sleep 300 & echo $! > $1/kid; sleep 300; echo 不该走到这 > $1/h1.reach' $H $T 2>>$T/h1.err & H1=$!
sleep 1; bash -c 'unset DM_DEVLOCK_HELD; source $0/devlock.sh; t0=$(date +%s); devlock_acquire 后面排队的 2>>$1/w.err; echo $(( $(date +%s) - t0 )) > $1/wait; devlock_release' $H $T & W=$!
wait $W; waited=$(cat $T/wait); sleep 1
kill -0 $H1 2>/dev/null && bad "持锁者没被终止" || ok "持锁者被终止"
kill -0 "$(cat $T/kid)" 2>/dev/null && { bad "子孙 sleep 还活着"; kill "$(cat $T/kid)"; } || ok "子孙清干净"
[ ! -e $T/h1.reach ] && ok "持锁者没继续往下跑" || bad "持锁者继续往下跑了"
[ "$waited" -ge 4 ] && [ "$waited" -le 10 ] && ok "后面的号等了 ${waited} 秒（上限 6 秒）就拿到锁" || bad "后面的号等了 ${waited} 秒"
grep -q '【红】设备锁看门狗：持有者「赖着不走」' $T/h1.err && grep -q '已持有' $T/wd.log && ok "记了红：$(head -1 $T/wd.log | cut -c1-80)…" || bad "没记红"
grep -q '上限 6 秒' $T/w.err && ok "等锁方提示带上限：$(grep -m1 上限 $T/w.err)" || bad "等锁方提示没有上限：$(cat $T/w.err)"
[ ! -d $DM_DEVLOCK_DIR ] && ok "锁目录已清" || bad "锁目录还在"
echo "2. 正常持锁 3 秒"
bash -c 'source $0/devlock.sh; devlock_acquire 正常 6 2>/dev/null; sleep 3; devlock_release; sleep 6; echo 活着 > $1/h2.alive' $H $T; [ -e $T/h2.alive ] && ok "放锁后进程照常走完，没被杀" || bad "被误杀"
echo "3. 嵌套"
bash -c 'source $0/devlock.sh; devlock_acquire 外层 8 2>/dev/null; devlock_acquire 内层 2 2>/dev/null; bash -c "source $0/devlock.sh; devlock_acquire 子进程 2 2>/dev/null; sleep 4; devlock_release"; sleep 2; devlock_release; devlock_release; echo 走完 > $1/h3.done' $H $T
[ -e $T/h3.done ] && ok "嵌套 6 秒（内层上限 2 秒不生效，外层 8 秒）没被误杀" || bad "嵌套被误杀"
[ ! -d $DM_DEVLOCK_DIR ] && ok "锁已释放" || bad "锁没释放"
echo "4. set -e 的调用方（touch_e2e.sh 的写法）：持锁几秒后靠 EXIT 钩子放锁，锁必须放掉"
for hold in 0 1 2 3; do
  bash -c 'set -euo pipefail; source $0/devlock.sh; devlock_acquire "钩子放锁 $2" 60 2>/dev/null; trap devlock_release EXIT; sleep $2' $H $T $hold; rc=$?
  [ $rc = 0 ] && [ ! -d $DM_DEVLOCK_DIR ] && ok "持锁 $hold 秒：退出码 0、锁已放" || { bad "持锁 $hold 秒：退出码 ${rc}、锁目录$([ -d $DM_DEVLOCK_DIR ] && echo 还在 || echo 已放)"; rm -rf $DM_DEVLOCK_DIR; }
done
echo "5. 持有者已死的锁：set -e 的后来者要收回并拿到"
mkdir $DM_DEVLOCK_DIR; echo 999999 > $DM_DEVLOCK_DIR/pid; echo 死掉的持有者 > $DM_DEVLOCK_DIR/owner; date +%s > $DM_DEVLOCK_DIR/started; echo "Sun Oct 11 01:06:01 2026" > $DM_DEVLOCK_DIR/lstart; echo 600 > $DM_DEVLOCK_DIR/max
bash -c 'set -euo pipefail; unset DM_DEVLOCK_HELD; source $0/devlock.sh; devlock_acquire 后来者 60 2>$1/s5.err; trap devlock_release EXIT; cat $DM_DEVLOCK_DIR/owner > $1/s5.owner' $H $T; rc=$?
[ $rc = 0 ] && [ "$(cat $T/s5.owner 2>/dev/null)" = 后来者 ] && ok "死锁被收回，后来者拿到锁" || bad "退出码 ${rc}，锁内 owner「$(cat $DM_DEVLOCK_DIR/owner 2>/dev/null)」，输出：$(cat $T/s5.err)"
[ ! -d $DM_DEVLOCK_DIR ] && ok "后来者退出后锁已放" || { bad "后来者退出后锁还在"; rm -rf $DM_DEVLOCK_DIR; }
echo "6. 取不到锁要响亮：锁目录建不出来（父目录是个文件）→ 打【红】+ 非零退出"
: > $T/notdir; DM_DEVLOCK_DIR=$T/notdir/device.lock DM_DEVLOCK_QUEUE=$T/q6 bash -c 'set -euo pipefail; unset DM_DEVLOCK_HELD; source $0/devlock.sh; devlock_acquire 取不到的 5 2>$1/s6.err; echo 不该走到这 > $1/s6.reach' $H $T & P6=$!
( sleep 8; kill -KILL $P6 2>/dev/null ) & K6=$!; wait $P6 2>/dev/null; rc=$?; kill $K6 2>/dev/null
[ $rc != 0 ] && [ $rc != 137 ] && grep -q '【红】设备锁：取不到的 没取到锁' $T/s6.err && [ ! -e $T/s6.reach ] && ok "退出码 ${rc}：$(grep -m1 【红】 $T/s6.err | cut -c1-60)…" || bad "退出码 ${rc}（137 = 挂住被本测试杀掉），输出：$(head -2 $T/s6.err)"
rm -rf $T; [ $fail = 0 ] && echo "devlock_max_test 通过" || { echo "🔴 devlock_max_test 有不通"; exit 1; }
