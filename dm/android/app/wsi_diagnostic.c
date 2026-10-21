// #143 自有 WSI 诊断模式状态机：串行窗口生命周期、重建策略与排空式释放，不隐藏失败。
// 依据 vk_swapchain 的 FIFO/清屏语义与 app/lifecycle.c 的 Surface 借用规则；注入边界在 app/native.c 中用真实
// dm_vk_create_present + dm_vk_swapchain_* 实现。
// #143 owned WSI diagnostic state machine: serialized window lifecycle, recreation policy and drain-based
// release that never hides failures. Based on vk_swapchain's FIFO/clear semantics and the Surface borrowing rules
// of app/lifecycle.c; the injected boundary is implemented in app/native.c with the real
// dm_vk_create_present + dm_vk_swapchain_* calls.
#include "wsi_diagnostic.h"

#include <string.h>

void dm_wsi_init(dm_wsi *wsi, const dm_wsi_ops *ops, int enabled) {
    if (!wsi) return;
    memset(wsi, 0, sizeof *wsi);
    if (ops) wsi->ops = *ops;
    wsi->enabled = enabled ? 1 : 0;
    wsi->last = DM_WSI_IDLE;
}

static dm_wsi_status fail(dm_wsi *wsi, dm_wsi_status status) {
    wsi->error = status;
    wsi->last = status;
    return status;
}

// close waits for asynchronous acquire/submit/present ownership before release; failure retains the device.
static dm_wsi_status drain_close(dm_wsi *wsi) {
    if (!wsi->open) return DM_WSI_OK;
    if (!wsi->ops.close) return fail(wsi, DM_WSI_ERROR);
    if (wsi->ops.close(wsi->ops.ctx) != 0) return fail(wsi, DM_WSI_ERROR);
    wsi->open = 0;
    return DM_WSI_OK;
}

static dm_wsi_status open_window(dm_wsi *wsi) {
    if (!wsi->ops.open) return fail(wsi, DM_WSI_ERROR);
    // 真实打开边界：记录一次尝试与请求尺寸；redraw 快路径不会到这里。
    // The real open boundary: record one attempt with the requested size; the redraw fast path never reaches it.
    if (wsi->on_open) wsi->on_open(wsi->on_open_ctx, wsi->width, wsi->height);
    const int result = wsi->ops.open(wsi->ops.ctx, wsi->window, wsi->width, wsi->height);
    if (result == 0) {
        wsi->open = 1;
        wsi->needs_rebuild = 0;
        wsi->awaiting_surface = 0;
        wsi->error = 0;
        return DM_WSI_OK;
    }
    if (result == DM_WSI_UNSUPPORTED) return fail(wsi, DM_WSI_UNSUPPORTED);
    return fail(wsi, DM_WSI_ERROR);
}

dm_wsi_status dm_wsi_window_ready(dm_wsi *wsi, void *window, uint32_t width, uint32_t height) {
    if (!wsi) return DM_WSI_IDLE;
    if (!wsi->enabled) return (wsi->last = DM_WSI_IDLE);
    if (!window || !width || !height) return fail(wsi, DM_WSI_ERROR);
    // 同一窗口的重绘不重建；尺寸变化必须走重建（回调路径由 dm_wsi_resize 标记）。
    // A redraw of the same window keeps the device; a size change requires a rebuild (the callback path marks it
    // through dm_wsi_resize).
    if (wsi->window == window && wsi->open && !wsi->needs_rebuild && !wsi->error) {
        if (width != wsi->width || height != wsi->height) {
            wsi->width = width;
            wsi->height = height;
            wsi->needs_rebuild = 1;
            return (wsi->last = DM_WSI_REBUILD);
        }
        wsi->width = width;
        wsi->height = height;
        return wsi->last;
    }
    if (wsi->open) {
        const dm_wsi_status drained = drain_close(wsi);
        if (drained != DM_WSI_OK) return drained;
    }
    wsi->window = window;
    wsi->width = width;
    wsi->height = height;
    wsi->awaiting_surface = 0;
    wsi->error = 0;
    return open_window(wsi);
}

dm_wsi_status dm_wsi_window_lost(dm_wsi *wsi, void *window) {
    if (!wsi) return DM_WSI_IDLE;
    // 陈旧/不匹配的窗口回调不得关闭当前设备。| A stale or mismatched window callback must not close the current device.
    if (!wsi->enabled || !window || wsi->window != window) return (wsi->last = DM_WSI_IDLE);
    const dm_wsi_status drained = drain_close(wsi);
    if (drained != DM_WSI_OK) return drained;   /* 所有权保留，等待重试 | retained, caller may retry */
    wsi->window = NULL;
    wsi->awaiting_surface = 1;
    wsi->needs_rebuild = 0;
    return (wsi->last = DM_WSI_OK);
}

