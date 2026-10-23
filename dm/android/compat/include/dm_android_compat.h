// 安卓构建强制包含（CMake -include）：宿主源码里零散用到的苹果 libc 扩展，在 bionic 上给出同名同语义的薄实现。
// 只放「一行能说清、语义完全对得上」的；对不上的（Mach 线程挂起、实时调度策略、GCD、音频）在原文件按 __APPLE__ 分支各自实现，不在这里假装。
// Force-included in the Android build (CMake -include): Apple libc extensions used here and there in the host source, given thin implementations with the same name and semantics on bionic.
// Only things that are "explainable in one line with fully matching semantics" go here; those that do not match (Mach thread suspension, real-time scheduling policy, GCD, audio) are implemented in their own files under __APPLE__ branches, not faked here.
#pragma once
#if defined(__ANDROID__) || defined(__OHOS__)
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

// 苹果的 CLOCK_UPTIME_RAW（睡眠不走的单调钟）= Linux CLOCK_MONOTONIC；CLOCK_MONOTONIC_RAW 两边同名
// Apple's CLOCK_UPTIME_RAW (a monotonic clock that stops during sleep) = Linux CLOCK_MONOTONIC; CLOCK_MONOTONIC_RAW has the same name on both
#ifndef CLOCK_UPTIME_RAW
#define CLOCK_UPTIME_RAW CLOCK_MONOTONIC
#endif
static inline uint64_t clock_gettime_nsec_np(clockid_t c) {
    struct timespec ts; if (clock_gettime(c, &ts)) return 0;
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
#ifndef __OHOS__   // musl 自带 | musl has its own
static inline int getdtablesize(void) { return (int)sysconf(_SC_OPEN_MAX); }
#endif
static inline size_t pthread_get_stacksize_np(pthread_t t) {
    pthread_attr_t a; size_t s = 0;
    if (!pthread_getattr_np(t, &a)) { pthread_attr_getstacksize(&a, &s); pthread_attr_destroy(&a); }
    return s;
}
// 苹果 pthread_get_stackaddr_np 返回栈顶（最高地址）；bionic 的 attr 给的是栈底 + 大小
// Apple's pthread_get_stackaddr_np returns the stack top (highest address); bionic's attr gives the stack bottom + size
static inline void *pthread_get_stackaddr_np(pthread_t t) {
    pthread_attr_t a; void *lo = NULL; size_t s = 0;
    if (!pthread_getattr_np(t, &a)) { pthread_attr_getstack(&a, &lo, &s); pthread_attr_destroy(&a); }
    return (char *)lo + s;
}
// struct stat 的纳秒时间字段：苹果叫 st_mtimespec，Linux 叫 st_mtim
// Nanosecond time field of struct stat: called st_mtimespec on Apple, st_mtim on Linux
#define st_mtimespec st_mtim
#define st_atimespec st_atim
#define st_ctimespec st_ctim

// 存档事务（hle_fs.c）：clonefile 在安卓上是整树拷贝（含 user.* 扩展属性，存档目录只有几 MB），renamex_np(RENAME_SWAP) = renameat2(RENAME_EXCHANGE)
// Save transactions (hle_fs.c): clonefile on Android is a whole-tree copy (including user.* extended attributes; the save directory is only a few MB), renamex_np(RENAME_SWAP) = renameat2(RENAME_EXCHANGE)
int clonefile(const char *src, const char *dst, int flags);   // dm/android/compat/clonefile.c
#ifndef RENAME_SWAP
#define RENAME_SWAP (1u << 1)   // 与 Linux RENAME_EXCHANGE 同值 | Same value as Linux RENAME_EXCHANGE
#endif
static inline int renamex_np(const char *from, const char *to, unsigned flags) {
    return (int)syscall(__NR_renameat2, AT_FDCWD, from, AT_FDCWD, to, flags & RENAME_SWAP ? (1u << 1) : 0u);
}
#endif
