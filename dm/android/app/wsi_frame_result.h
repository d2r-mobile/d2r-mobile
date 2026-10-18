// #143 一帧 acquire→clearsubmit→present 的真实结果翻译：SUBOPTIMAL 必须被当作"已呈现但需重建"，不是错误。
// 依据 VK_KHR_swapchain 语义与已验收 vk_swapchain 的返回值（acquire 成功可带 suboptimal 标记，present 可直接返回
// VK_SUBOPTIMAL_KHR）；native.c 的 wsi_frame 就是调用本函数。
// #143 real result translation for one acquire -> clear-submit -> present frame: SUBOPTIMAL means "presented but
// recreation required", never an error. Based on VK_KHR_swapchain semantics and the accepted vk_swapchain return
// values (a successful acquire may set the suboptimal flag, present may return VK_SUBOPTIMAL_KHR directly); the
// native.c wsi_frame calls this function.
#pragma once

#include <stdint.h>
#include <vulkan/vulkan.h>

#include "wsi_diagnostic.h"

typedef struct {
    void *ctx;
    VkResult (*acquire)(void *ctx, int *suboptimal);
    VkResult (*submit)(void *ctx);
    VkResult (*present)(void *ctx, int *suboptimal);
} dm_wsi_frame_ops;

// 返回 OK/SUBOPTIMAL/REBUILD/SURFACE_LOST/ERROR。submit 失败时绝不调用 present；acquire 的 SUBOPTIMAL 仍会
// 提交并呈现该可用图像。
// Returns OK/SUBOPTIMAL/REBUILD/SURFACE_LOST/ERROR. present is never called after a failed submit, and a suboptimal
// acquire still submits and presents its usable image.
dm_wsi_status dm_wsi_present_frame(const dm_wsi_frame_ops *ops);
