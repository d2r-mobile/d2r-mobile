// #12：反射驱动的 Vulkan 计算消费方（有界子集）。解析器只接受本文件声明的形态，其余一律明确拒绝。
// #12: reflection-driven Vulkan compute consumer (bounded subset). The parser accepts only the shapes declared here and
// rejects everything else explicitly.
//
// 本切片支持的子集 | Supported subset in this slice:
//   · 单个 OpEntryPoint，执行模型 GLCompute(5)，入口名 "main"，OpExecutionMode LocalSize 恰好一次且目标为该入口；
//   · 描述符全部位于 set 0，每个绑定 count == 1；允许 storage buffer（StorageBuffer 存储类 + Block 结构体）
//     与一个 float32、非深度、普通 2D、非数组、非多重采样的 sampled image（UniformConstant + OpTypeSampledImage），
//     或与一个只含单个 uvec4 成员（Offset 0、16 字节、std140）的只读 Uniform Block 二选一。
//   · 至多一个 push constant 块（PushConstant 存储类，成员 Offset 装饰，总大小 4 字节对齐且 ≤ 128）；
//   · res.json 只用于交叉核对，绝不单独决定绑定：stage、sbufs/textures/cbufs 的 binding/count 必须与模块逐项一致，
//     cbufs 必须带 size 且等于模块推导的 16；images 必须为空，可选 push_constants 与可选 spirv_sha256 必须匹配。
//   · 采样图像只接受 OpTypeSampledImage 包装的普通 2D、非数组、非多重采样、Sampled=1、Sampled Type 为 float32、
//     Depth=0、Image Format=Unknown 的形式；storage image、整数/有符号采样、深度/阴影、数组、MS、非 2D 一律拒绝。
//   · Uniform Block 只接受单成员 uvec4、Offset 0、总大小 16、Block 装饰；数组、其他成员类型、未知 layout 一律拒绝。
//   · 任何不满足上述条件的模块，或模块与 sidecar 不一致，都在创建任何 Vulkan 对象之前失败。
//     A single OpEntryPoint, GLCompute model, "main", exactly one LocalSize for it; all descriptors on set 0 with
//     count == 1, only storage buffers (StorageBuffer storage + Block struct) and either one plain-2D, non-arrayed,
//     non-multisampled float32 non-depth sampled image (Sampled=1, Depth=0, Unknown format) or one read-only Uniform
//     Block with exactly one uvec4 member at Offset 0, size 16, std140. Storage images, integer/depth sampled types,
//     arrays, MS, non-2D, other uniform layouts and mixed sampled+uniform forms are rejected. At most one push-constant
//     block (PushConstant storage, Offset-decorated members, 4-byte aligned, <= 128 bytes). res.json is only
//     cross-checked, never trusted alone. Anything else fails before any Vulkan object is created.
//
// 这不是完整 SPIR-V 验证，也不是 Maxwell/NVN 语义证明；宿主 MoltenVK 通过不等于 Adreno/真机验收。
// This is not a full SPIR-V validator and not proof of Maxwell/NVN semantics; host MoltenVK success is not
// Adreno/device acceptance.
#ifndef DM_VK_SHADER_REFLECT_H
#define DM_VK_SHADER_REFLECT_H

#include "vk_host.h"
#include "vk_sampler.h"

#define DM_VK_REFLECT_ABI "dm-vk-reflect/1"
#define DM_VK_REFLECT_MAX_BINDINGS 8
#define DM_VK_REFLECT_MAX_PUSH 128
#define DM_VK_REFLECT_WHY 192

typedef enum { DM_VK_REFLECT_SBUF = 1, DM_VK_REFLECT_IMAGE = 2, DM_VK_REFLECT_SAMPLED_IMAGE = 3,
               DM_VK_REFLECT_CBUF = 4 } dm_vk_reflect_kind;

typedef struct {
    uint32_t set, binding, count, kind;   // kind = dm_vk_reflect_kind
} dm_vk_reflect_binding;

typedef struct {
    uint32_t local[3];
    uint32_t push_offset, push_size;      // 0/0 = 无 push constant | no push constants
    uint32_t cbuf_offset, cbuf_size;      // 0/0 = 无 uniform block | no uniform block; 本切片为 0/16
    uint32_t binding_count;
    dm_vk_reflect_binding bindings[DM_VK_REFLECT_MAX_BINDINGS];
    uint32_t code_bytes;
    uint32_t code_capacity;               // code 实际拥有的字节数；code_bytes 必须 ≤ 它 | owned bytes; code_bytes must not exceed it
    char code_sha256[65];                 // 模块内容摘要（总是计算） | always computed
    int sidecar_hash_bound;               // 1 = sidecar 带 spirv_sha256 且匹配 | sidecar carried a matching digest
    uint8_t *code;                        // 载入时读取的唯一副本，plan 拥有；执行用同一缓冲 | single owned copy used for execution
} dm_vk_shader_plan;
// 所有权契约 | Ownership contract:
//   · load 成功时 plan 拥有 code（malloc）；用 dm_vk_reflect_free 释放。free 之后结构被清零，重复 free 安全。
//   · 对结构做浅拷贝会共享 code：只能释放其中一个，或让所有副本都不释放；不要对同一 code 调用两次 free。
//   · 重新 load 到同一个 plan 前必须先 free，否则旧 code 泄漏（load 会 memset，不会释放调用者已有指针）。
//   · On success the plan owns code (malloc); release with dm_vk_reflect_free, which zeroes the struct so a repeat free
//     is safe. A shallow copy shares code: free exactly one owner (or neither). Free before reloading into the same plan;
//     load memsets the output and does not free a caller's existing pointer.

