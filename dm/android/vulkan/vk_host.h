// 可移植 Vulkan 底座：设备、缓冲、同步与离屏 GPU 作业；依据 Vulkan 1.1，不含 NVN 游戏语义。
// Portable Vulkan foundation: device, buffers, synchronization and offscreen GPU jobs; based on Vulkan 1.1, no game NVN semantics.
#pragma once
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <vulkan/vulkan.h>
#include "../../gfx/nvn/nvn_tex.h"

// 显式 opt-in 的验证层诊断（#42）：缺省关闭，dm_vk_create 的行为与签名不变。
// Explicit opt-in validation diagnostics (#42): off by default; dm_vk_create keeps its signature and behaviour.
typedef enum {
    DM_VK_VALIDATION_OFF = 0,        // 不请求验证层 | do not request the layer
    DM_VK_VALIDATION_OPTIONAL = 1,   // 有则启用并记录；没有则继续但状态写明未验证 | enable if present, otherwise continue with status "unavailable"
    DM_VK_VALIDATION_REQUIRED = 2    // 请求即必须：缺失时失败（VK_ERROR_LAYER_NOT_PRESENT），绝不静默跑未验证
} dm_vk_validation_mode;             // required: fail when missing, never run unvalidated silently

typedef struct {
    uint32_t struct_size;            // 必须是 sizeof(dm_vk_options) | must equal sizeof(dm_vk_options)
    uint32_t validation;             // dm_vk_validation_mode
} dm_vk_options;
#define DM_VK_OPTIONS_INIT { sizeof(dm_vk_options), DM_VK_VALIDATION_OFF }

typedef struct {
    VkInstance instance; VkPhysicalDevice physical; VkDevice device; VkQueue queue;
    uint32_t family; VkCommandPool pool;
    VkPhysicalDeviceMemoryProperties memory;
    VkPhysicalDeviceProperties properties;
    // 验证诊断：仅当通过 dm_vk_create_ex 请求时分配（堆上，生命周期与实例一致）。
    // Validation diagnostics, allocated on the heap only when requested through dm_vk_create_ex; lifetime matches the instance.
    uint32_t api_version;            // loader 报告的实例版本 | instance version reported by the loader
    uint32_t driver_api_version;     // VkPhysicalDeviceProperties::apiVersion
    uint32_t driver_version;         // VkPhysicalDeviceProperties::driverVersion（驱动构建号，别与 API 版本混用 | driver build, not the API version）
    uint32_t validation;             // 1 = VK_LAYER_KHRONOS_validation 已启用 | layer enabled
    struct dm_vk_diag *diag;         // 私有；accessors 读取 | private, read through the accessors
    VkDebugUtilsMessengerEXT messenger;
} dm_vk;
typedef struct {
    VkBuffer handle; VkDeviceMemory memory; void *mapped;
    VkDeviceSize size; int coherent;
} dm_vk_buffer;
typedef struct { VkDeviceSize size; VkBufferUsageFlags usage; } dm_vk_buffer_info;
typedef struct { uint32_t width, height; uint8_t *bytes; size_t capacity; } dm_vk_pixels;

