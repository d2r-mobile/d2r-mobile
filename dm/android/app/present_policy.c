// #117 窗口呈现复制：格式/行距/容量/溢出预检，居中裁剪，只清零可见宽度内的行。
// 依据 ANativeWindow_lock 返回缓冲的 row stride 语义与仓储现有 256×256 诊断呈现行为；纯 C11，无平台依赖。
// #117 window presentation copy: format/stride/capacity/overflow preflight, centred crop, and rows cleared only
// across the visible width. Based on the row-stride semantics of ANativeWindow_lock buffers and the repository's
// existing 256x256 diagnostic presentation; plain C11 with no platform dependency.
#include "present_policy.h"

#include <string.h>

int dm_present_copy(const dm_present_source *source, const dm_present_target *target, dm_present_rect *copied) {
    if (copied) memset(copied, 0, sizeof *copied);
    if (!source || !target || !copied || !source->pixels || !target->bits) return 0;
    if (!target->format_rgba8888) return 0;
    if (!source->width || !source->height || !target->width || !target->height) return 0;
    if (target->stride < target->width) return 0;
    // 64 位预检：源紧排字节数与目标整缓冲字节数都不得在 size_t 里溢出。
    // 64-bit preflight: neither the tight source bytes nor the whole target buffer may overflow size_t.
    if ((uint64_t)source->width > (uint64_t)SIZE_MAX / 4u / (uint64_t)source->height) return 0;
    const size_t required = (size_t)source->width * source->height * 4u;
    if (source->capacity < required) return 0;
    if ((uint64_t)target->stride * 4u > (uint64_t)SIZE_MAX / (uint64_t)target->height) return 0;

    const uint32_t copy_w = source->width < target->width ? source->width : target->width;
    const uint32_t copy_h = source->height < target->height ? source->height : target->height;
    const uint32_t src_x = (source->width - copy_w) / 2u;
    const uint32_t src_y = (source->height - copy_h) / 2u;
    const uint32_t dst_x = (target->width - copy_w) / 2u;
    const uint32_t dst_y = (target->height - copy_h) / 2u;

    // 只清零可见宽度；stride 中超过 width 的填充保持平台原值。| Clear only the visible width; stride padding keeps the platform bytes.
    for (uint32_t y = 0; y < target->height; y++)
        memset(target->bits + (size_t)y * target->stride * 4u, 0, (size_t)target->width * 4u);
    for (uint32_t y = 0; y < copy_h; y++)
        memcpy(target->bits + ((size_t)(dst_y + y) * target->stride + dst_x) * 4u,
               source->pixels + ((size_t)(src_y + y) * source->width + src_x) * 4u,
               (size_t)copy_w * 4u);

    copied->x = dst_x;
    copied->y = dst_y;
    copied->width = copy_w;
    copied->height = copy_h;
    return 1;
}
