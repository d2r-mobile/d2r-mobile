#!/bin/bash
# 真机 vs Mac 第 N 帧逐像素对拍（每次 main 更新后跑）：Mac 截两次（取自身非确定区作遮罩）→ 真机截一次 → frame_cmp.py。
# 用法：dm/ios/tools/frame_cmp.sh [帧号=300] [--rect x0,y0,x1,y1 ...]   产物与日志在 $DM_ROOT/run/ios/cmp/（每次清空重来）
# 退出码 0 = 遮罩外逐位全等且屏幕级三项全过。宽屏产物对拍：DM_GAME_RES=宽x高 让 Mac 按同分辨率出图（front_mac.m）。Mac 截到图即停该进程（按自记 pid）；真机按看门狗 DM_WATCH 收尾。
# 帧号与时间：两端出图节拍同为 60 Hz、游戏出图间隔 2（约 30 帧/秒，窗口与无窗口一致），第 N 帧约在 N/30 秒；
# 缺省 300（约 10 秒）落在选角页稳态（r5 起打开即选角，约 2.4 秒到主菜单）。
# Per-pixel comparison of frame N, real device vs Mac (run after every main update): Mac captures twice (its own non-deterministic regions become the mask) → the real device captures once → frame_cmp.py.
# Usage: dm/ios/tools/frame_cmp.sh [frame=300] [--rect x0,y0,x1,y1 ...]   Build output and logs are in $DM_ROOT/run/ios/cmp/ (cleared and redone every time)
# Exit code 0 = bit-identical outside the mask and all three screen-level checks pass. Widescreen build output comparison: DM_GAME_RES=WxH makes Mac output frames at the same resolution (front_mac.m). The Mac process is stopped as soon as its image is captured (by the self-recorded pid); the real device is wound up by the watchdog DM_WATCH.
# Frame number and time: both ends present frames at a 60 Hz cadence with a game present interval of 2 (about 30 frames/second, the same with and without a window), so frame N is at about N/30 seconds;
# the default 300 (about 10 seconds) lands in the steady state of the character select page (since r5 it opens straight to character select, about 2.4 seconds to the main menu).
set -u
cd "$(dirname "$0")/.."
N=300; case "${1:-}" in [0-9]*) N=$1; shift ;; esac
OUT=${DM_OUT:-${DM_ROOT:-$HOME/dm-work}/build/cache/dm/out/3.3.94012-r10}
C=${DM_ROOT:-$HOME/dm-work}/run/ios/cmp; rm -rf "$C"; mkdir -p "$C"
source tools/local_env.sh && local_env_need DM_DEVICE || exit 1   # 本机设备配置（不进仓，见 dm/ios/local.env.example） | Local device configuration (not committed, see dm/ios/local.env.example)
D=$DM_DEVICE; B=${DM_BUNDLE_ID:?先在本机配置里设 DM_BUNDLE_ID（App 包名，见 dm/ios/local.env.example）}
SAVE_REF=${DM_ROOT:-$HOME/dm-work}/data/switch/save_ref/0000000000000001/0   # 标准存档：每局拷新副本（两端起始状态一致） | Standard save: a fresh copy per run (same starting state on both ends)
t(){ echo "[$(date +%T)] $*"; }

