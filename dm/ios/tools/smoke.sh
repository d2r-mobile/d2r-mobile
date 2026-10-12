#!/bin/bash
# 真机启动冒烟：构建、装机后按次数启动，验证主菜单、进局、撤下加载屏三项。
# Real device launch smoke test: build and install, then launch the given number of times, verifying three items: main menu, entering the game, loading screen dismissed.
set -u

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/local_env.sh" && local_env_need DM_DEVICE || exit 1   # 本机设备配置（不进仓，见 dm/ios/local.env.example） | Local device configuration (not committed, see dm/ios/local.env.example)
D=$DM_DEVICE
B=${DM_BUNDLE_ID:?先在本机配置里设 DM_BUNDLE_ID（App 包名，见 dm/ios/local.env.example）}
N=${1:-1}
ROOT=${DM_ROOT:-$HOME/dm-work}/run/ios/smoke
RUN="$ROOT/$(date +%Y%m%d_%H%M%S)"
R=${DM_ROOT:-$HOME/dm-work}/run/ios
# 派生数据放被测树自己的 .iosdd（与 accept ⑧ 同）：共享 $DM_ROOT/run/ios/dd 会被别的工作树同时构建覆盖，装出签名不全的包（ApplicationVerificationFailed）
# Derived data goes in the tree under test's own .iosdd (same as accept ⑧): the shared $DM_ROOT/run/ios/dd gets overwritten by other worktrees building at the same time, installing a package with incomplete signing (ApplicationVerificationFailed)
TREE=$(git rev-parse --show-toplevel 2>/dev/null || pwd); DD=$TREE/.iosdd
APP=$DD/Build/Products/Release-iphoneos/DmIOS.app
KEYS_DEFAULT='X@OpenPanel:MainMenuButtonRibbon>OpenPanel:CharacterSelectPanel,C0.84:0.208@OpenPanel:CharacterSelectPanel%1>UpdateCharacterInfo,MSG:CharacterSelect:LoadCharacter:0@UpdateCharacterInfo%0.3>FrontEndNavigation:ToGame'
KEYS=${SMOKE_KEYS:-$KEYS_DEFAULT}
TIMEOUT_BIN=${TIMEOUT_BIN:-$(command -v timeout || command -v gtimeout || true)}

if [ -z "$TIMEOUT_BIN" ]; then
  echo "SMOKE RED 0/$N 用时 0 秒"
  echo "【红】缺少 timeout 命令"
  exit 2
fi
case "$N" in
  ''|*[!0-9]*|0) echo "SMOKE RED 0/$N 用时 0 秒"; echo "【红】次数必须是正整数"; exit 2;;
esac