// 同一上下文的调用须由宿主串行化；销毁前先让全部 GPU 作业完成。
// Host must serialize calls using the same context; complete all GPU jobs before destruction.
VkResult dm_vk_create(dm_vk *out);
VkResult dm_vk_create_ex(const dm_vk_options *options, dm_vk *out);   // 显式 opt-in 诊断；options == NULL 等价于 dm_vk_create
int dm_vk_validation_enabled(const dm_vk *vk);
uint32_t dm_vk_validation_errors(const dm_vk *vk);
uint32_t dm_vk_validation_warnings(const dm_vk *vk);
// 仅当 debug messenger 真正装好 (capture=1) 才可能计数；层启用但 capture=0 必须能从状态看出来。
// Counters are only possible when the debug messenger is installed (capture=1); a layer enabled without capture must be visible in the status.
int dm_vk_validation_capture(const dm_vk *vk);
const char *dm_vk_validation_status(const dm_vk *vk);
const char *dm_vk_validation_layer_version(const dm_vk *vk);   // "spec x.y.z, impl N" when the layer was found
// 把首个错误**在锁内**拷进调用者缓冲（完整、不会读到半截字符串）；返回拷贝长度。
// Copies the first error into the caller buffer under the lock (complete text, never a torn string); returns the copied length.
size_t dm_vk_validation_first_error(const dm_vk *vk, char *dst, size_t cap);
void dm_vk_destroy(dm_vk *vk);
VkResult dm_vk_buffer_create(dm_vk *vk, const dm_vk_buffer_info *info, dm_vk_buffer *out);
void dm_vk_buffer_destroy(dm_vk *vk, dm_vk_buffer *buffer);
VkResult dm_vk_buffer_sync(dm_vk *vk, dm_vk_buffer *buffer, int upload);
VkResult dm_vk_begin(dm_vk *vk, VkCommandBuffer *out);
// 提交始终消费命令缓冲；等待错误时确认设备空闲，无法确认完成则终止进程而不释放在用资源。
// Submit consumes the command buffer; wait errors require device completion, otherwise fail-stop without freeing in-use resources.
VkResult dm_vk_submit(dm_vk *vk, VkCommandBuffer command);
VkResult dm_vk_compute_pixels(dm_vk *vk, const char *shader_path, dm_vk_pixels *pixels);
VkResult dm_vk_clear_pixels(dm_vk *vk, dm_vk_pixels *pixels);
VkResult dm_vk_shader_load(dm_vk *vk,const char *path,VkShaderModule *out);
VkResult dm_vk_draw_pixels(dm_vk *vk,const char *vertex_path,const char *fragment_path,dm_vk_pixels *pixels);

// 仅宿主 RGBA8 块线性 2D/数组上传；成功后由调用者持有，失败不改 out。
// Host-only RGBA8 block-linear 2D/array upload; caller owns success, failure leaves out unchanged.
typedef struct {
    VkImage image; VkDeviceMemory memory; VkImageView view;
    VkFormat format; VkImageViewType view_type;
    uint32_t width, height, levels, layers;
} dm_vk_texture;
// out 不得已有资源；source 须覆盖完整 NVN 存储。销毁前完成后续 GPU 使用。
// out must not already own resources; source must cover full NVN storage. Complete later GPU uses before destruction.
VkResult dm_vk_texture_upload(dm_vk *vk, const nvn_texdesc *desc,
    const void *source, size_t capacity, dm_vk_texture *out);
void dm_vk_texture_destroy(dm_vk *vk, dm_vk_texture *texture);

// 线性 RGBA8 区域更新／读回（#11 行距验收的 Vulkan 对应）：region 以纹素计，须落在该级范围内；
// row_bytes 是行首到行首的字节数（0 = 紧排），非 0 时须 ≥ region.width×4，行距里的填充既不读也不写。
// 前置条件：texture 处于 dm_vk_texture_upload 留下的 SHADER_READ_ONLY_OPTIMAL；本模块不追踪布局，调用者保证串行。
// Linear RGBA8 region update/readback (the Vulkan counterpart of the #11 row-stride acceptance): region is in texels and
// must lie inside that level; row_bytes is start-to-start bytes (0 = tight) and must be >= region.width*4 when non-zero;
// padding inside the row stride is neither read nor written.
// Precondition: the texture sits in the SHADER_READ_ONLY_OPTIMAL state left by dm_vk_texture_upload; this module does not
// track layouts, and the caller guarantees serialized use.
typedef struct { uint32_t x, y, width, height; } dm_vk_region;
VkResult dm_vk_texture_update_rows(dm_vk *vk, dm_vk_texture *texture, uint32_t level, uint32_t layer,
    const dm_vk_region *region, const void *source, size_t source_size, uint32_t row_bytes);
VkResult dm_vk_texture_read_rows(dm_vk *vk, const dm_vk_texture *texture, uint32_t level, uint32_t layer,
    const dm_vk_region *region, uint32_t row_bytes, void *dest, size_t dest_size);
