#!/bin/bash
# 改写等价常驻回归：结构核对（structck）+ Unicorn 逐处等价（equiv）+ 宿主门功能（gatefn），三模块一条命令，秒级。
# 用法：dm/verify/regress.sh [改写产物目录] [模块…]
#   默认目录 = product.sh path keypage，默认模块 main sdk subsdk0
#   --struct-only：只跑 structck + gatefn（不跑 equiv），给出包产物用
#   --fresh：先用本工作树的 patch_nso.py 重出到 run/verify/dm/fresh/，验它，并与给定目录逐文件比对（查共用产物是否过期）
# 环境：DM_PY（默认 build/cache/dm/venv/bin/python）、DM_NSO_DIR、SEED、JOBS、LLVM_BIN
# 末尾汇总表：每模块 结构 / 等价 两行带分母与射程；任何一项 FAIL 退出码 1。
# Standing rewrite-equivalence regression: structural check (structck) + per-site Unicorn equivalence (equiv) + host gate function (gatefn), three modules in one command, in seconds.
# Usage: dm/verify/regress.sh [rewrite build output dir] [module...]
#   default dir = product.sh path keypage, default modules main sdk subsdk0
#   --struct-only: run only structck + gatefn (no equiv), for release build outputs
#   --fresh: first regenerate with this worktree's patch_nso.py into run/verify/dm/fresh/, verify it, and compare it file by file with the given dir (checks whether the shared build output is stale)
# Environment: DM_PY (default build/cache/dm/venv/bin/python), DM_NSO_DIR, SEED, JOBS, LLVM_BIN
# Final summary table: per module, two rows (structure / equivalence) with denominator and coverage; any FAIL gives exit code 1.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
PY=${DM_PY:-${DM_ROOT:-$HOME/dm-work}/build/cache/dm/venv/bin/python}
NSO_DIR=${DM_NSO_DIR:-${DM_ROOT:-$HOME/dm-work}/data/switch/3.3.94012/Update/ExeFS}
RUN=${DM_ROOT:-$HOME/dm-work}/run/verify/dm
FRESH=0; STRUCT_ONLY=0
[ "${1:-}" = --fresh ] && { FRESH=1; shift; }
[ "${1:-}" = --struct-only ] && { STRUCT_ONLY=1; shift; }   # 只做结构核对 + 门功能（出包产物 touch 用：等价试验与 keypage 共用改写路径，不重复跑） | Structural check + gate function only (for the release build output touch: the equivalence trials share the rewrite path with keypage and are not rerun)
OUT=${1:-$(bash "$(dirname "$0")/../tools/product.sh" path keypage)}; [ $# -gt 0 ] && shift
MODS=${*:-main sdk subsdk0}
mkdir -p "$RUN"
TS=$(date +%m%d_%H%M%S)_$$
LOG=$RUN/regress_$TS.log
echo "# regress $TS out=$OUT mods=$MODS fresh=$FRESH struct_only=$STRUCT_ONLY seed=${SEED:-1} git=$(git -C "$HERE" rev-parse --short HEAD) $(git -C "$HERE" status --porcelain -- . ../tools | wc -l | tr -d ' ')处未提交" | tee "$LOG"

if [ $FRESH = 1 ]; then
  F=$RUN/fresh; rm -rf "$F"; mkdir -p "$F"
  for m in $MODS; do
    "$PY" "$HERE/../tools/patch_nso.py" "$NSO_DIR/$m" "$F" "$m" >> "$LOG" 2>&1 || { echo "FAIL patch_nso $m" | tee -a "$LOG"; exit 1; }
    for e in patched.bin fixups.tsv gates.tsv sites.tsv; do
      if [ ! -e "$OUT/$m.$e" ]; then r=缺; elif cmp -s "$F/$m.$e" "$OUT/$m.$e"; then r=同; else r=不同; fi
      echo "FRESH $m.$e 与 $OUT $r" | tee -a "$LOG"
    done
  done
  OUT=$F
fi

rc=0
for m in $MODS; do
  "$PY" "$HERE/structck.py" "$OUT" "$m" "$NSO_DIR/$m" >> "$LOG" 2>&1 || rc=1
  [ $STRUCT_ONLY = 1 ] || "$PY" "$HERE/equiv.py" "$OUT" "$m" "$NSO_DIR/$m" --seed "${SEED:-1}" ${JOBS:+--jobs $JOBS} --tsv "$RUN/eq_${m}_$TS.tsv" >> "$LOG" 2>&1 || rc=1
done
# 门功能回归：用宿主现行 entry.S 现编测试台，逐个门执行 _dm_gate 核对回写契约（dm/verify/gatefn）
# Gate function regression: build a test bench on the spot from the host's current entry.S and execute _dm_gate gate by gate to check the write-back contract (dm/verify/gatefn)
GT=$RUN/gate_test_$TS
if cc -O1 -o "$GT" "$HERE/gatefn/gate_test.c" "$HERE/gatefn/gate_tramp.S" "$HERE/../host/entry.S" >> "$LOG" 2>&1; then
  G=(); for m in $MODS; do [ -s "$OUT/$m.gates.tsv" ] && G+=("$OUT/$m.gates.tsv"); done
  "$GT" "${G[@]}" >> "$LOG" 2>&1 || rc=1; rm -f "$GT"
else echo "FAIL 门功能测试台编译失败" >> "$LOG"; rc=1; fi
echo "---- 汇总（${LOG}）"
grep -E '^(FRESH|STRUCT|EQUIV|GATEFN|FAIL|DIFF|NOEFF|MUTMISS)' "$LOG" | grep -v '^FRESH.* 同$'
[ $rc = 0 ] && echo "REGRESS OK $(basename "$OUT")" || echo "REGRESS FAIL $(basename "$OUT")"
exit $rc
