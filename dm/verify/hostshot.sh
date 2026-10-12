#!/bin/bash
# 宿主截图（画面闸门用）：标准存档新副本起 dmhost，第 N 帧 DM_NVN_SHOT 截图，截到即按自记 pid 停。
# 用法：hostshot.sh <dmhost> <产物目录> <帧号 | 触发子串+帧偏移> <输出.png> [超时秒=120]   其余环境变量（DM_KEYS、DM_AOC_OWNED 等）原样透传
#   HOSTSHOT_WRAP：宿主命令前缀（如 taskpolicy -c background 让宿主跑慢，量随墙钟走的动画）
#   第 3 参含「+」= 按 UI 消息截图（DM_NVN_SHOT_UI），否则按帧号（DM_NVN_SHOT）。存档：DM_GATE_SAVE（缺省现行标准存档 save_ref_warlock），起跑前打印其指纹
# Host screenshot (for the screenshot gate): start dmhost on a fresh copy of the standard save, take the DM_NVN_SHOT screenshot at frame N, and stop by the self-recorded pid as soon as it is captured.
# Usage: hostshot.sh <dmhost> <build output dir> <frame number | trigger substring+frame offset> <output.png> [timeout seconds=120]   other environment variables (DM_KEYS, DM_AOC_OWNED, etc.) are passed through unchanged
#   HOSTSHOT_WRAP: host command prefix (e.g. taskpolicy -c background to make the host run slowly, for measuring animations that follow the wall clock)
#   3rd argument containing "+" = screenshot by UI message (DM_NVN_SHOT_UI), otherwise by frame number (DM_NVN_SHOT). Save: DM_GATE_SAVE (default: the current standard save save_ref_warlock), its fingerprint is printed before the run starts
set -u
BIN=$1; OUT=$2; N=$3; PNG=$4; TO=${5:-600}
SV=${DM_GATE_SAVE:-${DM_ROOT:-$HOME/dm-work}/data/switch/save_ref_warlock/0000000000000001}
echo "SAVE_SHA $("$(dirname "$0")/savesha.sh" "$SV") $SV"
W=$(mktemp -d "${PNG%.png}.XXXX"); cp -R "$SV/0" "$W/save" && chmod -R u+w "$W/save" || { echo "拷存档失败" >&2; exit 2; }
case "$N" in *+*) SHOTENV="DM_NVN_SHOT_UI=$N:$PNG" ;; *) SHOTENV="DM_NVN_SHOT=$N:$PNG" ;; esac
mkdir -p "$W/temp"
${HOSTSHOT_WRAP:-} env DM_SAVE="$W/save" DM_TEMP="$W/temp" "$SHOTENV" DM_WATCH_STALL=15 DM_WATCH=$((TO * 5 / 4)) "$BIN" "$OUT" main sdk > "$W/host.log" 2>&1 & pid=$!
printf '%s\n' "$pid" > "${PNG%.png}.pid"
s=$SECONDS
# 只认图形侧写盘完成行「截图 宽x高 格式 n → 路径 成功」：DM_NVN_SHOT_UI 的「命中 → 第 +N 帧截图 路径」行也含路径，按它判会在截图前停宿主（假「截到」）
# Only accept the graphics-side write-complete line 「截图 宽x高 格式 n → 路径 成功」(screenshot WxH format n → path succeeded): the DM_NVN_SHOT_UI line 「命中 → 第 +N 帧截图 路径」(hit → screenshot at frame +N path) also contains the path, and judging by it would stop the host before the screenshot (a false "captured")
until grep -q "→ $PNG 成功" "$W/host.log" 2>/dev/null; do
  grep -q "→ $PNG 失败" "$W/host.log" 2>/dev/null && { kill $pid; echo "截图写盘失败" >&2; rm -rf "$W"; exit 1; }
  grep -q "读取画面未撤（等待" "$W/host.log" 2>/dev/null && { kill $pid; wait $pid 2>/dev/null; cp "$W/host.log" "${PNG%.png}.log"; echo "读取画面未撤（等待 15 秒）" >&2; rm -rf "$W"; exit 1; }
  grep -q "坐标核对不一致" "$W/host.log" 2>/dev/null && { kill $pid; wait $pid 2>/dev/null; cp "$W/host.log" "${PNG%.png}.log"; echo "站位核对不一致，宿主已撤掉截图" >&2; rm -rf "$W"; exit 1; }   # 不等超时：宿主不会再截图 | Do not wait for the timeout: the host will not take a screenshot any more
  kill -0 $pid 2>/dev/null || { echo "宿主在第 $N 帧前退出（日志 ${PNG%.png}.log）" >&2; cp "$W/host.log" "${PNG%.png}.log"; tail -3 "$W/host.log" >&2; rm -rf "$W"; exit 1; }
  [ $((SECONDS - s)) -gt "$TO" ] && { kill $pid; echo "$TO 秒未到第 $N 帧" >&2; grep '【红】看门狗' "$W/host.log" | tail -1 >&2 || true; rm -rf "$W"; exit 1; }
  sleep 1
done
kill $pid 2>/dev/null; wait $pid 2>/dev/null; cp "$W/host.log" "${PNG%.png}.log"; rm -rf "$W"   # 完整宿主日志留在截图旁（闸门按它判 GPU 错误） | The full host log is kept next to the screenshot (the gate check judges GPU errors from it)
[ -s "$PNG" ] || { echo "日志说截到但 $PNG 不存在" >&2; exit 1; }
echo "截到 ${PNG}（$((SECONDS - s)) 秒）"
