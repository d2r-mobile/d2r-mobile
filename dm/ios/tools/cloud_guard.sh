# 本机起 iOS 包的总闸：不许连真实 iCloud。被 smoke_mac.sh、touch_e2e_mac.sh、uireg/run_mac.sh source。
# 为什么要有：iCloud 同步只按路径认「真存档」（DM_SAVE == Documents/save，cloud_ios.m 的 dm_cloud_boot）。开发机登着真实账号时，
# Mac 上跑的 iOS 包就是那个账号的又一台设备——测试存档换到这个路径、或者测试局改了这个路径下的角色，都会被传进真实云端。
# 做法：每一局都带 DM_TEST_ICLOUD_OFF=1（cloud_ios.m：整个不同步）；跑完再核启动日志，出现任何同步动作就报红。
# 不用「指到一次性本地目录」的办法：那会让本来不同步的测试存档开始同步，同一脚本里前一个实例传上去的角色会被后一个空存档实例下载回来。
#   cloud_guard_env <Documents 目录> [调用方已带的环境，空格分隔的 K=V]
#       设好 CG_ENV（要加给 open 的那一条 K=V；调用方自己指了本地云端目录——iCloud 用例——时为空）。返回非零 = 这一局不许起（已打印红字）。
#   cloud_guard_check <启动日志>     本该不同步的局出现了同步动作 → 返回非零并打印红字
#   cloud_guard_clean <Documents 目录>   留给调用方收尾时调（现在没有要清的东西）
# 真要测真实 iCloud：DM_REAL_ICLOUD=1 且 DM_REAL_ICLOUD_CONFIRM=写入真实云端 两个都给才放开，放开时每局打印红字提醒。
# Master gate for launching the iOS app on this Mac: connecting to real iCloud is not allowed. Sourced by smoke_mac.sh, touch_e2e_mac.sh and uireg/run_mac.sh.
# Why it exists: iCloud sync recognizes the "real save" by path only (DM_SAVE == Documents/save, dm_cloud_boot in cloud_ios.m). When the development machine is signed in to a real account,
# the iOS app running on the Mac is one more device of that account -- a test save swapped into this path, or a test run modifying a character under this path, gets uploaded to the real cloud.
# What it does: every run carries DM_TEST_ICLOUD_OFF=1 (cloud_ios.m: no sync at all); after the run the launch log is checked, and any sync action is reported as a failure.
# The "point it at a one-off local directory" approach is not used: that would make test saves that otherwise do not sync start syncing, and a character uploaded by an earlier instance in the same script would be downloaded back by a later empty-save instance.
#   cloud_guard_env <Documents directory> [environment the caller already carries, space-separated K=V]
#       Sets CG_ENV (the K=V to add to open; empty when the caller specified a local cloud directory itself -- iCloud cases). Non-zero return = this run must not start (red text already printed).
#   cloud_guard_check <launch log>     a sync action appeared in a run that should not sync → returns non-zero and prints red text
#   cloud_guard_clean <Documents directory>   for the caller to call at wrap-up (currently there is nothing to clean)
# To really test real iCloud: it is opened up only when both DM_REAL_ICLOUD=1 and DM_REAL_ICLOUD_CONFIRM=写入真实云端 are given, and when opened up a red reminder is printed for every run.
CG_ENV=; CG_REAL=; CG_LOCAL=
cloud_guard_env() {
  CG_ENV=; CG_REAL=; CG_LOCAL=
  if [ "${DM_REAL_ICLOUD:-}" = 1 ]; then
    [ "${DM_REAL_ICLOUD_CONFIRM:-}" = "写入真实云端" ] || { echo "【红】DM_REAL_ICLOUD=1 还要同时给 DM_REAL_ICLOUD_CONFIRM=写入真实云端 才放开；这一局不起" >&2; return 1; }
    echo "【红】本局连的是真实 iCloud（DM_REAL_ICLOUD=1）：Documents/save 里的改动会同步到这个账号的所有设备" >&2; CG_REAL=1; return 0
  fi
  case " ${2:-} " in
    *" DM_TEST_ICLOUD_SUB="*) echo "【红】DM_TEST_ICLOUD_SUB 走的是真实 iCloud 容器，没有显式放开（DM_REAL_ICLOUD）不许带；这一局不起" >&2; return 1;;
    *" DM_TEST_ICLOUD_DIR="*|*" DM_TEST_ICLOUD_ROOT="*) CG_LOCAL=1; return 0;;   # 调用方自己指了本地目录（iCloud 用例） | The caller specified a local directory itself (iCloud cases)
  esac
  CG_ENV="DM_TEST_ICLOUD_OFF=1"
}
cloud_guard_check() {
  [ "$CG_REAL" = 1 ] || [ "$CG_LOCAL" = 1 ] && return 0   # 显式放开的、调用方自己指了本地目录的：不核 | Explicitly opened up, or the caller specified a local directory itself: not checked
  local bad; bad=$(LC_ALL=C grep -a -E 'iCloud 同步：(启动 |.* 上传（|.* 下载（|.*记删除标记)' "$1" 2>/dev/null | head -1)
  [ -z "$bad" ] && return 0
  echo "【红】这一局本该不同步，却出现了 iCloud 同步动作：$(echo "$bad" | cut -c1-160)。可能已写进真实云端，立刻停下报告，别再起局" >&2; return 1
}
cloud_guard_clean() { return 0; }
