#!/bin/bash
# Mac 上的 iOS 冒烟：同一个 iOS 包（arm64 iphoneos 的 DmIOS.app，不是 Mac 宿主）按「Designed for iPad」在 Apple 芯片 Mac 上原样启动，
# 判据与 smoke.sh 相同（主菜单 / 进局 / 撤屏 ×N + 玩家路径 + 真出声），写同一份台账（结果列前缀「Mac:」，末列设备）。做法出处：
# SMOKE_MAC_PKG_ONLY=1：只构建、包装、（远端时）传到 smoke_mac_in/<目录名>，不取锁、不跑、不记冒烟台账，末行「SMOKE-MAC 只出包 <路径>」。
#   出回归包用（回归包出完不再单跑一次冒烟占 mini 锁，那一遍的第一局本身就验了起包到进局；合入的冒烟门槛仍由 merge.sh 管）。
# 用法：smoke_mac.sh <次数>。SMOKE_MAC_HOST=local（缺省）| <ssh 目标，如 用户名@主机>：远端只跑不构建——本机构建、包好后 rsync 过去，结果拉回本机记账。
# 能替真机的：iOS 外壳（main.m / front_ios.m / UIKit / 资源定位 / 生命周期）起得来、进得了局、不被看门狗杀、真出声。不能替的：热态、真实触点、A19 GPU 时间、安装。
# iOS smoke test on Mac: the same iOS app (arm64 iphoneos DmIOS.app, not the Mac host) launched as is on an Apple silicon Mac as "Designed for iPad",
# criteria same as smoke.sh (main menu / enter game / screen dismissal ×N + player path + real sound), written to the same ledger (result column prefixed "Mac:", device in the last column). Source of the method:
# SMOKE_MAC_PKG_ONLY=1: only build, wrap, and (when remote) transfer to smoke_mac_in/<directory name>; no lock taken, no run, no smoke-test ledger entry, last line 「SMOKE-MAC 只出包 <路径>」(SMOKE-MAC package only <path>).
#   For building regression packages (after a regression package is built, no separate smoke test is run to occupy the second Mac's lock; the first run of that pass itself verifies package launch through entering the game; the merge smoke-test gate is still managed by merge.sh).
# Usage: smoke_mac.sh <count>. SMOKE_MAC_HOST=local (default) | <ssh target, e.g. user@host>: the remote only runs, does not build -- build and wrap locally, rsync it over, and pull the results back to the local machine for the ledger.
# What it can stand in for the real device on: the iOS shell (main.m / front_ios.m / UIKit / resource location / lifecycle) launches, gets into the game, is not killed by the watchdog, really produces sound. What it cannot: thermal state, real touch points, A19 GPU time, installation.
set -u   # 变量后紧跟中文标点一律写 ${名}：无 UTF-8 区域（ssh 非交互）时 bash 会把标点字节并进变量名（mini「$segs；」报未绑定） | Whenever a variable is immediately followed by Chinese punctuation always write ${name}: without a UTF-8 locale (non-interactive ssh) bash merges the punctuation bytes into the variable name (on the second Mac 「$segs；」 was reported as unbound)
N=${1:-1}
# 真机档位（缺省开）：Mac 上的窗口不是手机的比例和安全区，布局缩放 / 控件内缩 / 命中框和真机不是一套数，「mini 过、真机不同」多次出在这里。
# 缺省带上真机的画面分辨率与左右安全区（iPhone 17 Pro Max：2868x1320、左右各 62 点），前端按这块画面矩形摆控件、换算触点。DM_MAC_DEVICE_PROFILE=0 回到按 Mac 窗口原样跑
# Real device profile (on by default): a window on Mac does not have the phone's aspect ratio and safe area; layout scale / widget inset / hit boxes are not the same set of numbers as on a real device, and "passes on the second Mac, differs on real device" has come from here many times.
# By default it carries the real device's picture resolution and left/right safe areas (iPhone 17 Pro Max: 2868x1320, 62 points on each side), and the front end places widgets and converts touch points by this picture rectangle. DM_MAC_DEVICE_PROFILE=0 goes back to running as-is by the Mac window
DEVP=""; [ "${DM_MAC_DEVICE_PROFILE:-1}" != 0 ] && DEVP="--env DM_GAME_RES=2868x1320 --env DM_TEST_SAFE_L=62 --env DM_TEST_SAFE_R=62"
# 在有人用着的这台 Mac 上直接跑（不是经 ssh 派到远端那一段）时，出图不走显示节拍：别的窗口把游戏窗口完全盖住，系统就不给它发显示节拍，
# 冒烟会红在「显示节拍激活中连续 2.0 秒没回调」（一天里撞了多次，红的是桌面不是改动）。DM_PRESENT_LINK=0 = 自己取 drawable，窗口被盖也照出帧。
# 远端（无人桌面的 mini）和真机冒烟仍走缺省的显示节拍那条路，那条路由它们守。要在本机也走显示节拍：SMOKE_MAC_PRESENT_LINK=1。
# When run directly on this Mac while someone is using it (not the part dispatched to the remote machine over ssh), presentation does not use the
# display link: when other windows completely cover the game window, the system stops sending it display-link ticks,
# and the smoke test fails with "no callback for 2.0 consecutive seconds while the display link is active" (hit several times in one day; the failure came
# from the desktop state, not from the change under test). DM_PRESENT_LINK=0 = acquire the drawable ourselves, so frames are still presented with the window covered.
# The remote machine (the second Mac with an unattended desktop) and real-device smoke tests still take the default display-link path, and that path is
# guarded by them. To use the display link on this machine too: SMOKE_MAC_PRESENT_LINK=1.
[ -z "${SMOKE_MAC_STAGE:-}" ] && [ "${SMOKE_MAC_PRESENT_LINK:-0}" = 0 ] && DEVP="$DEVP --env DM_PRESENT_LINK=0"
# 本脚本运行时要用到的同目录文件，只列这一处：起跑前逐个核在不在（缺了当场红，不跑到一半才报），派到远端时也按这份清单同步。
# 新加一个 source / 调用的同目录文件，就加进这里—— 漏了后加的三份，远端一起就报「DM_BUNDLE_ID 未设」，在台账留了假红。
# The same-directory files this script needs at run time, listed in this one place only: before starting, each is checked to be present (a missing one is a failure on the spot, not reported halfway through the run), and when dispatching to the remote machine the sync also goes by this list.
# When a new same-directory file is sourced / called, add it here -- the three added later were missed, the remote side reported 「DM_BUNDLE_ID 未设」(DM_BUNDLE_ID not set) as soon as it started, and a false failure was left in the ledger.
SMOKE_MAC_DEPS="local_env.sh logport.py devlock.sh devbattery.sh cloud_guard.sh save_key.sh launch_guard.sh smoke_touch.txt uireg"
_sm_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd); _sm_miss=
for _sm_f in $SMOKE_MAC_DEPS; do [ -e "$_sm_dir/$_sm_f" ] || _sm_miss="$_sm_miss $_sm_f"; done
[ -z "$_sm_miss" ] || { echo "SMOKE-MAC RED 0/${1:-1} 用时 0 秒"; echo "【红】冒烟脚本的依赖文件缺了：${_sm_miss}（目录 ${_sm_dir}），没有起跑"; exit 2; }
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/local_env.sh"   # 本机配置（包名、团队号、设备号） | Local config (package name, team ID, device ID)
B=${DM_BUNDLE_ID:?先在本机配置里设 DM_BUNDLE_ID（App 包名，见 dm/ios/local.env.example）}
TOOLS=$(cd "$(dirname "$0")" && pwd)
KEYS_DEFAULT='X@OpenPanel:MainMenuButtonRibbon>OpenPanel:CharacterSelectPanel,C0.84:0.208@OpenPanel:CharacterSelectPanel%1>UpdateCharacterInfo,MSG:CharacterSelect:LoadCharacter:0@UpdateCharacterInfo%0.3>FrontEndNavigation:ToGame'
KEYS=${SMOKE_KEYS:-$KEYS_DEFAULT}
case "$N" in ''|*[!0-9]*|0) echo "SMOKE-MAC RED 0/$N 用时 0 秒"; echo "【红】次数必须是正整数"; exit 2;; esac
main_menu_timeout=${SMOKE_MAIN_MENU_TIMEOUT:-40}; game_screen_timeout=${SMOKE_GAME_SCREEN_TIMEOUT:-30}

