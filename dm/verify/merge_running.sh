#!/bin/bash
# 有合入（merge.sh）在跑吗？有则退出码 0 并打持有者，没有则 1。按 merge.sh 登记的 active/<pid>（进程启动时刻）判：
# pid 已死、或 pid 被别的进程复用（启动时刻对不上）都算陈旧并清掉。不按命令行文本匹配。
# 用法：while bash dm/verify/merge_running.sh > /dev/null; do sleep 15; done
# Is a merge (merge.sh) running? If so exits 0 and prints the holder; otherwise 1. Decided by the active/<pid> (process start time) registered by merge.sh:
# a dead pid, or a pid reused by another process (start time mismatch), counts as stale and is cleared. Does not match command line text.
# Usage: while bash dm/verify/merge_running.sh > /dev/null; do sleep 15; done
export LC_ALL=C
ACTIVE=${DM_ROOT:-$HOME/dm-work}/run/verify/dm/merge/active
pid_start() { local loc=C; printf %s "${2:-}" | LC_ALL=C grep -q '[^ -~]' && loc=zh_CN.UTF-8; LC_ALL=$loc ps -o lstart= -p "$1" 2>/dev/null | tr -s ' '; }
live=1
for f in "$ACTIVE"/*; do
  [ -f "$f" ] || continue; p=${f##*/}; s=$(cat "$f" 2>/dev/null)
  case $p in *[!0-9]*|'') continue;; esac
  if kill -0 "$p" 2>/dev/null && [ "$(pid_start "$p" "$s")" = "$s" ]; then echo "合入在跑：pid ${p}（启动 ${s}）"; live=0
  else rm -f "$f"; fi
done
exit $live
