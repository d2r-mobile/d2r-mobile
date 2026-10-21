// #125 自有 VS/PS 图形接口反射：固定 float2 position + float4 color、单个 float4 varying、无 descriptor/push。
// #125 owned VS/PS graphics interface reflection: fixed float2 position + float4 color, one float4 varying, no descriptors/push.
#pragma once
#include "vk_host.h"
#include "vk_indexed.h"

#define DM_VK_GRAPHICS_WHY 192
#define DM_VK_GRAPHICS_MAX_CODE (256u * 1024u)

typedef struct {
    uint32_t position_location;
    uint32_t color_location;
    uint32_t position_components, color_components;
    uint32_t varying_location;
    uint32_t vs_code_bytes, ps_code_bytes;
    uint8_t *vs_code, *ps_code;       // plan 拥有 | owned by plan
    char vs_sha256[65], ps_sha256[65];
    int sidecar_hash_bound;
} dm_vk_graphics_plan;

// metadata_path 可为 NULL；否则要求精确 JSON：
// {"vs_stage":"vs","vs_sha256":"<64hex>","ps_stage":"ps","ps_sha256":"<64hex>"}
// metadata_path may be NULL; otherwise exact JSON as above.
VkResult dm_vk_graphics_plan_load(const char *vs_spv, const char *ps_spv, const char *metadata_path,
                                  dm_vk_graphics_plan *plan, char *why, size_t why_cap);
void dm_vk_graphics_plan_free(dm_vk_graphics_plan *plan);
// 重新扫描 plan 拥有的字节并核对字段/哈希；纯 CPU，不创建 Vulkan 对象。
// Rescan the plan's owned bytes and check fields/hashes; pure CPU, no Vulkan objects.
int dm_vk_graphics_plan_check(const dm_vk_graphics_plan *plan, char *why, size_t why_cap);

// 用已校验的 plan 代码和 location 构建实际 indexed pipeline；不读取 draw 的 shader 路径。
// Build the actual indexed pipeline from the checked plan code and locations; draw shader paths are ignored.
VkResult dm_vk_draw_indexed_plan_offscreen(dm_vk *vk, const dm_vk_indexed_draw *draw,
                                           const dm_vk_graphics_plan *plan, dm_vk_pixels *pixels);
