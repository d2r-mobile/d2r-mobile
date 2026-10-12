#!/bin/bash
# 苹果 Instruments 命令行录制 + 导出：真机上把 App 拉进营地 / 野外，预热后用 xctrace 挂上进程录一段，再把 .trace 里每张表导出成 XML。
# 以后量「时间花在哪」先用它，不先造开关。模板：Game Performance / Game Performance Overview / Metal System Trace / Power Profiler / CPU Counters / System Trace / Time Profiler。
# 用法（真机）：XT_TREE=<已构建的树（有 .iosdd）> xtrace.sh <camp|wild|menu|blank> <模板名> [录制秒=30] [预热秒=30]
#   menu = 停在主菜单（触摸覆盖层隐藏）、blank = 纯 Metal 空白页（-mode blank，零子视图）：给「系统合成器走不走直接上屏」做对照（看 displayed-surfaces-interval 的 direct-to-display 列）
# 用法（本机 / mini，带窗口宿主）：XT_OWNER=<线名+在量什么> XT_TREE=<树> XT_OUT=<产物> xtrace.sh mac camp <模板名> [录制秒=30] [预热秒=60]   可调 XT_RES / XT_3D / XT_ENV / XT_DIR；进门取 mini 测量锁（mlock.sh）
#   XT_INSTALL=1 先装机（装的就是 XT_TREE 的包）；XT_ENV="DM_X=1 DM_Y=2" 追加 App 环境；XT_EXPORT=0 只录不导出（之后可 xtrace.sh export <trace>）
#   单独导出：xtrace.sh export <x.trace>  → 同目录 toc.xml + tables/<schema>.xml
# 产物：$DM_ROOT/run/ios/xtrace/<时刻>_<场景>_<模板>/{t.trace, toc.xml, tables/*.xml, launch.log, record.log}。走设备锁（测量类），单段占机 = 预热 + 录制 + 约 20 s。
# Apple Instruments command-line recording + export: on the real device, bring the App into the camp / wilderness, after warm-up attach xctrace to the process and record a stretch, then export every table in the .trace as XML.
# From now on, to measure 'where the time goes' use this first instead of building switches first. Templates: Game Performance / Game Performance Overview / Metal System Trace / Power Profiler / CPU Counters / System Trace / Time Profiler.
# Usage (real device): XT_TREE=<built tree (with .iosdd)> xtrace.sh <camp|wild|menu|blank> <template name> [record seconds=30] [warm-up seconds=30]
#   menu = stay on the main menu (touch overlay hidden), blank = pure Metal blank page (-mode blank, zero subviews): a control for 'whether the system compositor goes direct-to-display' (look at the direct-to-display column of displayed-surfaces-interval)
# Usage (local machine / the second Mac, windowed host): XT_OWNER=<line name + what is being measured> XT_TREE=<tree> XT_OUT=<build output> xtrace.sh mac camp <template name> [record seconds=30] [warm-up seconds=60]   adjustable: XT_RES / XT_3D / XT_ENV / XT_DIR; takes the second Mac's measurement lock on entry (mlock.sh)
#   XT_INSTALL=1 installs first (what is installed is XT_TREE's bundle); XT_ENV="DM_X=1 DM_Y=2" appends App environment; XT_EXPORT=0 records only without exporting (later you can run xtrace.sh export <trace>)
#   Standalone export: xtrace.sh export <x.trace>  → toc.xml + tables/<schema>.xml in the same directory
# Build output: $DM_ROOT/run/ios/xtrace/<time>_<scene>_<template>/{t.trace, toc.xml, tables/*.xml, launch.log, record.log}. Goes through the device lock (measurement class); device occupancy per segment = warm-up + recording + about 20 s.
export DM_DEVLOCK_CLASS=measure
export XT_TABLES="ca-client-buffer-wait-interval ca-client-present-request display-vsyncs-interval display-surface-swap displayed-surfaces-interval metal-gpu-intervals metal-gpu-state-intervals metal-application-encoders-list metal-application-intervals metal-driver-event-intervals metal-command-buffer-error metal-shader-profiler-intervals metal-shader-profiler-shader-list gpu-performance-state-intervals gpu-performance-device-state-intervals gpu-shader-profiler-interval gpu-shader-profiler-sample gpu-counter-info gpu-counter-value device-thermal-state-intervals context-switch cpu-state graphics-compiler-activity-intervals graphics-compiler-spill-events hang-risks system-load thread-info"
set -u
t(){ echo "[$(date +%T)] $*"; }
# xctrace 每录一次会在用户临时目录留一个 instruments*.ktrace（约 1.5 GB）且不清：85 个共 120 GB 把 mini 写满。录完删本次的，顺带删半小时前的陈货。
# Each xctrace recording leaves an instruments*.ktrace (about 1.5 GB) in the user temp directory and never cleans it: 85 of them totaling 120 GB filled up the second Mac. Delete this recording's after it finishes, and along the way delete stale ones older than half an hour.
kt_mark(){ KT0=$1/.kt0; : > "$KT0"; }
kt_clean(){ local T; T=$(getconf DARWIN_USER_TEMP_DIR); find "$T" -maxdepth 1 -name 'instruments*.ktrace' \( -newer "$KT0" -o -mmin +30 \) -delete 2>/dev/null; }
xexport(){   # <trace>：导出目录页与每张表（同名表多份时加序号） | <trace>: export the table of contents and every table (add an index when there are several tables with the same name)
  local tr=$1 d; d=$(dirname "$tr"); mkdir -p "$d/tables"
  # 真机 trace 导目录页时 xctrace 会崩（ProcessLoader 段错误，单表导出不受影响）→ 退回按内置表名逐张试
  # xctrace crashes when exporting the table of contents of a real-device trace (ProcessLoader segmentation fault; single-table export is unaffected) → fall back to trying the built-in table names one by one
  xcrun xctrace export --input "$tr" --toc --output "$d/toc.xml" 2> "$d/export.log" || { t "目录页导不出，按内置表名逐张导"; for s in $XT_TABLES; do echo "<table schema=\"$s\"/>"; done > "$d/toc.xml"; }
  python3 - "$tr" "$d" <<'PY'
import sys, re, os, subprocess, collections
tr, d = sys.argv[1:3]; toc = open(d + '/toc.xml').read(); seen = collections.Counter()
skip = re.compile(os.environ.get('XT_SKIP', r'^(kdebug|os-log|os-signpost|time-sample|time-profile|counters-profile)'))   # 原始内核事件表单张可上 GB，缺省不导；XT_SKIP='^$' 全导
only = re.compile(os.environ.get('XT_ONLY', '^(' + '|'.join(os.environ['XT_TABLES'].split()) + ')$'))   # 缺省只导读数用得上的表（全导约 6 分钟）；XT_ONLY=. 全导
for s in re.findall(r'<table [^>]*schema="([^"]+)"', toc):
    seen[s] += 1
    if skip.search(s) or not only.search(s): continue
    out = f'{d}/tables/{s}' + (f'.{seen[s]}' if seen[s] > 1 else '') + '.xml'
    xp = f'/trace-toc/run[@number="1"]/data/table[@schema="{s}"][{seen[s]}]'
    r = subprocess.run(['xcrun', 'xctrace', 'export', '--input', tr, '--xpath', xp, '--output', out], capture_output=True, text=True)
    print(f'{s}\t{out if r.returncode == 0 else "失败：" + r.stderr.strip()[:80]}')
PY
}
[ "${1:-}" = export ] && { xexport "${2:?缺 .trace 路径}"; exit; }
if [ "${1:-}" = mac ]; then   # 本机（Mac / mini）带窗口宿主：xtrace.sh mac <camp|wild> <模板> [录制秒=30] [预热秒=60]；份额类读数在这里取，不占手机 | Local machine (Mac / the second Mac) windowed host: xtrace.sh mac <camp|wild> <template> [record seconds=30] [warm-up seconds=60]; share-type readings are taken here, without occupying the phone
  SC=${2:?camp|wild}; TPL=${3:?模板名}; SEC=${4:-30}; WARM=${5:-60}; W=${XT_TREE:?XT_TREE = 含 dm/host/dmhost 的树}; OUT=${XT_OUT:?XT_OUT = 产物目录}
  source "$(dirname "$0")/mlock.sh"; mlock_acquire "${XT_OWNER:-未署名} xctrace $SC $(basename "$TPL" .tracetemplate) 约 $((WARM + SEC + 40)) s"; trap mlock_release EXIT   # 同一时刻只跑一份测量；批量脚本自己持锁时这里不再取 | Only one measurement runs at a time; when a batch script holds the lock itself it is not taken again here
  SV=${XT_SAVE:-${DM_ROOT:-$HOME/dm-work}/data/switch/save_ref_warlock/0000000000000001}; R=${XT_DIR:-${XT_RUN:-${DM_ROOT:-$HOME/dm-work}/run/xtrace}/$(date +%m%d_%H%M%S)_mac_${SC}_$(basename "$TPL" .tracetemplate | tr ' ' '_')}; mkdir -p "$R/temp"   # XT_DIR：调用方指定本局目录（别用「取最新目录」，会拿到别人那局） | XT_DIR: the caller specifies this run's directory (do not use 'take the newest directory', it will pick up someone else's run)
  [ "$(df -m "$R" | awk 'NR==2{print $4}')" -ge 20480 ] || { echo "🔴 磁盘剩余不足 20 GB，不跑（清 ${DM_ROOT:-$HOME/dm-work}/run/xtrace 里旧的 t.trace）"; exit 1; }
  cp -R "$SV/0" "$R/save" && chmod -R u+w "$R/save" || { echo "🔴 拷存档失败"; exit 1; }
  K='X@OpenPanel:MainMenuButtonRibbon>OpenPanel:CharacterSelectPanel,MSG:CharacterSelect:LoadCharacter:0@OpenPanel:CharacterSelectPanel%1>FrontEndNavigation:ToGame'
  [ "$SC" = camp ] || { echo "🔴 mac 模式目前只接了营地（野外要回放前缀，后补）"; exit 2; }
  ( cd "$W/dm/host" && bash "${XT_GUIRUN:-$W/dm/tools/guirun.sh}" -o "$R/host.log" -e "$R/host.err" -p "$R/pid" DM_WINDOW=1 DM_GAME_RES=${XT_RES:-2868x1320} DM_3D_SCALE=${XT_3D:-0.75} \
      DM_KEYS="$K" DM_SAVE="$R/save" DM_TEMP="$R/temp" DM_NVN_SHMAP="$W/dm/gfx/nvn/shader_map.tsv" DM_WATCH=$((WARM + SEC + 60)) ${XT_ENV:-} -- "$W/dm/host/dmhost" "$OUT" main sdk ) &
  t "已起带窗口宿主，预热 $WARM s"; sleep "$WARM"; p=$(cat "$R/pid" 2>/dev/null); kill -0 "$p" 2>/dev/null || { t "🔴 宿主不在了"; tail -3 "$R/host.log" "$R/host.err"; exit 1; }
  t "录制 $TPL $SEC s（pid ${p}）"; kt_mark "$R"; timeout -k 5 $((SEC + 60)) xcrun xctrace record --template "$TPL" --attach "$p" --time-limit "${SEC}s" --output "$R/t.trace" --no-prompt > "$R/record.log" 2>&1; kill "$p" 2>/dev/null; kt_clean
  [ -d "$R/t.trace" ] || { t "🔴 没有录到 trace"; tail -5 "$R/record.log"; exit 1; }
  t "录完 → $R/t.trace"; [ "${XT_EXPORT:-1}" = 0 ] || xexport "$R/t.trace" | tee "$R/tables.tsv" | wc -l | xargs -I{} echo "导出 {} 张表 → $R/tables/"
  # 导出完就删录制本体与存档副本（单局 100–400 MB；73 局把 mini 磁盘写满，之后的录制存不下、出空表）。XT_KEEP_TRACE=1 保留
  # Delete the recording itself and the save copy as soon as the export finishes (100–400 MB per run; 73 runs filled the second Mac's disk, and later recordings could not be stored and produced empty tables). XT_KEEP_TRACE=1 keeps them
  [ "${XT_EXPORT:-1}" = 0 ] || [ "${XT_KEEP_TRACE:-0}" = 1 ] || for sub in t.trace save save.work temp; do [ -e "$R/$sub" ] && find "$R/$sub" -delete; done
  echo "RUN $R PID $p"; exit   # 出表：xtrace_report.py <目录>（自动按 $R/pid 的进程号过滤） | Produce tables: xtrace_report.py <dir> (automatically filters by the process id in $R/pid)