dm_wsi_status dm_wsi_resize(dm_wsi *wsi, uint32_t width, uint32_t height) {
    if (!wsi) return DM_WSI_IDLE;
    if (!wsi->enabled) return (wsi->last = DM_WSI_IDLE);
    if (!width || !height) return fail(wsi, DM_WSI_ERROR);
    wsi->width = width;
    wsi->height = height;
    if (wsi->open) wsi->needs_rebuild = 1;
    if (wsi->error) return wsi->last;
    return (wsi->last = wsi->needs_rebuild ? DM_WSI_REBUILD : DM_WSI_IDLE);
}

dm_wsi_status dm_wsi_pause(dm_wsi *wsi) {
    if (!wsi) return DM_WSI_IDLE;
    wsi->paused = 1;
    return (wsi->last = DM_WSI_IDLE);
}

dm_wsi_status dm_wsi_resume(dm_wsi *wsi) {
    if (!wsi) return DM_WSI_IDLE;
    wsi->paused = 0;
    return wsi->last;
}

dm_wsi_status dm_wsi_tick(dm_wsi *wsi) {
    if (!wsi) return DM_WSI_IDLE;
    if (!wsi->enabled) return (wsi->last = DM_WSI_IDLE);
    if (wsi->error == DM_WSI_UNSUPPORTED) return (wsi->last = DM_WSI_UNSUPPORTED);
    // 错误态、暂停、无窗口、等待替换：不触碰设备，也不空转等待。| No device calls and no spin while failed, paused, windowless or awaiting.
    if (wsi->error || wsi->paused || !wsi->window || wsi->awaiting_surface) return (wsi->last = DM_WSI_IDLE);
    if (wsi->needs_rebuild) {
        const dm_wsi_status drained = drain_close(wsi);
        if (drained != DM_WSI_OK) return drained;
        const dm_wsi_status opened = open_window(wsi);
        if (opened != DM_WSI_OK) return opened;
        wsi->rebuilds++;
    } else if (!wsi->open) {
        const dm_wsi_status opened = open_window(wsi);
        if (opened != DM_WSI_OK) return opened;
    }
    if (!wsi->ops.frame) return fail(wsi, DM_WSI_ERROR);
    switch ((dm_wsi_status)wsi->ops.frame(wsi->ops.ctx)) {
    case DM_WSI_OK:
        wsi->frames++;
        return (wsi->last = DM_WSI_OK);
    case DM_WSI_SUBOPTIMAL:
        // 已呈现的 SUBOPTIMAL：本帧有效，下一 tick 重建。| Presented SUBOPTIMAL frame stays valid; rebuild on the next tick.
        wsi->frames++;
        wsi->needs_rebuild = 1;
        return (wsi->last = DM_WSI_SUBOPTIMAL);
    case DM_WSI_REBUILD:
        wsi->needs_rebuild = 1;
        wsi->rebuilds++;
        return (wsi->last = DM_WSI_REBUILD);
    case DM_WSI_SURFACE_LOST: {
        // Surface 丢失：先排空/关闭，再等待替换；关闭失败仍保留所有权并报 ERROR。
        // Surface lost: drain/close first, then await a replacement; a failed close retains ownership and reports ERROR.
        const dm_wsi_status drained = drain_close(wsi);
        wsi->losses++;
        wsi->needs_rebuild = 0;
        if (drained != DM_WSI_OK) return drained;
        wsi->awaiting_surface = 1;
        return (wsi->last = DM_WSI_SURFACE_LOST);
    }
    case DM_WSI_UNSUPPORTED:
        return fail(wsi, DM_WSI_UNSUPPORTED);
    default:
        return fail(wsi, DM_WSI_ERROR);
    }
}

dm_wsi_status dm_wsi_stop(dm_wsi *wsi) {
    if (!wsi) return DM_WSI_IDLE;
    if (!wsi->enabled) return (wsi->last = DM_WSI_IDLE);
    wsi->paused = 1;
    const dm_wsi_status drained = drain_close(wsi);
    if (drained != DM_WSI_OK) return drained;
    wsi->window = NULL;
    wsi->awaiting_surface = 0;
    wsi->needs_rebuild = 0;
    return (wsi->last = DM_WSI_OK);
}

void dm_wsi_set_open_observer(dm_wsi *wsi, dm_wsi_open_observer observer, void *ctx) {
    if (!wsi) return;
    wsi->on_open = observer;
    wsi->on_open_ctx = observer ? ctx : NULL;
}
