#!/bin/bash
# 视觉等价尺子（同进程 A/B/C）：每帧一个宿主、同一冻结拍连截三张——A、B 用真值设置（DM_NVN_RULER=1：TBDR 优化全关），
# 提交边界切到被测设置（缺省全开 + 调用方透传的环境）截 C。噪声 = FLIP(A,B)，差异 = FLIP(B,C)，-ppd 95；判「FLIP 差异 ≤ 0.01 且 ≤ 2×噪声 + 0.002」并且「大差像素（任一通道差 > 30/255）数：差异 ≤ 2×噪声 + 画面像素的 0.005%」。
# 跨运行比不可用：两次同设置独立运行的噪声底营地单帧 0.0108。只能测运行中可切换的设置。
# 用法：ruler.sh <dmhost> <产物目录> <宽x高> <输出目录> [帧号…，缺省 02 营地 16 野外 18 战斗 06 菜单]   产物：<输出目录>/<帧号>_{A,B,C}.png、<帧号>.log
# 口径（RULER_MODE）：exact（缺省）= 合并 / 跳过类，应与真值同图，按 2×噪声线判；
#   approx = 降分辨率 / 换滤波 / 近似类，按「FLIP(B,C) ≤ 0.02」判（实机上 3D 内部 0.75–0.80 倍、FLIP 0.017–0.023 肉眼无差别），
#   局部缺损判据放宽为「差 > 60/255 的像素数 B–C ≤ 2×A–B + 画面 0.05%」（边缘本来就会动；阈值待用 X1 的图校准，行里把两档像素数都打出来）。
# 负对照：DM_NVN_RULER_NEG=<片元键> 被测设置下丢掉该着色器的绘制，尺子必须报不等价。
# Visual-equivalence ruler (same process A/B/C): one host per frame, three consecutive shots of the same frozen tick - A and B use the ground-truth settings (DM_NVN_RULER=1: all TBDR optimizations off),
# at a commit boundary switch to the settings under test (default all on + the environment passed through by the caller) and shoot C. Noise = FLIP(A,B), difference = FLIP(B,C), -ppd 95; the verdict is "FLIP difference ≤ 0.01 and ≤ 2×noise + 0.002" and "count of large-difference pixels (any channel difference > 30/255): difference ≤ 2×noise + 0.005% of the frame's pixels".
# Cross-run comparison is unusable: the noise floor between two independent runs with the same settings is 0.0108 for a single camp frame. Only settings that can be switched at run time can be tested.
# Usage: ruler.sh <dmhost> <build output dir> <WxH> <output dir> [frame ids..., default 02 camp 16 wilderness 18 combat 06 menu]   Build output: <output dir>/<frame id>_{A,B,C}.png, <frame id>.log
# Mode (RULER_MODE): exact (default) = merge / skip class, should produce the same image as ground truth, judged by the 2×noise line;
#   approx = lower-resolution / filter-change / approximation class, judged by "FLIP(B,C) ≤ 0.02" (on a real device, with the 3D internal scale at 0.75–0.80×, FLIP 0.017–0.023 shows no difference to the naked eye),
#   the local-defect criterion is relaxed to "count of pixels with difference > 60/255: B–C ≤ 2×A–B + 0.05% of the frame" (edges move anyway; the threshold is pending calibration with X1 images, and the line prints the pixel counts for both tiers).
# Negative control: DM_NVN_RULER_NEG=<fragment key> drops the draws of that shader under the settings under test; the ruler must report non-equivalence.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
FRAMES="$HERE/frames.tsv"
BIN=${1:?用法：ruler.sh <dmhost> <产物目录> <宽x高> <输出目录> [帧号…]}
PRODUCTS=${2:?缺少产物目录}
GAME_RES=${3:?缺少宽x高}
OUTPUT=${4:?缺少输出目录}
shift 4 || true
mkdir -p "$OUTPUT/tmp" || exit 2
selected=''
if [ "$#" -gt 0 ]; then
  for wanted in "$@"; do
    row=$(awk -F '\t' -v w="$wanted" '$1 == w || $2 == w { print; exit }' "$FRAMES")
    [ -n "$row" ] || { echo "失败：未知帧编号或名称 $wanted" >&2; exit 2; }
    selected="$selected$row"$'\n'
  done
