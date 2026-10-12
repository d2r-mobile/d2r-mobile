#!/bin/bash
# 真机生命周期一轮（界面测试 TouchSmokeTests/LifecycleSmoke）：进局 → 主屏键回桌面 30 秒 → 激活同一进程 → 再跑 20 秒，拉回 boot.txt 按日志判。
# 用法：dm/ios/tools/lifecycle.sh。冒烟类，走设备锁；构建用本树 .iosdd_ui（不碰共享目录）。末行 LIFECYCLE OK|RED
# One round of real device lifecycle (UI test TouchSmokeTests/LifecycleSmoke): enter the game → home button back to the home screen for 30 seconds → activate the same process → run 20 more seconds, pull back boot.txt and judge by the log.
# Usage: dm/ios/tools/lifecycle.sh. Smoke-test class, goes through the device lock; the build uses this tree's .iosdd_ui (does not touch the shared directory). Last line LIFECYCLE OK|RED
set -u
cd "$(dirname "$0")/.."
source tools/local_env.sh && local_env_need DM_DEVICE DM_IOS_UDID || exit 1   # 本机设备配置（不进仓，见 dm/ios/local.env.example） | Local device configuration (not committed, see dm/ios/local.env.example)
D=$DM_DEVICE; UDID=$DM_IOS_UDID; B=${DM_BUNDLE_ID:?先在本机配置里设 DM_BUNDLE_ID（App 包名，见 dm/ios/local.env.example）}
TREE=$(git rev-parse --show-toplevel); DD=$TREE/.iosdd_ui; R=${DM_ROOT:-$HOME/dm-work}/run/ios/lifecycle/$(date +%m%d_%H%M%S)_$$; mkdir -p "$R"
t(){ echo "[$(date +%T)] $*"; }
MODE=${1:-ui}
if [ "$MODE" = sim ]; then   # 应用内模拟（DM_TEST_BG）：不需要手机开界面自动化；验我们自己的全部逻辑，验不了系统侧（表面回收、系统是否续上节拍回调） | In-app simulation (DM_TEST_BG): does not need UI automation enabled on the phone; verifies all of our own logic, cannot verify the system side (surface reclamation, whether the system resumes the display-link callback)
  DM_IOS_DD="$TREE/.iosdd" bash run.sh build > "$R/build.log" 2>&1 || { t "🔴 构建失败"; grep -E "error:" "$R/build.log" | head -5; echo "LIFECYCLE RED"; exit 1; }
  source tools/devlock.sh; devlock_acquire "${SMOKE_OWNER:-未署名} 生命周期（应用内模拟）"; trap devlock_release EXIT
  pidof(){ timeout 30 xcrun devicectl device info processes --device $D 2>/dev/null | grep 'DmIOS.app/DmIOS' | awk '{print $1}' | tr '\n' ' '; }
  for p in $(pidof); do timeout 30 xcrun devicectl device process terminate --device $D --pid $p > /dev/null 2>&1; done; sleep 2
  DM_IOS_DD="$TREE/.iosdd" bash run.sh install > "$R/install.log" 2>&1 || { t "🔴 装机失败"; tail -3 "$R/install.log"; echo "LIFECYCLE RED"; exit 1; }
  KEYS='X@OpenPanel:MainMenuButtonRibbon>OpenPanel:CharacterSelectPanel,C0.84:0.208@OpenPanel:CharacterSelectPanel%1>UpdateCharacterInfo,MSG:CharacterSelect:LoadCharacter:0@UpdateCharacterInfo%0.3>FrontEndNavigation:ToGame'
  t "拉起（进局后第 25 秒起模拟后台 30 秒）"
  DEVICECTL_CHILD_DM_SAVE_FRESH=warlock DEVICECTL_CHILD_DM_AUDIO=1 DEVICECTL_CHILD_DM_TEST_BG=25,30 DEVICECTL_CHILD_DM_UIMSG=1 DEVICECTL_CHILD_DM_KEYS="$KEYS" timeout -k 5 30 xcrun devicectl device process launch --device $D $B -mode boot > "$R/launch.txt" 2>&1
  sleep 78; P0=$(pidof); trc=0; [ -n "$P0" ] || trc=1
