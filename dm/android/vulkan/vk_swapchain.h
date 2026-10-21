// 有界 own swapchain：真实 VK_KHR_swapchain 创建、清屏提交与 present；不含 NVN 游戏语义或完整 WSI 框架。
// Bounded owned swapchain: real VK_KHR_swapchain creation, clear-only submit and present; no NVN game semantics or general WSI framework.
#pragma once
#include "vk_host.h"

typedef struct {
    VkSwapchainKHR handle;
    VkRenderPass render_pass;
    VkFormat format;
    VkColorSpaceKHR color_space;
    VkExtent2D extent;
    VkClearColorValue clear;         // clear value retained for HUD render-pass load
    VkPresentModeKHR present_mode;
    uint32_t image_count;
    VkImage *images;                 // driver-owned; only this module references them
    VkImageView *views;              // owned
    VkFramebuffer *framebuffers;     // owned
    VkCommandBuffer *commands;       // allocated from dm_vk::pool; owned
    int commands_allocated;          // true only after vkAllocateCommandBuffers succeeds
    VkSemaphore image_available;     // owned; acquire semaphore
    VkSemaphore *render_finished;    // one per image; present wait semaphore
    VkFence *present_fences;         // one per image; VK_EXT_swapchain_maintenance1 completion proof
    VkFence fence;                   // submission completion only
    uint8_t *present_pending;        // per image: a present fence must be waited before reuse/retire
    VkFence acquire_fence;           // proof that vkAcquireNextImageKHR acquisition operation completed
    int acquire_pending;
    uint32_t image_index;            // valid only while acquired/submitted
    int acquired, submitted, fence_pending, needs_recreate, retire_blocked;
    VkResult retire_error;
} dm_vk_swapchain;

typedef struct {
    uint32_t struct_size;            // sizeof(dm_vk_swapchain_options)
    uint32_t width, height;          // requested size; clamped to surface caps when currentExtent is undefined
    VkClearColorValue clear;         // clear-only frame content
    VkPresentModeKHR present_mode;   // only VK_PRESENT_MODE_FIFO_KHR is accepted by this bounded slice
} dm_vk_swapchain_options;
#define DM_VK_SWAPCHAIN_OPTIONS_INIT { sizeof(dm_vk_swapchain_options), 0, 0, {{0,0,0,1}}, VK_PRESENT_MODE_FIFO_KHR }

// 创建/替换：成功时 out 拥有一套新对象，旧 swapchain 在新对象建立后销毁；失败时旧对象也按 fail-closed 销毁，
// 不声称旧链仍可回滚使用。调用前/后宿主串行化；recreate 先等待所有 in-flight acquire/present/submit fences。
// Create/replace: on success out owns fresh objects and the old chain is retired after creation; on failure the old
// chain is also destroyed (fail-closed), never claimed to be a usable rollback. Host serializes; recreate waits for
// all in-flight presentation fences before retiring the old chain.
VkResult dm_vk_swapchain_create(dm_vk *vk, const dm_vk_swapchain_options *options, dm_vk_swapchain *out);
VkResult dm_vk_swapchain_recreate(dm_vk *vk, dm_vk_swapchain *chain, const dm_vk_swapchain_options *options);

// acquire 成功后可 submit；OUT_OF_DATE 不产生图像；SUBOPTIMAL 仍产生可用图像并由 suboptimal 标记。
// On success the image may be submitted. OUT_OF_DATE acquires nothing; SUBOPTIMAL acquires a usable image and sets
// suboptimal so the caller can present it and then recreate.
VkResult dm_vk_swapchain_acquire(dm_vk *vk, dm_vk_swapchain *chain, int *suboptimal);
VkResult dm_vk_swapchain_submit(dm_vk *vk, dm_vk_swapchain *chain);
// Present only consumes a submitted frame; OUT_OF_DATE/SUBOPTIMAL mark needs_recreate and never free in-flight objects.
VkResult dm_vk_swapchain_present(dm_vk *vk, dm_vk_swapchain *chain, int *suboptimal);
// 先等待每个 in-flight present fence（maintenance1 证明呈现资源可释放），再销毁所有对象。
// 任一 present fence 无法证明完成时返回错误并保留全部对象（fail-closed，不静默释放）。
// Waits each in-flight present fence (maintenance1 proof that presentation resources can be released) before destroying objects.
// If any proof is unavailable/fails, returns an error and retains all objects (fail-closed, no silent release).
VkResult dm_vk_swapchain_destroy(dm_vk *vk, dm_vk_swapchain *chain);
