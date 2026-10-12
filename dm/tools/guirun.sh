#!/bin/bash
# guirun.sh [-o 标准输出] [-e 标准错误] [-p pid文件] [KEY=VAL ...] -- <程序> [参数…]
#   在图形会话里经 LaunchServices 起一个临时 .app，它的入口脚本 exec 目标程序：这样目标的线程才拿得到真实的调度优先档。
#   从 ssh / 终端 / launchctl 直接起的进程，界面档与交互档线程一律被系统封顶到普通档，帧时序量出来不真。
#   open 不带调用方的环境：目标要的变量一律写成 KEY=VAL 传（PATH / HOME 由会话给）。要有人登录着图形会话。
#   等目标结束才返回；拿不到目标的退出码（exec 之后没人能 wait 它）⇒ 调用方按目标的输出判结果。-p 文件写目标进程的 pid。
# guirun.sh [-o stdout] [-e stderr] [-p pidfile] [KEY=VAL ...] -- <program> [args...]
#   Launches a temporary .app through LaunchServices inside the GUI session; its entry script execs the target program, which is the only way
#   the target's threads get their real scheduling priority tier.
#   For a process launched directly from ssh / a terminal / launchctl, the system clamps every user-interactive and user-initiated thread to the
#   default tier, so the frame timing measured there is not real.
#   open does not pass the caller's environment: every variable the target needs must be passed as KEY=VAL (PATH / HOME come from the session).
#   Someone must be logged in to the GUI session.
#   Returns only after the target exits; the target's exit code is not available (after exec nobody can wait on it) => the caller judges the
#   result from the target's output. -p file receives the target process's pid.
set -u
O=/dev/null; E=/dev/null; P=; ENV=()
while [ $# -gt 0 ]; do case $1 in -o) O=$2; shift 2;; -e) E=$2; shift 2;; -p) P=$2; shift 2;; --) shift; break;; *=*) ENV+=(--env "$1"); shift;; *) break;; esac; done
[ $# -ge 1 ] || { echo "用法：guirun.sh [-o f] [-e f] [-p pidf] [K=V…] -- <程序> [参数…]" >&2; exit 2; }
T=$(mktemp -d "${TMPDIR:-/tmp}/guirun.XXXXXX") || exit 2; A=$T/GuiRun.app; trap 'rm -rf "$T"' EXIT; mkdir -p "$A/Contents/MacOS"
PF=${P:-$T/pid}
{ printf '#!/bin/bash\necho $$ > %q\ncd %q\n' "$PF" "$PWD"
  # 图形会话起的进程打开文件软上限只有 256 ⇒ 抬到内核每进程上限；抬不上去就在标准错误里报出来、不起目标
  # A process launched from the GUI session has a soft open-file limit of only 256 => raise it to the kernel per-process limit;
  # if it cannot be raised, report that on stderr and do not launch the target
  printf 'n=$(sysctl -n kern.maxfilesperproc); ulimit -n "$n" || { echo "guirun：ulimit -n $n 失败（软上限仍是 $(ulimit -Sn)），不起目标" >&2; exit 97; }\n'
  printf '%q ' exec "$@"; echo; } > "$A/Contents/MacOS/run"; chmod +x "$A/Contents/MacOS/run"   # 必须 exec：优先档只跟 LaunchServices 起的那个进程，它的子进程又会被封顶 | must exec: the QoS tier applies only to the process LaunchServices itself launched; a child of that process would be clamped again
cat > "$A/Contents/Info.plist" <<PL
<?xml version="1.0" encoding="UTF-8"?><!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict><key>CFBundleExecutable</key><string>run</string><key>CFBundleIdentifier</key><string>local.guirun.$$</string><key>CFBundlePackageType</key><string>APPL</string></dict></plist>
PL
open -n -W --stdout "$O" --stderr "$E" ${ENV[@]+"${ENV[@]}"} "$A" || exit 125