# ---------- 跑（在放包的那台 Mac 上执行；本机直接调，远端经 ssh 以 SMOKE_MAC_STAGE=run 调）----------
# 入参：W = 包好的 <名>.app，RUN = 结果目录。标准输出逐行同 smoke.sh；末行 SMOKE-MAC OK|RED；另写 $RUN/segs.txt 给外层记账
# ---------- Run (executed on the Mac holding the package; called directly on the local machine, called over ssh with SMOKE_MAC_STAGE=run on the remote) ----------
# Arguments: W = the wrapped <name>.app, RUN = result directory. Stdout line by line same as smoke.sh; last line SMOKE-MAC OK|RED; additionally writes $RUN/segs.txt for the outer layer's ledger
run_stage() {
  local W=$1 RUN=$2 ok=0 known_pk=0 segs="" T0; T0=$(date +%s)
  exe="$W/Wrapper/DmIOS.app/DmIOS"; mkdir -p "$RUN"
  [ -x "$exe" ] || { echo "【红】包不完整：$exe"; echo "SMOKE-MAC RED 0/$N 用时 0 秒"; return 2; }
  # 同一台 Mac 上所有 Mac 冒烟共用一个 App 容器（同一 bundle id：boot.txt、存档会互相覆盖），串行：借设备锁的实现、换一把锁
  # All Mac smoke tests on the same Mac share one App container (same bundle id: boot.txt and saves would overwrite each other), so they are serialized: borrow the device lock's implementation, with a different lock
  export DM_DEVLOCK_DIR=${DM_ROOT:-$HOME/dm-work}/run/ios/macsmoke.lock; source "$TOOLS/devlock.sh"; source "$TOOLS/cloud_guard.sh"; source "$TOOLS/save_key.sh"; source "$TOOLS/launch_guard.sh"
  devlock_acquire "${SMOKE_OWNER:-未署名} Mac 冒烟"
  # 找实例：系统有时把包迁到随机路径下运行（App Translocation，/private/var/folders/…/d/Wrapper/DmIOS.app/DmIOS），不能按原路径认，按可执行文件后缀认。
  # iOS 包在 Mac 上同一 bundle id 只能有一个实例（再 open 报 -10670）：开跑前已有实例 = 别人的或上次残留的，不替人结束，报出 pid 即红
  # open 报错时把现场留下（一次合入红：玩家路径两次 open 都报 No such file or directory，90 秒后触摸玩家路径也起不来，下一次合入又好了；当时本机另有两个任务在出冒烟包，故意重叠复现 1 次没复现，因果未定）。
  # 只取证不改行为：包路径各层在不在、软链指向、挂载表里的 Wrapper、同机上别的构建 / 实例、锁的持有者。下次再撞到，读 $RUN/open_fail_diag.txt
  # Finding instances: the system sometimes relocates the package to run under a random path (App Translocation, /private/var/folders/.../d/Wrapper/DmIOS.app/DmIOS), so it cannot be matched by the original path; match by executable suffix.
  # An iOS app on Mac can have only one instance per bundle id (another open reports -10670): an instance already present before starting = someone else's or a leftover from last time; do not end it for them, report the pid and fail
  # When open fails, keep the evidence (one failed merge: both player path opens reported No such file or directory, 90 seconds later the touch player path could not launch either, and the next merge was fine again; at the time two other tasks on this machine were building smoke-test packages; one deliberate overlap attempt did not reproduce it, causality undetermined).
  # Evidence only, no behavior change: whether each level of the package path exists, where the symlinks point, the Wrapper in the mount table, other builds / instances on the same machine, the lock holder. Next time it is hit, read $RUN/open_fail_diag.txt
  open_diag() { { echo "== $(date '+%Y-%m-%d %H:%M:%S') $1"; ls -ld "$W" "$W/Wrapper" "$W/Wrapper/DmIOS.app" "$W/WrappedBundle" "$exe" 2>&1; echo "软链 → $(readlink "$W/WrappedBundle" 2>&1)"; echo "-- 挂载表里的 Wrapper"; mount | grep -F Wrapper;
      echo "-- 同机的构建 / 实例"; ps -axo pid=,lstart=,command= | grep -E "xcodebuild|/Wrapper/DmIOS\.app/DmIOS|smoke_mac\.sh|touch_e2e" | grep -v grep | cut -c1-260; echo "-- 锁持有者 $(cat "$DM_DEVLOCK_DIR/owner" 2>/dev/null)"; } >> "$RUN/open_fail_diag.txt" 2>&1; }
  # 只认本包（bundle id = $B）：同机还可能跑着回归多实例的重签包 <包名>.reg<N>（各自独立容器），按可执行文件后缀认会把它们当成本包、收尾时一起杀掉。
  # 查不到 bundle id 的（刚起、系统还没登记）照旧算本包，只排除明确是别的 bundle 的
  # Only this package is recognized (bundle id = $B): the same machine may also be running the regression multi-instance re-signed packages <package name>.reg<N> (each with its own container); matching by executable suffix would take them as this package and kill them along at wrap-up.
  # Those whose bundle id cannot be found (just launched, not yet registered by the system) still count as this package; only those clearly belonging to another bundle are excluded
  pids() { local p b; for p in $(ps -axo pid=,comm= | awk '{ pid=$1; $1=""; sub(/^ /,""); if ($0 ~ /\/Wrapper\/DmIOS\.app\/DmIOS$/) print pid }'); do
      b=$(lsappinfo info -only bundleID -app "$p" 2>/dev/null | sed -n 's/.*="\(.*\)"$/\1/p'); [ -z "$b" ] || [ "$b" = "$B" ] && echo "$p"; done; }
  stop_all() { local p; for p in $(pids); do kill -TERM "$p" 2>/dev/null; done; sleep 1; for p in $(pids); do kill -KILL "$p" 2>/dev/null; done; }
  SK_C=''; CLEAN='stop_all; [ -z "$SK_C" ] || savekey_out "$SK_C"; devlock_release'   # 退出钩子由调用处统一挂（外层还要记账），这里只登记要做的清理 | The exit hook is installed centrally by the call site (the outer layer also has to record in the ledger); here only the cleanup to be done is registered
  container() { local m; for m in "$HOME"/Library/Containers/*/.com.apple.containermanagerd.metadata.plist; do
      [ "$(/usr/libexec/PlistBuddy -c 'Print :MCMMetadataIdentifier' "$m" 2>/dev/null)" = "$B" ] && { echo "$(dirname "$m")/Data"; return 0; }; done; return 1; }
  freeport() { python3 -c "import socket;s=socket.socket();s.bind(('127.0.0.1',0));print(s.getsockname()[1])"; }
  # 资源：容器的 Library/Application Support/romfs/data（与真机开发装机同一位置）。容器由系统在首次启动时建；缺资源就从 SMOKE_MAC_ROMFS 克隆（APFS 克隆不占空间）
  # iCloud 总闸（tools/cloud_guard.sh）：这个脚本起的每一个实例都把云端指到一次性本地目录，不连真实 iCloud
  # Assets: the container's Library/Application Support/romfs/data (same location as a development install on a real device). The container is created by the system on first launch; missing assets are cloned from SMOKE_MAC_ROMFS (APFS clones take no space)
  # iCloud master gate (tools/cloud_guard.sh): every instance launched by this script points the cloud at a one-off local directory and does not connect to real iCloud
  cloud_guard_env "" "" || return 1; local CGA=${CG_ENV:+--env $CG_ENV}
  local C; C=$(container) || { open -n "$W" $CGA --args -mode boot "${LG_ARGS[@]}"; for _ in $(seq 1 30); do C=$(container) && break; sleep 0.5; done; stop_all; }
  [ -n "${C:-}" ] || { echo "【红】系统没有给 $B 建容器（首次启动失败？包未签进本机？）"; echo "SMOKE-MAC RED 0/$N 用时 $(( $(date +%s) - T0 )) 秒"; return 2; }
  # 存档钥匙（tools/save_key.sh）：整轮冒烟用自己名下的一份测试存档（缺省钥匙 smoke），不动别的线留在公共位置的那份
  # Save key (tools/save_key.sh): the whole smoke round uses a test save under its own name (default key smoke) and does not touch the copy other developers leave in the common location
  SK_C=$C; DM_SAVE_KEY=${DM_SAVE_KEY:-smoke} savekey_in "$C" "" || return 1
  launch_guard_pre "$C"   # 起包不被系统「恢复窗口」弹框挡住（tools/launch_guard.sh） | Keep the launch from being blocked by the system's "restore windows" dialog (tools/launch_guard.sh)
  local rom="$C/Library/Application Support/romfs" src=${SMOKE_MAC_ROMFS:-${DM_ROOT:-$HOME/dm-work}/build/romfs_zhCN/data}
  if [ ! -f "$rom/data/.build.config" ]; then
    [ -f "$src/.build.config" ] || { echo "【红】容器里没有资源，$src 也没有（SMOKE_MAC_ROMFS 指到 romfs 的 data 目录）"; echo "SMOKE-MAC RED 0/$N 用时 0 秒"; return 2; }
    mkdir -p "$rom" && rm -rf "$rom/data" && cp -cR "$src" "$rom/data" || { echo "【红】克隆资源失败"; return 2; }
    echo "资源：已从 $src 克隆进容器"
  fi
  # 被测机器数据盘余量 < 5 GB 不跑（mini 两次红时数据盘只剩 140–476 MB：仪器原始录制没清；清到 27 GB 后的测量恢复正常。与两次红的因果未做对照）
  # Do not run when the machine under test's data disk has < 5 GB free (on the two failures on the second Mac the data disk had only 140–476 MB left: raw Instruments recordings had not been cleaned; measurements returned to normal after cleaning to 27 GB. The causal link to the two failures was not tested with a control)
  local free_gb; free_gb=$(df -g "$HOME" | awk 'NR==2 {print $4}')
  [ "${free_gb:-0}" -ge 5 ] || { echo "【红】被测机器 $(hostname -s) 数据盘只剩 ${free_gb} GB（< 5 GB），不跑"; echo "SMOKE-MAC RED 0/$N 用时 0 秒"; devlock_release; CLEAN=''; return 2; }
  # 屏幕睡着时取不到可绘制表面，游戏主线程卡在取图（第 1 局进局后停在拍 4、15 秒看门狗；回溯在 NVN 取图调用里，后两局正常）。跑之前唤醒屏幕并在本次运行期间保持不睡
  # When the screen is asleep no drawable surface can be obtained and the game main thread stalls acquiring the image (run 1 stopped at tick 4 after entering the game, 15-second watchdog; the backtrace was inside the NVN acquire-image call, the next two runs were normal). Wake the screen before running and keep it awake for the duration of this invocation
  caffeinate -u -t 3 >/dev/null 2>&1; caffeinate -d -w $$ >/dev/null 2>&1 &
  local pre; pre=$(pids | tr '\n' ' ')
  [ -z "$pre" ] || { echo "【红】本机已有 $B 的实例在跑（pid ${pre}，非本次启动）：先结束它再跑"; echo "SMOKE-MAC RED 0/$N 用时 0 秒"; devlock_release; CLEAN=''; return 2; }
  for i in $(seq 1 "$N"); do
    local OUT="$RUN/$i" attempt=0 result=1 stage=启动 START port tA seg prev d w nm tt nv kv chan; mkdir -p "$OUT"; START=$(date +%s)
    local pk=0 tL="" tM="" tG="" tS="" exited=1
    while [ "$attempt" -lt 3 ]; do
      attempt=$((attempt + 1)); port=$(freeport); tA=$(date +%s)
      open -n "$W" $DEVP $CGA --env DM_SAVE_FRESH=warlock --env DM_WATCH=$((main_menu_timeout + game_screen_timeout + 5)) --env DM_UIMSG=1 --env DM_LOG_PORT="$port" --env "DM_KEYS=$KEYS" \
        --args -mode boot "${LG_ARGS[@]}" > "$OUT/launch_${attempt}.txt" 2>&1; local lrc=$?
      [ "$lrc" -ne 0 ] && [ -z "$(pids)" ] && { sleep 2; echo "第 $i 次：open 失败（$(tail -1 "$OUT/launch_${attempt}.txt" | cut -c1-80)），重试一次"; open -n "$W" $DEVP $CGA --env DM_SAVE_FRESH=warlock --env DM_WATCH=$((main_menu_timeout + game_screen_timeout + 5)) --env DM_UIMSG=1 --env DM_LOG_PORT="$port" --env "DM_KEYS=$KEYS" --args -mode boot "${LG_ARGS[@]}" >> "$OUT/launch_${attempt}.txt" 2>&1; lrc=$?; tA=$(date +%s); }
      eval "$(python3 "$TOOLS/logport.py" read "$port" "$OUT/console_${attempt}.txt" "$main_menu_timeout" "$game_screen_timeout")"
      cat "$OUT/console_${attempt}.txt" >> "$RUN/console_${i}.txt"
      chan=""; [ "$lrc" -ne 0 ] && chan="（open 退出码 ${lrc}）"
      [ "$result" -ne 0 ] && [ -z "$tL" ] && [ "$lrc" -eq 0 ] && chan="（日志端口零行：App 没起来或未监听）"
      [ "$exited" = 1 ] || { chan="${chan}（quit 后实例未退）"; stop_all; }
      [ "$result" -eq 0 ] && break
      [ "$pk" = 1 ] && [ "$attempt" -lt 3 ] && { known_pk=1; echo "第 $i/$N 次出现已知PK，补跑 $attempt/2"; continue; }
      break
    done
    seg=""; prev=$tA; for kv in "启动:$tL:3" "主菜单:$tM:10" "进局:$tG:5" "撤屏:$tS:8"; do IFS=: read -r nm tt nv <<<"$kv"; [ -n "$tt" ] || break; d=$((tt - prev)); w=""; [ $d -gt $((nv * 2)) ] && w="⚠️慢"; seg="$seg $nm ${d}s$w"; prev=$tt; done
    if [ "$result" -eq 0 ]; then ok=$((ok + 1)); segs="${segs:+${segs}；}$i:OK$seg"; echo "第 $i/$N 次 OK：用时 $(( $(date +%s) - START )) 秒；分段$seg"
    else segs="${segs:+${segs}；}$i:RED@$stage$seg"; echo "第 $i/$N 次 RED：卡在：${stage}；分段$seg${chan}"; fi
  done
  # 玩家路径：不带 -mode、不带看门狗与存档变量（= 玩家点图标），进局 35 s 后开暂停菜单再静置 20 s；须真出声
  # Player path: no -mode, no watchdog or save variables (= the player tapping the icon), 35 s after entering the game open the pause menu then idle 20 s; must really produce sound
  local PLAYER_RES=跳过
  if [ -z "${SMOKE_KEYS:-}" ] && [ "${SMOKE_PLAYER:-1}" = 1 ]; then
    stop_all; local port pr au_real au_null hud_ok red_n; port=$(freeport)
    local KP="$KEYS_DEFAULT,PLUS@UnloadPanel:LoadScreenPanel%35>OpenPanel:PlayerInventoryExpansionLayout,R@OpenPanel:PlayerInventoryExpansionLayout%1>OpenPanel:SkillsTreePanel,R@OpenPanel:SkillsTreePanel%1>OpenPanel:PauseLayout"
    # 有一次合入在 mini 上这一步报「Launch failed … Launchd job spawn failed（POSIX 162）」、日志 0 行，原样重跑即过；原因没查到（系统日志读不到），
    # 当时台账里 mini 上没有别的 Mac 冒烟在跑（锁落在被测机器上：run_stage 在哪台跑就锁哪台的 macsmoke.lock）。本机没有实例在跑时等 2 秒重试一次，两次的报错都留在 player_launch.txt
    # Once during a merge this step on the second Mac reported "Launch failed ... Launchd job spawn failed (POSIX 162)" with 0 log lines, and passed when rerun unchanged; the cause was not found (the system log could not be read),
    # at the time the ledger showed no other Mac smoke test running on the second Mac (the lock lives on the machine under test: whichever machine run_stage runs on, that machine's macsmoke.lock is taken). When no instance is running on this machine, wait 2 seconds and retry once; both error messages are kept in player_launch.txt
    open -n "$W" $DEVP $CGA --env DM_SAVE_TEST=1 --env DM_LOG_PORT="$port" --env DM_UIMSG=1 --env "DM_KEYS=$KP" > "$RUN/player_launch.txt" 2>&1 \
      || { [ -z "$(pids)" ] && { sleep 2; echo "玩家路径：open 失败（$(tail -1 "$RUN/player_launch.txt" | cut -c1-80)），重试一次"; open_diag 玩家路径; open -n "$W" $DEVP $CGA --env DM_SAVE_TEST=1 --env DM_LOG_PORT="$port" --env DM_UIMSG=1 --env "DM_KEYS=$KP" >> "$RUN/player_launch.txt" 2>&1; }; }
    pr=$(python3 "$TOOLS/logport.py" player "$port" "$RUN/player.txt" "$main_menu_timeout" 35 20)
    au_real=$(grep -c 'audio::OpenAudioOut .*真出声' "$RUN/player.txt" 2>/dev/null); au_null=$(grep -c 'audio::OpenAudioOut .*静音接收端' "$RUN/player.txt" 2>/dev/null)
    # 产品缺省必须读得到受控玩家（UIKit 底栏数字与难度条靠它；曾经拍入口只在带测试变量时才挂，玩家路径下一直没有）
    # Product defaults must be able to read the controlled player (the UIKit bottom bar numbers and difficulty bar rely on it; the tick entry used to be hooked only when test variables were set, so it was always missing on the player path)
    hud_ok=$(LC_ALL=C grep -a 'dm_hud：' "$RUN/player.txt" 2>/dev/null | LC_ALL=C grep -a -v -c '未读到玩家')
    # 玩家路径日志不许有红行（产品缺省路径上的常驻红行 = 没人看的告警；曾经每次启动有 14 条面板栈假红）
    # The player path log must have no failure lines (a permanent failure line on the product default path = an alarm nobody looks at; there used to be 14 false panel-stack failures at every launch)
    red_n=$(LC_ALL=C grep -a -c '【红】' "$RUN/player.txt" 2>/dev/null)
    if [[ "$pr" == result=0* ]] && [ "${au_real:-0}" -gt 0 ] && [ "${au_null:-0}" = 0 ] && [ "${hud_ok:-0}" -gt 0 ] && [ "${red_n:-0}" = 0 ]; then PLAYER_RES=OK; echo "玩家路径 OK：进局 35 s + 暂停菜单 20 s 仍活着；AudioOut 真出声 ${au_real} 路（${pr}）"
    else PLAYER_RES=RED; echo "玩家路径 RED：${pr}；AudioOut 真出声 ${au_real:-0} 路 / 静音接收端 ${au_null:-0} 路（须真出声 > 0、静音 0）；读到玩家状态 ${hud_ok:-0} 行（须 > 0）；红行 ${red_n:-0} 条（须 0）：$(LC_ALL=C grep -a -m1 '【红】' "$RUN/player.txt" 2>/dev/null | cut -c1-80)"; fi
    segs="${segs:+${segs}；}玩家路径:$PLAYER_RES"
  fi
  # 触摸玩家路径：从空存档起，只用触点——点职业、创建、选模式、起名 → 进局；不碰摇杆点三下空地坐标不变；点脚下的小站开小站面板（进局落在小站上，营地 NPC 都不在画面里；按类号 tap_unit 落真实触点，走世界点按 → 交互同一条路）。
  # 固定种子与时间（NPC 站位要可重复，触点脚本是盲点）；其余是产品缺省。排在玩家路径之后：它把 save.test 清空了
  # Touch player path: starting from an empty save, using touch points only -- tap the class, create, choose mode, name → enter the game; without touching the stick, tap empty ground three times and the coordinates stay unchanged; tap the waypoint underfoot to open the waypoint panel (the game is entered standing on the waypoint, with none of the camp NPCs in view; tap_unit by class number lands a real touch point, going through the same world tap → interact path).
  # Fixed seed and time (NPC positions must be repeatable, the touch point script taps blind); the rest are product defaults. Placed after the player path: it empties save.test
  local TOUCH_RES=跳过
  if [ -z "${SMOKE_KEYS:-}" ] && [ "${SMOKE_TOUCH:-1}" = 1 ]; then
    stop_all; local tport tr p0 p1 nt dlg; tport=$(freeport)
    cp "${SMOKE_TOUCH_SCRIPT:-$TOOLS/smoke_touch.txt}" "$C/Documents/smoke_touch.txt"   # SMOKE_TOUCH_SCRIPT：换一份脚本（验判据会不会红用） | SMOKE_TOUCH_SCRIPT: substitute another script (for verifying whether the criterion will fail)
    # smoke_touch.txt 的裸坐标是按 667x375 的 16:9 画面写的：带 DM_TOUCH_REF=667x375 由前端按游戏画面矩形换算（界面按画面高缩放、水平居中），
    # 所以真机档位（画面 2.17:1、上下留边）与原样档位（DM_MAC_DEVICE_PROFILE=0，换算为恒等）用同一份脚本
    # The bare coordinates in smoke_touch.txt are written for a 667x375 16:9 picture: with DM_TOUCH_REF=667x375 the front end converts by the game picture rectangle (UI scaled by picture height, horizontally centered),
    # so the real device profile (picture 2.17:1, margins top and bottom) and the as-is profile (DM_MAC_DEVICE_PROFILE=0, conversion is the identity) use the same script
    open -n "$W" $DEVP $CGA --env DM_TOUCH_REF=667x375 --env DM_SAVE_FRESH=empty --env DM_LOG_PORT="$tport" --env DM_UIMSG=1 --env DM_TOUCH_SCRIPT=smoke_touch.txt --env DM_TOUCH_SCRIPT_START=menu \
      --env DM_GAME_SEED=0x12345678 --env DM_FIXED_TIME=1790000000 > "$RUN/touch_launch.txt" 2>&1 || open_diag 触摸玩家路径
    tr=$(python3 "$TOOLS/logport.py" until "$tport" "$RUN/touch.txt" $((main_menu_timeout + 50)) '[script] 全部')
    p0=$(LC_ALL=C grep -a '\[script\] pos ' "$RUN/touch.txt" | sed -n 1p | sed 's/.*pos \(.*)\).*/\1/'); p1=$(LC_ALL=C grep -a '\[script\] pos ' "$RUN/touch.txt" | sed -n 2p | sed 's/.*pos \(.*)\).*/\1/')
    nt=$(LC_ALL=C grep -a -c '世界目标.*没有目标' "$RUN/touch.txt"); dlg=$(LC_ALL=C grep -a -c 'OpenPanel WaypointsPanel' "$RUN/touch.txt")
    if [[ "$tr" == result=0* ]] && [ -n "$p0" ] && [ "$p0" = "$p1" ] && [ "${nt:-0}" -ge 3 ] && [ "${dlg:-0}" -gt 0 ]; then TOUCH_RES=OK; echo "触摸玩家路径 OK：空存档建角进局；点空地 ${nt} 下坐标不变（${p0}）；点小站开小站面板（${tr}）"
    else TOUCH_RES=RED; echo "触摸玩家路径 RED：${tr}；点空地前后坐标「${p0}」/「${p1}」（须相同且非空）；没有目标 ${nt:-0} 下（须 ≥3）；开小站面板 ${dlg:-0} 次（须 >0）；日志 $RUN/touch.txt"; fi
    segs="${segs:+${segs}；}触摸路径:$TOUCH_RES"
  fi
  local left; left=$(pids | tr '\n' ' ')
  [ -n "$left" ] && { echo "【红】结束后仍有本包 DmIOS pid：$left"; cp "$C/Documents/boot.txt" "$RUN/boot_left.txt" 2>/dev/null; stop_all; }
  stop_all; local cg_ok=1; cat "$RUN"/*.txt "$RUN"/*/*.txt "$C/Documents/boot.txt" 2>/dev/null > "$RUN/.cg_all" ; cloud_guard_check "$RUN/.cg_all" || cg_ok=0; rm -f "$RUN/.cg_all"; cloud_guard_clean "$C/Documents"
  savekey_out "$C"; SK_C=''; devlock_release; CLEAN=''
  printf '%s\n' "$segs" > "$RUN/segs.txt"; printf '%s\n' "$ok" > "$RUN/ok.txt"
  [ "$known_pk" -eq 1 ] && echo "【已知PK】占不到 pthread 键 507：已按规则补跑"
  if [ "$ok" -eq "$N" ] && [ -z "$left" ] && [ "$PLAYER_RES" != RED ] && [ "$TOUCH_RES" != RED ] && [ "$cg_ok" = 1 ]; then echo "SMOKE-MAC OK $ok/$N 用时 $(( $(date +%s) - T0 )) 秒"; return 0; fi
  # 红了就把被测机器当时的负载和占 CPU 前几名留下（mini 上两次红时有 xctrace 700% 和成批宿主在跑，事后才查到）
  # On failure, save the load of the machine under test at that moment and its top CPU consumers (on the two failures on the second Mac, xctrace at 700% and a batch of hosts were running, only found afterwards)
  echo "被测机器 $(hostname -s) 负载 $(sysctl -n vm.loadavg)；占 CPU 前 5：$(ps -axo pcpu=,comm= | sort -rn | head -5 | awk '{ n=split($2,a,"/"); printf "%s%% %s；", $1, a[n] }')"
  echo "SMOKE-MAC RED $ok/$N 用时 $(( $(date +%s) - T0 )) 秒"; return 1
}
# ---------- 界面固定回归 b 局（uireg/，在放包的那台 Mac 上执行；运行约 85 秒：进局 → 走到 NPC 旁 → 逐项点界面，判据只读日志）----------
# 入参同 run_stage。结果一行写进 $RUN/uireg_res.txt：OK(通N 未判M) 或 RED(通N 不通M 红K)；判据 = 有判据的项全通（通 > 0、不通 = 0）且日志【红】0 行，「未判」不计
# ---------- Fixed UI regression run b (uireg/, executed on the Mac holding the package; runs about 85 seconds: enter the game → walk up to the NPC → tap through the UI item by item, criteria read only the log) ----------
# Arguments same as run_stage. One result line is written to $RUN/uireg_res.txt: OK(通N 未判M) or RED(通N 不通M 红K) (通 = pass, 不通 = fail, 未判 = unjudged, 红 = red); criterion = all items that have a criterion pass (pass > 0, fail = 0) and 0 lines of 【红】(red) in the log; 「未判」(unjudged) is not counted
uireg_stage() {
  local W=$1 RUN=$2 ul un ur res; mkdir -p "$RUN"
  ul=$(PYTHONUTF8=1 UIREG_APP="$W" UIREG_OUT="$RUN/uireg" UIREG_RUNNER= python3 "$TOOLS/uireg/uireg.py" iphone b 2>&1 | tee "$RUN/uireg.txt" | LC_ALL=C grep -a '^# 共 ' | tail -1)
  un=$(printf '%s' "$ul" | sed -n 's/.*通 \([0-9]*\)、不通 \([0-9]*\)、未判 \([0-9]*\).*/\1 \2 \3/p'); ur=$(LC_ALL=C sed -n 's/.*日志【红】\([0-9]*\) 行.*/\1/p' "$RUN/uireg.txt" | tail -1)
  set -- $un
  if [ "${1:-0}" -gt 0 ] && [ "${2:-1}" = 0 ] && [ "${ur:-1}" = 0 ]; then res="OK(通$1 未判$3)"; else res="RED(通${1:-?} 不通${2:-?} 红${ur:-?})"; fi
  printf '%s\n' "$res" > "$RUN/uireg_res.txt"; echo "界面回归 b 局 ${res}：${ul:-没有出结果行}"
}
CLEAN=''
# 清一小时以前的 b 局包副本（<目录>/uireg_*）；挂载表里还有它的路径就跳过留到下次——包退出后 Wrapper 的挂载不一定立刻撤，删掉还挂着的源目录会留下卸不掉的挂载条目
# Clean run-b package copies older than one hour (<dir>/uireg_*); if the mount table still has its path, skip it and leave it for next time -- the Wrapper mount is not necessarily removed right after the package exits, and deleting a source directory that is still mounted leaves a mount entry that cannot be unmounted
if [ "${SMOKE_MAC_STAGE:-}" = prune ]; then
  find "${SMOKE_MAC_WRAP:?}" -maxdepth 1 -name 'uireg_*' -mmin +60 2>/dev/null | while IFS= read -r d; do mount | LC_ALL=C grep -qF "$d/" || rm -rf "$d"; done; exit 0; fi
