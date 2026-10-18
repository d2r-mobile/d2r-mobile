// #143 WSI 与 app/lifecycle 的集成桥：确保排空发生在旧 Surface 引用被释放之前，且排空失败时保留真实引用。
// 依据 app/lifecycle.c 的 surface_created/surface_destroyed 契约与 dm_wsi 的排空式关闭；纯 C，可宿主测试顺序与失败。
// #143 integration bridge between WSI and app/lifecycle: the drain happens before the old Surface reference is
// released, and a failed drain keeps the real reference. Based on app/lifecycle.c's surface_created/surface_destroyed
// contract and dm_wsi's drain-based close; plain C so host tests can check ordering and failure handling.
#pragma once

#include <stdint.h>

#include "wsi_diagnostic.h"

typedef struct {
    void *ctx;
    int (*surface_created)(void *ctx, void *window);    // 真实 dm_app_lifecycle_surface_created
    int (*surface_destroyed)(void *ctx, void *window);  // 真实 dm_app_lifecycle_surface_destroyed
    void (*trace)(void *ctx, const char *event);        // 仅用于观察顺序；可为 NULL
} dm_wsi_lifecycle_ops;

typedef struct {
    dm_wsi *wsi;
    dm_wsi_lifecycle_ops lifecycle;
    void *window;   // 生命周期当前持有的窗口令牌 | token the lifecycle currently owns
    int accepted;
} dm_wsi_bridge;

void dm_wsi_bridge_init(dm_wsi_bridge *bridge, dm_wsi *wsi, const dm_wsi_lifecycle_ops *lifecycle);

// 新窗口/重绘：窗口不同时先排空旧设备（旧引用仍被生命周期持有），再让生命周期取新引用，最后打开 WSI。
// 返回 1 = 可继续出帧；0 = 拒绝或失败，调用者不得绘制，也不得释放引用。
// New window/redraw: when the token differs, drain the old device first (the lifecycle still owns that reference),
// then let the lifecycle acquire the new one, then open WSI. Returns 1 when frames may continue; 0 rejects or fails,
// and the caller must neither draw nor release a reference.
int dm_wsi_bridge_window_ready(dm_wsi_bridge *bridge, void *window, uint32_t width, uint32_t height);

// 窗口销毁：只有排空成功才调用生命周期销毁；失败保留真实引用并返回 0。
// Window destroyed: the lifecycle release happens only after a successful drain; failure keeps the real reference and returns 0.
int dm_wsi_bridge_window_destroyed(dm_wsi_bridge *bridge, void *window);
// 尺寸变化：走真实 dm_wsi_resize（排程重建），并先校验窗口令牌，避免陈旧回调改到新窗口。
// Size change: goes through the real dm_wsi_resize (schedules a rebuild) after validating the window token, so a
// stale callback cannot resize the current window.
dm_wsi_status dm_wsi_bridge_window_resized(dm_wsi_bridge *bridge, void *window, uint32_t width, uint32_t height);

// 窗口令牌是否是当前已被接受的窗口；所有回调副作用之前必须先问这里。
// Whether the token is the currently accepted window; every callback asks this before any side effect.
int dm_wsi_bridge_owns_window(const dm_wsi_bridge *bridge, const void *window);

typedef enum {
    DM_WSI_TIMER_STOP = 0,   // 停表（失败/等待/暂停/无窗口/未恢复）| stop the timer
    DM_WSI_TIMER_KEEP = 1    // 继续跑帧（必要时由调用者启动）| keep frames running (start if not armed)
} dm_wsi_timer_action;

// 唯一的调度决策：eligibility + 一次 tick + 定时器动作。失败/等待/暂停/无窗口/未恢复都不 tick 且 STOP；
// 每个 tick 结果都在这里处理，因此错误后不会留下持续唤醒。
// The single scheduling decision: eligibility + one tick + timer action. Failed/awaiting/paused/windowless/not-resumed
// states do not tick and return STOP; every tick result is handled here, so an error leaves no recurring wakeups.
dm_wsi_status dm_wsi_bridge_dispatch(dm_wsi_bridge *bridge, int resumed, dm_wsi_timer_action *timer);
