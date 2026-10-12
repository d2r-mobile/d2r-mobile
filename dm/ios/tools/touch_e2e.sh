#!/bin/bash
# 真机触屏端到端：装好的 App 按触点脚本自动操作，拉回日志与截图。用法：
#   [PREFIX=c2s.jsonl] tools/touch_e2e.sh <脚本文件> <结果目录> [秒=60]
# PREFIX：先回放一段原版命令包（如出营地到野外），固定种子/时间；回放窗口结束后触点脚本接手（脚本时间要排在窗口之后）。
# 脚本格式见 front_ios.m 的 touch_script_start（首次进游戏后计时）。存档每局换术士标准存档新副本（DM_SAVE_FRESH=warlock）。
# 只按本 App bundle / 自己启动的 pid 停进程；不碰别的 App 与设备设置。
# Real-device touch end-to-end: the installed App is operated automatically by a touch point script, and logs and screenshots are pulled back. Usage:
#   [PREFIX=c2s.jsonl] tools/touch_e2e.sh <script file> <result dir> [seconds=60]
# PREFIX: first replay a stretch of the original game's command packets (e.g. from leaving the camp to the wilderness), with fixed seed/time; after the replay window ends the touch point script takes over (script times must be scheduled after the window).
# For the script format see touch_script_start in front_ios.m (timing starts after first entering the game). Each run swaps in a fresh copy of the standard warlock save (DM_SAVE_FRESH=warlock).
# Processes are stopped only by this App's bundle / the pid we launched ourselves; other Apps and device settings are not touched.
set -euo pipefail
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/local_env.sh" && local_env_need DM_DEVICE || exit 1   # 本机设备配置（不进仓，见 dm/ios/local.env.example） | Local device configuration (not committed, see dm/ios/local.env.example)
D=$DM_DEVICE; B=${DM_BUNDLE_ID:?先在本机配置里设 DM_BUNDLE_ID（App 包名，见 dm/ios/local.env.example）}
SCRIPT=$1; OUT=$2; CAP=${3:-60}
mkdir -p "$OUT"; cp "$SCRIPT" "$OUT/script.txt"
# 设备锁（可重入：外层已持锁的调用不受影响）。直接调本脚本时没人取锁——Mac 版 touch_e2e_mac.sh 自己取、真机版以前靠调用者取，口径不一
# 电量闸：取锁前读电量，< 30% 暂停全部真机局、充到 60% 放行；读数记进本局产物 battery.txt（表头用）
# Device lock (reentrant: calls whose outer layer already holds the lock are unaffected). When this script is called directly nobody takes the lock -- the Mac version touch_e2e_mac.sh takes it itself, while the real-device version used to rely on the caller, an inconsistent convention
# Battery gate: read the battery level before taking the lock; below 30% pause all real-device runs, release them once charged to 60%; the reading is recorded in this run's output battery.txt (used for the header)
source "$(dirname "$0")/devbattery.sh"; devbattery_gate || { echo "【红】电量闸拒绝：${DEVBATTERY_LINE:-}" | tee "$OUT/battery.txt"; exit 3; }; echo "$DEVBATTERY_LINE" > "$OUT/battery.txt"
source "$(dirname "$0")/devlock.sh"; devlock_acquire "${SMOKE_OWNER:-未署名} 真机触屏 $(basename "$OUT")"; trap devlock_release EXIT
# 本局编号做产物名前缀：旧局文件名不同，不会被当成本局结果拉回（App 开日志前就退出的一局曾拉回前一天的旧 c2s / target）。
# 不逐个推空文件清旧产物：每次 devicectl 约 20 s，10 个文件把锁内时间拖到 324 s
# Use this run's id as the prefix of the output names: files from old runs have different names and will not be pulled back as this run's results (a run where the App exited before opening its logs once pulled back the previous day's old c2s / target).
# Do not clear old output by pushing empty files one by one: each devicectl call takes about 20 s, and 10 files stretched the time under lock to 324 s
RID=r$(date +%H%M%S)
awk -v r="$RID" '($2=="shot"||$2=="tshot"){$3=r"_"$3} {print}' "$SCRIPT" > "$OUT/script_run.txt"
t(){ echo "[$(date +%T)] $*"; }
dc(){ timeout 60 xcrun devicectl device "$@"; }   # 每次 devicectl 限 60 s：无超时的调用卡住会长时间占设备锁（UDC 卡 27 分钟饿死整条队） | Each devicectl call is capped at 60 s: a call without a timeout that hangs holds the device lock for a long time (UDC hung for 27 minutes and starved the whole queue)
pids(){ dc info processes --device $D --json-output "$OUT/.ps.json" >/dev/null 2>&1; python3 -c "import json,sys;print(' '.join(str(p['processIdentifier']) for p in json.load(open(sys.argv[1]))['result']['runningProcesses'] if 'DmIOS.app/DmIOS' in p.get('executable','')))" "$OUT/.ps.json"; }
for p in $(pids); do t "停残留 DmIOS pid $p"; dc process terminate --device $D --pid $p >/dev/null 2>&1 || true; done
dc copy to --device $D --domain-type appDataContainer --domain-identifier $B --source "$OUT/script_run.txt" --destination Documents/touch_script.txt >/dev/null
[ -n "${PREFIX:-}" ] && dc copy to --device $D --domain-type appDataContainer --domain-identifier $B --source "$PREFIX" --destination Documents/e2e_prefix.jsonl >/dev/null
ENV=$(CAP=$CAP RID=$RID PREFIX=${PREFIX:-} E2E_ENV=${E2E_ENV:-} E2E_KEYS=${E2E_KEYS:-} python3 - <<'EOF'
import json, os
r = os.environ["RID"]
env = {
  "DM_SAVE_FRESH": "warlock",   # 档位不设 = 产品缺省（主机档）。之前这里写死掌机档（DM_OPMODE=0），触屏脚本看到的界面参数与玩家看到的不是同一份；要测掌机档用 E2E_ENV 传
  "DM_TOUCH_SCRIPT": "touch_script.txt", "DM_TOUCH_CONTROL_LOG": "1",
  "DM_C2S_LOG": r + "_c2s.jsonl", "DM_UIMSG": r + "_uimsg.jsonl", "DM_STATE": r + "_state.jsonl",
  "DM_TICKS": r + "_ticks.jsonl", "DM_TICKS_FULL": "1", "DM_TICKS_N": "0", "DM_TARGET_LOG": r + "_target.jsonl",
  "DM_KEYS": os.environ.get("E2E_KEYS") or "X@OpenPanel:MainMenuButtonRibbon>OpenPanel:CharacterSelectPanel,C0.84:0.208@OpenPanel:CharacterSelectPanel%1>UpdateCharacterInfo,MSG:CharacterSelect:LoadCharacter:0@UpdateCharacterInfo%0.3>FrontEndNavigation:ToGame",   # E2E_KEYS：换起局键序（同 touch_e2e_mac.sh）
  "DM_WATCH": str(int(os.environ["CAP"]) + 60),
}
if os.environ["PREFIX"]:
    env.update({"DM_C2S_REPLAY": "e2e_prefix.jsonl", "DM_GAME_SEED": "305419896", "DM_FIXED_TIME": "1790895128"})
if os.environ.get("E2E_ENV"): env.update(json.loads(os.environ["E2E_ENV"]))   # 额外环境（JSON），对照实验用，如 {"DM_GAME_SEED":"1"}
env = {k: v for k, v in env.items() if v is not None}   # E2E_ENV 里写 null = 去掉这个缺省（如 {"DM_SAVE_FRESH":null,"DM_SAVE_TEST":"1"}：沿用上一局的 save.test）
print(json.dumps(env))
EOF
)
dc process launch --device $D --terminate-existing --environment-variables "$ENV" --json-output "$OUT/launch.json" $B -mode boot > "$OUT/launch.log" 2>&1
PID=$(python3 -c "import json;print(json.load(open('$OUT/launch.json'))['result']['process']['processIdentifier'])")
t "启动 pid $PID，跑 ${CAP}s"; sleep "$CAP"
dc process terminate --device $D --pid "$PID" > "$OUT/stop.log" 2>&1 && t "已停 pid $PID" || t "pid $PID 已自行退出"
# 只拉本局产物（Documents 里有 9 GB romfs，不整目录拉）：启动日志、四份记录、脚本里的截图
# E2E_PULL（空格分隔，不带本局前缀，如 "c2s.jsonl state.jsonl s2.png"）：只拉这几份——每次 copy from 约 20 s，全拉会把锁内时间拖过 2 分钟
# Pull only this run's build output (Documents holds a 9 GB romfs, so do not pull the whole directory): boot log, the four records, screenshots taken by the script
# E2E_PULL (space-separated, without this run's prefix, e.g. "c2s.jsonl state.jsonl s2.png"): pull only these files -- each copy from takes about 20 s, and pulling everything pushes the time under lock past 2 minutes
ALL="boot.txt ${RID}_c2s.jsonl ${RID}_uimsg.jsonl ${RID}_ticks.jsonl ${RID}_target.jsonl ${RID}_state.jsonl $(awk '$2=="shot"{print $3; print $3".txt"} $2=="tshot"{print $3; print $3".json"}' "$OUT/script_run.txt")"
[ -n "${E2E_PULL:-}" ] && ALL=$(for f in $E2E_PULL; do [ "$f" = boot.txt ] && echo boot.txt || echo "${RID}_$f"; done)
for f in $ALL; do
  g=${f#${RID}_}   # 拉回后去掉本局前缀，结果目录里的文件名与旧版一致（e2e_* 照旧） | After pulling, strip this run's prefix so the file names in the result directory match the old version (e2e_* unchanged)
  case $f in ${RID}_*.jsonl) g=e2e_$g;; esac
  dc copy from --device $D --domain-type appDataContainer --domain-identifier $B --source "Documents/$f" --destination "$OUT/$g" >> "$OUT/copy.log" 2>&1 || t "【红】拉不到 $f（本局没写）"
done
left=$(pids); [ -z "$left" ] && t "设备上 DmIOS 进程 0" || t "【红】仍有 DmIOS 进程 $left"
