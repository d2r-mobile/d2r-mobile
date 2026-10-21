// #143 一帧 WSI 结果翻译实现：先 acquire，再 submit，最后 present；SUBOPTIMAL 在任一阶段都保留为"已呈现"。
// 依据 VkResult 语义；本文件只翻译结果，不拥有任何 Vulkan 对象。
// #143 frame result translation: acquire, then submit, then present; SUBOPTIMAL at any stage stays a "presented"
// result. Based on VkResult semantics; this file translates results only and owns no Vulkan objects.
#include "wsi_frame_result.h"

dm_wsi_status dm_wsi_present_frame(const dm_wsi_frame_ops *ops) {
    if (!ops || !ops->acquire || !ops->submit || !ops->present) return DM_WSI_ERROR;
    int acquire_suboptimal = 0, present_suboptimal = 0;
    VkResult result = ops->acquire(ops->ctx, &acquire_suboptimal);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) return DM_WSI_REBUILD;
    if (result == VK_ERROR_SURFACE_LOST_KHR) return DM_WSI_SURFACE_LOST;
    if (result == VK_SUBOPTIMAL_KHR) acquire_suboptimal = 1;
    else if (result != VK_SUCCESS) return DM_WSI_ERROR;

    // acquire 的 SUBOPTIMAL 仍产生可用图像：继续提交与呈现，绝不提前返回。
    // A suboptimal acquire still yields a usable image: submit and present it instead of returning early.
    result = ops->submit(ops->ctx);
    if (result != VK_SUCCESS) return DM_WSI_ERROR;

    result = ops->present(ops->ctx, &present_suboptimal);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) return DM_WSI_REBUILD;
    if (result == VK_ERROR_SURFACE_LOST_KHR) return DM_WSI_SURFACE_LOST;
    // 先判真实错误，再应用 SUBOPTIMAL 标记：否则 acquire 的 suboptimal 会把 present 的 DEVICE_LOST 掩盖成"有效帧"。
    // Check real errors before applying suboptimal flags, otherwise an acquire suboptimal masks a present device loss
    // as a valid frame.
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) return DM_WSI_ERROR;
    if (result == VK_SUBOPTIMAL_KHR || present_suboptimal || acquire_suboptimal) return DM_WSI_SUBOPTIMAL;
    return DM_WSI_OK;
}
