#ifndef DM_ANDROID_PERFORMANCE_HINT_H
#define DM_ANDROID_PERFORMANCE_HINT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// acquire 等待后开始、真实 present 后结束；仅同窗口/贴图/线程的完整周期可报告。
// Begin after acquire waits, end after real present; report only a complete same-window/image/thread cycle.
void dm_android_performance_frame_begin(const void *window,int index);
void dm_android_performance_frame_end(const void *window,int index);
// 扣除 Vulkan acquire/present 整个调用墙钟（含少量驱动CPU），不扣真实GPU依赖等待。
// Exclude entire Vulkan acquire/present call wall time (including some driver CPU), not GPU dependency waits.
void dm_android_performance_wsi_calls(uint64_t duration_ns);
// 暂停清空周期并关闭会话；恢复后才允许新周期。所有入口由同一互斥锁串行化。
// Pause clears the cycle and closes the session; resume permits new cycles. One mutex serializes all entrypoints.
void dm_android_performance_close(void);
void dm_android_performance_resume(void);

#ifdef __cplusplus
}
#endif

#endif