// 设备/派发上限（测试可用小值构造；执行时从 VkPhysicalDeviceLimits 填充）。
// Device/dispatch limits (tests may use small values; execution fills this from VkPhysicalDeviceLimits).
typedef struct {
    uint32_t max_workgroup_size[3];
    uint32_t max_invocations;
    uint32_t max_workgroup_count[3];
    uint32_t max_push_constants;
    VkDeviceSize max_storage_range;
    VkDeviceSize max_uniform_range;
} dm_vk_reflect_limits;

// 载入并交叉核对 spv + res.json。失败返回 VK_ERROR_INITIALIZATION_FAILED 并写 why；成功时 plan 完整可用。
// Load and cross-check spv + res.json. Returns VK_ERROR_INITIALIZATION_FAILED with `why` on failure; on success the plan
// is complete and usable.
VkResult dm_vk_reflect_load(const char *spv_path, const char *res_json_path, dm_vk_shader_plan *plan,
                            char *why, size_t why_cap);

// 释放 plan 拥有的模块字节并清零；对零值 plan 安全。 | Free the plan's module bytes and zero it; safe on a zeroed plan.
void dm_vk_reflect_free(dm_vk_shader_plan *plan);

// 纯校验：plan 字段是否仍与其字节一致、是否满足 limits 与 words 个 uint 的 1D 派发。失败写 why。
// Pure check: do the plan fields still match their bytes, and do they fit `limits` for a 1D dispatch of `words` uints?
int dm_vk_reflect_check(const dm_vk_shader_plan *plan, const dm_vk_reflect_limits *limits, uint32_t words,
                        char *why, size_t why_cap);

// 执行：用 plan 拥有的字节建管线，按 plan 的 LocalSize 做 1D 派发并把 words 个 uint 读回画布。
// Execute: build the pipeline from the plan's owned bytes, dispatch 1D by the plan's LocalSize, read back the words.
VkResult dm_vk_reflect_compute(dm_vk *vk, const dm_vk_shader_plan *plan, dm_vk_pixels *canvas,
                               const void *push, uint32_t push_bytes);

// 最小采样消费子集：一个 storage buffer + 一个 sampler2D。纹理/采样器由调用者拥有，必须是 upload 后的
// RGBA8 UNORM 2D 单层、SHADER_READ_ONLY_OPTIMAL；失败不改画布且不销毁调用者资源。
// Minimal sampled consumer: one storage buffer plus one sampler2D. Texture/sampler are caller-owned, RGBA8 UNORM 2D
// single-layer in SHADER_READ_ONLY_OPTIMAL after upload; failure leaves the canvas and caller resources untouched.
VkResult dm_vk_reflect_compute_sampled(dm_vk *vk, const dm_vk_shader_plan *plan, dm_vk_pixels *canvas,
                                       const void *push, uint32_t push_bytes,
                                       const dm_vk_texture *texture, const dm_vk_sampler *sampler);

// 最小固定布局 uniform 消费子集：一个只读 16 字节 std140 uvec4 块 + 一个输出 storage buffer。调用者提供 16 字节
// 同步拷贝输入，函数返回后不再引用；失败不改画布、不改调用者输入、回收全部自有资源。
// Minimal fixed-layout uniform consumer: one read-only 16-byte std140 uvec4 block plus one output storage buffer. The
// caller supplies 16 synchronous-copy bytes; they are not referenced after return. Failure leaves canvas/input unchanged.
VkResult dm_vk_reflect_compute_uniform(dm_vk *vk, const dm_vk_shader_plan *plan, dm_vk_pixels *canvas,
                                       const void *uniform_bytes, uint32_t uniform_size,
                                       const void *push, uint32_t push_bytes);

// 十六进制 SHA-256（供 sidecar 绑定与测试核对）；out 至少 65 字节。
// Hex SHA-256 (for sidecar binding and tests); out must hold at least 65 bytes.
void dm_vk_sha256_hex(const void *data, size_t size, char out[65]);

#endif
