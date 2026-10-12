#!/bin/bash
set -u
export LC_ALL=C
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CASE=${1:?用法：touchchain.sh <用例名>}
CASE_DIR="$ROOT/verify/touchcases/$CASE"
[ -f "$CASE_DIR/script.txt" ] && [ -f "$CASE_DIR/assert.py" ] || { echo "【红】未知或不完整用例：$CASE" >&2; exit 2; }
BIN="$ROOT/host/dmhost"
OUT=${DM_HOST_OUT:-"$(bash "$(dirname "$0")/../tools/product.sh" path touch)"}
REF=${DM_TOUCH_SAVE:-"${DM_ROOT:-$HOME/dm-work}/data/switch/save_ref_warlock/0000000000000001/0"}
# 野外等前缀：用例目录里 prefix 文件（一行，前缀目录相对工作区根的路径）声明；环境变量 DM_TOUCH_PREFIX 可覆盖。accept 按目录收集用例时靠它带上前缀
# Replay prefixes such as the wilderness one: declared by a prefix file in the case directory (one line, the prefix directory's path relative
# to the workspace root); the environment variable DM_TOUCH_PREFIX can override it. accept relies on this file to bring the prefix along when
# it collects cases by directory
PREFIX=${DM_TOUCH_PREFIX:-$( [ -f "$CASE_DIR/prefix" ] && head -1 "$CASE_DIR/prefix" )}
case "$PREFIX" in ""|/*) ;; *) PREFIX="${DM_ROOT:-$HOME/dm-work}/$PREFIX";; esac   # 用例的 prefix 文件里写相对工作区根的路径（如 run/verify/dm/…）；绝对路径原样用 | a case's prefix file holds a path relative to the workspace root (e.g. run/verify/dm/...); an absolute path is used as is
STAMP=$(date +%s)
RUNROOT=${DM_TOUCH_RUNROOT:-${DM_ROOT:-$HOME/dm-work}/run/verify/dm/touchchain}
RUN="$RUNROOT/${CASE}_${STAMP}_$$"
START=$SECONDS
status=2
mkdir -p "$RUN/user" || exit 2
if [ -n "$PREFIX" ]; then
    [ -f "$PREFIX/manifest.json" ] && [ -f "$PREFIX/c2s.jsonl" ] || { echo "【红】前缀不完整：$PREFIX" >&2; exit 2; }
    sha=$(sed -n 's/.*"start_save_sha": "\([0-9a-f]*\)".*/\1/p' "$PREFIX/manifest.json")
    root=""   # 起始存档指纹 = data/switch/*/MANIFEST.sha256 文件本身的 SHA-256（同 dm/tools/host_savecmp.sh） | Starting save fingerprint = SHA-256 of the data/switch/*/MANIFEST.sha256 file itself (same as dm/tools/host_savecmp.sh)
    for m in "${DM_ROOT:-$HOME/dm-work}"/data/switch/*/MANIFEST.sha256; do [ "$(shasum -a 256 "$m" | cut -c1-64)" = "$sha" ] && { root=$(dirname "$m"); break; }; done
    [ -n "$root" ] || { echo "【红】按指纹找不到前缀存档：$sha" >&2; exit 2; }
    REF="$root/0000000000000001/0"
    export DM_C2S_REPLAY="$PREFIX/c2s.jsonl" DM_TOUCH_PREFIX="$PREFIX"   # 宿主 touch_mac 据此改为 DM:ReplayDone 后起局 | Based on this, the host's touch_mac starts the run after DM:ReplayDone instead
    # 选第 2 行（术士）再进局：点右侧存档列表第 2 行 → 载入高亮的角色（同真机冒烟的键序）。旧写法「下 → A 确认选人 → A」在产品布局下走不通——覆盖层把「确认选人」那条消息换成了空操作
    # Select row 2 (Warlock) then enter the game: tap row 2 of the save list on the right → load the highlighted character (same key sequence as the real device smoke test). The old "Down → A to confirm character → A" does not work under the product layout - the overlay replaced the "confirm character" message with a no-op
    export DM_KEYS='X@OpenPanel:MainMenuButtonRibbon>OpenPanel:CharacterSelectPanel,C0.84:0.208@OpenPanel:CharacterSelectPanel%1>UpdateCharacterInfo,MSG:CharacterSelect:LoadCharacter:0@UpdateCharacterInfo%0.3>FrontEndNavigation:ToGame'
fi
cp -R "$REF" "$RUN/user/0" || { echo "【红】术士存档副本失败：$REF" >&2; exit 2; }
chmod -R u+w "$RUN/user/0" || exit 2
export DM_SAVE="$RUN/user/0" DM_GAME_SEED=305419896 DM_FIXED_TIME=1790895128
export DM_TOUCH_SCRIPT="$CASE_DIR/script.txt"
# 功能用例跑在 iOS 包同一份界面覆盖层上（测的就是玩家看到的布局；画面闸门仍对原版真值、不挂）。
# accept 起跑前生成一次经 DM_ROMFS_OVERLAY 传进来；单跑本脚本时自己生成到局目录（生成脚本幂等，约 3 秒）。
# Functional cases run on the same UI overlay as the iOS app (what is tested is the layout the player sees; the screenshot gate still compares against the original-game ground truth and does not attach it).
# accept generates it once before the run and passes it in via DM_ROMFS_OVERLAY; when this script is run alone it generates it into the run directory itself (the generation script is idempotent, about 3 seconds).
if [ -z "${DM_ROMFS_OVERLAY:-}" ]; then
    python3 "$ROOT/ios/tools/gen_romfs_overlay.py" "${DM_ROMFS_SRC:-${DM_ROOT:-$HOME/dm-work}/data/switch/3.3.94012/Update/RomFS}" "$RUN/romfs_overlay" > /dev/null || { echo "【红】界面覆盖层生成失败（gen_romfs_overlay.py）" >&2; exit 2; }
    export DM_ROMFS_OVERLAY="$RUN/romfs_overlay"
fi
# 进局：X 进选角页 → 载入第 1 个角色。产品布局（前端换成键鼠版）下主菜单按 A 没有落点，原来的「A → 难度框 → A」进不了局
# Game entry: X to the character select page → load the 1st character. Under the product layout (front end switched to the keyboard-and-mouse version) pressing A on the main menu has nothing to land on, so the old "A → difficulty box → A" cannot enter the game
export DM_KEYS=${DM_KEYS:-'X@OpenPanel:MainMenuButtonRibbon>OpenPanel:CharacterSelectPanel,MSG:CharacterSelect:LoadCharacter:0@OpenPanel:CharacterSelectPanel%1>FrontEndNavigation:ToGame'}
export DM_TICKS="$RUN/ticks.jsonl" DM_TICKS_FULL=1 DM_TICKS_N=${DM_TICKS_N:-${PREFIX:+720}}
[ -f "$CASE_DIR/ticks" ] && export DM_TICKS_N=${DM_TICKS_N:-$(head -1 "$CASE_DIR/ticks")}   # 用例自带总拍数（如等通知 10 秒自己关） | The case brings its own total tick count (e.g. waiting for a notification that closes itself after 10 seconds)
export DM_TICKS_N=${DM_TICKS_N:-900}
export DM_C2S_LOG="$RUN/c2s.jsonl" DM_UIMSG="$RUN/uimsg.jsonl" DM_STATE="$RUN/state.jsonl" DM_WATCH_STALL=15 DM_WATCH=90
# 功能用例的判据都是命令包 / 游戏状态（逻辑判据），不需要产品节奏：出图不限速 + 确定性客体时钟快进，
# 起局按状态（进局 / 回放结束且读取画面已撤，touch_mac.c），触点脚本按服务端游戏拍推进，与出图速度、本机负载无关。
# 原先按产品节奏每例 17–19 s、时限 20 s，本机一并行就超时报红。
# 快进下服务端拍跑在客户端加载前面：读取画面约第 600 拍才撤，所以总拍数给到 900（前缀例 720 起算点不同，见上）
# Functional case criteria are all command packets / game state (logic criteria) and do not need product pacing: unthrottled rendering + fast-forward on the deterministic guest clock,
# the run starts by state (game entered / replay finished and loading screen dismissed, touch_mac.c), and the touch point script advances by server game ticks, independent of rendering speed and machine load.
# Previously at product pacing each case took 17–19 s with a 20 s limit, and timed out (a failure) as soon as anything ran in parallel on this machine.
# Under fast-forward the server ticks run ahead of client loading: the loading screen is only dismissed around tick 600, so the total tick count is set to 900 (prefix cases use 720 with a different starting point, see above)
export DM_PRESENT_HZ=0 DM_DETCLOCK=33333333
[ -x "$BIN" ] || timeout 75s make -C "$ROOT/host" dmhost >/dev/null || { echo "【红】宿主编译失败" >&2; exit 2; }
set +e
timeout 100s "$BIN" "$OUT" main sdk > "$RUN/host.log" 2>&1
rc=$?; [ "$rc" -eq 0 ] || { grep '【红】看门狗' "$RUN/host.log" | tail -1 >&2 || true; exit "$rc"; }
host_status=$?
timeout 5s python3 "$CASE_DIR/assert.py" "$RUN"
assert_status=$?
# 与宿主状态探针的接口约定：每例局目录必须有宿主写的 state.jsonl（DM_STATE，首拍必写），缺了说明宿主没带状态探针，断言库读不到状态
# Interface contract with the host state probe: each case's run directory must have a state.jsonl written by the host (DM_STATE, always written on the first tick); if missing, the host was built without the state probe and the assertion library cannot read state
[ -s "$RUN/state.jsonl" ] || { echo "断言失败：局目录缺 state.jsonl（宿主未写 DM_STATE）" >&2; assert_status=1; }
set -e
if [ "$host_status" -eq 0 ] && [ "$assert_status" -eq 0 ]; then
    status=0
    [ "${DM_TOUCH_KEEP:-0}" = 1 ] || rm -rf "$RUN"
    echo "TOUCHCHAIN $CASE OK 用时 $((SECONDS - START)) 秒"
    exit 0
fi
echo "【红】touchchain $CASE：host=$host_status assert=$assert_status；局目录：$RUN" >&2
echo "TOUCHCHAIN $CASE RED 用时 $((SECONDS - START)) 秒"
exit 1