if [ "${SMOKE_MAC_STAGE:-}" = uireg ]; then uireg_stage "${SMOKE_MAC_WRAP:?}" "${SMOKE_MAC_RUN:?}"; exit 0; fi
if [ "${SMOKE_MAC_STAGE:-}" = run ]; then trap 'eval "$CLEAN"' EXIT; trap 'exit 143' INT TERM; run_stage "${SMOKE_MAC_WRAP:?}" "${SMOKE_MAC_RUN:?}"; exit $?; fi

# ---------- 外层：构建 → 包 Wrapper →（本机 | 远端）跑 → 记账 ----------
# ---------- Outer layer: build → wrap in Wrapper → run (local | remote) → record in ledger ----------
HOST=${SMOKE_MAC_HOST:-local}
TREE=$(git rev-parse --show-toplevel 2>/dev/null || pwd); DD=$TREE/.iosdd_mac   # 与真机冒烟的 .iosdd 分开：两种目标的中间产物互不覆盖 | Separate from the real device smoke test's .iosdd: intermediate outputs of the two targets do not overwrite each other
RUN="${DM_ROOT:-$HOME/dm-work}/run/ios/smoke_mac/$(date +%Y%m%d_%H%M%S)_$$"; mkdir -p "$RUN"
LEDGER=${SMOKE_LEDGER:-${DM_ROOT:-$HOME/dm-work}/run/ios/smoke_ledger.tsv}; LEDGER_RES=中断; ok=0; T0=$(date +%s)
ledger_write() {   # 与 smoke.sh 同表；结果列带「Mac:」前缀（merge.sh 现在只认无前缀的真机 OK），末列 = 设备 | Same table as smoke.sh; the result column carries the "Mac:" prefix (merge.sh now only accepts unprefixed real device OK), last column = device
  local head dirty dev; head=$(git -C "$TREE" rev-parse HEAD 2>/dev/null || echo 无); dirty=$(git -C "$TREE" status --porcelain --untracked-files=no 2>/dev/null | wc -l | tr -d ' ')
  dev="Mac:$([ "$HOST" = local ] && hostname -s || echo "$HOST")"
  [ -s "$LEDGER" ] || printf '时刻\t调用方\t被测树\tHEAD\t未提交改动\t结果\t成功/次数\t用时秒\t各次分段\t结果目录\n' >> "$LEDGER"
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s/%s\t%s\t%s\t%s\t%s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "${SMOKE_OWNER:-未署名}" "$TREE" "$head" "$dirty" \
    "${SMOKE_KEYS:+反例:}Mac:$LEDGER_RES" "$ok" "$N" "$(( $(date +%s) - T0 ))" "$(cat "$RUN/segs.txt" 2>/dev/null || echo 无)" "$RUN" "$dev" >> "$LEDGER"
}
trap 'eval "$CLEAN"; ledger_write' EXIT; trap 'exit 143' INT TERM
echo "SMOKE-MAC 开始 $N 次（${HOST}），结果目录 $RUN"
UIREG_REDMARK=${DM_ROOT:-$HOME/dm-work}/run/ios/uireg_b_last_red.txt; [ -s "$UIREG_REDMARK" ] && echo "上次界面回归 b 局红（只报不拦）：$(cat "$UIREG_REDMARK")"
( cd "$TREE/dm/ios" && DM_GEN_ONLY=1 DM_IOS_DD="$DD" bash run.sh build && source tools/asc_env.sh && asc_auth &&
  xcodebuild -project DmIOS.xcodeproj -scheme DmIOS -configuration Release -destination 'platform=macOS,arch=arm64,variant=Designed for iPad' \
    -derivedDataPath "$DD" -allowProvisioningUpdates ${ASC_AUTH[@]+"${ASC_AUTH[@]}"} build ) > "$RUN/build.log" 2>&1 \
  || { LEDGER_RES=构建失败; echo "【红】构建失败：$RUN/build.log"; grep -E "error:|🔴" "$RUN/build.log" | head -5; echo "SMOKE-MAC RED 0/$N 用时 $(( $(date +%s) - T0 )) 秒"; exit 2; }
