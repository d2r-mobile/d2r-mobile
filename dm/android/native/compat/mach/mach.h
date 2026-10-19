// 过渡内存读取接口：只读自身进程，失败返回错误，不直接解引用未知地址。
// Transitional memory reading: read only this process, report failures, never dereference unknown addresses directly.
#pragma once
#include "../../host_posix.h"
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/mman.h>
typedef uintptr_t vm_address_t;
typedef size_t vm_size_t;
typedef int kern_return_t;
typedef unsigned mach_msg_type_number_t;
typedef unsigned mach_port_t;
enum { KERN_SUCCESS = 0, KERN_INVALID_ADDRESS = 1, KERN_NOT_SUPPORTED = 46 };
typedef struct { int seconds, microseconds; } time_value_t;
typedef struct { time_value_t user_time, system_time; } thread_basic_info_data_t;
typedef thread_basic_info_data_t *thread_info_t;
typedef struct { uint64_t phys_footprint, resident_size, virtual_size; } task_vm_info_data_t;
typedef task_vm_info_data_t *task_info_t;
typedef struct { uint64_t __x[29], __fp, __lr, __sp, __pc; uint32_t __cpsr; } arm_thread_state64_t;
typedef arm_thread_state64_t *thread_state_t;
typedef mach_port_t *thread_act_array_t;
enum { THREAD_BASIC_INFO = 3, THREAD_BASIC_INFO_COUNT = sizeof(thread_basic_info_data_t) / 4,
       TASK_VM_INFO = 22, TASK_VM_INFO_COUNT = sizeof(task_vm_info_data_t) / 4,
       ARM_THREAD_STATE64 = 6, ARM_THREAD_STATE64_COUNT = sizeof(arm_thread_state64_t) / 4 };
#define __darwin_arm_thread_state64_get_fp(state) ((state).__fp)
#define __darwin_arm_thread_state64_get_lr(state) ((state).__lr)
#define __darwin_arm_thread_state64_get_sp(state) ((state).__sp)
#define __darwin_arm_thread_state64_get_pc(state) ((state).__pc)
static inline mach_port_t mach_task_self(void) { return getpid(); }
static inline mach_port_t mach_thread_self(void) { return gettid(); }
static inline mach_port_t pthread_mach_thread_np(pthread_t thread) { return pthread_gettid_np(thread); }
static inline kern_return_t vm_read_overwrite(mach_port_t task, vm_address_t source, vm_size_t size, vm_address_t destination, vm_size_t *copied) {
    *copied = 0;
    if (task != mach_task_self() || dm_android_read_memory(source, (void *)destination, size)) return KERN_INVALID_ADDRESS;
    *copied = size; return KERN_SUCCESS;
}
static inline kern_return_t task_info(mach_port_t task, int flavor, task_info_t info, mach_msg_type_number_t *count) {
    if (task != mach_task_self() || flavor != TASK_VM_INFO || *count < TASK_VM_INFO_COUNT) return KERN_NOT_SUPPORTED;
    FILE *file = fopen("/proc/self/statm", "r"); if (!file) return KERN_NOT_SUPPORTED;
    unsigned long virtual_pages, resident_pages;
    int got = fscanf(file, "%lu %lu", &virtual_pages, &resident_pages); fclose(file);
    if (got != 2) return KERN_NOT_SUPPORTED;
    uint64_t page = sysconf(_SC_PAGESIZE);
    info->phys_footprint = info->resident_size = resident_pages * page;
    info->virtual_size = virtual_pages * page; *count = TASK_VM_INFO_COUNT; return KERN_SUCCESS;
}
static inline kern_return_t thread_info(mach_port_t thread, int flavor, thread_info_t info, mach_msg_type_number_t *count) {
    if (flavor != THREAD_BASIC_INFO || *count < THREAD_BASIC_INFO_COUNT) return KERN_NOT_SUPPORTED;
    char path[128], line[4096]; snprintf(path, sizeof path, "/proc/self/task/%u/stat", thread);
    FILE *file = fopen(path, "r"); if (!file) return KERN_NOT_SUPPORTED;
    char *got = fgets(line, sizeof line, file); fclose(file);
    char *end = got ? strrchr(line, ')') : NULL; if (!end) return KERN_NOT_SUPPORTED;
    char *save = NULL, *token = strtok_r(end + 1, " ", &save); uint64_t ticks[2] = {0}; unsigned found = 0;
    for (unsigned field = 3; token && field <= 15; ++field, token = strtok_r(NULL, " ", &save)) {
        if (field == 14 || field == 15) { ticks[field - 14] = strtoull(token, NULL, 10); found++; }
    }
    long rate = sysconf(_SC_CLK_TCK); if (rate <= 0 || found != 2) return KERN_NOT_SUPPORTED;
    info->user_time = (time_value_t){ticks[0] / rate, (ticks[0] % rate) * 1000000 / rate};
    info->system_time = (time_value_t){ticks[1] / rate, (ticks[1] % rate) * 1000000 / rate};
    *count = THREAD_BASIC_INFO_COUNT; return KERN_SUCCESS;
}
static inline kern_return_t task_threads(mach_port_t task, thread_act_array_t *threads, mach_msg_type_number_t *count) {
    if (task != mach_task_self()) return KERN_NOT_SUPPORTED;
    DIR *directory = opendir("/proc/self/task"); if (!directory) return KERN_NOT_SUPPORTED;
    mach_port_t *values = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (values == MAP_FAILED) { closedir(directory); return KERN_NOT_SUPPORTED; }
    struct dirent *entry; unsigned n = 0;
    while ((entry = readdir(directory)) && n < 4096 / sizeof *values) {
        if (entry->d_name[0] >= '0' && entry->d_name[0] <= '9') values[n++] = strtoul(entry->d_name, NULL, 10);
    }
    closedir(directory); *threads = values; *count = n; return KERN_SUCCESS;
}
static inline kern_return_t vm_deallocate(mach_port_t task, vm_address_t address, vm_size_t size) {
    return task == mach_task_self() && !munmap((void *)address, size) ? KERN_SUCCESS : KERN_INVALID_ADDRESS;
}
static inline kern_return_t mach_port_deallocate(mach_port_t task, mach_port_t port) { (void)task; (void)port; return KERN_SUCCESS; }
// 其他线程寄存器暂停/读取尚未移植；明确返回不支持，仅影响诊断。
// Suspending/reading another thread's registers is not ported; report unsupported, affecting diagnostics only.
static inline kern_return_t thread_suspend(mach_port_t thread) { (void)thread; return KERN_NOT_SUPPORTED; }
static inline kern_return_t thread_resume(mach_port_t thread) { (void)thread; return KERN_NOT_SUPPORTED; }
static inline kern_return_t thread_get_state(mach_port_t thread, int flavor, thread_state_t state, mach_msg_type_number_t *count) {
    (void)thread; (void)flavor; (void)state; (void)count; return KERN_NOT_SUPPORTED;
}
