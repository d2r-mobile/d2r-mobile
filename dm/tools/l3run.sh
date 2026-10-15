#!/bin/bash
# L3 四场景并行；每段独立起档，判定沿用 l3seg.sh。
# L3 four scenes in parallel; each segment starts from its own save independently, and the verdict reuses l3seg.sh.
set -u
set -o pipefail
source "$(cd "$(dirname "$0")" && pwd)/../ios/tools/local_env.sh"   # 本机配置（不进仓）：MINI_SSH = 第二台 Mac 的 ssh 目标 | Local machine config (not in the repository): MINI_SSH = ssh target of the second Mac
here=$(cd "$(dirname "$0")/../.." && pwd); remote=${MINI_SSH:?MINI_SSH 必填：第二台 Mac 的 ssh 目标（用户名@主机），放进本机环境或 ~/.config/dm-ios/local.env}; out=${DM_OUT:-${DM_ROOT:-$HOME/dm-work}/build/cache/dm/out/3.3.94012-r28-touch}
if [ "${L3_SKIP_SYNC:-0}" != 1 ]; then remote_root=$(timeout 600s "$here/dm/tools/mini_sync.sh" "$out" | tail -1) || exit 1; else remote_root=${MINI_ROOT:?L3_SKIP_SYNC=1 需要 MINI_ROOT=远端同步目录}; fi
stamp=$(date +%Y%m%d_%H%M%S); root=${L3_RUN_DIR:-${DM_ROOT:-$HOME/dm-work}/run/l3/$stamp}; mkdir -p "$root"; start=$(date +%s); pids=
for spec in 'cvd5 16000' 'camp1 16000' 'wild1 16000' 'war13_v4 16000'; do
  set -- $spec
  ( timeout 130s ssh -o BatchMode=yes -o ConnectTimeout=5 "$remote" "L3_RUN_DIR='$remote_root/l3/$stamp/$1' MINI_ROOT='$remote_root' DM_OUT='$remote_root/out/$(basename "$out")' L3_TRUTH_ROOT='$remote_root/data/switch/truth_runs' L3_WAR_ROOT='$remote_root/verify/war13_v4' L3_SAVE_ROOT='$remote_root/data/switch' '$remote_root/src/dm/tools/l3seg.sh' '$1' '$2'" >"$root/$1.log" 2>&1 ) & pids="$pids $!"
done
rc=0; for p in $pids; do wait "$p" || rc=1; done
grep -hE 'REGRESSION|METRIC_RED|L3SEG|推进|存档完整性' "$root"/*.log || true
if [ "$rc" -eq 0 ] && ! grep -q 'L3SEG .* RED' "$root"/*.log; then result=OK; else result=RED; fi
wall=$(( $(date +%s)-start )); echo "L3 $result 用时 $wall 秒"; timeout 20s ssh -o BatchMode=yes -o ConnectTimeout=5 "$remote" "pgrep -af dmhost || true" || true; du -sh "$root"; exit 0   # pgrep 只列不判 | pgrep only lists, does not decide
