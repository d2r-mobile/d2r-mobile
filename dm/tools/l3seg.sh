#!/bin/bash
# L3：去掉前 30% 采样后回归，外推到 216000 拍。
# 红线：堆在用或 Metal 外推增长 > 256 MB，或句柄外推增长 > 16；RSS/phys_footprint 只报告。
# L3: regress after dropping the first 30% of samples, extrapolate to 216000 ticks.
# Failure line: extrapolated growth of heap in use or Metal > 256 MB, or extrapolated handle growth > 16; RSS/phys_footprint are report-only.
set -u
set -o pipefail
scene=${1:-}; ticks=${2:-}
case "$scene" in cvd5|camp1|wild1|war13_v4) ;; *) echo '用法：l3seg.sh <cvd5|camp1|wild1|war13_v4> <段长拍数>' >&2; exit 2;; esac
case "$ticks" in ''|*[!0-9]*) echo '段长拍数必须是正整数' >&2; exit 2;; esac
[ "$ticks" -gt 0 ] || { echo '拍数必须大于 0' >&2; exit 2; }
stamp=$(date +%Y%m%d_%H%M%S)_$scene; run=${L3_RUN_DIR:-${DM_ROOT:-$HOME/dm-work}/run/l3/$stamp}; here=$(cd "$(dirname "$0")/.." && pwd)
mini=${MINI_ROOT:-${DM_ROOT:-$HOME/dm-work}/run/mini}; bin=${SWHOST:-$mini/src/dm/host/dmhost}; out=${DM_OUT:-$mini/out/$(bash "$(dirname "$0")/product.sh" name touch)}
truth_root=${L3_TRUTH_ROOT:-${DM_ROOT:-$HOME/dm-work}/data/switch/truth_runs}; war_root=${L3_WAR_ROOT:-${DM_ROOT:-$HOME/dm-work}/run/verify/dm/v4_truth_bootstrap_1002/war13_v4}; save_root=${L3_SAVE_ROOT:-${DM_ROOT:-$HOME/dm-work}/data/switch}
case "$scene" in war13_v4) truth=$war_root; save=$save_root/save_ref_warlock_v2/0000000000000001;; cvd5|camp1|wild1) truth=$truth_root/$scene; save=$save_root/save_ref_warlock/0000000000000001;; *) echo "未知场景：$scene" >&2; exit 2;; esac
mkdir -p "$run"; [ -x "$bin" ] || { echo "🔴 缺少 mini 宿主：$bin" >&2; exit 1; }; [ -d "$out" ] || { echo "🔴 产物目录不存在：$out" >&2; exit 1; }; [ -f "$truth/c2s.jsonl" ] || { echo "🔴 缺少命令包：$truth/c2s.jsonl" >&2; exit 1; }
if [ -f "$truth/meta.json" ]; then ms=$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1])).get("save",[""])[-1])' "$truth/meta.json"); [ -d "$ms" ] && save=$ms; fi
[ -d "$save/0" ] || { echo "🔴 起始存档不存在：$save/0" >&2; exit 1; }
mkdir -p "$run/save"; cp -R "$save/0" "$run/save/0" || exit 1
awk -v hi="$ticks" 'match($0,/"tick"[[:space:]]*:[[:space:]]*[0-9]+/){t=substr($0,RSTART,RLENGTH);sub(/^[^0-9]*/,"",t);if(t<hi)print}' "$truth/c2s.jsonl" > "$run/c2s.jsonl"
seed=0x12345678; fixed=1790000000
if [ -f "$truth/meta.json" ]; then seed=$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1])).get("game_seed",305419896))' "$truth/meta.json"); fixed=$(python3 -c 'import json,sys;v=json.load(open(sys.argv[1])).get("fix_time");print(v if v is not None else 1790000000)' "$truth/meta.json"); fi
last=$((ticks-1)); keys="A@OpenPanel:MainMenuButtonRibbon>OpenPanel:CharacterDifficultyModal,A@OpenPanel:CharacterDifficultyModal>FrontEndNavigation:ToGame,R@DM:Tick:${last}#>OpenPanel:PauseLayout,A@OpenPanel:PauseLayout%1>PausePanelMessage:ExitGame"
rss(){ ps -o rss= -p "$1" 2>/dev/null | awk '{print $1+0}'; }; footprint(){ echo 0; }   # 不再外部 vmmap——它每秒遍历宿主整个虚拟内存，把宿主拖慢 29%（同负载 A/B：205 → 145 拍/秒）；phys_footprint 改由宿主 L3MEM 行用 task_info 自报 | No more external vmmap -- it walks the host's entire virtual memory every second and slows the host down by 29% (A/B under the same load: 205 → 145 ticks/second); phys_footprint is now self-reported by the host's L3MEM line via task_info
start=$(date +%s); export DM_SAVE="$run/save/0" DM_C2S_REPLAY="$run/c2s.jsonl" DM_TICKS="$run/ticks.jsonl" DM_TICKS_N="$ticks" DM_PRESENT_HZ=0 DM_DETCLOCK=33333333 DM_WATCH=110 DM_GAME_SEED="$seed" DM_FIXED_TIME="$fixed" DM_KEYS="$keys" DM_SOAK="$run/soak.tsv,1"; [ "${DM_TEST_LEAK:-}" = 1 ] && export DM_TEST_LEAK=1; [ "${DM_TEST_LEAK_GPU:-}" = 1 ] && export DM_TEST_LEAK_GPU=1
timeout 115s "$bin" "$out" main sdk >"$run/host.log" 2>&1 & pid=$!; trap 'kill "$pid" 2>/dev/null || true' EXIT INT TERM
while kill -0 "$pid" 2>/dev/null; do target=$(pgrep -P "$pid" | head -1); [ -n "$target" ] || target=$pid; if [ -s "$run/ticks.jsonl" ]; then tick=$(tail -1 "$run/ticks.jsonl" | sed -n 's/.*"tick"[[:space:]]*:[[:space:]]*\([0-9][0-9]*\).*/\1/p'); [ -n "$tick" ] && printf '%s\t%s\t%s\t%s\n' "$(date +%s)" "$tick" "$(rss "$target")" "$(footprint "$target")" >> "$run/pressure.tsv"; fi; sleep 1; done
wait "$pid"; status=$?; wall=$(( $(date +%s)-start )); [ "$wall" -gt 0 ] || wall=1; [ -s "$run/pressure.tsv" ] || { echo '🔴 没有采到进局压力样本'; exit 1; }
awk -v p="$run/pressure.tsv" 'BEGIN{while((getline < p)>0){rss[$2]=$3; fp[$2]=$4}} /L3MEM 拍 [0-9]/{tick=$5; best=""; for (k in rss) if (k <= tick && (best == "" || k > best)) best=k; print "0\t" tick "\t" $7 "\t" $9 "\t" $11 "\t" $13 "\t" (best == "" ? 0 : rss[best]) "\t" (best == "" ? 0 : fp[best])}' "$run/host.log" > "$run/samples.tsv"
[ -s "$run/samples.tsv" ] || { echo '🔴 没有采到 L3MEM 样本' >&2; exit 1; }
python3 "$here/tools/l3reg.py" "$run/samples.tsv" 216000 > "$run/regression.log"; reg=$?
cmp=$here/verify/savecmp.py; savecmp=3; if find "$run/save" -type f -name '*.d2s' -print -quit | grep -q .; then python3 "$cmp" "$run/save" "$run/save" --a-time="$fixed" --b-time="$fixed" >"$run/savecmp.log" 2>&1; savecmp=$?; else echo 'SAVECMP 射程不足：没有 .d2s' >"$run/savecmp.log"; fi
cat "$run/regression.log"; tail -4 "$run/savecmp.log"; awk -v s="$scene" -v t="$ticks" -v w="$wall" -v st="$status" -v sc="$savecmp" 'BEGIN{printf "L3SEG %s ticks=%d tick_per_sec=%.2f wall=%ds status=%d savecmp=%d\n",s,t,t/w,w,st,sc}'
if [ "$reg" -eq 0 ] && [ "$savecmp" -eq 0 ] && [ "$status" -eq 0 ]; then echo "L3SEG $scene OK"; else echo "L3SEG $scene RED"; fi
du -sh "$run"; exit 0