fi
SC=${1:?camp|wild|menu|blank}; TPL=${2:?模板名}; SEC=${3:-30}; WARM=${4:-30}   # 占机 = 预热 + 录制 + 约 60 s，测量类单轮上限 4 分钟 | Device occupancy = warm-up + recording + about 60 s; the single-round limit for measurement jobs is 4 minutes
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/local_env.sh" && local_env_need DM_DEVICE DM_IOS_UDID || exit 1   # 本机设备配置（不进仓，见 dm/ios/local.env.example） | Local device configuration (not committed, see dm/ios/local.env.example)
D=$DM_DEVICE; XD=${XT_UDID:-$DM_IOS_UDID}; B=${DM_BUNDLE_ID:?先在本机配置里设 DM_BUNDLE_ID（App 包名，见 dm/ios/local.env.example）}; W=${XT_TREE:?XT_TREE 必填}
APP=$W/.iosdd/Build/Products/Release-iphoneos/DmIOS.app; [ -d "$APP" ] || { echo "🔴 $APP 不存在（先构建）"; exit 1; }
R=${DM_ROOT:-$HOME/dm-work}/run/ios/xtrace/$(date +%m%d_%H%M%S)_${SC}_$(basename "$TPL" .tracetemplate | tr ' ' '_'); mkdir -p "$R"
K_CAMP='X@OpenPanel:MainMenuButtonRibbon>OpenPanel:CharacterSelectPanel,MSG:CharacterSelect:LoadCharacter:0@OpenPanel:CharacterSelectPanel%1>FrontEndNavigation:ToGame'
K_WILD='X@OpenPanel:MainMenuButtonRibbon>OpenPanel:CharacterSelectPanel,C0.84:0.208@OpenPanel:CharacterSelectPanel%1>UpdateCharacterInfo,MSG:CharacterSelect:LoadCharacter:0@UpdateCharacterInfo%0.3>FrontEndNavigation:ToGame'
A='A@DM:ReplayDone+2'; for i in $(seq 1 40); do A="$A,$(echo LSL LSU LSR LSD | cut -d' ' -f$(( (i - 1) % 4 + 1 )))@+1,A@+2"; done
envs=(DEVICECTL_CHILD_DM_BRIGHTNESS=0.5 DEVICECTL_CHILD_DM_SAVE_FRESH=warlock DEVICECTL_CHILD_DM_UIMSG=1)   # DM_UIMSG=1：按键序列按界面消息触发，漏了就停在菜单页（录错过一次） | DM_UIMSG=1: the key sequence is triggered by UI messages; without it the run stays on the menu page (recorded wrongly once)
case "$SC" in camp|wild|menu|blank) ;; *) echo "🔴 未知场景 $SC"; exit 2;; esac
MODE=boot; [ "$SC" = blank ] && MODE=blank
if [ "$SC" = camp ]; then envs+=(DEVICECTL_CHILD_DM_KEYS="$K_CAMP")
elif [ "$SC" = menu ] || [ "$SC" = blank ]; then :   # 不带按键：停在主菜单 / 空白页 | No keys: stay on the main menu / blank page
else envs+=(DEVICECTL_CHILD_DM_KEYS="$K_WILD,$A" DEVICECTL_CHILD_DM_C2S_REPLAY=m1_wild.jsonl DEVICECTL_CHILD_DM_GAME_SEED=305419896); fi
for e in ${XT_ENV:-}; do envs+=(DEVICECTL_CHILD_$e); done
pid(){ xcrun devicectl device info processes --device $D 2>/dev/null | grep "DmIOS.app/DmIOS" | awk '{print $1}'; }
source "$W/dm/ios/tools/devlock.sh"; devlock_acquire "${XT_OWNER:-未署名} xctrace $SC $TPL"; trap devlock_release EXIT; trap 'exit 143' INT TERM
[ "${XT_INSTALL:-0}" = 1 ] && { timeout -k 5 120 xcrun devicectl device install app --device $D "$APP" > "$R/install.log" 2>&1 || { t "🔴 装机失败"; exit 1; }; }
[ "$SC" = wild ] && { timeout -k 5 60 xcrun devicectl device copy to --device $D --domain-type appDataContainer --domain-identifier $B --source ${XT_WILD_C2S:-${DM_ROOT:-$HOME/dm-work}/run/verify/dm/run_sta_contract_1002_090004/wild_prefix_original_warlock/c2s.jsonl} --destination Documents/m1_wild.jsonl > /dev/null 2>&1 || t "🔴 推野外回放前缀失败"; }
env "${envs[@]}" timeout -k 5 30 xcrun devicectl device process launch --device $D --terminate-existing $B -mode $MODE > "$R/launch.log" 2>&1 || { t "🔴 拉起失败"; tail -3 "$R/launch.log"; exit 1; }
t "已拉起（${SC}），预热 $WARM s"; sleep "$WARM"
p=$(pid); [ -n "$p" ] || { t "🔴 预热后进程不在（App 已退出）"; exit 1; }
t "录制 $TPL $SEC s（pid ${p}）"; kt_mark "$R"; timeout -k 5 $((SEC + 40)) xcrun xctrace record --device "$XD" --template "$TPL" --attach "$p" --time-limit "${SEC}s" --output "$R/t.trace" --no-prompt > "$R/record.log" 2>&1; rc=$?; kt_clean
p=$(pid); [ -n "$p" ] && xcrun devicectl device process terminate --device $D --pid $p > /dev/null 2>&1
xcrun devicectl device copy from --device $D --domain-type appDataContainer --domain-identifier $B --source Documents/perf.txt --destination "$R/perf.txt" > /dev/null 2>&1   # App 自记的每 10 秒一行：核场景与温控 | The line the App records itself every 10 seconds: verify the scene and thermal control
devlock_release; trap - EXIT
tail -4 "$R/perf.txt" 2>/dev/null | cut -f1-5,9-11; tail -4 "$R/perf.txt" 2>/dev/null | grep -q 局内 || t "🔴 录制期间 App 不在局内（看上面 perf.txt 末几行的场景列）：这条不算数"
[ -d "$R/t.trace" ] || { t "🔴 没有录到 trace（xctrace 退出码 ${rc}）"; tail -5 "$R/record.log"; exit 1; }
t "录完 → $R/t.trace（$(du -sh "$R/t.trace" | cut -f1)）"; [ "${XT_EXPORT:-1}" = 0 ] || xexport "$R/t.trace" | tee "$R/tables.tsv" | wc -l | xargs -I{} echo "导出 {} 张表 → $R/tables/"
