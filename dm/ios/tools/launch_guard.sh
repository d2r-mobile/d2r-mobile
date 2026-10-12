# 本机起 iOS 包不被系统的「恢复窗口」弹框挡住。被 smoke_mac.sh、touch_e2e_mac.sh、uireg/run_mac.sh source。
# 为什么要有：某一局在系统恢复窗口的阶段被杀，系统会记一笔；之后每次起这个包都先弹模态框「…unexpectedly quit while reopening windows… Reopen / Don't Reopen」，
# 包停在后台（没有启动日志），所有线的本机局都起不来，看上去像锁屏。
#   起包的 open 命令在 --args 后面带上 "${LG_ARGS[@]}"（-ApplePersistenceIgnoreState YES：这一次启动不恢复窗口、也不问）
#   launch_guard_pre <容器 Data 目录>       起包前调：删掉上次留下的窗口状态（没有就是空操作）
#   launch_guard_blocked <包名> <说明>      起包后等不到启动日志时调：红字报「起包被挡」并带上系统登记的应用状态，返回非零
# Keeps launching the iOS app on this Mac from being blocked by the system's "restore windows" dialog. Sourced by smoke_mac.sh, touch_e2e_mac.sh and uireg/run_mac.sh.
# Why it exists: if some run is killed during the system's window restoration phase, the system makes a note of it; from then on every launch of this package first shows the modal dialog "...unexpectedly quit while reopening windows... Reopen / Don't Reopen",
# the package sits in the background (no launch log), nobody's local runs can start, and it looks like a locked screen.
#   The open command that launches the package carries "${LG_ARGS[@]}" after --args (-ApplePersistenceIgnoreState YES: this launch neither restores windows nor asks)
#   launch_guard_pre <container Data directory>       call before launching: deletes the window state left from last time (a no-op if there is none)
#   launch_guard_blocked <bundle id> <description>      call when no launch log appears after launching: reports 「起包被挡」(launch blocked) in red together with the application state registered by the system, and returns non-zero
LG_ARGS=(-ApplePersistenceIgnoreState YES)
launch_guard_pre() { local d="$1/Library/Saved Application State"; [ -d "$d" ] && find "$d" -maxdepth 1 -name '*.savedState' -exec rm -rf {} + 2>/dev/null; return 0; }
launch_guard_blocked() {
  local st; st=$(lsappinfo info -only pid,applicationState,isHidden -app "$1" 2>/dev/null | tr '\n' ' ')
  echo "【红】起包被挡：$2，启动日志没出现。系统登记的应用状态：${st:-查不到（进程没起来）}。多半是桌面上有系统弹框压着（恢复窗口 / 权限 / 损坏提示），到这台机器的桌面看一眼；不是被测代码的问题" >&2
  return 1
}