# 直接 open 构建产物会报 incorrect executable format：要包成 <名>.app/Wrapper/DmIOS.app + WrappedBundle 软链
# Directly opening the build output reports incorrect executable format: it must be wrapped as <name>.app/Wrapper/DmIOS.app + a WrappedBundle symlink
W="$DD/DmMacSmoke.app"; rm -rf "$W"; mkdir -p "$W/Wrapper" && cp -cR "$DD/Build/Products/Release-iphoneos/DmIOS.app" "$W/Wrapper/" && ln -s Wrapper/DmIOS.app "$W/WrappedBundle" \
  || { LEDGER_RES=构建失败; echo "【红】包 Wrapper 失败"; exit 2; }
if [ "${SMOKE_MAC_PKG_ONLY:-0}" = 1 ] && [ "$HOST" = local ]; then trap 'eval "$CLEAN"' EXIT; echo "SMOKE-MAC 只出包 $W"; exit 0; fi
if [ "$HOST" = local ]; then
  run_stage "$W" "$RUN"; rc=$?
else
  # SMOKE_MAC_RNAME=<目录名>：远端目录名不用工作树名（缺省仍是它）。远端那个目录的路径上留了卸不掉的失效挂载点时（挂载源已不存在，只有重启能清），从它 open 一律报「没有这个文件」，换个目录名绕开。
  # SMOKE_MAC_RNAME=<directory name>: the remote directory name does not use the worktree name (which remains the default). When a stale mount point that cannot be unmounted is left on that remote directory's path (the mount source no longer exists; only a reboot clears it), every open from it reports 「没有这个文件」(no such file); switch directory names to get around it.
  S="ssh -o BatchMode=yes -o ConnectTimeout=8 $HOST"; RD="dm-work/run/ios/smoke_mac_in/${SMOKE_MAC_RNAME:-$(basename "$TREE")}"; RR="$RD/run_$(basename "$RUN")"
  # 传包带墙钟上限（包约 260 MB，局域网几十秒；给 180 s）并把 rsync 的报错原文打出来：mini 盘满时 rsync 报 No space left 后挂着不退，一次合入卡了 17 分钟
  # Package transfer has a wall-clock limit (package about 260 MB, tens of seconds over LAN; 180 s given) and prints rsync's error text verbatim: when the second Mac's disk is full rsync reports No space left and then hangs without exiting; one merge was stuck for 17 minutes
  TB=$(command -v timeout || command -v gtimeout)
  { $S "mkdir -p $RD/tools" && ${TB:+$TB 180} rsync -a --delete "$W" "$HOST:$RD/" && ${TB:+$TB 30} rsync -a "$TOOLS/smoke_mac.sh" $(for _sm_f in $SMOKE_MAC_DEPS; do printf '%s ' "$TOOLS/$_sm_f"; done) "$HOST:$RD/tools/"; } > "$RUN/rsync.log" 2>&1 \
    || { LEDGER_RES=传包失败; echo "【红】传包到 $HOST 失败（退出码 $?；124 = 超时）：$(tail -2 "$RUN/rsync.log" | tr '\n' ' ' | cut -c1-200)"; echo "SMOKE-MAC RED 0/$N 用时 $(( $(date +%s) - T0 )) 秒"; exit 2; }
  if [ "${SMOKE_MAC_PKG_ONLY:-0}" = 1 ]; then trap 'eval "$CLEAN"' EXIT; echo "SMOKE-MAC 只出包 $HOST:$RD/DmMacSmoke.app"; exit 0; fi
  $S "SMOKE_MAC_STAGE=run SMOKE_MAC_WRAP=\$HOME/$RD/DmMacSmoke.app SMOKE_MAC_RUN=\$HOME/$RR SMOKE_OWNER='${SMOKE_OWNER:-未署名}' SMOKE_PLAYER=${SMOKE_PLAYER:-1} DM_MAC_DEVICE_PROFILE=${DM_MAC_DEVICE_PROFILE:-1} ${SMOKE_KEYS:+SMOKE_KEYS='$SMOKE_KEYS'} bash \$HOME/$RD/tools/smoke_mac.sh $N"; rc=$?
  rsync -a "$HOST:$RR/" "$RUN/" 2>/dev/null; $S "rm -rf \$HOME/$RR"
