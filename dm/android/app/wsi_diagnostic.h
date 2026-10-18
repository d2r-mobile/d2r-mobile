// #143 显式自有 WSI 诊断模式的生命周期控制器：窗口就绪/丢失、暂停/恢复、重建与停机的单一状态机。
// 依据已验收 vk_swapchain（FIFO 清屏提交、maintenance1 完成证明）与 app/lifecycle.c 的 Surface 借用语义；
// 不含 Vulkan/Android 头文件，便于宿主注入真实失败与陈旧回调。
// #143 lifecycle controller for the explicit owned WSI diagnostic mode: the only state machine for window
// readiness/loss, pause/resume, rebuild and stop. Based on the accepted vk_swapchain (FIFO clear submit,
// maintenance1 completion proof) and the Surface borrowing rules of app/lifecycle.c; no Vulkan/Android headers,
// so host tests can inject real failures and stale callbacks.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DM_WSI_OK = 0,          // 一帧已呈现 | a frame was presented
    DM_WSI_SUBOPTIMAL,      // 已呈现，但必须重建 | presented, recreation scheduled
    DM_WSI_REBUILD,         // 需要重建，本 tick 未呈现 | recreation required, nothing presented
    DM_WSI_SURFACE_LOST,    // Surface 丢失，已排空并等待替换 | surface lost, drained, awaiting replacement
    DM_WSI_IDLE,            // 未启用/暂停/无窗口/错误态，未触碰设备 | disabled, paused, windowless or failed
    DM_WSI_UNSUPPORTED,     // 缺 maintenance 能力，显式模式下不回落 | maintenance capability missing, no fallback
    DM_WSI_ERROR            // 其他失败，保持可达所有权 | other failure, ownership retained
} dm_wsi_status;

// 注入边界：全部同步、串行，由 Android 控制线程调用。open 返回 0/UNSUPPORTED/ERROR；close 仅在排空后调用，
// 失败必须保留所有权；frame 内部完成 acquire→clear-submit→present 并返回已翻译的状态。Vulkan 的 acquire/present
// 本身是异步的：链内保存 presentation fence，真正的完成证明由 close（dm_vk_swapchain_destroy）等待后给出，
// 因此"排空"指该证明成功，而不是假设帧内没有任何在途工作。
// Injected boundary, fully synchronous and serialized on the Android control thread. open returns
// 0/UNSUPPORTED/ERROR; close is only called after a drain and must retain ownership on failure; frame performs
// acquire -> clear-submit -> present and returns the translated status. Vulkan acquire/present are themselves
// asynchronous: the chain tracks presentation fences and close (dm_vk_swapchain_destroy) performs the real
// completion proof, so "drained" means that proof succeeded, not that no work is ever in flight inside a frame.
typedef struct {
    void *ctx;
    int (*open)(void *ctx, void *window, uint32_t width, uint32_t height);
    int (*close)(void *ctx);
    int (*frame)(void *ctx);
} dm_wsi_ops;

// 真实打开边界的观察者：只在真正调用 ops.open 之前触发一次，参数是本次请求的尺寸。
// 普通 redraw 走同一窗口快路径、不打开，因此不会触发。
// Observer at the real open boundary: fired once immediately before ops.open with the requested size. An ordinary
// redraw takes the same-window fast path and never opens, so it never fires.
typedef void (*dm_wsi_open_observer)(void *ctx, uint32_t width, uint32_t height);

typedef struct {
    dm_wsi_ops ops;
    int enabled;
    int open;
    int paused;
    int awaiting_surface;   // 丢失后等待替换，期间不自动重开 | after loss, no automatic reopen
    int needs_rebuild;
    int error;              // 最近一次 UNSUPPORTED/ERROR；非 0 时不再绘制 | sticky failure
    void *window;           // 当前借用窗口令牌（只比较，不解引用）| borrowed token, compared only
    uint32_t width, height;
    uint32_t frames, rebuilds, losses;
    dm_wsi_status last;
    dm_wsi_open_observer on_open;   // 只在真实 ops.open 前触发 | fired only before a real ops.open
    void *on_open_ctx;
} dm_wsi;

void dm_wsi_init(dm_wsi *wsi, const dm_wsi_ops *ops, int enabled);

// 注册/清除真实打开边界观察者。| Install or clear the real-open-boundary observer.
void dm_wsi_set_open_observer(dm_wsi *wsi, dm_wsi_open_observer observer, void *ctx);

// 新窗口或同一窗口的重绘：替换窗口先排空旧设备；打开失败进入 UNSUPPORTED/ERROR，不回落 CPU 路径。
// New window or redraw of the same one: replacing drains the old device first; open failures become
// UNSUPPORTED/ERROR with no fallback to the CPU path.
dm_wsi_status dm_wsi_window_ready(dm_wsi *wsi, void *window, uint32_t width, uint32_t height);
// Surface 丢失/销毁：匹配令牌时排空并关闭，然后等待替换；不匹配的陈旧回调被忽略。
// Surface lost/destroyed: on a matching token, drain and close, then await a replacement; stale tokens are ignored.
dm_wsi_status dm_wsi_window_lost(dm_wsi *wsi, void *window);
dm_wsi_status dm_wsi_resize(dm_wsi *wsi, uint32_t width, uint32_t height);
dm_wsi_status dm_wsi_pause(dm_wsi *wsi);
dm_wsi_status dm_wsi_resume(dm_wsi *wsi);

// 至多一帧。未启用、暂停、无窗口、等待替换或错误态一律返回 IDLE 且不调用注入边界（不空转）。
// At most one frame. Disabled, paused, windowless, awaiting a surface or failed states return IDLE without
// touching the injected boundary (no spinning).
dm_wsi_status dm_wsi_tick(dm_wsi *wsi);

// 停机：排空并释放；失败时保留窗口/设备所有权并返回 ERROR 供调用者传播。
// Stop: drain and release; on failure the window/device ownership is retained and ERROR is returned to propagate.
dm_wsi_status dm_wsi_stop(dm_wsi *wsi);

#ifdef __cplusplus
}
#endif
