// #112 有界 typed vertex/index 离屏绘制：固定 float2 position + float4 color、uint16 index、triangle list。
// #112 bounded typed vertex/index offscreen draw: fixed float2 position + float4 color, uint16 indices, triangle list.
#pragma once
#include "vk_host.h"

#define DM_VK_INDEXED_VERTEX_STRIDE 24u
#define DM_VK_INDEXED_MAX_VERTICES 65536u
#define DM_VK_INDEXED_MAX_INDICES (DM_VK_INDEXED_MAX_VERTICES * 3u)

typedef struct {
    float position[2];   // NDC，有限且 [-1,1] | finite NDC in [-1,1]
    float color[4];      // RGBA，有限且 [0,1] | finite RGBA in [0,1]
} dm_vk_indexed_vertex;

typedef struct {
    const dm_vk_indexed_vertex *vertices;
    uint32_t vertex_count;
    uint32_t vertex_stride;     // 必须等于 sizeof(dm_vk_indexed_vertex)=24
    const uint16_t *indices;
    uint32_t index_count;       // 必须是 3 的倍数
    const char *vertex_spv;
    const char *fragment_spv;
} dm_vk_indexed_draw;

// 校验失败在创建任何 Vulkan 对象之前返回；成功时调用者仍拥有输入和输出，函数不保留/释放调用者指针。
// Validation failures return before any Vulkan object exists. On success the caller still owns inputs and output;
// the function never retains or frees caller pointers. Borrowed vertex/index bytes are copied before submission.
VkResult dm_vk_draw_indexed_offscreen(dm_vk *vk, const dm_vk_indexed_draw *draw, dm_vk_pixels *pixels);
