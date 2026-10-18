// #143 WSI 生命周期桥实现：顺序固定为"排空旧设备 → 生命周期取新引用 → 打开 WSI"与"排空 → 生命周期释放"。
// 依据 app/lifecycle.c 的引用所有权；任何排空失败都保留窗口令牌与设备所有权，由调用者传播错误。
// #143 bridge implementation with the fixed order "drain old device -> lifecycle acquires -> open WSI" and
// "drain -> lifecycle releases". Based on app/lifecycle.c's reference ownership; any failed drain keeps the window
// token and the device ownership so the caller can propagate the error.
#include "wsi_bridge.h"

#include <stddef.h>

static void trace(const dm_wsi_bridge *bridge, const char *event) {
    if (bridge->lifecycle.trace) bridge->lifecycle.trace(bridge->lifecycle.ctx, event);
}

void dm_wsi_bridge_init(dm_wsi_bridge *bridge, dm_wsi *wsi, const dm_wsi_lifecycle_ops *lifecycle) {
    if (!bridge) return;
    bridge->wsi = wsi;
    bridge->window = NULL;
    bridge->accepted = 0;
    if (lifecycle) bridge->lifecycle = *lifecycle;
    else bridge->lifecycle = (dm_wsi_lifecycle_ops){0};
}

int dm_wsi_bridge_window_ready(dm_wsi_bridge *bridge, void *window, uint32_t width, uint32_t height) {
    if (!bridge || !bridge->wsi || !window || !width || !height) return 0;
    if (bridge->window && bridge->window != window && bridge->accepted) {
        // 旧窗口仍被生命周期持有：必须先排空，否则 GPU 对象会引用已释放的窗口。
        // The lifecycle still owns the old window: drain first, otherwise GPU objects would reference a released window.
        if (dm_wsi_window_lost(bridge->wsi, bridge->window) == DM_WSI_ERROR) return 0;
        trace(bridge, "drain-old");
    }
    if (!bridge->lifecycle.surface_created) return 0;
    if (!bridge->lifecycle.surface_created(bridge->lifecycle.ctx, window)) return 0;
    trace(bridge, "lifecycle-acquire");
    bridge->window = window;
    bridge->accepted = 1;
    const dm_wsi_status status = dm_wsi_window_ready(bridge->wsi, window, width, height);
    trace(bridge, status == DM_WSI_OK ? "wsi-open" : "wsi-open-failed");
    return status == DM_WSI_OK;
}

int dm_wsi_bridge_window_destroyed(dm_wsi_bridge *bridge, void *window) {
    if (!bridge || !bridge->wsi || !bridge->accepted || bridge->window != window) return 0;
    if (dm_wsi_window_lost(bridge->wsi, window) == DM_WSI_ERROR) {
        // 排空未证明：保留真实 ANativeWindow 引用（不调用 lifecycle 销毁）。| Drain unproven: keep the real reference.
        trace(bridge, "drain-failed-retain");
        return 0;
    }
    trace(bridge, "drain-ok");
    if (bridge->lifecycle.surface_destroyed) bridge->lifecycle.surface_destroyed(bridge->lifecycle.ctx, window);
    trace(bridge, "lifecycle-release");
    bridge->window = NULL;
    bridge->accepted = 0;
    return 1;
}

dm_wsi_status dm_wsi_bridge_window_resized(dm_wsi_bridge *bridge, void *window, uint32_t width, uint32_t height) {
    if (!bridge || !bridge->wsi) return DM_WSI_IDLE;
    // 陈旧窗口的尺寸变化不得作用于当前窗口。| A stale window's resize must not affect the current window.
    if (!dm_wsi_bridge_owns_window(bridge, window)) return DM_WSI_IDLE;
    const dm_wsi_status status = dm_wsi_resize(bridge->wsi, width, height);
    trace(bridge, "wsi-resize");
    return status;
}

int dm_wsi_bridge_owns_window(const dm_wsi_bridge *bridge, const void *window) {
    return bridge && bridge->accepted && window && bridge->window == window;
}

dm_wsi_status dm_wsi_bridge_dispatch(dm_wsi_bridge *bridge, int resumed, dm_wsi_timer_action *timer) {
    if (timer) *timer = DM_WSI_TIMER_STOP;
    if (!bridge || !bridge->wsi) return DM_WSI_IDLE;
    dm_wsi *wsi = bridge->wsi;
    if (!wsi->enabled || !bridge->accepted || !bridge->window) return (wsi->last = DM_WSI_IDLE);
    if (!resumed || wsi->paused) return (wsi->last = DM_WSI_IDLE);
    if (wsi->awaiting_surface) return (wsi->last = DM_WSI_IDLE);
    // 终态错误：不 tick、不进设备，保持 STOP（错误本身仍由调用者记录/传播）。
    // Sticky failure: no tick, no device work, STOP retained (the caller still logs/propagates the error).
    if (wsi->error == DM_WSI_UNSUPPORTED) return (wsi->last = DM_WSI_UNSUPPORTED);
    if (wsi->error) return (wsi->last = DM_WSI_ERROR);
    const dm_wsi_status status = dm_wsi_tick(wsi);
    if (status == DM_WSI_OK || status == DM_WSI_SUBOPTIMAL || status == DM_WSI_REBUILD) {
        if (timer) *timer = DM_WSI_TIMER_KEEP;
    } else {
        if (timer) *timer = DM_WSI_TIMER_STOP;   // ERROR/UNSUPPORTED/SURFACE_LOST/IDLE
    }
    return status;
}
