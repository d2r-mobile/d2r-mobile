#!/bin/bash
# devbattery.sh 单测：假读数（DEVBATTERY_FAKE）+ 临时目录，不碰设备。期望值按规则手算：< 30 拒并落标记、标记在时 < 60 拒、≥ 60 撤标记放行、读不到拒
# Unit test for devbattery.sh: fake readings (DEVBATTERY_FAKE) + temporary directory, no device touched. Expected values are computed by hand from the rules: < 30 refuse and drop the marker; with the marker present, < 60 refuse; ≥ 60 remove the marker and pass; unreadable refuse
cd "$(dirname "$0")"; T=$(mktemp -d); export DEVBATTERY_DIR=$T;   # 单测一律写临时目录，不碰 $DM_ROOT/run/ios 的真日志与告警 | Unit tests always write to a temporary directory and never touch the real logs and alerts in $DM_ROOT/run/ios
 source ./devbattery.sh; bad=0
ck() { local want=$1 got; shift; DEVBATTERY_FAKE="$*" devbattery_gate 2>/dev/null; got=$?; [ "$got" = "$want" ] && echo "通 $* → $got" || { echo "不通 $* → $got（期望 $want）"; bad=1; }; }
ck 0 80 0 1 1          # 80%：放行 | 80%: pass
ck 1 25 1 1 0          # 25%：拒，落标记 | 25%: refuse, drop the marker
[ -f $T/device_low_battery ] && echo "通 低电落标记" || { echo "不通 低电没落标记"; bad=1; }
ck 1 45 1 1 0          # 标记在、45%：拒 | Marker present, 45%: refuse
ck 0 60 1 1 0          # 60%：撤标记放行 | 60%: remove the marker and pass
[ ! -f $T/device_low_battery ] && echo "通 撤标记" || { echo "不通 标记没撤"; bad=1; }
ck 0 45 1 1 0          # 没标记、45%：放行 | No marker, 45%: pass
out=$(DEVBATTERY_FAKE="50 0 1 0" devbattery_gate 2>&1); case "$out" in *供电跟不上*) echo "通 插电不充记红";; *) echo "不通 插电不充没记红"; bad=1;; esac
out=$(DEVBATTERY_FAKE="99 0 1 0" devbattery_gate 2>&1); case "$out" in *供电跟不上*) echo "不通 99% 停充被当成供电跟不上"; bad=1;; *) echo "通 99% 停充不记红";; esac
DEVBATTERY_FAKE= DM_IOS_UDID=00000000-0000000000000000 devbattery_gate 2>/dev/null && { echo "不通 读不到却放行"; bad=1; } || echo "通 读不到拒"
grep -q '读不到' $T/battery.log && echo "通 读不到记「读不到」" || { echo "不通 读不到没记对"; bad=1; }
DEVBATTERY_FAKE= DM_IOS_UDID= DM_LOCAL_ENV=/dev/null HOME=$T devbattery_gate 2>/dev/null && { echo "不通 没配 UDID 却放行"; bad=1; } || echo "通 没配 UDID 拒"
grep -q '没配 UDID' $T/battery.log && echo "通 没配 UDID 记「没配 UDID」" || { echo "不通 没配 UDID 没分开记"; bad=1; }
grep -q 通知 $T/device_alert.txt && echo "通 告警文件有记录" || { echo "不通 告警文件没记录"; bad=1; }
# devlock 接入：device.lock 目录取锁前过闸——电量够直接取到；电量低等满上限退出 4、不取锁
# devlock integration: go through the gate before taking the device.lock directory lock - with enough battery the lock is taken directly; with low battery it waits out the full limit, exits 4 and does not take the lock
L=$T/device.lock; export DEVBATTERY_DIR=$T
DM_DEVBATTERY=1 DEVBATTERY_FAKE="80 0 1 0" DM_DEVLOCK_DIR=$L bash -c 'source ./devlock.sh; devlock_acquire 单测 2>/dev/null; [ -d "$DEVLOCK_DIR" ] && devlock_release' && echo "通 devlock 电量够取到锁" || { echo "不通 devlock 电量够没取到"; bad=1; }
DM_DEVBATTERY=1 DEVBATTERY_FAKE="10 0 1 0" DM_DEVBATTERY_WAIT_S=2 DM_DEVBATTERY_POLL_S=1 DM_DEVLOCK_DIR=$L bash -c 'source ./devlock.sh; devlock_acquire 单测 2>/dev/null; echo 不该走到这' >/dev/null; r=$?
[ $r = 4 ] && [ ! -d $L ] && echo "通 devlock 电量低退出 4 且没取锁" || { echo "不通 devlock 电量低 → $r"; bad=1; }
rm -rf "$T"; [ $bad = 0 ] && echo "devbattery 单测全过" || { echo "devbattery 单测有不通"; exit 1; }
