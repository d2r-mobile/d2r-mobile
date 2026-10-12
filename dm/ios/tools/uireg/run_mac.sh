#!/bin/bash
# uireg 的起局脚本（Mac 上的 iOS 包，在本机跑）：run_mac.sh <标签> <上限秒> < 触点脚本。约定见 uireg.py 文件头与「usage.md」。
# 玩家路径：只带触点脚本，不带按键脚本接管；PW_ENV（空格分隔的 K=V）原样加到起包环境；PW_SAVE 非空 = 换上容器 Data 下这份存档的副本，跑完挪回。
# 包按 UIREG_MODE 挑：iphone = $UIREG_APP（缺省 <树>/.iosdd_mac/DmMacSmoke.app，smoke_mac.sh 包好的那份）；ipad / ipadfull = $UIREG_APP_IPAD（没有缺省，出法见usage.md）。
# 不拷包、不同步，直接从给的路径起（计时里「拷包」恒为 0）。要换包请另起目录名，包在跑的时候别动它的 Wrapper 那层：
#   系统把 <包>/Wrapper 挂载出去跑，挂着时源目录被删或整体替换，会留下卸不掉的失效挂载点，那个路径从此 open 不了。
# Run launcher script for uireg (the iOS app on Mac, run on this machine): run_mac.sh <tag> <limit seconds> < touch point script. Conventions are in the uireg.py file header and usage.md.
# Player path: only a touch point script, no key script taking over; PW_ENV (space-separated K=V) is added as-is to the app launch environment; non-empty PW_SAVE = swap in a copy of this save under the container's Data, and move it back after the run.
# The app is picked by UIREG_MODE: iphone = $UIREG_APP (default <tree>/.iosdd_mac/DmMacSmoke.app, the one packaged by smoke_mac.sh); ipad / ipadfull = $UIREG_APP_IPAD (no default, see usage.md for how to produce it).
# No copying or syncing of the app, it is launched directly from the given path (the 「拷包」(copy app) entry in the timing is always 0). To switch apps use another directory name, and do not touch the app's Wrapper layer while it is running:
#   the system mounts <app>/Wrapper out to run it; if the source directory is deleted or replaced wholesale while mounted, a stale mount point that cannot be unmounted is left behind, and that path can never be opened again.
set -u
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/../local_env.sh"   # 本机配置（包名、团队号、设备号） | Local config (package name, team ID, device ID)
B=${DM_BUNDLE_ID:?先在本机配置里设 DM_BUNDLE_ID（App 包名，见 dm/ios/local.env.example）}
TAG=$1; CAP=$2; HERE=$(cd "$(dirname "$0")" && pwd)
O=${UIREG_OUT:-${DM_ROOT:-$HOME/dm-work}/run/uireg}/pw/$TAG; mkdir -p "$O"; cat > "$O/script.txt"; : > "$O/launch.log"
fail(){ echo "$*" | tee "$O/fail.txt"; echo "计时：${1}" > "$O/timing.txt"; exit 2; }
case ${UIREG_MODE:-iphone} in
  iphone) W=${UIREG_APP:-$(git -C "$HERE" rev-parse --show-toplevel 2>/dev/null)/.iosdd_mac/DmMacSmoke.app};;
  *) W=${UIREG_APP_IPAD:-};;
