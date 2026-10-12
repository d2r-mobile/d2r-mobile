# 测试机电量闸（测试 iPhone 01:46 没电关机、断到 09:26——插在 Mac 上供电跟不上，屏幕常亮 10 小时电量一路掉到 1%）。
# 用法：source 本文件后 devbattery_gate || exit 1（真机局取设备锁之前调）。读法：ideviceinfo 电池域（USB 不通再走网络 -n）。
#   电量 < 30%：落暂停标记 $DM_ROOT/run/ios/device_low_battery、记【红】、记进 $DM_ROOT/run/ios/device_alert.txt，拒绝开跑（返回 1），之后所有真机局都拒绝；
#   有暂停标记时电量 ≥ 60% 才撤标记放行（充电中途 30%–60% 一律拒绝，免得刚过线又跑没电）；
#   插着电却没在充、且电量低于 90% 才记【红】提醒（供电跟不上），但不拦——系统在高电量下会自己停充（全天停在 99–100%、「充电=0」，旧判据「低于 100%」整下午误报）；读不到电量记【红】并拒绝（多半设备已离线）。
# 放行时把「电量 N%（充电 / 未充电，电源 有 / 无）」写进 DEVBATTERY_LINE，调用方记进表头 / 日志；每次读数追加到 $DM_ROOT/run/ios/battery.log。
# 测试开关：DEVBATTERY_FAKE="<电量> <充电 0/1> <电源 0/1>" 不读设备，直接用这组数（单测 devbattery_test.sh 用）。
# Battery gate for the test device (the test iPhone ran out of power and shut down at 01:46 and stayed offline until 09:26: power from the Mac it was
# plugged into could not keep up, and with the screen always on for 10 hours the charge fell all the way to 1%).
# Usage: after sourcing this file, devbattery_gate || exit 1 (call it before taking the device lock for a real-device run). How it reads: the
# ideviceinfo battery domain (falls back to network -n if USB is unavailable).
#   Charge < 30%: drops the pause marker $DM_ROOT/run/ios/device_low_battery, logs [RED], records it in $DM_ROOT/run/ios/device_alert.txt, and refuses
#   to start (returns 1); every real-device run after that is refused too;
#   with the pause marker present, the marker is removed and runs are allowed only at charge ≥ 60% (30%–60% in the middle of charging is always refused,
#   so a run does not drain the device again right after crossing the line);
#   plugged in but not charging, and charge below 90%: only then log [RED] as a warning (power cannot keep up), without blocking. The system stops
#   charging by itself at high charge (sitting at 99–100% all day with "charging=0"; the old criterion "below 100%" raised false alarms all afternoon).
#   If the charge cannot be read, log [RED] and refuse (most likely the device is offline).
# When a run is allowed, "charge N% (charging / not charging, power present / absent)" is written into DEVBATTERY_LINE for the caller to put in the
# table header / log; every reading is appended to $DM_ROOT/run/ios/battery.log.
# Test switch: DEVBATTERY_FAKE="<charge> <charging 0/1> <power 0/1>" does not read the device and uses these values directly (used by the unit test
# devbattery_test.sh).
DEVBATTERY_LOW=${DEVBATTERY_LOW:-30}; DEVBATTERY_RESUME=${DEVBATTERY_RESUME:-60}
DEVBATTERY_DIR=${DEVBATTERY_DIR:-${DM_ROOT:-$HOME/dm-work}/run/ios}
devbattery_read() {   # 输出「电量 充电 电源」三个数（0/1），读不到返回 1，没配 UDID 返回 2，本机缺工具返回 3 | Prints three numbers "level charging power" (0/1); returns 1 when unreadable, 2 when no UDID is configured, 3 when a local tool is missing
  if [ -n "${DEVBATTERY_FAKE:-}" ]; then echo "$DEVBATTERY_FAKE"; return 0; fi
  local u=${DM_IOS_UDID:-} out
  [ -n "$u" ] || { source "$(dirname "${BASH_SOURCE[0]}")/local_env.sh" 2>/dev/null; u=${DM_IOS_UDID:-}; }   # 调用方没 source 本机配置时自己取 | Fetch it ourselves when the caller has not sourced the local configuration
  [ -n "$u" ] || return 2   # 没配 UDID（与「读不到」分开报） | UDID not configured (reported separately from "unreadable")
  # 本机缺工具与「设备没应答」分开报（返回 3）：timeout 来自 coreutils，macOS 不自带；缺它时下面两条命令根本起不来，原先一律报成「设备多半离线」并让调用方白等到上限
  # A missing local tool is reported separately from "the device did not answer" (returns 3): timeout comes from coreutils and macOS does not ship it; without it the two commands below cannot even start, and this used to be reported as "device probably offline" while the caller waited until its limit
  local tb; tb=$(command -v timeout || command -v gtimeout) || return 3
  command -v ideviceinfo > /dev/null || return 3
  out=$("$tb" 20 ideviceinfo -u "$u" -q com.apple.mobile.battery 2>/dev/null) || out=$("$tb" 20 ideviceinfo -n -u "$u" -q com.apple.mobile.battery 2>/dev/null) || return 1
  awk '/^BatteryCurrentCapacity:/{p=$2} /^BatteryIsCharging:/{c=($2=="true")} /^ExternalConnected:/{e=($2=="true")} /^FullyCharged:/{f=($2=="true")} END{ if (p=="") exit 1; print p, c+0, e+0, f+0 }' <<<"$out"
}
devbattery_gate() {
  mkdir -p "$DEVBATTERY_DIR"; local flag=$DEVBATTERY_DIR/device_low_battery log=$DEVBATTERY_DIR/battery.log r p c e f now; now=$(date '+%m-%d %H:%M:%S')
  r=$(devbattery_read); local rc=$?
  if [ $rc = 2 ]; then
    echo "$now 没配 UDID" >> "$log"; DEVBATTERY_LINE="电量 没配 UDID"
    echo "【红】没配测试机 UDID（本机配置 ~/.config/dm-ios/local.env 的 DM_IOS_UDID 为空），读不了电量，这局不跑" >&2; devbattery_notify "没配测试机 UDID，真机局已拒绝"; return 1; fi
  if [ $rc = 3 ]; then
    echo "$now 本机缺工具" >> "$log"; DEVBATTERY_LINE="电量 读不了：本机缺 timeout 或 ideviceinfo"
    echo "【红】本机缺读电量要用的命令：timeout（brew install coreutils）$(command -v timeout gtimeout > /dev/null && echo 有 || echo 没有)，ideviceinfo（brew install libimobiledevice）$(command -v ideviceinfo > /dev/null && echo 有 || echo 没有)。与设备无关，装好再跑" >&2; return 3; fi
  if [ $rc != 0 ]; then
    echo "$now 读不到" >> "$log"; DEVBATTERY_LINE="电量 读不到"
    echo "【红】测试机电量读不到（ideviceinfo 不通，设备多半离线或没电关机），这局不跑" >&2; devbattery_notify "测试机电量读不到，真机局已拒绝"; return 1; fi
  read -r p c e f <<<"$r"; f=${f:-0}
  DEVBATTERY_LINE="电量 ${p}%（$([ "$c" = 1 ] && echo 充电 || echo 未充电)，电源 $([ "$e" = 1 ] && echo 有 || echo 无)）"
  echo "$now $p% 充电=$c 电源=$e 充满=$f" >> "$log"
  if [ -f "$flag" ]; then
    if [ "$p" -ge "$DEVBATTERY_RESUME" ]; then rm -f "$flag"; echo "测试机电量回到 ${p}%（≥ ${DEVBATTERY_RESUME}%），撤掉暂停标记，真机局放行" >&2
    else echo "【红】测试机电量暂停中：现 ${p}%，充到 ${DEVBATTERY_RESUME}% 才放行（标记 ${flag}：$(head -1 "$flag")）" >&2; return 1; fi
  elif [ "$p" -lt "$DEVBATTERY_LOW" ]; then
    echo "$now 电量 ${p}% 充电=$c 电源=$e" > "$flag"
    echo "【红】测试机电量 ${p}% 低于 ${DEVBATTERY_LOW}%：暂停全部真机局，充到 ${DEVBATTERY_RESUME}% 再放行（标记 ${flag}）" >&2
    devbattery_notify "测试机电量 ${p}%，真机局已暂停，充到 ${DEVBATTERY_RESUME}% 放行"; return 1
  fi
  [ "$e" = 1 ] && [ "$c" = 0 ] && [ "$f" = 0 ] && [ "$p" -lt "${DEVBATTERY_NOCHARGE:-90}" ] && echo "【红】测试机插着电源却没在充（${p}%）：供电跟不上，局照跑，请换充电头 / 线" >&2
  return 0
}
devbattery_notify() {   # 记进 battery.log 与 device_alert.txt（巡检读这个文件；不用系统通知：osascript 首次会弹权限框） | Record into battery.log and device_alert.txt (the patrol reads this file; system notifications are not used: osascript pops a permission dialog the first time)
  echo "$(date '+%m-%d %H:%M:%S') 通知：$1" | tee -a "$DEVBATTERY_DIR/battery.log" >> "$DEVBATTERY_DIR/device_alert.txt"
}