elif [ "$MODE" = switcher ]; then   # 多任务界面里划掉：LifecycleSmoke/testSwitcherKill 一局 + testReenter 一局，判划掉前捡进背包的 r30 在不在 | Swiped away in the app switcher: one run of LifecycleSmoke/testSwitcherKill + one run of testReenter, checking whether the r30 picked up into the inventory before the swipe is still there
  DM_GEN_ONLY=1 DM_IOS_DD="$DD" bash run.sh build > "$R/gen.log" 2>&1 || { t "🔴 生成失败"; tail -3 "$R/gen.log"; echo "LIFECYCLE RED"; exit 1; }
  source tools/asc_env.sh; asc_auth
  xcodebuild build-for-testing -project DmIOS.xcodeproj -scheme DmIOS -configuration Release -destination "platform=iOS,id=$UDID" -derivedDataPath "$DD" -allowProvisioningUpdates ${ASC_AUTH[@]+"${ASC_AUTH[@]}"} > "$R/build.log" 2>&1 \
    || { t "🔴 构建失败"; grep -E "error:" "$R/build.log" | head -5; echo "LIFECYCLE RED"; exit 1; }
  source tools/devlock.sh; devlock_acquire "${SMOKE_OWNER:-未署名} 生命周期（多任务界面划掉）"; trap devlock_release EXIT
  pull(){ timeout 60 xcrun devicectl device copy from --device $D --domain-type appDataContainer --domain-identifier $B --source "Documents/$1" --destination "$R/$2" > /dev/null 2>&1; }
  t "第 1 局：进局 → 捡 r30 → 多任务界面停 3 秒 → 结束进程"
  xcodebuild test-without-building -project DmIOS.xcodeproj -scheme DmIOS -configuration Release -destination "platform=iOS,id=$UDID" -derivedDataPath "$DD" -only-testing:TouchSmokeTests/LifecycleSmoke/testSwitcherKill > "$R/test1.log" 2>&1
  pull boot.txt kill_boot.txt || { t "🔴 拉第 1 局日志失败"; echo "LIFECYCLE RED"; exit 1; }
  t "第 2 局：重进同一角色、记物品表"
  xcodebuild test-without-building -project DmIOS.xcodeproj -scheme DmIOS -configuration Release -destination "platform=iOS,id=$UDID" -derivedDataPath "$DD" -only-testing:TouchSmokeTests/LifecycleSmoke/testReenter > "$R/test2.log" 2>&1
  pull boot.txt re_boot.txt; pull lc_state.jsonl re_state.jsonl
  devlock_release; trap - EXIT
  K="$R/kill_boot.txt"; grep -a "前端：scene\|前端：失活\|进后台等游戏存盘\|存档提交 #\|DM_TEST_GIVE" "$K" | sed 's#/[^ ]*/Documents/#Documents/#g' | cut -c1-160
  cls=$(grep -a "DM_TEST_GIVE：.*「r30」" "$K" | grep -o "物品类号 [0-9]*" | head -1 | grep -o "[0-9]*$")
  gt=$(grep -an "DM_TEST_GIVE：.*「r30」" "$K" | head -1 | cut -d: -f1); ct=$(grep -an "存档提交 #" "$K" | tail -1 | cut -d: -f1)
  have=$( [ -n "$cls" ] && [ -s "$R/re_state.jsonl" ] && tail -1 "$R/re_state.jsonl" | grep -c "\"cls\":$cls," || echo 0)
  echo "r30 物品类号 ${cls:-无}；第 1 局最后一次存档提交在 r30 之后：$([ -n "$gt" ] && [ -n "$ct" ] && [ "$ct" -gt "$gt" ] && echo 是 || echo 否)；第 2 局物品表里有它：$([ "${have:-0}" -gt 0 ] && echo 有 || echo 没有)"
  [ "${have:-0}" -gt 0 ] && echo "LIFECYCLE OK" || echo "LIFECYCLE RED"; exit 0