# 前置：真机 RomFS 完整（46 个文件）。缺了游戏会在 nnMain 早退，症状只是「没截到帧」，在这里直接报根因
# Precondition: the real device's RomFS is complete (46 files). If any is missing the game exits early in nnMain and the only symptom is "no frame captured"; report the root cause directly here
xcrun devicectl device info files --device $D --domain-type appDataContainer --domain-identifier $B --subdirectory Documents/romfs/data --json-output "$C/romfs_ls.json" > /dev/null 2>&1
nf=$(python3 -c "import json; d=json.load(open('$C/romfs_ls.json')); print(sum(not f['resources']['isDirectory'] for f in d['result']['files']))" 2>/dev/null)
[ "${nf:-0}" = 46 ] || { t "🔴 真机 Documents/romfs/data 文件数 ${nf:-0} ≠ 46：RomFS 不全，先推送（见 dm/ios/README.md 推送一节）"; exit 1; }
make -s -C ../host || { t "🔴 Mac 宿主编译失败"; exit 1; }
# Mac 两次：第二次压到后台 QoS（能效核）跑慢，使到第 N 帧时墙钟不同，按墙钟推进的动画才会在两次间露出差异
# （两次全速连跑到第 300 帧同为 1–3 秒，动画相位相同、自身差异 0；慢跑 3/3 次露出约 6,600 像素）
# Mac twice: the second run is pushed to background QoS (efficiency cores) to run slow, so the wall clock at frame N differs and wall-clock-driven animation shows differences between the two runs
# (two back-to-back full-speed runs both reach frame 300 in 1–3 seconds, with the same animation phase and 0 self-difference; the slow run exposed about 6,600 pixels in 3/3 tries)
for k in a b; do
  POL=""; [ $k = b ] && POL="taskpolicy -c background"
  rm -rf "$C/save_$k"; cp -R "$SAVE_REF" "$C/save_$k" && chmod -R u+w "$C/save_$k" || { t "🔴 拷标准存档失败"; exit 1; }
  DM_SAVE="$C/save_$k" DM_NVN_SHOT="$N:$C/mac_$k.png" DM_WATCH=120 $POL ../host/dmhost "$OUT" main sdk > "$C/mac_$k.log" 2>&1 & pid=$!
  s=$SECONDS
  until grep -q "截图.*$C/mac_$k.png" "$C/mac_$k.log" 2>/dev/null; do
    kill -0 $pid 2>/dev/null || { t "🔴 Mac 第 $k 次在截到第 $N 帧前退出"; tail -3 "$C/mac_$k.log"; exit 1; }
    [ $((SECONDS - s)) -gt 120 ] && { kill $pid; t "🔴 Mac 第 $k 次 120 秒未到第 $N 帧"; exit 1; }
    sleep 1
  done
  kill $pid 2>/dev/null; wait $pid 2>/dev/null
  t "Mac 第 $k 次截到（$((SECONDS - s)) 秒）"
done

./run.sh build > "$C/ios_build.log" 2>&1 || { t "🔴 真机构建失败"; tail -5 "$C/ios_build.log"; exit 1; }
./run.sh install >> "$C/ios_build.log" 2>&1 || { t "🔴 真机装机失败"; tail -5 "$C/ios_build.log"; exit 1; }
# 真机存档：App 自己把 Documents/save 换成资源里的 save_ref 新副本（DM_SAVE_FRESH）。勿用 devicectl --remove-existing-content：它会清掉整个 Documents
# Real-device save: the App itself replaces Documents/save with a fresh copy of save_ref from its resources (DM_SAVE_FRESH). Do not use devicectl --remove-existing-content: it wipes the entire Documents
DEVICECTL_CHILD_DM_SAVE_FRESH=1 DEVICECTL_CHILD_DM_NVN_SHOT="$N:cmp_$N.png" DEVICECTL_CHILD_DM_SCREEN_SHOT="$((N / 30 + 3)):screen.png" DEVICECTL_CHILD_DM_WATCH=${DM_IOS_WATCH:-45} ./run.sh boot 120 > "$C/ios_run.log" 2>&1
cp "${DM_ROOT:-$HOME/dm-work}/run/ios/out/boot.txt" "$C/ios.log" 2>/dev/null
grep -q "截图.*cmp_$N.png 成功" "$C/ios.log" || { t "🔴 真机没截到第 $N 帧（看 $C/ios.log；可加大 DM_IOS_WATCH）"; exit 1; }
xcrun devicectl device copy from --device $D --domain-type appDataContainer --domain-identifier $B \
  --source "Documents/cmp_$N.png" --destination "$C/ios.png" > /dev/null 2>&1 || { t "🔴 拉回真机截图失败"; exit 1; }
t "真机截到并拉回"
# 屏幕级截图（整个窗口：方向、黑边、实际显示）：在第 N 帧之后约 3 秒截；三项自动核见 screen_check.py（DM_SCREEN_WANT=full | 16:9，缺省 full）
# Screen-level screenshot (the whole window: orientation, black bars, what is actually displayed): taken about 3 seconds after frame N; for the three automatic checks see screen_check.py (DM_SCREEN_WANT=full | 16:9, default full)
for f in screen.png screen.png.txt; do
  xcrun devicectl device copy from --device $D --domain-type appDataContainer --domain-identifier $B --source "Documents/$f" --destination "$C/$f" > /dev/null 2>&1 || { t "🔴 拉回屏幕级截图 $f 失败"; exit 1; }
done
python3 tools/screen_check.py "$C/screen.png" "$C/screen.png.txt" "$C/ios.log" --want "${DM_SCREEN_WANT:-full}"; SC=$?

python3 tools/frame_cmp.py "$C/mac_a.png" "$C/mac_b.png" "$C/ios.png" --mask-out "$C/mask.png" "$@"; FC=$?
exit $(( FC || SC ))
