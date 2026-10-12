#!/bin/bash
set -u
set -o pipefail

source "$(cd "$(dirname "$0")" && pwd)/../ios/tools/local_env.sh"   # 本机配置（不进仓）：MINI_SSH = 第二台 Mac 的 ssh 目标 | Local machine config (not in the repository): MINI_SSH = ssh target of the second Mac
REMOTE=${MINI_SSH:?MINI_SSH 必填：第二台 Mac 的 ssh 目标（用户名@主机），放进本机环境或 ~/.config/dm-ios/local.env}
ROOT=${MINI_ROOT:-${DM_ROOT:-$HOME/dm-work}/run/mini}
OUT=${1:-$(bash "$(dirname "$0")/product.sh" path touch)}
OUT_NAME=$(basename "$OUT")
HERE=$(cd "$(dirname "$0")/../.." && pwd)
COMMIT=$(git -C "$HERE" rev-parse HEAD) || exit 1
TREE=$(find "$HERE/dm" -type f -print0 | sort -z | xargs -0 shasum -a 256 | shasum -a 256 | awk '{print $1}') || exit 1
STAMP="$COMMIT $TREE $OUT_NAME"
DIR="${COMMIT:0:12}_${OUT_NAME}"
REMOTE_DIR="$ROOT/$DIR"
REMOTE_ROOT="$REMOTE:$REMOTE_DIR"

started=$(date +%s)
if timeout 20s ssh -o BatchMode=yes -o ConnectTimeout=5 "$REMOTE" "test -f '$REMOTE_DIR/STAMP' && grep -Fxq '$STAMP' '$REMOTE_DIR/STAMP'"; then
  echo "已同步跳过：$STAMP"
  echo "$REMOTE_DIR"
  exit 0
fi

[ -d "$OUT" ] || { echo "🔴 产物目录不存在：$OUT" >&2; exit 1; }
timeout 20s ssh -o BatchMode=yes -o ConnectTimeout=5 "$REMOTE" "mkdir -p '$REMOTE_DIR/src' '$REMOTE_DIR/out' '$REMOTE_DIR/data/switch' '$REMOTE_DIR/verify'" || exit 1

sync_dir() {
  local source=$1 destination=$2 follow=${3:-}
  local args=(-a --delete --info=stats2)
  [ -n "$follow" ] && args+=(-L)
  timeout 600s rsync "${args[@]}" "$source" "$REMOTE_ROOT/$destination" || exit 1
}

sync_dir "$HERE/dm/" src/dm
sync_dir "$OUT/" "out/$OUT_NAME"
sync_dir "${DM_ROOT:-$HOME/dm-work}/data/switch/truth_runs/" data/switch/truth_runs
sync_dir "${DM_ROOT:-$HOME/dm-work}/data/switch/save_ref_warlock/" data/switch/save_ref_warlock L
sync_dir "${DM_ROOT:-$HOME/dm-work}/data/switch/save_ref_warlock_v2/" data/switch/save_ref_warlock_v2 L
sync_dir "${DM_ROOT:-$HOME/dm-work}/data/switch/save_ref/" data/switch/save_ref L
sync_dir "${DM_ROOT:-$HOME/dm-work}/run/verify/dm/v4_truth_bootstrap_1002/war13_v4/" verify/war13_v4

timeout 600s ssh -o BatchMode=yes -o ConnectTimeout=5 "$REMOTE" "make -s -C '$REMOTE_DIR/src/dm/host' dmhost" || exit 1
timeout 20s ssh -o BatchMode=yes -o ConnectTimeout=5 "$REMOTE" "printf '%s\\n' '$STAMP' > '$REMOTE_DIR/STAMP'; : > '$REMOTE_DIR/.mini_sync_owner'" || exit 1
timeout 20s ssh -o BatchMode=yes -o ConnectTimeout=5 "$REMOTE" \
  "find '$ROOT' -maxdepth 1 -mindepth 1 -type d -name '[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]_*' -exec test -f '{}/.mini_sync_owner' \\; -print0 | xargs -0 -r ls -dt | tail -n +4 | xargs -r rm -rf" || exit 1

bytes=$(timeout 20s ssh -o BatchMode=yes -o ConnectTimeout=5 "$REMOTE" "du -sk '$REMOTE_DIR' | awk '{print \$1 * 1024}'") || exit 1
elapsed=$(( $(date +%s) - started ))
echo "同步字节数：$bytes"
echo "同步耗时：${elapsed}s"
echo "$REMOTE_DIR"
