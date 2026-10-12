#!/bin/bash
# 发热归因 A / B（各 N 秒，缺省 300）：B = -mode blank（最小 Metal 60 帧清屏中灰，屏幕与显示链本身）；A = 我们的 App 停在选角页（boot，标准存档）。
# 每 10 秒一行 DM_HEAT（温控、CPU%、出图帧率、GPU 毫秒均值 / p95、亮度、最高刷新）+ DM_THRCPU 按线程 CPU；A 另开 DM_NVN_TSV 逐帧计时。用法：dm/ios/tools/heat_ab.sh [秒=300]（先 run.sh build，再 run.sh install）
# Heat attribution A / B (N seconds each, default 300): B = -mode blank (minimal Metal clearing the screen to mid-gray at 60 fps: the screen and the display chain themselves); A = our App sitting on the character select page (boot, standard save).
# One DM_HEAT line every 10 seconds (thermal state, CPU%, output frame rate, GPU ms mean / p95, brightness, max refresh rate) + DM_THRCPU per-thread CPU; A additionally enables DM_NVN_TSV per-frame timing. Usage: dm/ios/tools/heat_ab.sh [seconds=300] (first run.sh build, then run.sh install)
set -u
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/local_env.sh" && local_env_need DM_DEVICE || exit 1   # 本机设备配置（不进仓，见 dm/ios/local.env.example） | Local device configuration (not committed, see dm/ios/local.env.example)
S=${1:-300}; D=$DM_DEVICE; B=${DM_BUNDLE_ID:?先在本机配置里设 DM_BUNDLE_ID（App 包名，见 dm/ios/local.env.example）}; R=${DM_ROOT:-$HOME/dm-work}/run/ios/heat; RUN=$(date +%H%M%S); mkdir -p $R
t(){ echo "[$(date +%T)] $*"; }
pid(){ xcrun devicectl device info processes --device $D 2>/dev/null | grep "DmIOS.app/DmIOS" | awk '{print $1}'; }
for g in B A; do
  m=boot; [ $g = B ] && m=blank
  DEVICECTL_CHILD_DM_HEAT="heat_${g}_$RUN.tsv,10" DEVICECTL_CHILD_DM_THRCPU="thr_${g}_$RUN.tsv,10" DEVICECTL_CHILD_DM_NVN_TSV="frames_${g}_$RUN.tsv" DEVICECTL_CHILD_DM_SAVE_FRESH=1 DEVICECTL_CHILD_DM_WATCH=$((S + 20)) \
    xcrun devicectl device process launch --device $D --terminate-existing $B -mode $m > $R/launch_$g.log 2>&1 || { t "🔴 $g 启动失败"; exit 1; }
  t "${g}（${m}）开始 $S 秒"; sleep $S
  p=$(pid); [ -n "$p" ] && xcrun devicectl device process terminate --device $D --pid $p > /dev/null 2>&1; sleep 3
  [ -z "$(pid)" ] || t "🔴 $g 停不掉"
  for f in heat thr frames; do [ $g = B ] && [ $f = frames ] && continue
    xcrun devicectl device copy from --device $D --domain-type appDataContainer --domain-identifier $B --source "Documents/${f}_${g}_$RUN.tsv" --destination $R/${f}_$g.tsv > /dev/null 2>&1 || t "🔴 拉 ${f}_$g 失败"; done
  t "$g 结束"
done
python3 - $R <<'PY'
import sys, statistics as st
R = sys.argv[1]
for g, name in (('B', '空白负对照'), ('A', '我们的 App 选角页')):
    rows = [l.rstrip('\n').split('\t') for l in open(f'{R}/heat_{g}.tsv')][1:]
    rows = [r for r in rows if len(r) == 9]
    th = [int(r[1]) for r in rows]; cpu = [float(r[2]) for r in rows]; fps = [float(r[3]) for r in rows]
    ga = [float(r[4]) for r in rows if float(r[4]) >= 0]; gp = [float(r[5]) for r in rows if float(r[5]) >= 0]
    print(f'{g} {name}：{len(rows)} 行 / {rows[-1][0] if rows else 0} 秒；温控 起 {th[0] if th else "?"} 止 {th[-1] if th else "?"}（序列 {"".join(map(str, th))}）；'
          f'CPU% 中位 {st.median(cpu):.1f}（{min(cpu):.1f}–{max(cpu):.1f}）；出图帧率 中位 {st.median(fps):.1f}；'
          + (f'GPU 每帧 均值中位 {st.median(ga):.2f} ms、p95 中位 {st.median(gp):.2f} ms；' if ga else 'GPU 无（空白模式不取）；')
          + f'亮度 {rows[0][7]}；最高刷新 {rows[0][8]}')
PY