esac
[ -x "$W/Wrapper/DmIOS.app/DmIOS" ] || fail "没有包（画面档 ${UIREG_MODE:-iphone}）：${W:-未设 UIREG_APP_IPAD}"
C=""; for m in "$HOME"/Library/Containers/*/.com.apple.containermanagerd.metadata.plist; do
  [ "$(/usr/libexec/PlistBuddy -c 'Print :MCMMetadataIdentifier' "$m" 2>/dev/null)" = "$B" ] && C="$(dirname "$m")/Data"; done
D="$C/Documents"; [ -n "$C" ] && [ -d "$D" ] || fail "本机没有 $B 的容器（先跑一次 smoke_mac.sh 1）"
# 同一台 Mac 上同包只能起一个实例：与 Mac 冒烟 / 触屏用例共用 macsmoke 锁（可重入：外层已持锁的调用不再排队）
# Only one instance of the same app can be started on one Mac: shares the macsmoke lock with the Mac smoke test / touch cases (reentrant: a call whose outer layer already holds the lock does not queue again)
T_B=$(date +%s); export DM_DEVLOCK_DIR=${DM_ROOT:-$HOME/dm-work}/run/ios/macsmoke.lock; source "$HERE/../devlock.sh"; devlock_acquire "${SMOKE_OWNER:-未署名} 界面回归 $TAG"; T_C=$(date +%s)
# 只认本包（bundle id = $B）：同机还可能跑着回归多实例的重签包 <包名>.reg<N>（各自独立容器），按可执行文件后缀认会把它们当成本包、收尾时一起杀掉。
# 查不到 bundle id 的（刚起、系统还没登记）照旧算本包，只排除明确是别的 bundle 的
# Only this package is recognized (bundle id = $B): the same machine may also be running the regression multi-instance re-signed packages <package name>.reg<N> (each with its own container); matching by executable suffix would take them as this package and kill them along at wrap-up.
# Those whose bundle id cannot be found (just launched, not yet registered by the system) still count as this package; only those clearly belonging to another bundle are excluded
pids() { local p b; for p in $(ps -axo pid=,comm= | awk '{ pid=$1; $1=""; sub(/^ /,""); if ($0 ~ /\/Wrapper\/DmIOS\.app\/DmIOS$/) print pid }'); do
  b=$(lsappinfo info -only bundleID -app "$p" 2>/dev/null | sed -n 's/.*="\(.*\)"$/\1/p'); [ -z "$b" ] || [ "$b" = "$B" ] && echo "$p"; done; }
stop_all(){ local p; for p in $(pids); do kill -TERM "$p" 2>/dev/null; done; sleep 1; for p in $(pids); do kill -KILL "$p" 2>/dev/null; done; }
pre=$(pids | tr '\n' ' '); [ -z "$pre" ] || { devlock_release; fail "本机已有 $B 的实例在跑（pid ${pre}，非本次启动）"; }
# 换存档的收尾：跑过的那份留在 Data 下（save.uireg_ran，只留最近一份），原存档挪回 Documents/save。挪不回 = 容器里的正式存档不在原位，之后所有本机包都受影响：
# 报红（fail.txt + 退出码 4）并写明原存档在哪，不静默（只读副本挪不走，原存档留在 swap 目录里没人知道）
# 换存档一律换进测试存档目录 Documents/save.test 并带 DM_SAVE_TEST=1 起局，不碰产品存档 Documents/save：挂产品存档目录的局会连真实 iCloud，
# 登着真实账号的机器上换进去的测试角色会被同步上云。跑完再查一遍日志，出现同步动作就报红。
# Wrap-up of a save swap: the copy that was run stays under Data (save.uireg_ran, only the latest one kept), and the original save moves back to Documents/save. Failing to move it back = the real save in the container is not in place, which affects every local package afterwards:
# report a failure (fail.txt + exit code 4) and state where the original save is, never silently (a read-only copy cannot be moved away, and nobody would know the original save was left in the swap directory)
# A save swap always swaps into the test save directory Documents/save.test and starts the run with DM_SAVE_TEST=1, without touching the product save Documents/save: a run that mounts the product save directory connects to real iCloud,
# and on a machine signed in to a real account the swapped-in test character would be synced to the cloud. After the run the log is checked once more, and any sync action is reported as a failure.
SV="$D/save.test"; SWAP=""; BAD=""; restore(){ [ -n "$SWAP" ] || return 0; chmod -R u+w "$SV" 2>/dev/null; rm -rf "$C/save.uireg_ran" "$SV.work"
  if mv "$SV" "$C/save.uireg_ran" && mv "$SWAP" "$SV"; then SWAP=""; else BAD=1; echo "【红】换存档后挪不回：原测试存档在 ${SWAP}，Documents/save.test 不是原来那份，手工核对后挪回" | tee "$O/fail.txt"; [ -f "$O/timing.txt" ] && sed -i '' '1s/$/ 【红】换存档后挪不回，见 fail.txt/' "$O/timing.txt"; fi; }   # timing.txt 那行 uireg.py 会抄进表头 | uireg.py copies the timing.txt line into the table header
trap 'stop_all; restore; savekey_out "$C"; devlock_release; [ -z "$BAD" ] || exit 4' EXIT; trap 'exit 143' INT TERM
# 存档钥匙（../save_key.sh）：带 DM_SAVE_KEY 的换进自己那份测试存档；带 DM_SAVE_TEST=1 接着用却不带钥匙的不起。换存档的局（PW_SAVE）自带存档，不走钥匙
# Save key (../save_key.sh): runs with DM_SAVE_KEY swap in their own test save; runs continuing with DM_SAVE_TEST=1 but without a key are not started. Save-swap runs (PW_SAVE) bring their own save and do not use a key
source "$HERE/../launch_guard.sh"; launch_guard_pre "$C"; source "$HERE/../save_key.sh"; [ -n "${PW_SAVE:-}" ] || savekey_in "$C" "${PW_ENV:-}" || fail "存档钥匙不许起这一局（见上一行红字）"
if [ -n "${PW_SAVE:-}" ]; then
  [ -d "$C/$PW_SAVE" ] || fail "容器 Data 下没有存档 ${PW_SAVE}（d 局专用，备法见usage.md）"
  # SWAP 等原存档真的挪走了才置（先置的话，挪不走时收尾会把没动过的原存档当成「跑过的那份」挪走）；副本一律加写权限：
  # 外来存档目录常是只读的，原样拷进来游戏的存档事务清不掉它生成的 save.work，之后任何包启动都 FATAL
  # SWAP is set only once the original save has really been moved away (if set first, when the move fails the cleanup would move away the untouched original save as 'the one that was run'); copies always get write permission added:
  # external save directories are often read-only, and copied in as-is the game's save transaction cannot clear the save.work it generates, after which any bundle FATALs on launch
  S1="$C/save.uireg_swap_$$"; [ -d "$SV" ] || mkdir -p "$SV"; mv "$SV" "$S1" || fail "换存档失败：原测试存档挪不走"; SWAP="$S1"; rm -rf "$SV.work"
  cp -R "$C/$PW_SAVE" "$SV" && chmod -R u+w "$SV" || fail "换存档失败：副本拷不进去"
fi
caffeinate -u -t 3 >/dev/null 2>&1   # 屏幕睡着时取不到可绘制表面，游戏主线程会卡在取图 | While the screen is asleep no drawable surface can be obtained, and the game's main thread gets stuck acquiring the drawable
RID=u$(date +%H%M%S); awk -v r="$RID" '$2=="shot"{$3=r"_"$3} {print}' "$O/script.txt" > "$D/touch_script.txt"
EX=(); for kv in ${PW_ENV:-}; do EX+=(--env "$kv"); done; [ -z "${PW_SAVE:-}" ] || EX+=(--env DM_SAVE_TEST=1)
# 本机起包的总闸（../cloud_guard.sh）：每一局都带「不同步」开关，自己指了本地云端目录的 iCloud 用例除外；跑完核启动日志
# Master gate for launching the package on this Mac (../cloud_guard.sh): every run carries the "no sync" switch, except iCloud cases that specify a local cloud directory themselves; the launch log is checked after the run
source "$HERE/../cloud_guard.sh"; cloud_guard_env "$D" "${PW_ENV:-}" || fail "总闸不许起这一局（见上一行红字）"; [ -z "$CG_ENV" ] || EX+=(--env "$CG_ENV")
# DM_UIMSG=1：界面消息行（UIMSG …）只有带了它才打进日志，前端 / 确认框 / 暂停菜单那批判据靠这些行；DM_WATCH：到点自己退出的兜底
# open 后 10 秒没有 boot.txt = 这次没起来：收掉重起一次，再不行整局判失败，不等满上限
# DM_UIMSG=1: UI message lines (UIMSG ...) are written to the log only with it, and the criteria for the front end / confirmation box / pause menu batch rely on these lines; DM_WATCH: the fallback that exits by itself when time is up
# No boot.txt 10 seconds after open = it did not start this time: kill it and start again once, and if that fails too the whole run is judged failed without waiting out the limit
launch(){ mv "$D/boot.txt" "$D/boot_prev.txt" 2>/dev/null
  open -n "$W" ${EX[@]+"${EX[@]}"} --env DM_TOUCH_SCRIPT=touch_script.txt --env DM_TOUCH_SCRIPT_START=menu --env DM_UIMSG=1 --env "DM_WATCH=$((CAP + 60))" --args -mode boot "${LG_ARGS[@]}" >> "$O/launch.log" 2>&1
  local i; for i in $(seq 20); do [ -s "$D/boot.txt" ] && return 0; sleep 0.5; done; return 1; }
TRY=1
if ! launch; then stop_all; TRY=2; launch || { launch_guard_blocked "$B" "两次 open 各等 10 秒" 2>&1 | tee "$O/fail.txt"; echo "计时：拷包 0 排锁 $((T_C - T_B)) 启动失败（两次都没有 boot.txt）" | tee "$O/timing.txt"; exit 3; }; fi
T_D=$(date +%s)
# 见到脚本末行的记号就收；上限秒只是兜底
# End the run as soon as the marker on the script's last line is seen; the limit in seconds is only a fallback
END=$((T_D + CAP)); while [ "$(date +%s)" -lt "$END" ]; do LC_ALL=C grep -a -q '\[script\] note 结束' "$D/boot.txt" 2>/dev/null && break; [ -n "$(pids)" ] || break; sleep 0.5; done; T_E=$(date +%s)
stop_all
cp "$D/boot.txt" "$O/boot.txt" 2>/dev/null; [ -f "$D/pwc2s.jsonl" ] && mv "$D/pwc2s.jsonl" "$O/c2s.jsonl"
# 真实 iCloud 闸：本该不同步的局出现了同步动作 = 连了真云端，这一局作废
# Real iCloud gate: a sync action appearing in a run that should not sync = it connected to the real cloud, and this run is void
cloud_guard_check "$O/boot.txt" || { BAD=1; echo "【红】这一局出现了 iCloud 同步动作，结果作废，立刻核云端目录" | tee "$O/fail.txt"; }; cloud_guard_clean "$D"
# 换存档的局：进局后日志里的角色名（宿主每次进局都打「刷怪积分牌：「<角色名>」…」）必须是换进来那份存档里的角色，不是 = 挂的不是换进去的那份
# Save-swap runs: the character name in the log after entering the run (the host prints 「刷怪积分牌：「<角色名>」…」(kill board: "<character name>"...) on every run entry) must be the character in the swapped-in save; if it is not = the save mounted is not the one swapped in
if [ -n "${PW_SAVE:-}" ]; then WHO=$(LC_ALL=C sed -n 's/.*刷怪积分牌：「\([^」]*\)」.*/\1/p' "$O/boot.txt" 2>/dev/null | tail -1)
  if [ -n "$WHO" ] && [ ! -f "$C/$PW_SAVE/$WHO/$WHO.d2s" ]; then BAD=1; echo "【红】换存档没生效：进局的角色「${WHO}」不在 ${PW_SAVE} 里，这一局不算" | tee "$O/fail.txt"; fi; fi
for f in "$D/${RID}"_*; do [ -f "$f" ] && mv "$f" "$O/$(basename "$f" | sed "s/^${RID}_//")"; done
echo "计时：拷包 0 排锁 $((T_C - T_B)) 起包 $((T_D - T_C)) 运行 $((T_E - T_D)) 收尾 $(( $(date +%s) - T_E )) 启动次数 $TRY" | tee "$O/timing.txt"
