// #117 窗口呈现复制策略：把自有 RGBA8 读回画布有界地复制进 Android 原生窗口缓冲（RGBA_8888、带行距）。
// 依据 Android ANativeWindow_lock 的 ANativeWindow_Buffer 字段语义；不含 Android/Vulkan 头文件，故可宿主独立测试。
// #117 window presentation copy policy: bounded copy of the owned RGBA8 readback canvas into an Android native
// window buffer (RGBA_8888, with row stride). Based on the ANativeWindow_Buffer field semantics of
// ANativeWindow_lock; no Android/Vulkan headers, so it is independently host-testable.
#pragma once

#include <stddef.h>
#include <stdint.h>

// 源：紧排 RGBA8 画布，capacity 必为字节数。| Source: tight RGBA8 canvas, capacity is a byte count.
typedef struct {
    const uint8_t *pixels;
    uint32_t width;
    uint32_t height;
    size_t capacity;
} dm_present_source;

// 目标：锁定后的 ANativeWindow_Buffer。stride 是行首到行首的像素数，format_rgba8888 来自 WINDOW_FORMAT_RGBA_8888。
// Target: a locked ANativeWindow_Buffer. stride is start-to-start pixels; format_rgba8888 mirrors
// WINDOW_FORMAT_RGBA_8888.
typedef struct {
    uint8_t *bits;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    int format_rgba8888;
} dm_present_target;

typedef struct {
    uint32_t x, y, width, height;
} dm_present_rect;

// 校验格式/行距/容量后清零目标可见区并把源居中裁剪复制进去，返回 1 与实际复制矩形；拒绝时返回 0，
// 目标一个字节都不写，rect 全零。行距中的填充字节既不读也不写。
// After validating format/stride/capacity, clears the visible target area and copies a centred crop, returning 1
// and the copied rectangle; on rejection returns 0 without writing any target byte and leaves rect zeroed.
// Padding inside the row stride is neither read nor written.
int dm_present_copy(const dm_present_source *source, const dm_present_target *target, dm_present_rect *copied);