else
  selected=$(printf '%s\n' 02 16 18 06 | while IFS= read -r wanted; do awk -F '\t' -v w="$wanted" '$1 == w { print; exit }' "$FRAMES"; done)
fi

run_frame() {
  local row id name source frame_tmp log page trigger keys env_json key value trig_base shot_ui pid
  row=$1
  local -a env_args
  id=$(printf '%s\n' "$row" | cut -f1); name=$(printf '%s\n' "$row" | cut -f2); source=$(printf '%s\n' "$row" | cut -f3)
  frame_tmp="$OUTPUT/tmp/$id"; mkdir -p "$frame_tmp"; log="$OUTPUT/$id.log"
  env_args=(DM_NVN_RULER=1 DM_NVN_SHOT_OUT="$GAME_RES" DM_GAME_RES="$GAME_RES" DM_TEMP="$frame_tmp")
  if [[ "$source" == shotgate_pages/*.json ]]; then
    page="$HERE/../$source"
    IFS=$'\t' read -r trigger keys env_json < <(python3 -c 'import json,sys; h=json.load(open(sys.argv[1]))["host"]; print("\t".join((h["trigger"],h.get("keys") or "-",json.dumps(h.get("env",{}),separators=(",",":")))))' "$page")
    [ "$keys" = - ] || env_args+=("DM_KEYS=$keys")
    while IFS=$'\t' read -r key value; do [ -n "$key" ] && env_args+=("$key=$value"); done < <(python3 -c 'import json,os,sys; [print(k+"\t"+os.path.expanduser(v)) for k,v in json.loads(sys.argv[1]).items()]' "$env_json")
  else
    IFS=: read -r _ combat tick <<<"$source"
    trigger="DM:Tick:${tick}#+20"
    env_args+=(DM_KEYS='X@OpenPanel:MainMenuButtonRibbon>OpenPanel:CharacterSelectPanel,DOWN@OpenPanel:CharacterSelectPanel>UpdateCharacterInfo,A@UpdateCharacterInfo%1>CharacterSelect:SelectCharacter,A@CharacterSelect:SelectCharacter>FrontEndNavigation:ToGame' DM_C2S_REPLAY=${DM_ROOT:-$HOME/dm-work}/run/verify/ryu/war_summon13_1002_takeover/c2s.jsonl DM_GAME_SEED=305419896 DM_FIXED_TIME=1790895128 "DM_TICK_HOLD=${tick}:SHOT")
  fi
  trig_base=$(printf '%s' "$trigger" | sed -E 's/\+[0-9]+$//')
  # 三张等间隔（各隔 8 帧）：切换在 B 写盘后的下一次提交才生效，C 紧跟 B 一帧会赶在切换之前出图（负对照实测：尺子变成空转）
  # Three shots at equal spacing (8 frames apart each): the switch only takes effect at the next commit after B is written to disk; a C that follows B by one frame would be rendered before the switch (negative control, measured: the ruler becomes a no-op)
  shot_ui="$trig_base+20:$frame_tmp/A.png,$trig_base+28:$frame_tmp/B.png,$trig_base+36:$frame_tmp/C.png"
  env_args+=("DM_NVN_SHOT_UI=$shot_ui")
  SV=${DM_GATE_SAVE:-${DM_ROOT:-$HOME/dm-work}/data/switch/save_ref_warlock/0000000000000001}
  W=$(mktemp -d "$frame_tmp/save.XXXX")
  cp -R "$SV/0" "$W/save" && chmod -R u+w "$W/save" || { echo "失败 ${id}：拷存档失败" >&2; return 1; }
  mkdir -p "$W/temp"
  timeout 45s env "${env_args[@]}" DM_SAVE="$W/save" DM_TEMP="$W/temp" DM_WATCH_STALL=15 DM_WATCH=225 "$BIN" "$PRODUCTS" main sdk >"$frame_tmp/host.log" 2>&1 & pid=$!
  while [ ! -s "$frame_tmp/A.png" ] || [ ! -s "$frame_tmp/B.png" ] || [ ! -s "$frame_tmp/C.png" ]; do
    kill -0 "$pid" 2>/dev/null || { echo "失败 ${id}：宿主提前退出" >&2; cp "$frame_tmp/host.log" "$log"; rm -rf "$W"; return 1; }
    sleep 1
  done
  kill "$pid" 2>/dev/null; wait "$pid" 2>/dev/null; cp "$frame_tmp/host.log" "$log"; rm -rf "$W"
  for x in A B C; do cp "$frame_tmp/$x.png" "$OUTPUT/${id}_$x.png"; done   # 留图：报红时要能看差在哪 | Keep the images: when it reports a failure, one must be able to see where the difference is
  flip="$HERE/../../third_party/flip/flip"
  noise=$(timeout 20s "$flip" -r "$frame_tmp/A.png" -t "$frame_tmp/B.png" -ppd 95 -v 1 -nerm 2>&1 | awk '/Mean:/{print $2; exit}')
  diff=$(timeout 20s "$flip" -r "$frame_tmp/B.png" -t "$frame_tmp/C.png" -ppd 95 -v 1 -nerm 2>&1 | awk '/Mean:/{print $2; exit}')
  [ -n "$noise" ] && [ -n "$diff" ] || { echo "失败 ${id}：FLIP 无 Mean" >&2; return 1; }
  # 局部判据：FLIP 均值对局部缺损不敏感（负对照：战斗帧丢 FXAA 有 4051 个像素差 > 30，均值只有 0.0016）——再数「大差像素」（任一通道差 > 30/255）
  # Local criterion: the FLIP mean is insensitive to local defects (negative control: a combat frame that drops FXAA has 4051 pixels with difference > 30, yet the mean is only 0.0016) - so also count "large-difference pixels" (any channel difference > 30/255)
  read -r big_ab big_bc npx big60_ab big60_bc < <(python3 - "$frame_tmp/A.png" "$frame_tmp/B.png" "$frame_tmp/C.png" <<'PY2'
import sys
import numpy as np
from PIL import Image
a, b, c = (np.asarray(Image.open(p).convert('RGB')).astype(np.int16) for p in sys.argv[1:4])
big = lambda x, y, th: int((np.abs(x - y).max(axis=2) > th).sum())
print(big(a, b, 30), big(b, c, 30), a.shape[0] * a.shape[1], big(a, b, 60), big(b, c, 60))
PY2
)
  [ -n "${npx:-}" ] || { echo "失败 ${id}：大差像素统计失败" >&2; return 1; }
  if [ "${RULER_MODE:-exact}" = approx ]; then
    verdict=$(awk -v d="$diff" -v ab="$big60_ab" -v bc="$big60_bc" -v px="$npx" 'BEGIN { print (d <= 0.02 && bc <= 2*ab + px*0.0005) ? "等价" : "不等价" }')
  else
    verdict=$(awk -v d="$diff" -v n="$noise" -v ab="$big_ab" -v bc="$big_bc" -v px="$npx" 'BEGIN { print (d <= 0.01 && d <= 2*n + 0.002 && bc <= 2*ab + px*0.00005) ? "等价" : "不等价" }')
  fi
  printf '%s 噪声 %.6f 差异 %.6f 大差像素(>30) 噪声 %d 差异 %d (>60) 噪声 %d 差异 %d 判定[%s] %s\n' "$id" "$noise" "$diff" "$big_ab" "$big_bc" "$big60_ab" "$big60_bc" "${RULER_MODE:-exact}" "$verdict"
  [ "$verdict" = 等价 ]
}
fail=0; total=0; equivalent=0
sem="$OUTPUT/tmp/sem.$$"
mkfifo "$sem" || exit 2
exec 9<>"$sem"; rm -f "$sem"
for _ in 1 2 3 4; do printf '\n' >&9; done
result_dir="$OUTPUT/tmp/results"; mkdir -p "$result_dir"
while IFS= read -r row; do
  [ -n "$row" ] || continue
  total=$((total + 1)); id=$(printf '%s\n' "$row" | cut -f1)
  read -u 9
  { if run_frame "$row"; then printf '1\n' >"$result_dir/$id"; else printf '0\n' >"$result_dir/$id"; fi; printf '\n' >&9; } &
done <<< "$selected"
wait
exec 9>&-
for result in "$result_dir"/*; do [ -f "$result" ] || continue; read -r ok <"$result"; [ "$ok" = 1 ] && equivalent=$((equivalent + 1)) || fail=1; done
echo "汇总：$equivalent/$total 帧等价"
rm -rf "$OUTPUT/tmp"
exit "$fail"
