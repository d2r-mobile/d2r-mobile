#!/bin/bash
# 宿主日志红线判据（shotgate 与 accept ② 共用一份关键字表：截图 / 存档绿而宿主日志有错也要红）
# 用法：logred.sh <宿主日志>   打印「行数<TAB>首条」；退出码 0 = 无红线，1 = 有红线，2 = 日志不存在
# 关键字：【红】行、FATAL、看门狗、命令缓冲错误（MTLCommandBufferStatus Error/Fault、CommandBufferCallbackError、Discarded victim）、GPU hang/error/fault/recovery、管线失败
#   不单配 Fault：会命中 nnosGetDefaultUserExceptionStack…
# Host log a failure line-line criterion (shotgate and accept ② share one keyword table: green screenshots / saves with errors in the host log must still fail)
# Usage: logred.sh <host log>   prints "<line count><TAB><first match>"; exit code 0 = no failure line, 1 = failure line found, 2 = log does not exist
# Keywords: 【红】(red) lines, FATAL, watchdog, command buffer errors (MTLCommandBufferStatus Error/Fault, CommandBufferCallbackError, Discarded victim), GPU hang/error/fault/recovery, pipeline failure
#   Fault is not matched on its own: it would hit nnosGetDefaultUserExceptionStack...
LOGRED='【红】|FATAL|看门狗|MTLCommandBufferStatus(Error|Fault)|CommandBufferCallbackError|Discarded \(victim|GPU (hang|error|fault|recovery)|管线.*失败|[Pp]ipeline.*fail'
[ -f "$1" ] || { printf '0\t（日志不存在 %s）\n' "$1"; exit 2; }
n=$(grep -cE "$LOGRED" "$1"); n=${n:-0}
printf '%s\t%s\n' "$n" "$(grep -m1 -E "$LOGRED" "$1" | cut -c1-140)"
[ "$n" = 0 ]
