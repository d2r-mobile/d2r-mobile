// Android 宿主的时间、线程栈、存档事务适配。| Android host time, thread stacks, and save transactions.
#pragma once
#include <pthread.h>
#include <stdint.h>
#include <time.h>
#include <sys/types.h>
uint64_t dm_android_clock_ns(clockid_t clock);
uint64_t dm_android_process_start_ns(void);
void *dm_android_stack_top(pthread_t thread);
size_t dm_android_stack_size(pthread_t thread);
int dm_android_fd_limit(void);
int dm_android_fd_path(int fd, char *path, size_t capacity);
int dm_android_read_memory(uintptr_t address, void *destination, size_t size);
enum { DM_ANDROID_READ_SIZE_BUCKETS = 7 };
typedef struct dm_android_read_stats {
    uint64_t calls, requested_bytes, returned_bytes, full, partial, errors;
    uint64_t size_buckets[DM_ANDROID_READ_SIZE_BUCKETS], wall_ns, wall_unavailable;
    uint64_t bucket_requested_bytes[DM_ANDROID_READ_SIZE_BUCKETS];
    uint64_t bucket_returned_bytes[DM_ANDROID_READ_SIZE_BUCKETS];
    uint64_t bucket_wall_ns[DM_ANDROID_READ_SIZE_BUCKETS];
    uint64_t bucket_wall_unavailable[DM_ANDROID_READ_SIZE_BUCKETS];
    uint32_t tid;
} dm_android_read_stats;
// 当前宿主线程的累计快照，不复位、不合并其它线程；full 包含成功的零字节读取。
// Cumulative snapshot of this host thread; never reset or combine other threads; full includes successful zero-byte reads.
// 覆盖本线程调用本 reader 的所有用途，包括非 NVN；不是全进程/全线程总计。
// Cover every use of this reader on this thread, including non-NVN calls; not a process-wide/all-thread total.
// 桶顺序：0、1..64、65..256、257..1024、1025..4096、4097..65536、>65536 字节。
// Bucket order: 0, 1..64, 65..256, 257..1024, 1025..4096, 4097..65536, >65536 bytes.
// 每桶字节/wall 与总计使用同一次实际读取及同一对时钟，逐项求和等于总计。
// Per-bucket bytes/wall use the same actual read and clock pair; their sums equal the totals.
// wall 是实际单调时钟差，包含调用期间被调度出去的时间，不是内核 CPU；失败/倒退另计 unavailable。
// Wall is an actual monotonic-clock delta, including time scheduled out during the call, not kernel CPU; clock failures/regressions count as unavailable.
void dm_android_current_thread_read_stats(dm_android_read_stats *out);
int dm_android_clonefile(const char *source, const char *destination, int flags);
int dm_android_rename_exchange(const char *first, const char *second, unsigned flags);
ssize_t dm_android_getxattr(const char *path, const char *name, void *value, size_t size, unsigned position, int options);
int dm_android_setxattr(const char *path, const char *name, const void *value, size_t size, unsigned position, int options);
int dm_android_removexattr(const char *path, const char *name, int options);
