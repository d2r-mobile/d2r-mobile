// Android 没有 Mach 实时策略；明确报告不支持。| Android has no Mach real-time policy; explicitly report unsupported.
#pragma once
#include "mach.h"
typedef int boolean_t;
typedef int *thread_policy_t;
typedef struct { uint32_t period, computation, constraint; boolean_t preemptible; } thread_time_constraint_policy_data_t;
typedef struct { int pth_curpri, pth_priority, pth_policy; } thread_extended_info_data_t;
enum { THREAD_TIME_CONSTRAINT_POLICY = 2, THREAD_TIME_CONSTRAINT_POLICY_COUNT = 4,
       THREAD_EXTENDED_INFO = 5, THREAD_EXTENDED_INFO_COUNT = 3 };
static inline kern_return_t thread_policy_set(mach_port_t thread, int flavor, thread_policy_t policy, unsigned count) {
    (void)thread; (void)flavor; (void)policy; (void)count; return KERN_NOT_SUPPORTED;
}
static inline kern_return_t thread_policy_get(mach_port_t thread, int flavor, thread_policy_t policy, unsigned *count, boolean_t *def) {
    (void)thread; (void)flavor; (void)policy; (void)count; *def = 1; return KERN_NOT_SUPPORTED;
}
