#!/bin/bash
# 画面闸门现场的自清：shotgate_gc.sh [根目录] [天数]。删根目录下「超过 N 天没动过」的一级结果目录（名字形如 月日_时分秒…）和 green/ 里同样旧的小结；
# 标了 KEEP 的（宿主截图失败 / 看门狗的现场）不删；想长期留某个现场（文档里引用了它）就在目录里放一个空文件 PIN。缺省根 $DM_ROOT/run/verify/dm/shotgate，缺省 3 天（SHOTGATE_KEEP_DAYS）。shotgate.sh 每次结束时调一次。
# 来由：这里攒了 956 个目录 34 GB 没人清，磁盘两次到线。
# Self-cleanup of screenshot gate artifacts: shotgate_gc.sh [root dir] [days]. Deletes first-level result directories under the root that have "not been touched for more than N days" (named like MMDD_HHMMSS...) and equally old summaries in green/;
# those marked KEEP (artifacts of a failed host screenshot / watchdog) are not deleted; to keep some artifacts long term (a document references them), put an empty file PIN in the directory. Default root $DM_ROOT/run/verify/dm/shotgate, default 3 days (SHOTGATE_KEEP_DAYS). shotgate.sh calls it once at the end of every run.
# Background: 956 directories and 34 GB piled up here with nobody cleaning, and the disk hit the limit twice.
ROOT=${1:-${DM_ROOT:-$HOME/dm-work}/run/verify/dm/shotgate}; DAYS=${2:-${SHOTGATE_KEEP_DAYS:-3}}
case "$ROOT" in */shotgate|*/shotgate/) ;; *) echo "shotgate_gc：根目录不是 shotgate（${ROOT}），不动"; exit 2;; esac
case "$DAYS" in ''|*[!0-9]*) echo "shotgate_gc：天数不是整数（${DAYS}），不动"; exit 2;; esac
[ -d "$ROOT" ] || exit 0
n=0
while IFS= read -r d; do
  [ -d "$d" ] && [ ! -L "$d" ] && [ ! -e "$d/KEEP" ] && [ ! -e "$d/PIN" ] || continue
  rm -rf -- "$d" && n=$((n+1))
done < <(find "$ROOT" -mindepth 1 -maxdepth 1 -type d -name '[0-9][0-9][0-9][0-9]_*' -mtime +"$DAYS" 2>/dev/null)
[ -d "$ROOT/green" ] && find "$ROOT/green" -maxdepth 1 -type f -name '*.html' -mtime +"$DAYS" -delete 2>/dev/null
[ $n -gt 0 ] && echo "shotgate_gc：清掉 $n 个超过 $DAYS 天的旧现场"
exit 0
