#!/bin/bash
# 局内切运行模式（真机过热切掌机即崩：参数门跳板越过原指令，只有收到通知 30/31 才走到）：产品节奏回放 cvd5 营地进局，
# DM_OE_SCRIPT 在进局（读取画面首次撤下）后第 AT 秒把运行模式 1 → 0（主机 → 掌机，等同前端过热降级），再跑到 DUR 秒由 timeout 收。
# 用法：opmode.sh [产物目录=out/<当前代号>-keypage] [AT=3] [DUR=32]
# 判据（全满足才绿）：切换前已进局（ToGame 之后读取画面撤下）；切换后游戏取走通知 30 与 31；进程活到 timeout（rc 124）、无 CRASH；
#   切换后至少再出 300 帧（宿主 [slowframe] 分母行）；宿主日志无红线（logred.sh）。反例：r24-keypage 在切换瞬间 SIGSEGV（pc main+0xe10de8）
# In-game operation mode switch (a real device overheating and switching to handheld crashed at once: the parameter-gate trampoline skipped an original instruction, reached only when notification 30/31 arrives): replay cvd5 at product pacing, entering the game in camp,
# DM_OE_SCRIPT switches the operation mode 1 → 0 (docked → handheld, equivalent to the front end's overheat downgrade) at second AT after game entry (the loading screen first dismissed), then runs to second DUR and is reaped by timeout.
# Usage: opmode.sh [build output dir=out/<current codename>-keypage] [AT=3] [DUR=32]
# Criteria (green only if all hold): already in the game before the switch (loading screen dismissed after ToGame); after the switch the game fetches notifications 30 and 31; the process lives until timeout (rc 124) with no CRASH;
#   at least 300 more frames are rendered after the switch (host [slowframe] denominator line); no failure line in the host log (logred.sh). Counterexample: r24-keypage took a SIGSEGV at the moment of the switch (pc main+0xe10de8)
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${1:-$(bash "$(dirname "$0")/../tools/product.sh" path keypage)}; AT=${2:-3}; DUR=${3:-32}
TR=${TRUTH_RUN:-${DM_ROOT:-$HOME/dm-work}/data/switch/truth_runs/cvd5}
ROOT=${DM_OPMODE_RUNROOT:-${DM_ROOT:-$HOME/dm-work}/run/verify/dm/opmode}; mkdir -p "$ROOT"
T=$(mktemp -d "$ROOT/$(date +%m%d_%H%M%S)_$$.XXXX")
cp -R "${DM_ROOT:-$HOME/dm-work}/data/switch/save_ref_warlock/0000000000000001/0" "$T/user0" && chmod -R u+w "$T/user0"
make -s -C "$HERE/../host" > "$T/build.log" 2>&1 || { echo "OPMODE RED 宿主构建失败（$T/build.log）"; exit 1; }
(cd "$HERE/../host" && timeout "$DUR" env DM_SAVE="$T/user0" DM_GAME_SEED=305419896 DM_FIXED_TIME=1790000000 \
  "DM_KEYS=A@OpenPanel:MainMenuButtonRibbon>OpenPanel:CharacterDifficultyModal,A@OpenPanel:CharacterDifficultyModal>FrontEndNavigation:ToGame" \
  DM_C2S_REPLAY="$TR/c2s.jsonl" DM_PRESENT_HZ=60 DM_PRESENT_INTERVAL=display DM_UIMSG=1 DM_SLOWFRAME=1 "DM_OE_SCRIPT=opmode=0@ingame+$AT" DM_GAME_RES=1952x900 \
  ./dmhost "$OUT" main sdk > "$T/host.log" 2>&1); rc=$?
echo "局目录 $T"
python3 - "$T/host.log" "$rc" <<'PY'
import sys, re
log, rc = sys.argv[1], int(sys.argv[2])
L = open(log, errors='replace').read().splitlines()
def first(pat, start=0):
    for i in range(start, len(L)):
        if re.search(pat, L[i]): return i
    return None
tg = first(r'FrontEndNavigation ToGame')
ul = first(r'UnloadPanel LoadScreenPanel', tg + 1) if tg is not None else None
dm = first(r'oe 运行模式 1 → 0')
n30 = first(r'oe 通知被游戏取走：30', dm or 0) if dm is not None else None
n31 = first(r'oe 通知被游戏取走：31', dm or 0) if dm is not None else None
crash = first(r'CRASH sig=')
den = [(i, int(m.group(1))) for i, l in enumerate(L) for m in [re.search(r'\[slowframe\]\t#\t至帧 \d+\tjudged=(\d+)', l)] if m]
before = max((j for i, j in den if dm is not None and i < dm), default=0)
after = max((j for i, j in den), default=0) - before
ok = []
ok.append(('切换前已进局', ul is not None and dm is not None and ul < dm))
ok.append(('切换后取走通知 30 / 31', n30 is not None and n31 is not None))
ok.append(('进程活到 timeout、无 CRASH', rc == 124 and crash is None))
ok.append(('切换后再出 ≥ 300 帧', dm is not None and after >= 300))
# 切换后游戏设的窗口尺寸须是宿主给的掌机档（1952x900 的 720/900 同比 = 1564x720）。用非原版宽度跑才看得出：通知处理把宽度 & 0x740
# （原版 1280|1600 推出的掩码）时 1564 → 1536（r26 及以前；r27 剪口改为取低 32 位）。原版宽度下这个缺陷不显形
wc = [m.group(1) for i, l in enumerate(L) if dm is not None and i > dm for m in [re.search(r'WindowCrop 0,0 (\d+x\d+)', l)] if m]
ok.append((f'切换后窗口尺寸 = 1564x720（实得 {wc[-1] if wc else "无"}）', bool(wc) and wc[-1] == '1564x720'))
for k, v in ok: print(f"OPMODE {'✓' if v else '✗'} {k}")
if crash is not None: print('OPMODE 崩溃行：' + L[crash][:160])
print(f"OPMODE {'OK' if all(v for _, v in ok) else 'RED'} 切换后帧 {after}，rc {rc}")
sys.exit(0 if all(v for _, v in ok) else 1)
PY
prc=$?
r=$("$HERE/logred.sh" "$T/host.log"); lrc=$?
echo "OPMODE 宿主日志红线 $(echo "$r" | cut -f1) 行"
[ $prc = 0 ] && [ $lrc = 0 ]
