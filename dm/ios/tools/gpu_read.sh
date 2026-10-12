#!/bin/bash
# 真机选角页 GPU 读数：第一局全屏逐帧 @gpu_ms（DM_NVN_TSV）取第 600–700 帧均值 / p95；第二局 16:9 串行化逐通道（DM_NVN_PASSGPU + DM_NVN_PASSGPU_SERIAL=1），供按通道号对照。
# 用法：DM_OUT=<产物> dm/ios/tools/gpu_read.sh     产物：$DM_ROOT/run/ios/gpu/（frames_full.tsv、passgpu_serial_16x9.tsv、日志）
# Real device character select page GPU readings: run 1, full-screen per-frame @gpu_ms (DM_NVN_TSV), mean / p95 over frames 600–700; run 2, 16:9 serialized per-pass (DM_NVN_PASSGPU + DM_NVN_PASSGPU_SERIAL=1), for cross-reference by pass number.
# Usage: DM_OUT=<build output> dm/ios/tools/gpu_read.sh     Output: $DM_ROOT/run/ios/gpu/ (frames_full.tsv, passgpu_serial_16x9.tsv, logs)
set -u
cd "$(dirname "$0")/.."
source tools/local_env.sh && local_env_need DM_DEVICE || exit 1   # 本机设备配置（不进仓，见 dm/ios/local.env.example） | Local device configuration (not committed, see dm/ios/local.env.example)
D=$DM_DEVICE; B=${DM_BUNDLE_ID:?先在本机配置里设 DM_BUNDLE_ID（App 包名，见 dm/ios/local.env.example）}; R=${DM_ROOT:-$HOME/dm-work}/run/ios/gpu; RUN=$(date +%H%M%S); mkdir -p $R
./run.sh build > $R/build.log 2>&1 || { echo "🔴 构建失败"; tail -5 $R/build.log; exit 1; }
./run.sh install >> $R/build.log 2>&1 || { echo "🔴 装机失败"; tail -5 $R/build.log; exit 1; }
pull(){ xcrun devicectl device copy from --device $D --domain-type appDataContainer --domain-identifier $B --source "Documents/$1" --destination "$R/$2" > /dev/null 2>&1 || echo "🔴 拉 $1 失败"; }
DEVICECTL_CHILD_DM_SAVE_FRESH=1 DEVICECTL_CHILD_DM_NVN_TSV=frames_$RUN.tsv DEVICECTL_CHILD_DM_WATCH=20 ./run.sh boot 60 > $R/full.run 2>&1
pull frames_$RUN.tsv frames_full.tsv; cp ${DM_ROOT:-$HOME/dm-work}/run/ios/out/boot.txt $R/full.log
DEVICECTL_CHILD_DM_SAVE_FRESH=1 DEVICECTL_CHILD_DM_GAME_RES=1600x900 DEVICECTL_CHILD_DM_NVN_PASSGPU=passgpu_$RUN.tsv DEVICECTL_CHILD_DM_NVN_PASSGPU_SERIAL=1 DEVICECTL_CHILD_DM_WATCH=20 ./run.sh boot 60 > $R/serial.run 2>&1
pull passgpu_$RUN.tsv passgpu_serial_16x9.tsv; cp ${DM_ROOT:-$HOME/dm-work}/run/ios/out/boot.txt $R/serial.log
python3 - $R <<'PY'
import sys
R = sys.argv[1]
g = sorted(float(l.split('\t')[2]) for l in open(f'{R}/frames_full.tsv') if '\t@gpu_ms\t' in l and 600 <= int(l.split('\t')[0]) <= 700)
print(f'全屏 第 600–700 帧 GPU：{len(g)} 帧，均值 {sum(g) / len(g):.2f} ms，p95 {g[int(len(g) * 0.95)]:.2f} ms' if g else '🔴 没有 @gpu_ms 行')
PY
grep -h "【红】" $R/full.log $R/serial.log | grep -v 贴图号 | sort -u | head -5
wc -l < $R/passgpu_serial_16x9.tsv
