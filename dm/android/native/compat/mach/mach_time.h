// Darwin 时间接口的等价单调时钟桥接。| Equivalent monotonic-clock bridge for Darwin timing calls.
#pragma once
#include "../../host_posix.h"
typedef struct { unsigned numer, denom; } mach_timebase_info_data_t;
static inline uint64_t mach_absolute_time(void) { return dm_android_clock_ns(CLOCK_MONOTONIC); }
static inline int mach_timebase_info(mach_timebase_info_data_t *info) { info->numer = 1; info->denom = 1; return 0; }
// 绝对期限等待避免相对等待积累漂移。| Absolute deadlines avoid accumulated relative-sleep drift.
static inline int mach_wait_until(uint64_t ns) {
    struct timespec deadline = {ns / 1000000000ull, ns % 1000000000ull};
    int error; do { error = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, NULL); } while (error == EINTR);
    return error;
}
