// #115 有界 indexed+texture 离屏绘制：固定 float2 position + float2 UV、uint16 index、单个 RGBA8 2D 纹理。
// #115 bounded indexed+textured offscreen draw: fixed float2 position + float2 UV, uint16 indices, one RGBA8 2D texture.
#pragma once
#include "vk_host.h"
#include "vk_graphics_interface.h"

#define DM_VK_INDEXED_TEXTURED_VERTEX_STRIDE 16u
#define DM_VK_INDEXED_TEXTURED_MAX_VERTICES 65536u
#define DM_VK_INDEXED_TEXTURED_MAX_INDICES (DM_VK_INDEXED_TEXTURED_MAX_VERTICES * 3u)

typedef struct {
    float position[2];   // NDC，有限且 [-1,1] | finite NDC in [-1,1]
    float uv[2];         // 有限且 [0,1] | finite in [0,1]
} dm_vk_indexed_textured_vertex;

typedef struct {
    const dm_vk_indexed_textured_vertex *vertices;
    uint32_t vertex_count;
    uint32_t vertex_stride;     // 必须等于 sizeof(dm_vk_indexed_textured_vertex)=16
    const uint16_t *indices;
    uint32_t index_count;       // 必须是 3 的倍数
    const uint8_t *texture_pixels;
    size_t texture_size;        // 必须精确等于 texture_width*texture_height*4
    uint32_t texture_width;
    uint32_t texture_height;
    const char *vertex_spv;
    const char *fragment_spv;
} dm_vk_indexed_textured_draw;

// 固定 nearest/clamp 单级 RGBA8 采样器；借用输入同步复制。失败不写画布、不释放调用者指针。
// Fixed nearest/clamp single-level RGBA8 sampler; borrowed inputs are copied synchronously. Failure never writes the
// canvas and never frees caller pointers.
VkResult dm_vk_draw_indexed_textured_offscreen(dm_vk *vk, const dm_vk_indexed_textured_draw *draw, dm_vk_pixels *pixels);
// 用已校验的纹理 plan-owned shader、UV location 和 sampler binding 构建实际 pipeline。
// Build the actual pipeline from a checked textured plan's owned shaders, UV location and sampler binding.
VkResult dm_vk_draw_indexed_textured_plan_offscreen(dm_vk *vk, const dm_vk_indexed_textured_draw *draw,
                                                     const dm_vk_graphics_plan *plan, dm_vk_pixels *pixels);