mkdir -p "$RUN"
TOOLS=$(cd "$(dirname "$0")" && pwd)
# 本机这头的端口每次现取一个空的：两台测试机各有各的设备锁，两边的冒烟会同时跑，写死同一个端口后起的那个转发绑不上、读到的是先起那台的日志
# The local end of the port is a free one picked fresh each time: the two test devices each have their own device lock and smoke tests on both run at
# the same time; with one hard-coded port the forward started later cannot bind, and what is read is the log of the device started first
DEV_PORT=${SMOKE_DEV_PORT:-27100}; LOCAL_PORT=${SMOKE_LOCAL_PORT:-$(python3 -c "import socket;s=socket.socket();s.bind(('127.0.0.1',0));print(s.getsockname()[1])")}
command -v iproxy >/dev/null || { echo "SMOKE RED 0/$N 用时 0 秒"; echo "【红】缺少 iproxy（brew install libimobiledevice）"; exit 2; }
# 设备锁：与 run.sh 碰手机路径同一把（dm/ios/tools/devlock.sh：可重入、记 lstart、旧格式锁兼容）
# 电量闸（测试机没电关机断了 7 小时）：取锁前读电量，< 30% 暂停全部真机局、充到 60% 放行（dm/ios/tools/devbattery.sh）
# Device lock: the same one as run.sh's phone-touching paths (dm/ios/tools/devlock.sh: reentrant, records lstart, compatible with old-format locks)
# Battery gate (the test device ran out of power and shut down, cutting things off for 7 hours): read the battery level before taking the lock; < 30% pauses all real device runs, released once charged to 60% (dm/ios/tools/devbattery.sh)
source "$(dirname "$0")/devbattery.sh"; devbattery_gate || { echo "SMOKE RED 0/$N 用时 0 秒"; echo "【红】电量闸拒绝：${DEVBATTERY_LINE:-}"; exit 2; }; echo "冒烟开跑前 $DEVBATTERY_LINE"
source "$(dirname "$0")/devlock.sh"
devlock_acquire "${SMOKE_OWNER:-未署名} 冒烟 $(basename "$(git rev-parse --show-toplevel 2>/dev/null || pwd)")"
# 冒烟台账：每次运行（含构建失败 / 被信号中断）追加一行，合入前按 HEAD 查 3/3。
# 列：时刻 调用方 被测树 HEAD 未提交改动数 结果 ok/N 用时秒 各次分段 结果目录；未提交改动数 > 0 时这行不能证明 HEAD 冒烟过
# 设了 SMOKE_KEYS（自定键序 = 反例 / 专项）的运行，结果列前缀「反例:」，不算该 HEAD 的冒烟记录
# Smoke-test ledger (a log of past smoke results, one line per run): every invocation (including build failures / signal interruptions) appends one line; before merge, 3/3 is checked by HEAD.
# Columns: time, caller, tree under test, HEAD, number of uncommitted changes, result ok/N, elapsed seconds, per-launch segments, result directory; when the number of uncommitted changes > 0 this line cannot prove HEAD passed the smoke test
# For invocations with SMOKE_KEYS set (custom key sequence = negative case / targeted test), the result column is prefixed with 「反例:」(negative case:), and they do not count as that HEAD's smoke-test record
LEDGER=${SMOKE_LEDGER:-${DM_ROOT:-$HOME/dm-work}/run/ios/smoke_ledger.tsv}; LEDGER_RES=中断; LEDGER_SEGS=""; ok=0
ledger_write() {
  local top head dirty
  top=$(git rev-parse --show-toplevel 2>/dev/null || pwd); head=$(git -C "$top" rev-parse HEAD 2>/dev/null || echo 无)
  dirty=$(git -C "$top" status --porcelain --untracked-files=no 2>/dev/null | wc -l | tr -d ' ')
  mkdir -p "$(dirname "$LEDGER")"
  [ -s "$LEDGER" ] || printf '时刻\t调用方\t被测树\tHEAD\t未提交改动\t结果\t成功/次数\t用时秒\t各次分段\t结果目录\n' >> "$LEDGER"
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s/%s\t%s\t%s\t%s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "${SMOKE_OWNER:-未署名}" "$top" "$head" "$dirty" \
    "${SMOKE_KEYS:+反例:}${SMOKE_TEST_STALE:+反例:}$LEDGER_RES" "$ok" "$N" "$(( $(date +%s) - ${T0:-$(date +%s)} ))" "${LEDGER_SEGS:-无}" "$RUN" >> "$LEDGER"
}
trap 'ledger_write; [ -n "${IPROXY_PID:-}" ] && kill "$IPROXY_PID" 2>/dev/null; devlock_release' EXIT
trap 'exit 143' INT TERM   # 收信号必须退出（经 EXIT 放锁）；只放锁不退出会让本进程继续碰手机而锁已归别人 | On receiving a signal it must exit (releasing the lock via EXIT); releasing the lock without exiting would let this process keep touching the phone while the lock already belongs to someone else

T0=$(date +%s)
echo "SMOKE 开始 $N 次，结果目录 $RUN"

if ! "$TIMEOUT_BIN" 600s env DM_NOINSTALL=1 DM_IOS_DD="$DD" bash dm/ios/run.sh build >"$RUN/build.log" 2>&1; then
  LEDGER_RES=构建失败
  echo "【红】构建失败：$RUN/build.log"
  echo "SMOKE RED 0/$N 用时 $(( $(date +%s) - T0 )) 秒"
  exit 2
fi
if ! "$TIMEOUT_BIN" 60s xcrun devicectl device install app --device "$D" "$APP" >"$RUN/install.log" 2>&1; then
  LEDGER_RES=装机失败
  echo "【红】装机失败：$RUN/install.log"
  echo "SMOKE RED 0/$N 用时 $(( $(date +%s) - T0 )) 秒"
  exit 2
fi

