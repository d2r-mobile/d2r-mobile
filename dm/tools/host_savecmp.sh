#!/bin/bash
# 存档逐字节对拍闸门（宿主侧）：宿主回放真值局 → 存盘退出 → 宿主存档 vs 真值 save_after（dm/verify/savecmp.py），并跑负对照。
# 用法：host_savecmp.sh <真值局目录> [产物目录]
#   真值局目录须有 meta.json（save = 起始存档指纹，game_seed）、c2s.jsonl、ticks.jsonl、save_after/0
#   环境：FT=固定墙钟秒（缺省 1790000000）；SC=savecmp.py 路径；A_TIME=真值 meta 无 wall 字段时真值侧时间的核对值；KEYS_FRONT=进局键序；KEYS_EXIT=存盘退出键序（缺省按真值 events.jsonl 生成；设为空串 = 不按键）
#   HSC_WALL=秒：外层退出期限（缺省120）；客体看门狗始终早30秒，长真值局显式放宽（不改逻辑拍/退出条件）。
# 步骤：
#   ① 起始存档指纹：data/switch/*/MANIFEST.sha256 的 sha（= dm/verify/savesha.sh 同算法）与 meta.json 的 save 比，找不到即报红退出；
#      宿主只拿 0/ 副本，拷完逐文件按 MANIFEST 核 sha。
#   ② 回放：DM_C2S_REPLAY + DM_GAME_SEED + DM_FIXED_TIME；过文件末拍后宿主客户端自发消息放行，并发合成 UI 消息 DM:ReplayDone（KEYS_EXIT 可等它）。
#   ③ 判据：savecmp.py 真值 vs 宿主 判绿；负对照 真值 vs 起始存档 必须判红。
# Byte-by-byte save comparison gate check (host side): the host replays a ground-truth run → saves and exits → host save vs ground-truth save_after (dm/verify/savecmp.py), and runs a negative control.
# Usage: host_savecmp.sh <ground-truth run dir> [build output dir]
#   The ground-truth run dir must contain meta.json (save = starting save fingerprint, game_seed), c2s.jsonl, ticks.jsonl, save_after/0
#   Environment: FT=fixed wall-clock seconds (default 1790000000); SC=path of savecmp.py; A_TIME=check value for the ground-truth side time when the ground-truth meta has no wall fields; KEYS_FRONT=key sequence to enter the run; KEYS_EXIT=save-and-exit key sequence (by default generated from the ground-truth events.jsonl; set to the empty string = no key presses)
#   HSC_WALL=seconds: outer exit deadline (default 120); the guest watchdog always fires 30 seconds earlier, relax it explicitly for long ground-truth runs (logic ticks / exit conditions are not changed).
# Steps:
#   ① Starting save fingerprint: the sha of data/switch/*/MANIFEST.sha256 (= same algorithm as dm/verify/savesha.sh) is compared with save in meta.json; if not found, report a failure and exit;
#      the host takes only the 0/ copy, and after copying checks the sha of every file against MANIFEST.
#   ② Replay: DM_C2S_REPLAY + DM_GAME_SEED + DM_FIXED_TIME; after the last tick of the file the host client's own spontaneous messages are let through, and a synthetic UI message DM:ReplayDone is sent (KEYS_EXIT can wait for it).
#   ③ Criterion: savecmp.py ground truth vs host must pass; the negative control ground truth vs starting save must fail.
set -u; HOLD=
export LC_ALL=C
TR=${1:?真值局目录}; OUT=${2:-${DM_ROOT:-$HOME/dm-work}/build/cache/dm/out/3.3.94012-r12}
FT=${FT:-1790000000}
X=${DM_ROOT:-$HOME/dm-work}; PY=$X/build/cache/dm/venv/bin/python3; HERE=$(cd "$(dirname "$0")/.." && pwd)
R=$X/run/platform; T=$R/hsc_$(basename "$TR")_$(date +%m%d_%H%M%S)_$$; mkdir -p "$T/user" || exit 1   # 每次新目录不覆盖（同名目录 rm -rf 丢了卡死现场） | A new directory every time, never overwritten (rm -rf of a same-name directory lost the diagnostics of a hang)
echo "局目录 $T"
red() { echo "【红】$*"; exit 2; }
WALL=${HSC_WALL:-600}
case "$WALL" in ''|*[!0-9]*) red "HSC_WALL 须为大于30的整数秒";; esac
WALL=$((10#$WALL)); [ "$WALL" -gt 30 ] || red "HSC_WALL 须大于30秒"
WATCH=$((WALL * 3 / 4))
echo "期限：宿主无进展看门狗 15 秒、总墙钟兜底 ${WATCH} 秒、退出墙钟 ${WALL} 秒"
for f in meta.json c2s.jsonl save_after/0; do [ -e "$TR/$f" ] || red "真值局缺 $f"; done

# ① 指纹
# ① Fingerprint
WANT=$($PY -c "import json,sys;m=json.load(open(sys.argv[1]));s=m['save'];print(s[0] if isinstance(s,list) else s)" "$TR/meta.json") || red "meta.json 读不出 save"
SEED=$($PY -c "import json,sys;v=json.load(open(sys.argv[1])).get('game_seed');print('' if v is None else v)" "$TR/meta.json")
REF=
for m in $X/data/switch/*/MANIFEST.sha256; do [ "$(shasum -a 256 "$m" | cut -c1-64)" = "$WANT" ] && { REF=$(dirname "$m"); break; }; done
[ -n "$REF" ] || red "起始存档指纹 $WANT 在 data/switch/*/MANIFEST.sha256 里找不到"
echo "① 起始存档 ${REF}（指纹 ${WANT:0:12}… 与 meta.json 一致）"
cp -R "$REF/0000000000000001/0" "$T/user/0" && chmod -R u+w "$T/user/0" || red "拷存档失败"
( cd "$T/user" && grep '  0000000000000001/0/' "$REF/MANIFEST.sha256" | sed 's|  0000000000000001/|  |' | shasum -a 256 -c --quiet ) || red "0/ 副本与 MANIFEST 不符"
NF=$(grep -c '  0000000000000001/0/' "$REF/MANIFEST.sha256"); echo "   0/ 副本 $NF 个文件逐个核 sha 通过"

# ② 回放
# ② Replay
K=${KEYS_FRONT:-A@OpenPanel:MainMenuButtonRibbon>OpenPanel:CharacterDifficultyModal,A@OpenPanel:CharacterDifficultyModal>FrontEndNavigation:ToGame}
# 存盘退出键序缺省按真值 events.jsonl 生成：选项页一打开即暂停并存盘，存档状态取决于暂停生效那一拍。
# 游玩段末拍 L（kind=play 的 last）按 Plus，L+18 按 R（技能树），局后末拍 E（kind=after_done 的 last_tick）入口按 R（选项页）并 DM_TICK_HOLD=E
# 冻结客体时钟直到 ExitGame ⇒ 暂停必落在拍 E（不冻结时 实测拍 N 按键暂停落在 N 或 N+1，取决于下一帧先不先跑一拍），再 A 保存并退出。
# 只适用于已转换术士君临的角色（PauseLayoutGarden 缺省光标即「保存并退出」）；未转换角色须自给 KEYS_EXIT（加 DpadDown）
# The save-and-exit key sequence is by default generated from the ground-truth events.jsonl: the game pauses and saves as soon as the options page opens, and the save state depends on the tick at which the pause takes effect.
# Press Plus at the last tick L of the play segment (last of kind=play), press R at L+18 (skill tree), press R (options page) at the entry of the last post-run tick E (last_tick of kind=after_done) with DM_TICK_HOLD=E
# freezing the guest clock until ExitGame ⇒ the pause must land on tick E (without freezing, a key press at tick N was measured to pause at N or N+1, depending on whether the next frame runs a tick first), then A to save and exit.
# Applies only to characters already converted to Reign of the Warlock (the default cursor of PauseLayoutGarden is 「保存并退出」(Save and Exit)); for unconverted characters KEYS_EXIT must be supplied by the caller (add DpadDown)
if [ -z "${KEYS_EXIT+x}" ] && [ -f "$TR/events.jsonl" ]; then
  KEYS_EXIT=$($PY - "$TR/events.jsonl" <<'PYEOF'
import json, sys
L = E = None
for line in open(sys.argv[1]):
    try: e = json.loads(line)
    except ValueError: continue
    if e.get('kind') == 'play': L = e['last']
    if e.get('kind') == 'after_done': E = e['last_tick']
if L is not None and E is not None:
    print(f'PLUS@DM:Tick:{L}#>OpenPanel:PlayerInventoryExpansionLayout,R@DM:Tick:{L + 18}#>OpenPanel:SkillsTreePanel,'
          f'R@DM:Tick:{E}#>OpenPanel:PauseLayout,A@OpenPanel:PauseLayout%1>PausePanelMessage:ExitGame\t{E}')
PYEOF
)
  HOLD=${KEYS_EXIT##*	}; KEYS_EXIT=${KEYS_EXIT%	*}
  echo "② 退出键序（按真值 events.jsonl 生成）：${KEYS_EXIT}；DM_TICK_HOLD=$HOLD"
fi
[ -n "${KEYS_EXIT:-}" ] && K="$K,$KEYS_EXIT"
cd "$HERE/host" || exit 1
make -s >/dev/null || red "宿主编译失败"   # 宿主须是当前源码（未重编的旧宿主假红） | The host must be built from the current source (a stale host that was not rebuilt gives a false failure)
mkdir -p "$T/temp"
env DM_SAVE="$T/user/0" DM_TEMP="$T/temp" ${SEED:+DM_GAME_SEED=$SEED} ${HOLD:+DM_TICK_HOLD=$HOLD} DM_FIXED_TIME=$FT "DM_KEYS=$K" DM_C2S_REPLAY="$TR/c2s.jsonl" DM_TICKS="$T/ticks.jsonl" DM_C2S_LOG="$T/out.c2s" \
    DM_UIMSG=1 DM_PRESENT_HZ=60 DM_WATCH_STALL=15 DM_WATCH=$WATCH ./dmhost "$OUT" main sdk > "$T/host.log" 2>&1 &
P=$!
for i in $(seq 1 $((WALL * 10))); do   # 退出判据不变：回到主菜单（第2次MainMenuButtonRibbon）；墙钟兜底WALL秒 | Exit criterion unchanged: back at the main menu (2nd MainMenuButtonRibbon); wall-clock fallback of WALL seconds
  [ "$(grep -c 'OpenPanel MainMenuButtonRibbon' "$T/host.log")" -ge 2 ] && { echo "② 已回到主菜单（第 $((i/10)) 秒）"; break; }
  kill -0 $P 2>/dev/null || { echo "② 宿主提前退出"; break; }; sleep 0.1
done
[ "$(grep -c 'OpenPanel MainMenuButtonRibbon' "$T/host.log")" -ge 2 ] || { kill $P 2>/dev/null; grep '【红】看门狗' "$T/host.log" | tail -1; red "${WALL} 秒内没回到主菜单"; }
sleep 2; kill $P 2>/dev/null; wait $P 2>/dev/null
grep -E 'DM_FIXED_TIME|DM_C2S_REPLAY|hid 脚本步.*(确认|红)|【红】|ExitGame' "$T/host.log" | grep -v 贴图号 | cut -c1-160
K6=0; [ -f "$TR/ticks.jsonl" ] && { $PY "$HERE/verify/ryu/selfcmp.py" "$TR/ticks.jsonl" "$T/ticks.jsonl" | tail -8; K6=${PIPESTATUS[0]}; }

# ③ 判据（时间字段核来源：宿主侧 --b-time=FT；真值侧 meta.json 有 wall_start/wall_end 用 --a-meta，否则须显式给 A_TIME=秒，不给则 savecmp 判「未核」红）
# ③ Criterion (time field source check: host side --b-time=FT; on the ground-truth side use --a-meta if meta.json has wall_start/wall_end, otherwise A_TIME=seconds must be given explicitly, and if it is not, savecmp fails with 「未核」(unverified))
SC=${SC:-$HERE/verify/savecmp.py}
MJ=$TR/meta.json; [ -f "$TR/meta.v2.json" ] && MJ=$TR/meta.v2.json          # 补记墙钟区间的版本 | The version with the wall-clock interval recorded afterwards
MJ=${META:-$MJ}   # 可显式指定 | Can be specified explicitly
AT="--a-meta=$MJ"; grep -q wall_start "$MJ" || AT=${A_TIME:+--a-time=$A_TIME}
echo "③ 正对照：真值 vs 宿主（$SC --b-time=$FT $AT）"; $PY "$SC" "$TR/save_after" "$T/user" --b-time=$FT $AT; G=$?
echo "③ 负对照：真值 vs 起始存档"; $PY "$SC" "$TR/save_after" "$REF/0000000000000001" --b-time=$FT $AT; N=$?
echo "结论：逐拍退出码 ${K6}、正对照退出码 ${G}（0 = 绿），负对照退出码 ${N}（须非 0）"
[ $K6 = 0 ] && [ $G = 0 ] && [ $N != 0 ]
