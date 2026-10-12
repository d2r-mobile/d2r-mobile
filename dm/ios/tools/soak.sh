#!/bin/bash
# 真机局内浸泡（同 Mac 浸泡口径）：r17 + 术士存档可写副本 + cvd5 真值局的种子 / 固定时刻 + cvd5 回放进局；回放完（DM:ReplayDone）后先按 B 关闭传送点菜单（确认 ClosePanel:WaypointsPanelExpansion），再
# 左摇杆按 左→上→右→下 各 8 秒循环走动到浸泡结束；DM_SOAK 每 60 秒一行，墙钟上限 35 分钟。
# 用法：[DM_OUT=产物目录（缺省 r17）] [SOAK_IV=采样秒（缺省 60）] dm/ios/tools/soak.sh [分钟=35]     产物：$DM_ROOT/run/ios/soak/（soak.tsv、boot.txt、报告）
# 判据：soak_report.py 四项（拍号增长、第 5 分钟后内存斜率、句柄 / 线程不变、帧时间 p95 前后段）+ 真机两项（每分钟温控档位与 footprint、p95 随时间）
# Real device in-game soak (same basis as the Mac soak): r17 + writable copy of the warlock save + the seed / fixed time of the cvd5 ground truth run + cvd5 replay to enter the game; after the replay finishes (DM:ReplayDone) first press B to close the waypoint menu (confirming ClosePanel:WaypointsPanelExpansion), then
# walk in a loop with the left stick left→up→right→down for 8 seconds each until the soak ends; one DM_SOAK line every 60 seconds, wall-clock limit 35 minutes.
# Usage: [DM_OUT=build output dir (default r17)] [SOAK_IV=sampling seconds (default 60)] dm/ios/tools/soak.sh [minutes=35]     Output: $DM_ROOT/run/ios/soak/ (soak.tsv, boot.txt, report)
# Criteria: soak_report.py's four items (tick number growth, memory slope after minute 5, handles / threads unchanged, frame time p95 first vs. second half) + two real device items (thermal state and footprint per minute, p95 over time)
set -u
cd "$(dirname "$0")/.."
source tools/local_env.sh && local_env_need DM_DEVICE || exit 1   # 本机设备配置（不进仓，见 dm/ios/local.env.example） | Local device configuration (not committed, see dm/ios/local.env.example)
MIN=${1:-35}; D=$DM_DEVICE; B=${DM_BUNDLE_ID:?先在本机配置里设 DM_BUNDLE_ID（App 包名，见 dm/ios/local.env.example）}; R=${DM_ROOT:-$HOME/dm-work}/run/ios/soak
TR=${DM_ROOT:-$HOME/dm-work}/data/switch/truth_runs/cvd5; CVD=$TR/c2s.jsonl; RUN=$(date +%H%M%S)
export DM_OUT=${DM_OUT:-$(bash "$(dirname "$0")/../../tools/product.sh" path keypage)}
SEED=$(python3 -c "import json;print(hex(json.load(open('$TR/meta.json'))['game_seed']))"); FT=$(python3 -c "import json;print(json.load(open('$TR/meta.json'))['fix_time'])")
K='X@OpenPanel:MainMenuButtonRibbon>OpenPanel:CharacterSelectPanel,MSG:CharacterSelect:LoadCharacter:0@OpenPanel:CharacterSelectPanel%1>FrontEndNavigation:ToGame,B@DM:ReplayDone>ClosePanel:WaypointsPanelExpansion'
# 小站关闭确认后循环走动：各步前置留空（上一步松开即按）；按键表上限 256 项 → 前 3 项进局 / 关小站 + 248 项 × 8 秒 ≈ 33 分钟
# After the waypoint close is confirmed, walk in a loop: each step's pre-delay is left empty (press as soon as the previous step releases); key table limit 256 entries → first 3 entries enter the game / close the waypoint + 248 entries × 8 seconds ≈ 33 minutes
W='LSL@+8'; for i in $(seq 1 247); do W="$W,$(echo LSU LSR LSD LSL | cut -d' ' -f$(( (i - 1) % 4 + 1 )))@+8"; done
K="$K,$W"
t(){ echo "[$(date +%T)] $*"; }
mkdir -p "$R"; rm -f "$R"/soak.tsv "$R"/boot.txt
./run.sh build > "$R/build.log" 2>&1 || { t "🔴 构建失败"; tail -5 "$R/build.log"; exit 1; }
./run.sh install >> "$R/build.log" 2>&1 || { t "🔴 装机失败"; tail -5 "$R/build.log"; exit 1; }
xcrun devicectl device copy to --device $D --domain-type appDataContainer --domain-identifier $B --source "$CVD" --destination Documents/cvd5_replay.jsonl > "$R/push.log" 2>&1 || { t "🔴 推回放文件失败"; exit 1; }
export DEVICECTL_CHILD_DM_KEYS="$K"
DEVICECTL_CHILD_DM_SAVE_FRESH=warlock DEVICECTL_CHILD_DM_GAME_SEED=$SEED DEVICECTL_CHILD_DM_FIXED_TIME=$FT DEVICECTL_CHILD_DM_C2S_REPLAY=cvd5_replay.jsonl \
DEVICECTL_CHILD_DM_SOAK="soak_$RUN.tsv,${SOAK_IV:-60}" DEVICECTL_CHILD_DM_PRESENT_HZ=60 DEVICECTL_CHILD_DM_WATCH=$((MIN * 60)) \
  xcrun devicectl device process launch --device $D --terminate-existing $B -mode boot > "$R/launch.log" 2>&1 || { t "🔴 启动失败"; tail -3 "$R/launch.log"; exit 1; }