# 端口转发必须点名设备：电脑上插着两台测试机时，不点名的 iproxy 自己挑一台，挑到另一台就是「包在听、这边全程拒连、日志端口零行」
# （合批冒烟 0/3：包装在、起在目标机上，转发连的是另一台）。按 DM_DEVICE 查出这台的 UDID；查不到就不跑，不拿「大概是它」去试
# Port forwarding must name the device: with two test devices plugged into the computer, an iproxy without a device name picks one by itself, and
# picking the other one gives "the app is listening, connections refused here the whole time, zero lines on the log port"
# (batch smoke test 0/3: the app was installed and launched on the target device, but the forward was connected to the other one). Look up this device's
# UDID from DM_DEVICE; if it cannot be found do not run, and do not try with "probably this one"
"$TIMEOUT_BIN" 20s xcrun devicectl list devices --json-output "$RUN/devices.json" >/dev/null 2>&1
UDID=$(python3 -c "import json,sys;print(next((d.get('hardwareProperties',{}).get('udid','') for d in json.load(open(sys.argv[1]))['result']['devices'] if sys.argv[2] in (d.get('identifier'), d.get('hardwareProperties',{}).get('udid'))), ''))" "$RUN/devices.json" "$D" 2>/dev/null)
if [ -z "$UDID" ]; then
  LEDGER_RES=查不到设备
  echo "【红】按 DM_DEVICE 查不到这台设备的 UDID（$RUN/devices.json）：端口转发没法点名设备"
  echo "SMOKE RED 0/$N 用时 $(( $(date +%s) - T0 )) 秒"
  exit 2
fi
iproxy -u "$UDID" "$LOCAL_PORT:$DEV_PORT" >"$RUN/iproxy.log" 2>&1 & IPROXY_PID=$!
sleep 1; kill -0 "$IPROXY_PID" 2>/dev/null || { LEDGER_RES=转发没起来; echo "【红】端口转发没起来（$RUN/iproxy.log：$(tail -1 "$RUN/iproxy.log" | cut -c1-120)）"; echo "SMOKE RED 0/$N 用时 $(( $(date +%s) - T0 )) 秒"; exit 2; }

# CoreDeviceService 是本用户的 XPC 服务，结束后下一条 devicectl 自动拉起；实测其坏会话让 launch 拖 37–120 s
# CoreDeviceService is this user's XPC service; after it is ended the next devicectl brings it up automatically; measured, its bad sessions make launch drag 37–120 s
cds_restart() {
  local p; p=$(pgrep -u "$(id -u)" -f 'CoreDeviceService.xpc/Contents/MacOS/CoreDeviceService' | head -1)
  [ -n "$p" ] && { kill -TERM "$p"; for _ in 1 2 3 4 5 6 7 8 9 10; do kill -0 "$p" 2>/dev/null || break; sleep 0.5; done; kill -0 "$p" 2>/dev/null && kill -KILL "$p"; }
  echo "健康检查：$1 → 重启 CoreDeviceService（原 pid ${p:-无}）"; LEDGER_SEGS="${LEDGER_SEGS:+${LEDGER_SEGS}；}重启服务:$1"
}

# 设备上本包 DmIOS 的 pid，一行一个。查询失败（devicectl 出错 / 超时 / 输出读不懂）返回 2、不输出——调用处不许把失败当成「没有进程」
# The pids of this package's DmIOS on the device, one per line. On query failure (devicectl error / timeout / unreadable output) returns 2 with no output -- the call site must not treat failure as "no process"
pids() {
  local json="$RUN/processes.json"
  rm -f "$json"
  # 每次查询都把返回码、用时、报错原文记进结果目录：「拉起前查设备进程表连续失败」两天出现在 12 次合入里，原先报错丢进 /dev/null，一条原文都没留下，定不了因
  # Every query records its return code, elapsed time and raw error text in the result directory: "querying the device process table before
  # launch failed repeatedly" showed up in 12 merges over two days, and the errors used to go to /dev/null, leaving not a single line of raw
  # text, so the cause could not be determined
  local _s=$(python3 -c 'import time;print(time.time())') _rc
  "$TIMEOUT_BIN" 15s xcrun devicectl device info processes --device "$D" --json-output "$json" >/dev/null 2>"$RUN/pids_last.err"; _rc=$?
  echo "$(date +%T) 查进程表 rc=$_rc 用时 $(python3 -c "import time;print(round(time.time()-$_s,2))") 秒；调用方 ${FUNCNAME[1]:-?}；$(head -c 300 "$RUN/pids_last.err" | tr '\n' ' ')" >> "$RUN/pids_trace.log"
  [ $_rc = 0 ] || return 2
  python3 - "$json" <<'PY'
import json
import sys

try:
    processes = json.load(open(sys.argv[1]))["result"]["runningProcesses"]
except (OSError, KeyError, TypeError, ValueError):
    raise SystemExit(2)
for process in processes:
    executable = process.get("executable", "")
    if executable.endswith("DmIOS.app/DmIOS"):
        print(process["processIdentifier"])
PY
}

