// 可移植 Vulkan 底座：设备、缓冲、同步与离屏 GPU 作业；依据 Vulkan 1.1，不含 NVN 游戏语义。
// Portable Vulkan foundation: device, buffers, synchronization and offscreen GPU jobs; based on Vulkan 1.1, no game NVN semantics.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <vulkan/vulkan.h>
#include "../../gfx/nvn/nvn_tex.h"
typedef struct {
    VkInstance instance; VkPhysicalDevice physical; VkDevice device; VkQueue queue;
    uint32_t family; VkCommandPool pool;
    VkPhysicalDeviceMemoryProperties memory;
    VkPhysicalDeviceProperties properties;
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
