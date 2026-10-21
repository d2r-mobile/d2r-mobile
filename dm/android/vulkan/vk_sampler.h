// #13：NVN 采样器 → Vulkan 采样器（有界子集）。参数以显式拷贝传入，Vulkan 层不持有 guest 对象指针。
// #13: NVN sampler -> Vulkan sampler (bounded subset). Parameters are passed by explicit copy; the Vulkan layer never
// holds a pointer to a guest object.
//
// 支持的子集 | Supported subset:
//   · min/mag filter：nearest / linear；mipmap：nearest / linear；NVN min filter 0/1（非 mipmap）按 Metal 的
//     NotMipmapped 语义把 lod_max 固定为 0。
//   · 寻址：repeat、mirrored repeat、clamp to edge（NVN 0 与 7 都按 Metal 的 clamp-to-edge）；mirror clamp 与
//     clamp-to-border 明确不支持。
//   · LOD：有限值，lod_min ≤ lod_max，bias 在设备 maxSamplerLodBias 内。
//   · 各向异性：本切片只接受 1.0；> 1 需要设备已启用 samplerAnisotropy（本层不启用），按 FEATURE_NOT_PRESENT 拒绝。
//   · 比较采样（cmp_mode != 0）未实现，明确拒绝。
//   · 非法枚举/NaN/Inf/越界不修改 out、不创建资源。
//   · 采样绘制只接受 dm_vk_texture_upload 产出的 2D 单层 RGBA8 UNORM 视图；数组/立方/其它格式在创建 GPU 对象前拒绝。
//     The sampled draw accepts only the 2D single-layer RGBA8 UNORM view produced by dm_vk_texture_upload; array/cube/
//     other formats are rejected before any GPU object exists.
//   · 非 mipmap 的 NVN min filter 0/1：Metal 的 NotMipmapped 只采样 level 0，因此有效 LOD 恒为 0（NVN 的正 lod_min
//     也被定义为 0），这只是映射结论，单级夹具不构成 LOD 像素证明。
//     Non-mipmapped NVN min filters 0/1: Metal's NotMipmapped samples level 0, so the effective LOD is always 0 (a
//     positive NVN lod_min is defined as 0). This is a mapping result only; the single-level fixture is not LOD proof.
//   · min/mag filter, mipmap and address modes above; anisotropy only 1.0 (feature is never enabled); comparison sampling
//     is unsupported; invalid enums/NaN/Inf/limits leave out untouched and allocate nothing.
#ifndef DM_VK_SAMPLER_H
#define DM_VK_SAMPLER_H

#include "vk_host.h"

typedef enum { DM_VK_FILTER_NEAREST = 0, DM_VK_FILTER_LINEAR = 1 } dm_vk_filter;
typedef enum { DM_VK_MIP_NEAREST = 0, DM_VK_MIP_LINEAR = 1 } dm_vk_mip;
typedef enum {
    DM_VK_ADDRESS_REPEAT = 0,
    DM_VK_ADDRESS_MIRRORED_REPEAT = 1,
    DM_VK_ADDRESS_CLAMP_TO_EDGE = 2,
    DM_VK_ADDRESS_CLAMP_TO_BORDER = 3,        // 描述里存在但 create 拒绝 | representable, rejected by create
    DM_VK_ADDRESS_MIRROR_CLAMP_TO_EDGE = 4    // 同上 | same
} dm_vk_address;

typedef struct {
    dm_vk_filter min_filter, mag_filter;
    dm_vk_mip mipmap;
    dm_vk_address address_u, address_v, address_w;
    float lod_min, lod_max, lod_bias;
    float max_aniso;                 // 必须为 1.0 | must be 1.0
    int compare_enable;              // 必须为 0 | must be 0
    int has_border;                  // 仅 clamp_to_border 时可为 1；该模式本身被拒绝 | only for border clamp, itself rejected
    float border[4];
} dm_vk_sampler_desc;

typedef struct { VkSampler handle; } dm_vk_sampler;

// 失败时不修改 out 且不留下 VkSampler。 | On failure out is untouched and no VkSampler is left behind.
VkResult dm_vk_sampler_create(dm_vk *vk, const dm_vk_sampler_desc *desc, dm_vk_sampler *out);
void dm_vk_sampler_destroy(dm_vk *vk, dm_vk_sampler *sampler);

// NVN samp_par（nvn_obj.h）的显式拷贝视图：调用者按字段复制，Vulkan 层不引用 guest 内存。
// Explicit copy view of NVN samp_par (nvn_obj.h): the caller copies fields; the Vulkan layer does not reference guest memory.
typedef struct {
    int minf, magf, ws, wt, wr, cmp_mode, cmp_func;
    float lod_min, lod_max, lod_bias, aniso, border[4];
} dm_vk_nvn_sampler;

// NVN 枚举 → 本切片子集；不支持/未知枚举返回错误而不是默认回退。
// NVN enums -> this slice's subset; unsupported/unknown enums return an error instead of falling back to a default.
VkResult dm_vk_sampler_from_nvn(dm_vk *vk, const dm_vk_nvn_sampler *nvn, dm_vk_sampler *out);

// 自有的采样绘制路径：用 2D 纹理 + 采样器绘制 width×height 的 RGBA8 画布；uv = fragcoord * (1/size) * uv_scale，
// lod 来自 push constant。返回前等待 GPU 完成（采样器可在此后安全销毁）。
// Owned sampled-draw path: draws a width×height RGBA8 canvas from a 2D texture + sampler; uv = fragcoord*(1/size)*uv_scale
// with lod from push constants. Returns after GPU completion, so the sampler may be destroyed afterwards.
VkResult dm_vk_sample_draw(dm_vk *vk, const char *vertex_path, const char *fragment_path,
                           const dm_vk_texture *texture, const dm_vk_sampler *sampler,
                           uint32_t width, uint32_t height, dm_vk_pixels *pixels, float uv_scale, float lod);

#endif