stop_pid() {
  local pid=$1
  "$TIMEOUT_BIN" 15s xcrun devicectl device process terminate --device "$D" --pid "$pid" >/dev/null 2>&1 || true
}

count_lines() {
  local pattern=$1
  [ -f "$2" ] || return 0
  grep -c -- "$pattern" "$2" 2>/dev/null || true
}

# 下一次拉起之前，等上一个本包进程确实从设备进程表里消失（查一次约 0.15 秒）。日志端口关了只说明 App 在退，进程表里还有它的那一小段里发 launch，
# 系统会把这次 launch 记到将死的进程头上，随后自己重拉一个不带任何环境变量的实例（没有日志端口、按键脚本、看门狗），后面几次 launch 都只是把它调到前台
# （出包冒烟：第 2 次 launch 距第 1 次 quit 约 1 秒，第 2、3 次与玩家路径全是日志端口零行）。超过上限还在 = 不是在退的实例，按 pid 结束并记一行。
# Before the next launch, wait until the previous process of this package has really disappeared from the device process table (one query takes about 0.15 seconds). The log port closing only shows the App is exiting; if launch is sent in the short window while the process table still has it,
# the system attributes this launch to the dying process, then relaunches on its own an instance without any environment variables (no log port, key script, watchdog), and the following launches merely bring it to the foreground
# (packaging smoke test: the 2nd launch was about 1 second after the 1st quit, and the 2nd and 3rd launches and the player path all had zero lines on the log port). Still there past the limit = not an exiting instance; end it by pid and log one line.
gone_timeout=${SMOKE_GONE_TIMEOUT:-5}; ENV_RELAUNCH=0
wait_gone() {
  local t0 p q rc lim=$gone_timeout; t0=$(date +%s)
  while :; do
    p=$(pids); rc=$?; [ -n "$p" ] && p="$(echo $p) "
    if [ "$rc" -ne 0 ]; then   # 查不到进程表 ≠ 已退出：接着等；查询一直失败到上限就记一行放行（收尾那道检查会再查） | Unable to query the process table ≠ exited: keep waiting; if queries keep failing up to the limit, log one line and let it through (the wrap-up check will query again)
      if [ $(( $(date +%s) - t0 )) -ge "$lim" ]; then
        echo "$1：拉起前查设备进程表连续失败 ${lim} 秒，不能确认上一局已退出；最后一次报错：$(head -c 200 "$RUN/pids_last.err" 2>/dev/null | tr '\n' ' ')（逐次记录在 $RUN/pids_trace.log）"; LEDGER_SEGS="${LEDGER_SEGS:+${LEDGER_SEGS}；}进程表查询失败:$1"; return
      fi
      sleep 0.3; continue
    fi
    [ -n "$p" ] || break
    if [ $(( $(date +%s) - t0 )) -ge "$lim" ]; then
      echo "$1：拉起前本包进程 pid ${p}等了 ${lim} 秒仍在进程表里，按 pid 结束"; LEDGER_SEGS="${LEDGER_SEGS:+${LEDGER_SEGS}；}拉起前强制结束:$1"
      for q in $p; do stop_pid "$q"; done; t0=$(date +%s); lim=$((lim + 5))
    fi
    sleep 0.3
  done
}
# 「起来了但没拿到环境」：launch 成功、日志端口零行、设备上却有本包进程。按 pid 结束，由调用处重起一次；单独记数（环境重起），不算红也不吞掉
# "Launched but did not get the environment": launch succeeded, zero lines on the log port, yet the device has this package's process. End it by pid and let the call site relaunch once; counted separately (environment relaunch), neither counted as red nor swallowed
noenv_instance() {
  local p q; p=$(pids | tr '\n' ' '); [ -n "$p" ] || return 1
  ENV_RELAUNCH=$((ENV_RELAUNCH + 1)); echo "$1：launch 成功但日志端口零行，设备上有本包进程 pid ${p}= 起来了但没拿到环境变量；按 pid 结束，重起一次（环境重起累计 ${ENV_RELAUNCH} 次）"
  for q in $p; do stop_pid "$q"; done; return 0
}
# 验这两道逻辑用的故障注入（结果记成反例，不算冒烟证据）：SMOKE_TEST_STALE=pre 在第 2 次的「等进程消失」之前、=post 在它之后，先拉起一个不带环境变量的实例
# Fault injection for verifying these two pieces of logic (results are recorded as negative cases, not counted as smoke-test evidence): SMOKE_TEST_STALE=pre launches an instance without environment variables before the 2nd "wait for the process to disappear", =post after it
stale_inject() { ( for v in $(env | sed -n 's/^\(DEVICECTL_CHILD_[A-Z_]*\)=.*/\1/p'); do unset "$v"; done; "$TIMEOUT_BIN" -k 5s 30s xcrun devicectl device process launch --device "$D" "$B" -mode boot >"$RUN/stale_launch.txt" 2>&1 ); sleep 3; echo "故障注入（$1）：已拉起一个不带环境变量的实例 pid $(pids | tr '\n' ' ')"; }

