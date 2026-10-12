#!/bin/bash
# uireg 的起局脚本（真机 iPhone）：UIREG_RUNNER=<本文件> python3 uireg.py iphone <局名…>。接口同 run_mac.sh（<标签> <上限秒> < 触点脚本）。
# 走 ../touch_e2e.sh：它自己取设备锁、按 pid 停 App、设备读本机配置（local_env.sh）。被测的是手机上已经装好的那个包——装包与本脚本之间放过锁的话，包可能已被别人换掉，
#   要保证是自己的包，就在外层先取设备锁把「装包 + 跑」包住（设备锁可重入）。
# 与 Mac 上的差别：只有 iphone 档；跑满上限秒才收（不提前收）；只拉 boot.txt 与命令包日志——截图每张约 20 秒不拉，所以「只看截图」的项在真机上都是未判；
#   没有 d 局的专用存档（PW_SAVE 非空的局不跑）。
# UIREG_PULL="put_00.png put_01.png"：额外拉回这几张截图（读画面的判据要用；每张约 20 秒，算在持锁时间里）。
# uireg run-start script (real-device iPhone): UIREG_RUNNER=<this file> python3 uireg.py iphone <run names...>. Same interface as run_mac.sh (<label> <limit seconds> < touch point script).
# Goes through ../touch_e2e.sh: it takes the device lock itself, stops the App by pid, and reads the device from the local config (local_env.sh). What is tested is the bundle already installed on the phone -- if the lock was released between installing and this script, the bundle may already have been replaced by someone else,
#   so to make sure it is your own bundle, take the device lock in an outer layer first to wrap 'install + run' (the device lock is reentrant).
# Differences from the Mac: only the iphone profile; the run ends only after the full limit in seconds (no early end); only boot.txt and the command packet log are pulled -- screenshots take about 20 seconds each and are not pulled, so 'screenshot-only' items are all unjudged on the real device;
#   there is no dedicated save for the d runs (runs with a non-empty PW_SAVE are not run).
# UIREG_PULL="put_00.png put_01.png": additionally pull back these screenshots (needed for criteria that read the picture; about 20 seconds each, counted in the lock hold time).
unset LANG LC_CTYPE; export LC_ALL=C   # touch_e2e.sh 里「变量后紧跟中文标点」的行在 UTF-8 区域设置下会被 bash 3.2 当成变量名的一部分 | Lines in touch_e2e.sh where 'a variable is immediately followed by Chinese punctuation' are treated by bash 3.2 as part of the variable name under a UTF-8 locale
TAG=$1; CAP=$2; HERE=$(cd "$(dirname "$0")" && pwd)
O=${UIREG_OUT:-${DM_ROOT:-$HOME/dm-work}/run/uireg}/pw/$TAG; mkdir -p "$O"; cat > "$O/in.txt"
[ "${UIREG_MODE:-iphone}" = iphone ] || { echo "计时：真机只有 iphone 档，没跑" > "$O/timing.txt"; exit 2; }
[ -z "${PW_SAVE:-}" ] || { echo "计时：真机上没有存档 ${PW_SAVE}，没跑" > "$O/timing.txt"; exit 2; }
rm -f "$O/boot.txt" "$O/boot_full.txt" "$O/c2s.jsonl" "$O/e2e_c2s.jsonl"
# 触点脚本的裸坐标按 667×375 的 16:9 画面写 → DM_TOUCH_REF 由 App 换算到真机屏；touch_e2e.sh 一定带按键脚本变量，这里给一条永不触发的占位键序，
#   并显式打开前端自动进选角页（DM_FE_AUTO，不带按键脚本时它缺省就开）；命令包日志名与存档由 touch_e2e.sh 自己定
# The touch point script's bare coordinates are written for a 667×375 16:9 picture → converted by the App to the real-device screen via DM_TOUCH_REF; touch_e2e.sh always passes the key script variable, so a placeholder key sequence that never triggers is given here,
#   and front-end auto-entry into the character select page is turned on explicitly (DM_FE_AUTO, which is on by default when no key script is passed); the command packet log name and the save are decided by touch_e2e.sh itself
J=$(python3 -c "
import json,sys
d={'DM_TOUCH_SCRIPT_START':'menu','DM_TOUCH_REF':'667x375','DM_FE_AUTO':'1'}
for kv in sys.argv[1].split():
    k,v=kv.split('=',1)
    if k not in ('DM_C2S_LOG','DM_SAVE_FRESH'): d[k]=v
print(json.dumps(d))" "${PW_ENV:-}")
t0=$(date +%s)
E2E_KEYS='B@OpenPanel:NoSuchPanelEver' E2E_ENV="$J" E2E_PULL="boot.txt c2s.jsonl ${UIREG_PULL:-}" bash "$HERE/../touch_e2e.sh" "$O/in.txt" "$O" "$CAP" > "$O/e2e.log" 2>&1
[ -f "$O/e2e_c2s.jsonl" ] && cp "$O/e2e_c2s.jsonl" "$O/c2s.jsonl"
# 包对不对读出来核：boot.txt 首行的构建号必须等于被测提交的提交时间戳（dm/ios/run.sh build 写进去的）。被测提交缺省 = 本脚本所在树的 HEAD，
# 包是从别的树出的就用 UIREG_EXPECT_COMMIT=<提交> 指明。不等 = 设备上跑的不是这一批的包（没装 / 被别人换了 / 树不干净出的包）→ 整局作废：日志换成一行红，各项都判不通。
# Whether the package is the right one is read out and checked: the build number on the first line of boot.txt must equal the commit timestamp of the
# commit under test (written in by dm/ios/run.sh build). The commit under test defaults to HEAD of the tree this script lives in;
# if the package was built from another tree, name it with UIREG_EXPECT_COMMIT=<commit>. A mismatch = the device is not running this batch's package
# (not installed / replaced by someone else / built from a dirty tree) → the whole run is void: the log is replaced by a single failure line and every item
# fails.
EXP=$(git -C "$HERE" log -1 --format=%ct ${UIREG_EXPECT_COMMIT:-} 2>/dev/null); GOT=$(LC_ALL=C grep -a -m1 -o 'boot: 版本 [^ ]* 构建 [0-9]*' "$O/boot.txt" 2>/dev/null | awk '{print $NF}')
if [ "${UIREG_BUILD_CHECK:-1}" = 1 ] && [ -f "$O/boot.txt" ] && [ "$GOT" != "$EXP" ]; then
  mv "$O/boot.txt" "$O/boot_wrongpkg.txt"; echo "[dm] 【红】包不对，整局作废：设备上这一局的构建号 ${GOT:-读不到}，期望 ${EXP:-读不到}（提交 $(git -C "$HERE" log -1 --format=%h ${UIREG_EXPECT_COMMIT:-} 2>/dev/null | cut -c1-9) 的提交时间戳）；原日志在 boot_wrongpkg.txt" > "$O/boot.txt"
fi
# 真机跑满上限秒才收：脚本走完之后静置的那一段（会有「无进展」看门狗红行）不属于这一局，日志截到「note 结束」为止，与 Mac 上见到记号就收的范围一致；全文留在 boot_full.txt
# On the real device the run only ends after the full limit in seconds: the idle stretch after the script finishes (which gets 「无进展」(no progress) watchdog failure lines) does not belong to this run, so the log is cut at 「note 结束」(note end), matching the range on the Mac where the run ends as soon as the marker is seen; the full text is kept in boot_full.txt
if LC_ALL=C grep -a -q '\[script\] note 结束' "$O/boot.txt" 2>/dev/null; then mv "$O/boot.txt" "$O/boot_full.txt"; LC_ALL=C awk '{ print } /\[script\] note 结束/ { exit }' "$O/boot_full.txt" > "$O/boot.txt"; fi
echo "计时：真机整局墙钟 $(( $(date +%s) - t0 )) 秒（含排锁、推脚本、拉日志）" > "$O/timing.txt"
