#!/bin/bash
# 批量截取画质度量帧；输出目录由调用方提供，不向工作树写产物。
# Batch-capture the image-quality metric frames; the output directory is provided by the caller, and no build output is written to the working tree.
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
FRAMES="$HERE/frames.tsv"
HOSTSHOT="$HERE/../hostshot.sh"
BIN=${1:?用法：shoot.sh <dmhost> <产物目录> <DM_GAME_RES> <输出目录> [并行数=4（本机自起宿主上限）] [编号…]}
PRODUCTS=${2:?缺少产物目录}
GAME_RES=${3:?缺少 DM_GAME_RES}
OUTPUT=${4:?缺少输出目录}
JOBS=${5:-4}
shift 5 || true

case "$JOBS" in ''|*[!0-9]*|0) echo "失败：并行数必须是正整数" >&2; exit 2 ;; esac
mkdir -p "$OUTPUT/tmp" || exit 2

# 统一从 frames.tsv 读取，支持传编号或名称；未指定时跑全部 20 帧。
# Always read from frames.tsv; accepts ids or names; when unspecified, runs all 20 frames.
selected=''
if [ "$#" -gt 0 ]; then
  for wanted in "$@"; do
    row=$(awk -F '\t' -v w="$wanted" '$1 == w || $2 == w { print; exit }' "$FRAMES")
    [ -n "$row" ] || { echo "失败：未知帧编号或名称 $wanted" >&2; exit 2; }
    selected="$selected$row
"
  done
else
  selected=$(cat "$FRAMES")
fi

start=$SECONDS
fail_file="$OUTPUT/tmp/failed.$$"
: > "$fail_file"
run_frame() {
  local row=$1 id name source png log frame_tmp trigger keys env_json page key value
  local -a env_args
  id=$(printf '%s\n' "$row" | cut -f1)
  name=$(printf '%s\n' "$row" | cut -f2)
  source=$(printf '%s\n' "$row" | cut -f3)
  png="$OUTPUT/$id.png"
  log="$OUTPUT/$id.log"
  frame_tmp="$OUTPUT/tmp/$id"
  mkdir -p "$frame_tmp"
  : > "$frame_tmp/run.log"

  env_args=(DM_NVN_SHOT_OUT=2868x1320 DM_GAME_RES="$GAME_RES" DM_TEMP="$frame_tmp")
  if [[ "$source" == shotgate_pages/*.json ]]; then
    page="$HERE/../$source"
    IFS=$'\t' read -r trigger keys env_json <<EOF2
$(python3 - "$page" <<'PY'
import json, sys
page = json.load(open(sys.argv[1]))
host = page['host']
print('\t'.join((host['trigger'], host.get('keys') or '-', json.dumps(host.get('env', {}), separators=(',', ':')))))
PY
)
EOF2
    [ "$keys" = - ] || env_args+=("DM_KEYS=$keys")
    while IFS=$'\t' read -r key value; do
      [ -n "$key" ] && env_args+=("$key=$value")
    done < <(python3 - "$env_json" <<'PY'
import json, os, sys
for key, value in json.loads(sys.argv[1]).items():
    print(key + '\t' + os.path.expanduser(value))
PY
)
  else
    IFS=: read -r _ combat tick <<EOF2
$source
EOF2
    trigger="DM:Tick:${tick}#+20"
    env_args+=(
      'DM_KEYS=X@OpenPanel:MainMenuButtonRibbon>OpenPanel:CharacterSelectPanel,DOWN@OpenPanel:CharacterSelectPanel>UpdateCharacterInfo,A@UpdateCharacterInfo%1>CharacterSelect:SelectCharacter,A@CharacterSelect:SelectCharacter>FrontEndNavigation:ToGame'
      "DM_C2S_REPLAY=${DM_ROOT:-$HOME/dm-work}/run/verify/ryu/war_summon13_1002_takeover/c2s.jsonl"
      DM_GAME_SEED=305419896
      DM_FIXED_TIME=1790895128
      "DM_TICK_HOLD=${tick}:SHOT"
    )
  fi

  if env "${env_args[@]}" "$HOSTSHOT" "$BIN" "$PRODUCTS" "$trigger" "$png" 180 >"$frame_tmp/run.log" 2>&1; then
    if [ -f "$log" ]; then cat "$frame_tmp/run.log" >> "$log"; else cp "$frame_tmp/run.log" "$log"; fi
    echo "完成 $id $name"
  else
    if [ -f "$log" ]; then cat "$frame_tmp/run.log" >> "$log"; else cp "$frame_tmp/run.log" "$log"; fi
    printf '%s\t%s\n' "$id" "$name" >> "$fail_file"
    echo "失败 $id ${name}（见 ${log}）" >&2
  fi
}

# Bash 3.2 无 wait -n，用 FIFO 令牌池维持并行上限。
# Bash 3.2 has no wait -n; use a FIFO token pool to keep the parallelism cap.
sem="$OUTPUT/tmp/sem.$$"
mkfifo "$sem" || exit 2
exec 9<>"$sem"
rm -f "$sem"
for _ in $(seq 1 "$JOBS"); do printf '\n' >&9; done
while IFS= read -r row; do
  [ -n "$row" ] || continue
  read -u 9
  { run_frame "$row"; printf '\n' >&9; } &
done <<EOF2
$selected
EOF2
wait
exec 9>&-

rc=0
if [ -s "$fail_file" ]; then
  rc=1
  echo "失败帧："
  cat "$fail_file"
fi
rm -rf "$OUTPUT/tmp"
elapsed=$((SECONDS - start))
echo "总墙钟：${elapsed}s"
exit "$rc"