find_pid() {
  pids | awk 'NR == 1 { print; exit }'
}

ok=0
known_pk=0
main_menu_timeout=${SMOKE_MAIN_MENU_TIMEOUT:-40}
game_screen_timeout=${SMOKE_GAME_SCREEN_TIMEOUT:-30}
case "$main_menu_timeout:$game_screen_timeout" in
  ''|*[!0-9:]*|*:*:*) echo "SMOKE RED 0/$N 用时 0 秒"; echo "【红】阶段上限必须是非负整数"; exit 2;;
esac
for i in $(seq 1 "$N"); do
  OUT="$RUN/$i"
  mkdir -p "$OUT"
  START=$(date +%s)
  attempt=0
  result=1
  stage=启动
  pid=""
  envre=0
  CONSOLE="$RUN/console_${i}.txt"
  : > "$CONSOLE"
  while [ "$attempt" -lt 3 ]; do
    attempt=$((attempt + 1))
    attempt_log="$OUT/console_${attempt}.txt"
    : > "$attempt_log"
    export DEVICECTL_CHILD_DM_SAVE_FRESH=warlock
    export DEVICECTL_CHILD_DM_WATCH=$((main_menu_timeout + game_screen_timeout + 5))
    export DEVICECTL_CHILD_DM_UIMSG=1
    export DEVICECTL_CHILD_DM_LOG_PORT=$DEV_PORT
    export DEVICECTL_CHILD_DM_KEYS="$KEYS"
    # 日志走 iproxy 读 App 测试端口（logport.py），devicectl 只用来拉起；不带 --terminate-existing：上一局已由 quit 退出，并由 wait_gone 等到进程表里没有它
    # （--console 首行约 76 s；--terminate-existing 在有存活实例时拖 37–120 s；copy from 文件通道同样卡）
    # The log is read from the App's test port via iproxy (logport.py), devicectl is only used to launch; no --terminate-existing: the previous run has already exited via quit, and wait_gone has waited until it is gone from the process table
    # (--console first line about 76 s; --terminate-existing drags 37–120 s when a live instance exists; the copy from file channel stalls the same way)
    python3 "$TOOLS/logport.py" quit "$LOCAL_PORT" 10 || { echo "第 $i/$N 次：上一局实例 quit 后仍在，改用 devicectl 结束"; for p in $(pids); do stop_pid "$p"; done; }
    [ "${CDS_DUE:-0}" = 1 ] && { cds_restart "上一局拉起超 10 s"; CDS_DUE=0; }
    [ "${SMOKE_TEST_STALE:-}" = pre ] && [ "$i" = 2 ] && [ "$envre" = 0 ] && stale_inject pre
    wait_gone "第 $i 次"
    [ "${SMOKE_TEST_STALE:-}" = post ] && [ "$i" = 2 ] && [ "$envre" = 0 ] && stale_inject post
    tA=$(date +%s)
    "$TIMEOUT_BIN" -k 5s 20s xcrun devicectl device process launch --device "$D" "$B" -mode boot >"$OUT/launch_${attempt}.txt" 2>&1; lrc=$?
    # 健康检查：拉起失败 / 超时 → 重启本用户 CoreDeviceService 再试一次；成功但 > 10 s → 下一局前重启
    # Health check: launch failed / timed out → restart this user's CoreDeviceService and try once more; succeeded but > 10 s → restart before the next run
    if [ "$lrc" -ne 0 ]; then
      cds_restart "第 $i 次拉起失败（rc=${lrc}）"; tA=$(date +%s)
      "$TIMEOUT_BIN" -k 5s 30s xcrun devicectl device process launch --device "$D" "$B" -mode boot >>"$OUT/launch_${attempt}.txt" 2>&1; lrc=$?
    elif [ $(( $(date +%s) - tA )) -gt 10 ]; then CDS_DUE=1; fi
    eval "$(python3 "$TOOLS/logport.py" read "$LOCAL_PORT" "$attempt_log" "$main_menu_timeout" "$game_screen_timeout")"
    cat "$attempt_log" >> "$CONSOLE"
    if [ "$result" -ne 0 ] && [ -z "$tL" ] && [ "$lrc" -eq 0 ] && [ "$envre" = 0 ] && noenv_instance "第 $i 次"; then envre=1; attempt=$((attempt - 1)); continue; fi
    [ "$pk" = 1 ] && { known_pk=1; stage=启动; }
    chan=""; [ "$lrc" -ne 0 ] && chan="（拉起失败：devicectl 退出码 ${lrc}）"
    [ "$result" -ne 0 ] && [ -z "$tL" ] && [ "$lrc" -eq 0 ] && chan="（日志端口零行：App 未监听或 iproxy 不通）"
    [ "$exited" = 1 ] || chan="${chan}（quit 后实例未退）"

    if [ "$result" -eq 0 ]; then
      break
    fi
    if [ "$pk" = 1 ] && [ "$attempt" -lt 3 ]; then
      echo "第 $i/$N 次出现已知PK，补跑 $((attempt))/2"
      continue
    fi
    break
  done

  # 分段耗时：连接=发起拉起→日志端口首行（devicectl 取设备隧道的耗时，不是 App 的；只列不判慢），主菜单=首行→MainMenuButtonRibbon，进局=主菜单→ToGame，撤屏=ToGame→读取画面撤下。
  # 正常值是初估（待真机数据校准）：主菜单 10 / 进局 5 / 撤屏 8 s；超过 2 倍标「⚠️慢」
  # Segment timings: connect = launch issued → first line on the log port (the time devicectl takes to get the device tunnel, not the App's; listed only, not judged slow), main menu = first line → MainMenuButtonRibbon, enter game = main menu → ToGame, screen dismissal = ToGame → loading screen dismissed.
  # Normal values are initial estimates (to be calibrated with real device data): main menu 10 / enter game 5 / screen dismissal 8 s; over 2× is marked 「⚠️慢」(slow)
  seg=""; prev=$tA; for kv in "连接:$tL:9999" "主菜单:$tM:10" "进局:$tG:5" "撤屏:$tS:8"; do IFS=: read -r nm tt nv <<<"$kv"; [ -n "$tt" ] || break; d=$((tt - prev)); w=""; [ $d -gt $((nv * 2)) ] && w="⚠️慢"; seg="$seg $nm ${d}s$w"; prev=$tt; done
  # App 自己的启动 = 日志里「启动计时 … T6 dm_main 前 N ms」（零点 = 进程创建）；实测 148–160 ms，超过 500 ms 标慢
  # The App's own launch = 「启动计时 … T6 dm_main 前 N ms」(launch timing ... T6 before dm_main N ms) in the log (zero point = process creation); measured 148–160 ms, over 500 ms is marked slow
  t6=$(sed -n 's/.*T6 dm_main 前 \([0-9]*\)\.[0-9]* ms.*/\1/p' "$CONSOLE" 2>/dev/null | head -1)
  if [ -n "$t6" ]; then w=""; [ "$t6" -gt 500 ] && w="⚠️慢"; seg="$seg 启动(App) ${t6}ms$w"; fi
  if [ "$result" -eq 0 ]; then
    echo "第 $i/$N 次 OK：主菜单 $(count_lines MainMenuButtonRibbon "$CONSOLE") 进局 $(count_lines 'FrontEndNavigation ToGame' "$CONSOLE") 撤屏 1，用时 $(( $(date +%s) - START )) 秒；分段$seg"
    ok=$((ok + 1))
    LEDGER_SEGS="${LEDGER_SEGS:+${LEDGER_SEGS}；}$i:OK$seg"
  else
    LEDGER_SEGS="${LEDGER_SEGS:+${LEDGER_SEGS}；}$i:RED@$stage$seg"
    echo "第 $i/$N 次 RED：卡在：${stage}（主菜单 $(count_lines MainMenuButtonRibbon "$CONSOLE")/进局 $(count_lines 'FrontEndNavigation ToGame' "$CONSOLE")/撤屏 0）；分段$seg${chan}"
  fi
