#!/bin/bash
# 计时轮次的并发检测（规矩：有并发的计时轮次一律作废）。
# 机理：Apple GPU 按利用率调频。别的进程同时用 GPU 会把频率抬高，被测进程反而变快
# （选角页串行化计时：单独三局 5.89–6.02 ms，旁边并发 2.9 个宿主时 3.11 ms），读数不可比。
# 用法：conc_watch.sh <被测 pid> <记录文件> [进程名=dmhost]
#   在被测进程起来后立即调用（它在前台阻塞到被测进程退出）；每秒记一行「时刻 别的同名进程数 负载」。
#   结束打一行：CONC OK 采样 N 秒 别的进程 最多 0 → 本轮有效（退出码 0）
#            CONC RED 采样 N 秒 别的进程 最多 M 均 X → 本轮作废（退出码 1）
# 只排除被测 pid 自身；同名的其他进程（含别的工作树起的局）一律算并发。
# Concurrency detection for timed rounds (rule: any timed round with concurrency is void).
# Mechanism: the Apple GPU scales its frequency by utilization. Other processes using the GPU at the same time raise the frequency, and the process under test actually gets faster
# (serialized timing of the character select page: 5.89–6.02 ms for three runs alone, 3.11 ms with 2.9 concurrent hosts alongside), so the readings are not comparable.
# Usage: conc_watch.sh <pid under test> <record file> [process name=dmhost]
#   Call it right after the process under test is up (it blocks in the foreground until the process under test exits); every second it records one line "time, number of other same-name processes, load".
#   At the end it prints one line: CONC OK 采样 N 秒 别的进程 最多 0 (sampled N seconds, other processes max 0) → this round is valid (exit code 0)
#            CONC RED 采样 N 秒 别的进程 最多 M 均 X (sampled N seconds, other processes max M avg X) → this round is void (exit code 1)
# Only the pid under test itself is excluded; all other processes of the same name (including runs started from other worktrees) count as concurrency.
set -u
P=${1:?被测 pid}; F=${2:?记录文件}; NAME=${3:-dmhost}
kill -0 "$P" 2>/dev/null || { echo "CONC RED 被测进程 $P 不存在（起晚了或已退出）"; exit 1; }
: > "$F" || { echo "CONC RED 记录文件 $F 写不了"; exit 1; }
while kill -0 "$P" 2>/dev/null; do
  n=$(ps -A -o pid=,comm= | awk -v me="$P" -v nm="$NAME" '$1 != me { c = $2; sub(".*/", "", c); if (c == nm) k++ } END { print k + 0 }')
  echo "$(date +%s) $n $(sysctl -n vm.loadavg | awk '{print $2}')" >> "$F"
  sleep 1
done
awk -v f="$F" '{ n++; s += $2; if ($2 > m) m = $2 } END {
  if (!n) { print "CONC RED 没采到样本（被测进程不到 1 秒就退出）"; exit 1 }
  if (m > 0) { printf "CONC RED 采样 %d 秒 别的进程 最多 %d 均 %.1f → 本轮作废（%s）\n", n, m, s / n, f; exit 1 }
  printf "CONC OK 采样 %d 秒 别的进程 最多 0 → 本轮有效\n", n }' "$F"