else
DM_GEN_ONLY=1 DM_IOS_DD="$DD" bash run.sh build > "$R/gen.log" 2>&1 || { t "🔴 生成失败"; tail -3 "$R/gen.log"; echo "LIFECYCLE RED"; exit 1; }
source tools/asc_env.sh; asc_auth
xcodebuild build-for-testing -project DmIOS.xcodeproj -scheme DmIOS -configuration Release -destination "platform=iOS,id=$UDID" -derivedDataPath "$DD" -allowProvisioningUpdates ${ASC_AUTH[@]+"${ASC_AUTH[@]}"} > "$R/build.log" 2>&1 \
  || { t "🔴 构建失败"; grep -E "error:" "$R/build.log" | head -5; echo "LIFECYCLE RED"; exit 1; }
source tools/devlock.sh; devlock_acquire "${SMOKE_OWNER:-未署名} 生命周期"; trap devlock_release EXIT
t "跑界面测试（约 90 秒）"
xcodebuild test-without-building -project DmIOS.xcodeproj -scheme DmIOS -configuration Release -destination "platform=iOS,id=$UDID" -derivedDataPath "$DD" -only-testing:TouchSmokeTests/LifecycleSmoke > "$R/test.log" 2>&1; trc=$?
grep -E "Test Case.*(passed|failed)|error:|XCTAssert" "$R/test.log" | tail -4
fi
timeout 60 xcrun devicectl device copy from --device $D --domain-type appDataContainer --domain-identifier $B --source Documents/boot.txt --destination "$R/boot.txt" > "$R/pull.log" 2>&1 || { t "🔴 拉 boot.txt 失败"; echo "LIFECYCLE RED"; exit 1; }
L="$R/boot.txt"; bad=0
chk(){ if [ "$2" = 1 ]; then echo "✓ $1"; else echo "✗ $1"; bad=1; fi; }
bg=$(grep -n "前端：sceneDidEnterBackground" "$L" | head -1 | cut -d: -f1); fg=$(grep -n "前端：sceneWillEnterForeground" "$L" | tail -1 | cut -d: -f1)
chk "同一份日志里先进后台、后回前台（进程没重开）：行 ${bg:-无} → ${fg:-无}" $([ -n "$bg" ] && [ -n "$fg" ] && [ "$fg" -gt "$bg" ] && echo 1 || echo 0)
chk "进后台时图形已挂起" $(grep -q "图形挂起：在途 GPU 工作已清空" "$L" && echo 1 || echo 0)
cb=$(grep "前端：sceneDidEnterBackground" "$L" | head -1 | sed -E 's/.*客体时钟 ([0-9.]+) s.*/\1/'); cf=$(grep "前端：sceneWillEnterForeground" "$L" | tail -1 | sed -E 's/.*客体时钟 ([0-9.]+) s.*/\1/')
chk "后台那段没算进游戏时间：客体时钟 进后台 ${cb:-无} s → 回前台 ${cf:-无} s（差 < 1 s）" $(python3 -c "print(1 if abs(float('${cf:-1e9}')-float('${cb:-0}'))<1.0 else 0)" 2>/dev/null || echo 0)
ab=$(grep "前端：sceneWillEnterForeground" "$L" | tail -1 | sed -E 's/.*声卡回调累计 ([0-9]+).*/\1/'); ae=$(grep -o "声卡回调 [0-9]*\|音频回调 [0-9]*" "$L" | tail -1 | grep -o "[0-9]*$")
post=$([ -n "$fg" ] && tail -n +"$fg" "$L" || true)
chk "回前台后音频恢复行出现" $(echo "$post" | grep -q "audio 恢复" && echo 1 || echo 0)
up=$(echo "$post" | grep "上屏 至帧" | tail -1); chk "回前台后仍在上屏：${up:-无上屏行}" $([ -n "$up" ] && echo 1 || echo 0)
# 回前台后的上屏质量：从「回前台」这个事件之后量，不按帧号凑整的窗口量。
# 原判据读最后一行「上屏 至帧 N」的掉帧数 ≤ 3：那一窗按帧号每 300 帧切一次，正好横跨进后台的那一刻——进后台时排着队没上屏的帧按规矩丢弃并记成掉帧，
# 游戏在「进后台」到「图形挂起」之间还会再出几帧，赶上几帧算几帧，同一个包量出来不是 3 就是 5，和回前台后卡不卡无关。
# 现在看三件事：① 回前台那一窗里真正上了屏的帧，间隔没有超过 25 ms 的（掉帧 − 丢弃 = 0）；② 回前台后的完整显示节拍窗（每窗 300 个节拍）至少 2 个，
# 空节拍与被丢弃的帧一个都不新增；③ 进后台那一刻丢弃的帧数只记录、给个上限（12，约半秒的量），防它失控。
# Present quality after returning to the foreground: measured from the "returned to foreground" event onward, not in windows rounded by frame number.
# The old criterion read the dropped-frame count ≤ 3 from the last "present, up to frame N" line: that window is cut every 300 frames by frame number
# and happens to straddle the moment of entering the background. Frames queued but not yet presented at that moment are discarded by rule and counted as
# dropped,
# and the game still produces a few more frames between "entering background" and "graphics suspended", as many as it manages to, so the same build
# measures either 3 or 5, which has nothing to do with whether it stutters after returning to the foreground.
# Three things are checked now: ① among the frames actually presented in the return-to-foreground window, no interval exceeds 25 ms (dropped − discarded
# = 0); ② at least 2 complete display-link windows after returning to the foreground (300 display-link ticks per window),
# with not a single new empty display-link tick or discarded frame; ③ the number of frames discarded at the moment of entering the background is only recorded and
# given an upper bound (12, about half a second's worth) to keep it from running away.
lg=$(tail -n +"${fg:-1}" "$L" | python3 -c '
import re, sys
P = sys.stdin.read().split("\n")
t = [tuple(map(int, m.groups())) for l in P for m in [re.search(r"显示节拍 至 (\d+)：节拍 \d+、出图 \d+、空节拍 (\d+)、合并丢弃旧帧 (\d+)", l)] if m]
u = [tuple(map(int, m.groups())) for l in P for m in [re.search(r"上屏 至帧 \d+：\d+ 帧，掉帧 (\d+)（丢弃 (\d+)）", l)] if m]
n = max(len(t) - 1, 0); de = t[-1][1] - t[0][1] if t else -1; dd = t[-1][2] - t[0][2] if t else -1
print(u[-1][0] - u[-1][1] if u else -1, u[-1][1] if u else -1, n, de, dd)')
set -- $lg
chk "回前台那一窗上了屏的帧没有间隔超 25 ms 的：${1:-无读数} 帧" $([ "${1:--1}" = 0 ] && echo 1 || echo 0)
chk "回前台后完整显示节拍窗 ≥ 2 个且空节拍、丢弃都不新增：${3:-0} 窗（每窗 300 节拍），空节拍 +${4:-?}，丢弃 +${5:-?}" $([ "${3:-0}" -ge 2 ] && [ "${4:--1}" = 0 ] && [ "${5:--1}" = 0 ] && echo 1 || echo 0)
chk "进后台那一刻丢弃的帧数在上限内（只防失控，≤ 12）：${2:-无读数} 帧" $([ "${2:--1}" -ge 0 ] && [ "${2:-99}" -le 12 ] && echo 1 || echo 0)
chk "回前台后 3D 比例没有因此降档" $(echo "$post" | grep -q "3D 渲染比例 0.7[02]" && echo 0 || echo 1)
chk "红行 0（实得 $(grep -c '【红】' "$L")）" $(grep -q '【红】' "$L" && echo 0 || echo 1)
chk "动作序列做完、进程还在（退出码 / 存活标志 $trc）" $([ "$trc" = 0 ] && echo 1 || echo 0)
chk "模拟回前台后 2 秒声卡回调有增量：$(grep -o '测试回前台后 2 s 声卡回调 +[0-9]*' "$L" | tail -1)" $([ "$MODE" != sim ] || grep -qE '测试回前台后 2 s 声卡回调 \+[1-9]' "$L" && echo 1 || echo 0)
[ "$MODE" = sim ] && for p in $(pidof); do timeout 30 xcrun devicectl device process terminate --device $D --pid $p > /dev/null 2>&1; done
echo "日志 $R"; [ $bad = 0 ] && echo "LIFECYCLE OK" || echo "LIFECYCLE RED"; exit $bad
