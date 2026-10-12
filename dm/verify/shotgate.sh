#!/bin/bash
# 对真值的画面闸门，一条命令：逐个关键画面起宿主截图（按 UI 消息触发、按页规格的键序）→ 与真值参照图比（shotgate.py）→ 汇总报告（并排出图）。
# 用法：dm/verify/shotgate.sh [产物目录=out/3.3.94012-r11] [页名…=全部]   宿主用本工作树 dm/host/dmhost（开跑先 make）
# 报告：有红 / 标 KEEP 时在 $DM_ROOT/run/verify/dm/shotgate/<时刻>/index.html（整份现场保留）；全绿时现场删掉、只留小结 shotgate/green/<时刻>.html。末行 SHOTGATE_ALL OK|RED → <报告路径>；退出码 0 = 全绿。旧现场由 shotgate_gc.sh 自清（缺省留 3 天）
# 页规格：dm/verify/shotgate_pages/<页>.json（truth = 参照图；host.trigger / host.keys = 宿主 DM_NVN_SHOT_UI 触发与 DM_KEYS；阈值与标定见 screenshot_gate.md）
# 指纹核对：参照图旁的 <页>.meta.json 记真值起始存档指纹，宿主所用存档（DM_GATE_SAVE，缺省现行标准存档）指纹不同即该页报红、不比
# Screenshot gate against ground truth, one command: for each key screen start the host and take a screenshot (triggered by UI message, with the page spec's key sequence) → compare with the ground-truth reference image (shotgate.py) → summary report (images side by side).
# Usage: dm/verify/shotgate.sh [build output dir=out/3.3.94012-r11] [page names...=all]   the host is this worktree's dm/host/dmhost (make runs first)
# Report: with any failure / KEEP mark, at $DM_ROOT/run/verify/dm/shotgate/<timestamp>/index.html (full artifacts kept); when all green the artifacts are deleted and only the summary shotgate/green/<timestamp>.html is kept. Last line SHOTGATE_ALL OK|RED → <report path>; exit code 0 = all green. Old artifacts are cleaned up by shotgate_gc.sh (kept 3 days by default)
# Page spec: dm/verify/shotgate_pages/<page>.json (truth = reference image; host.trigger / host.keys = the host's DM_NVN_SHOT_UI trigger and DM_KEYS; thresholds and calibration in screenshot_gate.md)
# Fingerprint check: <page>.meta.json next to the reference image records the fingerprint of the ground truth's starting save; if the save the host uses (DM_GATE_SAVE, default: the current standard save) has a different fingerprint, that page is reported as a failure and not compared
set -u
HERE=$(cd "$(dirname "$0")" && pwd); PY=${DM_PY:-${DM_ROOT:-$HOME/dm-work}/build/cache/dm/venv/bin/python}
OUT=${1:-$(bash "$(dirname "$0")/../tools/product.sh" path keypage)}; [ $# -gt 0 ] && shift
PAGES=${*:-$(cd "$HERE/shotgate_pages" && ls *.json | sed 's/\.json$//')}
BIN=${DM_HOST:-$HERE/../host/dmhost}
SV=${DM_GATE_SAVE:-${DM_ROOT:-$HOME/dm-work}/data/switch/save_ref_warlock/0000000000000001}; HSHA=$("$HERE/savesha.sh" "$SV")
TREE=$(basename "$(git -C "$HERE" rev-parse --show-toplevel 2>/dev/null || printf '%s' "$HERE")")
mkdir -p "${DM_ROOT:-$HOME/dm-work}/run/verify/dm/shotgate"
R=$(mktemp -d "${DM_ROOT:-$HOME/dm-work}/run/verify/dm/shotgate/$(date +%m%d_%H%M%S)_${TREE}_$$.XXXXXX") || { echo "🔴 无法创建 shotgate 结果目录"; exit 1; }
touch "$R/index.start"   # 起跑时刻（收崩溃报告用） | Run start time (used for collecting crash reports)
make -s -C "$HERE/../host" || { echo "🔴 宿主编译失败"; exit 1; }
# 宿主日志红线关键字表在 logred.sh（与 accept ② 共用）
# The host log a failure line-line keyword table lives in logred.sh (shared with accept ②)
ROUTE=${DM_NVN_ROUTE:-B}; [ -n "${DM_NVN_B:-}" ] && [ "${DM_NVN_B}" != all ] && ROUTE="A/B 混（DM_NVN_B=${DM_NVN_B}）"   # 着色器路线：宿主缺省 B，DM_NVN_ROUTE=A 切回 A | Shader route: the host defaults to B, DM_NVN_ROUTE=A switches back to A
echo "着色器路线 ${ROUTE}；产物 $(basename "$OUT")；宿主 $BIN"
rc=0; rows=""
page() {   # 单页：截图 + 比对，结果写 $R/$1.res（首行 rc，次行读数） | Single page: screenshot + comparison, result written to $R/$1.res (first line rc, second line readings)
  local p=$1 S=$HERE/shotgate_pages/$1.json T TRIG KEYS TSHA KE HL ERRS line prc=0
  read -r T TRIG KEYS < <($PY -c "import json,os; d=json.load(open('$S')); h=d['host']; print(os.path.expanduser(d['truth']), h['trigger'], h.get('keys') or '-')")
  local -a HE=(); while IFS= read -r kv; do [ -n "$kv" ] && HE+=("$kv"); done < <($PY -c "import json,os; [print(f'{k}={os.path.expanduser(v)}') for k,v in json.load(open('$S'))['host'].get('env',{}).items()]")   # 页规格 host.env：回放文件、种子等 | Page spec host.env: replay file, seed, etc.
  [ -f "$T" ] || { printf '1\n🔴 %s 缺真值参照图 %s\n' "$p" "$T" > "$R/$p.res"; return; }
  TSHA=$($PY -c "import json; print(json.load(open('${T%.png}.meta.json'))['save'][0])" 2>/dev/null)
  [ "$TSHA" = "$HSHA" ] || { printf '1\n🔴 %s 存档指纹不符：真值 %s / 宿主 %s\n' "$p" "${TSHA:0:12}" "${HSHA:0:12}" > "$R/$p.res"; return; }
  if [ "$KEYS" = - ]; then KE=(); else KE=(DM_KEYS="$KEYS"); fi
  # DM_BRIGHT_K=1：3D 画面加亮（nvn_mtl.m bright_lut， ×4 曝光）是有意偏离原版的后期效果，闸门比原版真值时关掉，只核翻译本身；加亮效果由合入前回归另验
  # DM_BRIGHT_K=1: 3D scene brightening (nvn_mtl.m bright_lut, ×4 exposure) is a post effect that intentionally deviates from the original game; it is turned off when the gate check compares against the original-game ground truth, so only the translation itself is checked; the brightening effect is verified separately by the pre-merge regression
  env DM_BRIGHT_K=1 ${KE[@]+"${KE[@]}"} ${HE[@]+"${HE[@]}"} DM_GATE_SAVE="$SV" "$HERE/hostshot.sh" "$BIN" "$OUT" "$TRIG" "$R/${p}_host.png" > "$R/${p}_shot.log" 2>&1 || {
       # 站位不一致是判定结果（DM_POSCHK 按设计撤截图），不是宿主故障：报站位红，不标 KEEP
       # A position mismatch is a verdict (DM_POSCHK withdraws the screenshot by design), not a host fault: report a position fail, do not mark KEEP
       PR=$(grep -h '【红】DM_POSCHK' "$R/${p}_host.log" 2>/dev/null | head -1)
       [ -n "$PR" ] && { printf '1\n🔴 %s 站位不一致：%s\n' "$p" "${PR#*【红】}" > "$R/$p.res"; return; }
       printf '1\n🔴 %s 宿主截图失败：%s（现场保留）\n' "$p" "$(tail -1 "$R/${p}_shot.log")" > "$R/$p.res"
       echo "$p 宿主截图失败 / 看门狗：本目录是现场，不许删、不许重跑覆盖" >> "$R/KEEP"
       pid=$(cat "$R/${p}_host.png.pid" 2>/dev/null)
       if [ -n "$pid" ]; then
         find ~/Library/Logs/DiagnosticReports -name 'dmhost-*' -newer "$R/index.start" -exec sh -c 'grep -qE "(^|[^0-9])PID[^0-9]*$1([^0-9]|$)|dmhost[^0-9]*\[$1\]" "$2" 2>/dev/null && cp "$2" "$3/"' sh "$pid" {} "$R" \; 2>/dev/null
       fi
       return; }
  # 宿主日志判据（截图绿而日志里 GPU 挂起 / 命令缓冲被拒也要红）：【红】行、命令缓冲错误、GPU 挂起 / 故障、建管线失败
  # Host log criterion (a green screenshot with a GPU hang / rejected command buffer in the log must still fail): 【红】(red) lines, command buffer errors, GPU hang / fault, pipeline creation failure
  HL=$R/${p}_host.log; ERRS=$("$HERE/logred.sh" "$HL" | cut -f1)
  line=$($PY "$HERE/shotgate.py" "$S" "$T" "$R/${p}_host.png" "$R/$p") || prc=1
  # 页规格 host.require_log：宿主日志必须出现的行（如 DM_POSCHK 站位一致），缺一行即红
  # Page spec host.require_log: lines that must appear in the host log (e.g. DM_POSCHK position match); one missing line is a fail
  local miss=$($PY -c "import json,sys; L=open('$HL',errors='replace').read(); [print(x) for x in json.load(open('$S'))['host'].get('require_log',[]) if x not in L]")
  # 截图前置结论由宿主一处判、每次截图必写一行（已撤 / 本局无读取画面 / 不适用）；缺行即红，「未撤」由 hostshot 当场判红
  # The screenshot precondition verdict is decided in one place by the host, and one line is always written per screenshot (dismissed / no loading screen in this run / not applicable); a missing line is a fail, and 「未撤」(not dismissed) is failed on the spot by hostshot
  grep -qE 'SHOT 前置：(读取画面已撤|本局无读取画面|不适用)' "$HL" || miss="${miss}${miss:+$'\n'}SHOT 前置结论行（宿主未写）"
  [ -n "$miss" ] && { prc=1; line="${line/ OK / RED（宿主日志缺：$(echo "$miss" | head -1 | cut -c1-80)）}"; }
  if [ "$ERRS" -gt 0 ]; then prc=1; line="${line/ OK / RED（宿主日志 $ERRS 行错误：$("$HERE/logred.sh" "$HL" | cut -f2 | cut -c1-120)）}"; line="${line/ RED / RED（宿主日志 $ERRS 行错误）}"; fi
  printf '%s\n%s\n' "$prc" "$line" > "$R/$p.res"
}
# 各页互相独立（每页自己的存档副本与截图），并行跑（SHOTGATE_JOBS，缺省 6）：实测令牌池 17 页 J=4 61 秒 / J=6 44 秒 / 单页 8–17 秒不随并发变长
# 令牌池（/bin/bash 3.2 无 wait -n）：任一页结束即起下一页，不按批等最慢的一页
# Pages are independent of each other (each has its own save copy and screenshot) and run in parallel (SHOTGATE_JOBS, default 6): measured with the token pool, 17 pages take 61 seconds at J=4 / 44 seconds at J=6 / a single page takes 8–17 seconds and does not get longer with concurrency
# Token pool (/bin/bash 3.2 has no wait -n): the next page starts as soon as any page ends, without waiting per batch for the slowest page
mkfifo "$R/sem" && exec 3<>"$R/sem" && rm "$R/sem"
for i in $(seq 1 ${SHOTGATE_JOBS:-6}); do echo >&3; done
for p in $PAGES; do read -u 3; { page "$p"; echo >&3; } & done; wait; exec 3>&-
for p in $PAGES; do
  { read -r prc; read -r line; } < "$R/$p.res"; [ "$prc" = 0 ] || rc=1
  echo "$line"; rows="$rows<li><a href=\"$p/report.html\">$p</a>：${line#SHOTGATE }</li>"
done
echo "<!doctype html><meta charset=utf-8><title>画面闸门</title><h1>画面闸门 $(date '+%m-%d %H:%M')</h1><p>产物 ${OUT}；宿主 ${BIN}；着色器路线 ${ROUTE}；存档指纹 ${HSHA:0:12}；每页宿主完整日志 *_host.log</p><ul>${rows}</ul>" > "$R/index.html"
[ -f "$R/KEEP" ] && echo "⚠️ 宿主截图失败 / 看门狗：现场 $R 已标 KEEP（含崩溃报告副本），保留，勿重跑抹掉"
# 全绿且没标 KEEP：整个结果目录删掉，只留一份首页小结 green/<目录名>.html（各页结果行）。全绿的现场重跑就有，留着只占盘：
#   原先只删图，每页那份内嵌图的 report.html（约 1.8 MB × 17 页）还在，一次仍占约 27 MB；攒到 956 个目录 34 GB。
# 红的、标了 KEEP 的原样全留。想留全绿的整份现场看：SHOTGATE_KEEP_IMG=1
# All green and not marked KEEP: delete the whole result directory, keeping only a front-page summary green/<directory name>.html (the result line of each page). All-green artifacts can be had again by rerunning, keeping them only takes disk:
#   previously only images were deleted, and each page's report.html with embedded images (about 1.8 MB × 17 pages) remained, still about 27 MB per run; it piled up to 956 directories and 34 GB.
# Failed ones and those marked KEEP are kept in full as they are. To keep the full artifacts of an all-green run for viewing: SHOTGATE_KEEP_IMG=1
ROOT=$(dirname "$R"); REP="$R/index.html"
if [ $rc = 0 ] && [ ! -f "$R/KEEP" ] && [ "${SHOTGATE_KEEP_IMG:-0}" != 1 ] && [ "$(basename "$ROOT")" = shotgate ] && [ -n "$(basename "$R")" ]; then
  mkdir -p "$ROOT/green"; REP="$ROOT/green/$(basename "$R").html"
  { sed -e 's#<a href="[^"]*">##g' -e 's#</a>##g' "$R/index.html"; echo "<p>全绿：现场目录已删，只留这份小结（SHOTGATE_KEEP_IMG=1 可保留整份现场）</p>"; } > "$REP" && rm -rf -- "$R"
fi
bash "$HERE/shotgate_gc.sh" "$ROOT"   # 超过 SHOTGATE_KEEP_DAYS（缺省 3）天的旧现场自清，KEEP 的不动 | Old run artifacts older than SHOTGATE_KEEP_DAYS (default 3) days are cleaned up automatically; those marked KEEP are left alone
echo "SHOTGATE_ALL $([ $rc = 0 ] && echo OK || echo RED) → $REP"; exit $rc
