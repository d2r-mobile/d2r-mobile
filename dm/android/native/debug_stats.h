// 真正出图与资源计数的线程安全快照，供 Android 调试浮层读取。
// Thread-safe snapshots of actual presentation/resource counters for the Android debug overlay.
#pragma once
#include <stdint.h>
typedef struct {
    uint64_t frame;
    double frame_ms,cpu_ms,gpu_ms,wait_ms,draws,passes,submits,snapshot_mb,buffer_mb;
    int gpu_valid;
} dm_android_debug_stats;
void dm_android_debug_get(dm_android_debug_stats *out);
