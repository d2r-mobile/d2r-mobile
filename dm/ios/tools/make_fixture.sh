#!/bin/bash
# 生成冒烟用的标准存档：在这台 Mac 上把 iOS 包从空存档起一局，用触点脚本先后建两个角色、各进一次营地并保存退出，再把存档收成标准存档。
# 之后出的 App 会带上它（run.sh build 从工作区拷），冒烟第一段「读档进局」就有档可读。存档只在你的工作区里，不进仓库。
# 用法：make_fixture.sh            产物：$DM_ROOT/data/switch/save_ref_warlock/0000000000000001/0/
# 前提：setup.sh 已跑完、dm/ios/local.env 填了 DM_TEAM_ID 与 DM_BUNDLE_ID。已有标准存档时不覆盖，要重做先删掉那个目录。
# Generate the standard save used by the smoke test: on this Mac, start a game in the iOS app from an empty save, use a touch point script to create two characters one after the other, enter the camp once with each and save and exit, then collect the result as the standard save.
# Apps built afterwards carry it (run.sh build copies it from the workspace), so the first smoke-test segment, "load a save and enter the game", has a save to load. The save stays in your workspace only and does not go into the repository.
# Usage: make_fixture.sh            Output: $DM_ROOT/data/switch/save_ref_warlock/0000000000000001/0/
# Prerequisites: setup.sh has finished, and dm/ios/local.env has DM_TEAM_ID and DM_BUNDLE_ID filled in. An existing standard save is not overwritten; to redo it, delete that directory first.
set -u
TOOLS=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd); export DM_ROOT=${DM_ROOT:-$HOME/dm-work}
source "$TOOLS/local_env.sh"; local_env_need DM_BUNDLE_ID || exit 1
B=$DM_BUNDLE_ID; DST=$DM_ROOT/data/switch/save_ref_warlock/0000000000000001/0
[ -d "$DST" ] && { echo "已有标准存档 $DST，不覆盖"; exit 0; }
GAME=${SMOKE_MAC_ROMFS:-$DM_ROOT/data/switch/3.3.94012/Update/RomFS/data}
[ -f "$GAME/.build.config" ] || { echo "✗ 没有游戏资源 $GAME"; exit 1; }
W=$(SMOKE_MAC_PKG_ONLY=1 bash "$TOOLS/smoke_mac.sh" 1 2>&1 | sed -n 's/^SMOKE-MAC 只出包 //p' | tail -1)
[ -d "$W" ] || { echo "✗ 没出成能在 Mac 上跑的包"; exit 1; }
export DM_DEVLOCK_DIR=$DM_ROOT/run/ios/macsmoke.lock; source "$TOOLS/devlock.sh"; devlock_acquire "生成标准存档" 300 || exit 1; trap devlock_release EXIT
pids() { ps -axo pid=,command= | awk '/\/Wrapper\/DmIOS\.app\/DmIOS/ {print $1}'; }
[ -z "$(pids)" ] || { echo "✗ 这台 Mac 上已有游戏实例在跑（pid $(pids | tr '\n' ' ')），先结束它"; exit 1; }
container() { local m; for m in "$HOME"/Library/Containers/*/.com.apple.containermanagerd.metadata.plist; do
    [ "$(/usr/libexec/PlistBuddy -c 'Print :MCMMetadataIdentifier' "$m" 2>/dev/null)" = "$B" ] && { echo "$(dirname "$m")/Data"; return 0; }; done; return 1; }
C=$(container) || { open -n "$W" --args -mode boot; for _ in $(seq 1 30); do C=$(container) && break; sleep 0.5; done; for p in $(pids); do kill -TERM "$p"; done; sleep 1; }
[ -n "${C:-}" ] || { echo "✗ 系统没有给 $B 建容器（包没签进这台 Mac？）"; exit 1; }
rom="$C/Library/Application Support/romfs"
[ -f "$rom/data/.build.config" ] || { mkdir -p "$rom" && cp -cR "$GAME" "$rom/data" || { echo "✗ 资源没放进容器"; exit 1; }; }
port=$(python3 -c "import socket;s=socket.socket();s.bind(('127.0.0.1',0));print(s.getsockname()[1])"); LOG=$(mktemp)
# 触点脚本：冒烟那份的前半（建角、进局、站定）→ 打开功能页、切到暂停页、点「保存并退出」（新角色要退回选角页才落盘）
# → 在选角页再建第二个角色、同样保存退出。出两个角色是因为冒烟的缺省键序要点选角列表的第 2 行来选角色，只有一个角色时那一下没有反应。
# Touch point script: the first half of the smoke test's script (create a character, enter the game, come to a stop) → open the function page, switch to the pause page, tap "Save and Exit" (a new character is written to disk only after returning to the character select page)
# → create a second character on the character select page and save and exit the same way. Two characters are made because the smoke test's default key sequence taps row 2 of the character list to pick a character, and with only one character that tap does nothing.
{ sed -n '1,/ pos$/p' "$TOOLS/smoke_touch.txt"
  printf '%s\n' '24.00 down 0 @menu' '24.12 up 0' '25.50 down 0 551,17' '25.62 up 0' '26.60 tap_ui Exit@PauseLayoutGarden 3' \
    '34.00 tap_ui CreateCharacter@CharacterSelectPanel 3' '36.50 down 0 488,209' '36.62 up 0' '37.30 down 0 433,189' '37.42 up 0' '40.00 down 0 334,343' '40.12 up 0' \
    '43.00 down 0 167,193' '43.12 up 0' '46.00 type Second' '47.00 alert ok' '55.00 pos' \
    '57.00 down 0 @menu' '57.12 up 0' '58.50 down 0 551,17' '58.62 up 0' '59.60 tap_ui Exit@PauseLayoutGarden 3' '67.00 note 存档完成'; } > "$C/Documents/fixture_touch.txt"
caffeinate -u -t 3 > /dev/null 2>&1
open -n "$W" --env DM_GAME_RES=2868x1320 --env DM_TEST_SAFE_L=62 --env DM_TEST_SAFE_R=62 --env DM_TOUCH_REF=667x375 --env DM_SAVE_FRESH=empty --env DM_LOG_PORT="$port" --env DM_UIMSG=1 \
  --env DM_TOUCH_SCRIPT=fixture_touch.txt --env DM_TOUCH_SCRIPT_START=menu --env DM_GAME_SEED=0x12345678 --env DM_FIXED_TIME=1790000000 --args -mode boot
r=$(python3 "$TOOLS/logport.py" until "$port" "$LOG" 160 '[script] 全部')   # 等触点脚本走完（建角、进局、保存并退出）；之后让 App 自己退出 | Wait for the touch point script to finish (create a character, enter the game, save and exit); then let the App quit by itself
for _ in $(seq 1 20); do [ -z "$(pids)" ] && break; sleep 0.5; done; for p in $(pids); do kill -TERM "$p"; done
case "$r" in result=0*) ;; *) echo "✗ 没走到建角进局（$r），日志 $LOG"; exit 1;; esac
LC_ALL=C grep -a -q '【红】' "$LOG" && { echo "✗ 触点脚本有一步没走成：$(LC_ALL=C grep -a '【红】' "$LOG" | head -1 | cut -c1-120)，日志 $LOG"; exit 1; }
n=$(find "$C/Documents/save.test" -name '*.d2s' 2> /dev/null | wc -l | tr -d ' ')
[ "$n" -ge 2 ] || { echo "✗ 存档目录里只有 $n 个角色文件（要 2 个），日志 $LOG"; exit 1; }
mkdir -p "$DST" && cp -R "$C/Documents/save.test/." "$DST/" && chmod -R u+w "$DST" && echo "标准存档已生成：$DST（$n 个角色）。重新出 App（run.sh build）后冒烟即可用"