t "已启动，浸泡 $MIN 分钟"
alive(){ xcrun devicectl device info processes --device $D 2>/dev/null | grep -q "DmIOS.app/DmIOS"; }
s=$SECONDS
while alive && [ $((SECONDS - s)) -lt $((MIN * 60 + 120)) ]; do sleep 60; done
[ $((SECONDS - s)) -lt $((MIN * 60 - 30)) ] && t "🔴 进程在第 $(((SECONDS - s) / 60)) 分钟就没了（崩溃或被杀）"
xcrun devicectl device copy from --device $D --domain-type appDataContainer --domain-identifier $B --source "Documents/soak_$RUN.tsv" --destination "$R/soak.tsv" > /dev/null 2>&1 || t "🔴 拉 soak.tsv 失败"
xcrun devicectl device copy from --device $D --domain-type appDataContainer --domain-identifier $B --source Documents/boot.txt --destination "$R/boot.txt" > /dev/null 2>&1 || t "🔴 拉 boot.txt 失败"
t "结束（墙钟 $(((SECONDS - s) / 60)) 分钟）"
python3 "${DM_ROOT:-$HOME/dm-work}/run/platform/soak_report.py" "$R/soak.tsv" "$R/boot.txt"
echo "⑤ 每分钟温控档位 / footprint："
python3 - "$R/soak.tsv" "$R/boot.txt" <<'PY'
import sys, re
th = {int(m[1]): int(m[2]) for m in re.finditer(r'\[浸泡\] 第 (\d+) 秒 温控档位 (\d+)', open(sys.argv[2], errors='replace').read())}
rows = [l.rstrip('\n').split('\t') for l in open(sys.argv[1])][1:]
for r in rows:
    s = int(float(r[0])); near = min(th, key=lambda k: abs(k - s)) if th else None
    print(f'  {s:5d} 秒 拍 {r[1]:>6} footprint {r[6]:>7} MiB  温控 {th[near] if near is not None else "?"}')
print('⑥ 帧时间 p95 随时间（每 300 帧一段）：')
seg = [(int(m[1]), float(m[2]), float(m[3])) for m in re.finditer(r'计时 至帧 (\d+)： 帧时间 p50 ([\d.]+) / p95 ([\d.]+)', open(sys.argv[2], errors='replace').read())]
for i in range(0, len(seg), 6): print('  ' + '  '.join(f'帧{f}:{p95:.1f}' for f, _, p95 in seg[i:i + 6]))
red = sorted({l.strip() for l in open(sys.argv[2], errors='replace') if '【红】' in l and '贴图号' not in l})
print('红：' + ('无' if not red else ' | '.join(red[:5])))
PY
