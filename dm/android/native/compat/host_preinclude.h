// 过渡期桥接上游 Darwin 名称到已实现的 POSIX 适配，仅供 Android 宿主编译。
// Transitional mapping of upstream Darwin names to implemented POSIX adapters, only for Android host builds.
#pragma once
#include "../host_posix.h"
#include "TargetConditionals.h"
#include <errno.h>
#include <sys/xattr.h>
#include <string.h>
static inline int dm_android_pthread_name(const char *name) {
    char short_name[16]; size_t n = strnlen(name, 15); memcpy(short_name, name, n); short_name[n] = 0;
    return pthread_setname_np(pthread_self(), short_name);
}
#define pthread_setname_np(name) dm_android_pthread_name(name)
#define clock_gettime_nsec_np dm_android_clock_ns
#define pthread_get_stackaddr_np dm_android_stack_top
#define pthread_get_stacksize_np dm_android_stack_size
#define getdtablesize dm_android_fd_limit
#define st_mtimespec st_mtim
#define st_atimespec st_atim
#define st_ctimespec st_ctim
#define CLOCK_UPTIME_RAW CLOCK_MONOTONIC
#define CLOCK_MONOTONIC_RAW_APPROX CLOCK_MONOTONIC
#define CLOCK_UPTIME_RAW_APPROX CLOCK_MONOTONIC
#define getxattr dm_android_getxattr
#define setxattr dm_android_setxattr
#define removexattr dm_android_removexattr
#define renamex_np dm_android_rename_exchange
#define RENAME_SWAP 2
#define XATTR_NOFOLLOW 0
#ifndef ENOATTR
#define ENOATTR ENODATA
#endif