done

# 玩家路径：产品缺省下 30 s alarm / 15 s 无进展看门狗曾杀掉桌面启动的 App，而所有测试都显式设 DM_WATCH 从未测到。
# 每次冒烟 1 局（不带任何启动参数 = 玩家点图标，原先带 -mode boot，掩盖了缺省模式是探针的缺陷）：去掉看门狗与存档等测试变量，只带日志口与按键；进局 35 s 后开暂停菜单（PLUS → R → R）再静置 20 s——跨过两种看门狗门限；反例运行（设了 SMOKE_KEYS）不跑
# Player path: under product defaults the 30 s alarm / 15 s no-progress watchdog used to kill an App launched from the home screen, and since all tests explicitly set DM_WATCH this was never caught.
# 1 run per smoke test (with no launch arguments at all = the player tapping the icon; previously it carried -mode boot, masking the defect that the default mode was the probe): drop test variables such as watchdog and save, carrying only the log port and keys; 35 s after entering the game open the pause menu (PLUS → R → R) then idle 20 s -- crossing both watchdog thresholds; negative-case runs (SMOKE_KEYS set) do not run it
PLAYER_RES=跳过
if [ -z "${SMOKE_KEYS:-}" ] && [ "${SMOKE_PLAYER:-1}" = 1 ]; then
  python3 "$TOOLS/logport.py" quit "$LOCAL_PORT" 10 >/dev/null 2>&1 || for p in $(pids); do stop_pid "$p"; done
  KP="$KEYS_DEFAULT,PLUS@UnloadPanel:LoadScreenPanel%35>OpenPanel:PlayerInventoryExpansionLayout,R@OpenPanel:PlayerInventoryExpansionLayout%1>OpenPanel:SkillsTreePanel,R@OpenPanel:SkillsTreePanel%1>OpenPanel:PauseLayout"
  player_once() {
  wait_gone "玩家路径"
  env -u DEVICECTL_CHILD_DM_WATCH -u DEVICECTL_CHILD_DM_SAVE_FRESH DEVICECTL_CHILD_DM_SAVE_TEST=1 DEVICECTL_CHILD_DM_LOG_PORT=$DEV_PORT DEVICECTL_CHILD_DM_UIMSG=1 DEVICECTL_CHILD_DM_KEYS="$KP" \
    "$TIMEOUT_BIN" -k 5s 30s xcrun devicectl device process launch --device "$D" "$B" > "$RUN/player_launch.txt" 2>&1 || { cds_restart "玩家路径拉起失败"; env -u DEVICECTL_CHILD_DM_WATCH -u DEVICECTL_CHILD_DM_SAVE_FRESH DEVICECTL_CHILD_DM_SAVE_TEST=1 DEVICECTL_CHILD_DM_LOG_PORT=$DEV_PORT DEVICECTL_CHILD_DM_UIMSG=1 DEVICECTL_CHILD_DM_KEYS="$KP" "$TIMEOUT_BIN" -k 5s 30s xcrun devicectl device process launch --device "$D" "$B" >> "$RUN/player_launch.txt" 2>&1; }
  pr=$(python3 "$TOOLS/logport.py" player "$LOCAL_PORT" "$RUN/player.txt" "$main_menu_timeout" 35 20)
  }
  player_once
  # 同上一道防线：玩家路径日志零字节而设备上有本包进程 = 没拿到环境，结束后重起一次
  # Same line of defense as above: player path log zero bytes while the device has this package's process = the environment was not received; end it and relaunch once
  [ -s "$RUN/player.txt" ] || { noenv_instance "玩家路径" && player_once; }
  # 产品缺省必须真出声：玩家路径日志里要有真出声的 AudioOut 打开行，出现「静音接收端」即红
  # Product defaults must really produce sound: the player path log must contain an AudioOut open line with real sound output, and 「静音接收端」(muted sink) appearing means failure
  au_real=$(grep -c 'audio::OpenAudioOut .*真出声' "$RUN/player.txt" 2>/dev/null); au_null=$(grep -c 'audio::OpenAudioOut .*静音接收端' "$RUN/player.txt" 2>/dev/null)
  hud_ok=$(LC_ALL=C grep -a 'dm_hud：' "$RUN/player.txt" 2>/dev/null | LC_ALL=C grep -a -v -c '未读到玩家')   # 产品缺省必须读得到受控玩家（同 smoke_mac.sh） | Product defaults must be able to read the controlled player (same as smoke_mac.sh)
  red_n=$(LC_ALL=C grep -a -c '【红】' "$RUN/player.txt" 2>/dev/null)   # 玩家路径日志不许有红行（同 smoke_mac.sh） | The player path log must have no failure lines (same as smoke_mac.sh)
  if [[ "$pr" == result=0* ]] && [ "${au_real:-0}" -gt 0 ] && [ "${au_null:-0}" = 0 ] && [ "${hud_ok:-0}" -gt 0 ] && [ "${red_n:-0}" = 0 ]; then PLAYER_RES=OK; echo "玩家路径 OK：无看门狗变量，进局 35 s + 暂停菜单 20 s 仍活着；AudioOut 真出声 ${au_real} 路（${pr}）"
  else PLAYER_RES=RED; echo "玩家路径 RED：${pr}；AudioOut 真出声 ${au_real:-0} 路 / 静音接收端 ${au_null:-0} 路（须真出声 > 0、静音 0）；读到玩家状态 ${hud_ok:-0} 行（须 > 0）；红行 ${red_n:-0} 条（须 0）"; fi
  LEDGER_SEGS="${LEDGER_SEGS:+${LEDGER_SEGS}；}玩家路径:$PLAYER_RES"
