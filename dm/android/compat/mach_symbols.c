// 需要真符号（不是头文件里的内联）的两个苹果函数：汇编（nvn_entry.S 的计时入口）直接 bl _mach_absolute_time；hle_nvn.c 按 extern 声明调 thread_selfcounts。
// 本文件不包含 compat 的 mach_time.h，免得与那里的 static inline 同名冲突。
// Two Apple functions that need real symbols (not the inlines in headers): assembly (the timing entry in nvn_entry.S) does bl _mach_absolute_time directly; hle_nvn.c calls thread_selfcounts through an extern declaration.
// This file does not include compat's mach_time.h, to avoid clashing with the static inline of the same name there.
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

uint64_t mach_absolute_time(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
// 本线程累计 {指令数, 周期数}：苹果 libSystem 私有接口。安卓应用进程不许用硬件性能计数器（perf_event_paranoid），如实返回不可用、读数 0
// Cumulative {instructions, cycles} of this thread: a private Apple libSystem interface. Android app processes are not allowed hardware performance counters (perf_event_paranoid), so report unavailable honestly with readings of 0
int thread_selfcounts(int type, void *buf, size_t nbytes) { (void)type; memset(buf, 0, nbytes); return -1; }
