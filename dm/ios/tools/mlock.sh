#!/bin/bash
# mini（或本机）量测锁。只有一把：就是 Mac 冒烟、触屏用例、回归局共用的 macsmoke 锁（$DM_ROOT/run/ios/macsmoke.lock），本文件只是它的一层名字。
# 为什么不另设一把：带窗口的宿主一开窗就到前台，别人的 iOS 包窗口被盖住 / 失去前台后系统不再给显示刷新回调，对方整局不出帧。
#   曾有一次逐通道量测拿的是单独的「测量锁」，与另外两路回归局互不相识地并行了约 28 分钟，对方 5 局出秒级长帧、要重跑；
#   同样的事冒烟也撞过一次（那次只改了起局脚本，这把锁没并）。凡是在 mini 上开窗口、要显示刷新的任务都只认这一把。
# 用法不变：source mlock.sh; mlock_acquire "<线名> <在量什么> 约 <时长>"; trap mlock_release EXIT
# 批量脚本自己先取锁（它会 export XT_LOCK_HELD=1），里面逐局调的 xtrace.sh mac 见到这个变量就不再取；排队与「持有者已不在就接手」由 devlock.sh 管。
# Measurement lock for the second Mac (or the local machine). There is only one: the macsmoke lock shared by Mac smoke tests, touchscreen cases and regression runs ($DM_ROOT/run/ios/macsmoke.lock); this file is just another name layered over it.
# Why not set up a separate one: a windowed host comes to the foreground as soon as it opens its window, and once someone else's iOS app window is covered / loses the foreground the system stops delivering display refresh callbacks to it, so the other side outputs no frames for the whole run.
#   Once, a per-pass measurement took a separate "measurement lock" and ran in parallel with two other regression lines for about 28 minutes, neither aware of the other; 5 of the other side's runs got second-long frames and had to be rerun;
#   the smoke test also hit the same thing once (that time only the run launcher script was changed, and this lock was not merged in). Any task that opens a window on the second Mac and needs display refresh honors only this one lock.
# Usage unchanged: source mlock.sh; mlock_acquire "<line name> <what is being measured> approx <duration>"; trap mlock_release EXIT
# Batch scripts take the lock themselves first (it exports XT_LOCK_HELD=1), and xtrace.sh mac invoked per run inside sees this variable and does not take it again; queuing and "take over if the holder is gone" are handled by devlock.sh.
mlock_acquire() {
  [ "${XT_LOCK_HELD:-}" = 1 ] && return 0
  export DM_DEVLOCK_DIR=${XT_LOCK_DIR:-${DM_ROOT:-$HOME/dm-work}/run/ios/macsmoke.lock}
  source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/devlock.sh" || { echo "🔴 mlock：同目录下没有 devlock.sh"; return 1; }
  devlock_acquire "$1" || return 1
  export XT_LOCK_HELD=1; MLOCK_MINE=1
}
mlock_release() { [ "${MLOCK_MINE:-}" = 1 ] || return 0; devlock_release; MLOCK_MINE=; unset XT_LOCK_HELD; }