fi

[ "$ENV_RELAUNCH" -gt 0 ] && { echo "环境重起 ${ENV_RELAUNCH} 次（起来的实例没拿到环境变量，已按 pid 结束并重起；不算红）"; LEDGER_SEGS="${LEDGER_SEGS:+${LEDGER_SEGS}；}环境重起:${ENV_RELAUNCH}次"; }
left=""; left_rc=2
for _ in 1 2 3; do left=$(pids) && { left_rc=0; break; }; sleep 2; done   # devicectl 偶发失败重试；三次都失败 = 不能确认，按红报 | Retry on sporadic devicectl failures; all three failing = cannot confirm, report as a failure
left=$(echo $left)
if [ "$left_rc" -ne 0 ]; then
  echo "【红】结束后查设备进程表 3 次都失败（devicectl 出错 / 超时），不能确认本包已退出"
  LEDGER_SEGS="${LEDGER_SEGS:+${LEDGER_SEGS}；}收尾进程表查询失败"
elif [ -n "$left" ]; then
  echo "【红】结束后仍有本包 DmIOS pid：$left"
  # 残留实例的 App 端日志（Documents/boot.txt）在结束它之前拉回：下次拉起会清空重写，锁外别的任务一拉起证据就没了
  # The leftover instance's App-side log (Documents/boot.txt) is pulled back before ending it: the next launch clears and rewrites it, and as soon as another task outside the lock launches, the evidence is gone
  "$TIMEOUT_BIN" 60s xcrun devicectl device copy from --device "$D" --domain-type appDataContainer --domain-identifier "$B" --source Documents/boot.txt --destination "$RUN/boot_left.txt" >/dev/null 2>&1 \
    && echo "残留实例日志：$RUN/boot_left.txt（$(grep -c '' "$RUN/boot_left.txt") 行，末行：$(tail -1 "$RUN/boot_left.txt" | cut -c1-80)）"
  for pid in $left; do stop_pid "$pid"; done
fi

elapsed=$(( $(date +%s) - T0 ))
if [ "$ok" -eq "$N" ] && [ "$left_rc" -eq 0 ] && [ -z "$left" ] && [ "$PLAYER_RES" != RED ]; then
  [ "$known_pk" -eq 1 ] && echo "【已知PK】占不到 pthread 键 507：已按规则补跑，最终通过"
  LEDGER_RES=OK
  echo "SMOKE OK $ok/$N 用时 $elapsed 秒"
  exit 0
fi
[ "$known_pk" -eq 1 ] && echo "【已知PK】占不到 pthread 键 507：已按规则最多补跑 2 次"
LEDGER_RES=RED
echo "SMOKE RED $ok/$N 用时 $elapsed 秒"
exit 1