fi
ok=$(cat "$RUN/ok.txt" 2>/dev/null || echo 0)
[ "$rc" -eq 0 ] && LEDGER_RES=OK || LEDGER_RES=RED
# 界面回归 b 局【只报不拦，临时状态】：SMOKE_UIREG=1 时跑（缺省不跑；合入门槛 merge.sh 触发的那次冒烟会带上）。结果上、时间上都不拦——
# 冒烟主体出了结果就返回，b 局放到后台另取 macsmoke 锁跑，跑完往台账补一行（同一个 HEAD，结果列 Mac:界面回归b:OK / RED，不会被当成冒烟证据），
# 红了留现场并登记，下一次冒烟开头打印一句。
# 后台那一局用包的一份克隆副本（返回之前同步做好，目录名带本次运行号）：紧接着的下一次冒烟会在取锁之前重新包装 / 同步原来那个目录，
# 包开着的时候它的 Wrapper 被删或被整体替换，会留下卸不掉的失效挂载点。副本不当场删，下次到这里时清一小时以前、且挂载表里已经没有的（prune 阶段）。
# 升成门槛的条件：台账里攒满 30 遍后看偶发率再定（数法见 uireg/usage.md）。
# UI regression run b [report only, non-blocking; temporary status]: runs when SMOKE_UIREG=1 (not run by default; the smoke test triggered by the merge gate merge.sh includes it). It blocks neither on result nor on time --
# the main smoke test returns as soon as it has a result, run b goes to the background and takes the macsmoke lock separately, and when done appends a line to the ledger (same HEAD, result column Mac:界面回归b:OK / RED (UI regression b), which will not be taken as smoke-test evidence),
# on failure, the artifacts are preserved and registered, and the next smoke test prints a line about it at the start.
# That background run uses a cloned copy of the package (made synchronously before returning, directory name carrying this invocation's run number): the immediately following smoke test re-wraps / re-syncs the original directory before taking the lock,
# and if a package's Wrapper is deleted or wholly replaced while the package is open, it leaves a stale mount point that cannot be unmounted. The copy is not deleted on the spot; the next time we get here, those older than one hour and no longer in the mount table are cleaned (prune stage).
# Condition for promoting it to a gate: decide after 30 passes have accumulated in the ledger, looking at the flake rate (how to count: see uireg/usage.md).
if [ "$rc" -eq 0 ] && [ -z "${SMOKE_KEYS:-}" ] && [ "${SMOKE_UIREG:-0}" = 1 ]; then
  UH=$(git -C "$TREE" rev-parse HEAD 2>/dev/null || echo 无); UD=$(git -C "$TREE" status --porcelain --untracked-files=no 2>/dev/null | wc -l | tr -d ' ')
  UN=uireg_$(basename "$RUN")
  if [ "$HOST" = local ]; then SMOKE_MAC_STAGE=prune SMOKE_MAC_WRAP="$DD" bash "$TOOLS/smoke_mac.sh"; mkdir -p "$DD/$UN" && cp -cR "$W" "$DD/$UN/"; ucp=$?
  else $S "SMOKE_MAC_STAGE=prune SMOKE_MAC_WRAP=\$HOME/$RD bash \$HOME/$RD/tools/smoke_mac.sh; mkdir -p \$HOME/$RD/$UN && cp -cR \$HOME/$RD/DmMacSmoke.app \$HOME/$RD/$UN/"; ucp=$?; fi
  if [ "$ucp" -ne 0 ]; then echo "界面回归 b 局没跑：克隆包副本失败（只报不拦）"
  else
  ( t0=$(date +%s)
    if [ "$HOST" = local ]; then SMOKE_MAC_STAGE=uireg SMOKE_MAC_WRAP="$DD/$UN/DmMacSmoke.app" SMOKE_MAC_RUN="$RUN" bash "$TOOLS/smoke_mac.sh"
    else
      $S "SMOKE_MAC_STAGE=uireg SMOKE_MAC_WRAP=\$HOME/$RD/$UN/DmMacSmoke.app SMOKE_MAC_RUN=\$HOME/$RD/$UN/out SMOKE_OWNER='${SMOKE_OWNER:-未署名}' bash \$HOME/$RD/tools/smoke_mac.sh"
      rsync -a "$HOST:$RD/$UN/out/" "$RUN/" 2>/dev/null
    fi
    res=$(cat "$RUN/uireg_res.txt" 2>/dev/null || echo "RED(没有出结果)")
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "${SMOKE_OWNER:-未署名}" "$TREE" "$UH" "$UD" "Mac:界面回归b:${res%%(*}" "-" "$(( $(date +%s) - t0 ))" "界面回归b:$res" "$RUN" \
      "Mac:$([ "$HOST" = local ] && hostname -s || echo "$HOST")" >> "$LEDGER"
    case $res in OK*) : > "$UIREG_REDMARK";; *) echo "$(date '+%m-%d %H:%M') ${SMOKE_OWNER:-未署名} ${UH:0:9} ${res}，现场 $RUN/uireg（表在 tables/ 下，日志 pw/ur_iphone_b/boot.txt）" > "$UIREG_REDMARK";; esac
  ) > "$RUN/uireg_bg.log" 2>&1 < /dev/null &
  disown; echo "界面回归 b 局已放到后台跑（只报不拦），结果会补进台账；日志 $RUN/uireg_bg.log"
  fi
fi
exit "$rc"
