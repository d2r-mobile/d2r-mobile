// Android Vulkan分发列表：仅从所选loader/instance/device取得函数。
// Android Vulkan dispatch lists: resolve exclusively from the selected loader/instance/device.
#pragma once

#define DM_VK_GLOBAL_FUNCTIONS(X) \
    X(vkCreateInstance)

#define DM_VK_INSTANCE_FUNCTIONS(X) \
    X(vkCreateDevice) \
    X(vkDestroyInstance) \
    X(vkDestroySurfaceKHR) \
    X(vkEnumerateDeviceExtensionProperties) \
    X(vkEnumeratePhysicalDevices) \
    X(vkGetDeviceProcAddr) \
    X(vkGetPhysicalDeviceFeatures) \
    X(vkGetPhysicalDeviceFeatures2) \
    X(vkGetPhysicalDeviceFormatProperties) \
    X(vkGetPhysicalDeviceFormatProperties2) \
    X(vkGetPhysicalDeviceMemoryProperties) \
    X(vkGetPhysicalDeviceProperties) \
    X(vkGetPhysicalDeviceProperties2) \
    X(vkGetPhysicalDeviceQueueFamilyProperties) \
    X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
    X(vkGetPhysicalDeviceSurfaceFormatsKHR) \
    X(vkGetPhysicalDeviceSurfaceSupportKHR)

#ifdef __ANDROID__
#define DM_VK_ANDROID_INSTANCE_FUNCTIONS(X) \
    X(vkCreateAndroidSurfaceKHR)
#else
#define DM_VK_ANDROID_INSTANCE_FUNCTIONS(X)
#endif

#define DM_VK_DEVICE_FUNCTIONS(X) \
    X(vkAcquireNextImageKHR) \
    X(vkAllocateCommandBuffers) \
    X(vkAllocateDescriptorSets) \
    X(vkAllocateMemory) \
    X(vkBeginCommandBuffer) \
    X(vkBindBufferMemory) \
    X(vkBindImageMemory) \
    X(vkCmdBeginQuery) \
    X(vkCmdBeginRenderPass) \
    X(vkCmdBindDescriptorSets) \
    X(vkCmdBindIndexBuffer) \
    X(vkCmdBindPipeline) \
    X(vkCmdBindVertexBuffers) \
    X(vkCmdBlitImage) \
    X(vkCmdClearAttachments) \
    X(vkCmdClearColorImage) \
    X(vkCmdClearDepthStencilImage) \
    X(vkCmdCopyBuffer) \
    X(vkCmdCopyBufferToImage) \
    X(vkCmdCopyImage) \
    X(vkCmdCopyImageToBuffer) \
    X(vkCmdCopyQueryPoolResults) \
    X(vkCmdDispatch) \
    X(vkCmdDraw) \
    X(vkCmdDrawIndexed) \
    X(vkCmdEndQuery) \
    X(vkCmdEndRenderPass) \
    X(vkCmdFillBuffer) \
    X(vkCmdPipelineBarrier) \
    X(vkCmdPushConstants) \
    X(vkCmdResetQueryPool) \
    X(vkCmdSetBlendConstants) \
    X(vkCmdSetDepthBias) \
    X(vkCmdSetScissor) \
    X(vkCmdSetStencilCompareMask) \
    X(vkCmdSetStencilReference) \
    X(vkCmdSetStencilWriteMask) \
    X(vkCmdSetViewport) \
    X(vkCmdWriteTimestamp) \
    X(vkCreateBuffer) \
    X(vkCreateBufferView) \
    X(vkCreateCommandPool) \
    X(vkCreateComputePipelines) \
    X(vkCreateDescriptorPool) \
    X(vkCreateDescriptorSetLayout) \
    X(vkCreateFence) \
    X(vkCreateFramebuffer) \
    X(vkCreateGraphicsPipelines) \
    X(vkCreateImage) \
    X(vkCreateImageView) \
    X(vkCreatePipelineCache) \
    X(vkCreatePipelineLayout) \
    X(vkCreateQueryPool) \
    X(vkCreateRenderPass) \
    X(vkCreateSampler) \
    X(vkCreateSemaphore) \
    X(vkCreateShaderModule) \
    X(vkCreateSwapchainKHR) \
    X(vkDestroyBuffer) \
    X(vkDestroyBufferView) \
    X(vkDestroyCommandPool) \
    X(vkDestroyDescriptorPool) \
    X(vkDestroyDevice) \
    X(vkDestroyFence) \
    X(vkDestroyFramebuffer) \
    X(vkDestroyImage) \
    X(vkDestroyImageView) \
    X(vkDestroyPipeline) \
    X(vkDestroyPipelineLayout) \
    X(vkDestroyQueryPool) \
    X(vkDestroyRenderPass) \
    X(vkDestroySemaphore) \
    X(vkDestroyShaderModule) \
    X(vkDestroySwapchainKHR) \
    X(vkDeviceWaitIdle) \
    X(vkEndCommandBuffer) \
    X(vkFreeCommandBuffers) \
    X(vkFreeMemory) \
    X(vkGetBufferMemoryRequirements) \
    X(vkGetDeviceQueue) \
    X(vkGetFenceStatus) \
    X(vkGetImageMemoryRequirements) \
    X(vkGetPipelineCacheData) \
    X(vkGetSwapchainImagesKHR) \
    X(vkMapMemory) \
    X(vkQueuePresentKHR) \
    X(vkQueueSubmit) \
    X(vkQueueWaitIdle) \
    X(vkResetCommandBuffer) \
    X(vkResetDescriptorPool) \
    X(vkResetFences) \
    X(vkUnmapMemory) \
    X(vkUpdateDescriptorSets) \
    X(vkWaitForFences)

// 调用宏为function-like，避免改写dispatch结构的字段名。
// Function-like aliases do not rewrite dispatch member names.
#ifdef DM_VK_DISPATCH_CONTEXT
#define vkAcquireNextImageKHR(...) (DM_VK_DISPATCH_CONTEXT.vkAcquireNextImageKHR)(__VA_ARGS__)
#define vkAllocateCommandBuffers(...) (DM_VK_DISPATCH_CONTEXT.vkAllocateCommandBuffers)(__VA_ARGS__)
#define vkAllocateDescriptorSets(...) (DM_VK_DISPATCH_CONTEXT.vkAllocateDescriptorSets)(__VA_ARGS__)
#define vkAllocateMemory(...) (DM_VK_DISPATCH_CONTEXT.vkAllocateMemory)(__VA_ARGS__)
#define vkBeginCommandBuffer(...) (DM_VK_DISPATCH_CONTEXT.vkBeginCommandBuffer)(__VA_ARGS__)
#define vkBindBufferMemory(...) (DM_VK_DISPATCH_CONTEXT.vkBindBufferMemory)(__VA_ARGS__)
#define vkBindImageMemory(...) (DM_VK_DISPATCH_CONTEXT.vkBindImageMemory)(__VA_ARGS__)
#define vkCmdBeginQuery(...) (DM_VK_DISPATCH_CONTEXT.vkCmdBeginQuery)(__VA_ARGS__)
#define vkCmdBeginRenderPass(...) (DM_VK_DISPATCH_CONTEXT.vkCmdBeginRenderPass)(__VA_ARGS__)
#define vkCmdBindDescriptorSets(...) (DM_VK_DISPATCH_CONTEXT.vkCmdBindDescriptorSets)(__VA_ARGS__)
#define vkCmdBindIndexBuffer(...) (DM_VK_DISPATCH_CONTEXT.vkCmdBindIndexBuffer)(__VA_ARGS__)
#define vkCmdBindPipeline(...) (DM_VK_DISPATCH_CONTEXT.vkCmdBindPipeline)(__VA_ARGS__)
#define vkCmdBindVertexBuffers(...) (DM_VK_DISPATCH_CONTEXT.vkCmdBindVertexBuffers)(__VA_ARGS__)
#define vkCmdBlitImage(...) (DM_VK_DISPATCH_CONTEXT.vkCmdBlitImage)(__VA_ARGS__)
#define vkCmdClearAttachments(...) (DM_VK_DISPATCH_CONTEXT.vkCmdClearAttachments)(__VA_ARGS__)
#define vkCmdClearColorImage(...) (DM_VK_DISPATCH_CONTEXT.vkCmdClearColorImage)(__VA_ARGS__)
#define vkCmdClearDepthStencilImage(...) (DM_VK_DISPATCH_CONTEXT.vkCmdClearDepthStencilImage)(__VA_ARGS__)
#define vkCmdCopyBuffer(...) (DM_VK_DISPATCH_CONTEXT.vkCmdCopyBuffer)(__VA_ARGS__)
#define vkCmdCopyBufferToImage(...) (DM_VK_DISPATCH_CONTEXT.vkCmdCopyBufferToImage)(__VA_ARGS__)
#define vkCmdCopyImage(...) (DM_VK_DISPATCH_CONTEXT.vkCmdCopyImage)(__VA_ARGS__)
#define vkCmdCopyImageToBuffer(...) (DM_VK_DISPATCH_CONTEXT.vkCmdCopyImageToBuffer)(__VA_ARGS__)
#define vkCmdCopyQueryPoolResults(...) (DM_VK_DISPATCH_CONTEXT.vkCmdCopyQueryPoolResults)(__VA_ARGS__)
#define vkCmdDispatch(...) (DM_VK_DISPATCH_CONTEXT.vkCmdDispatch)(__VA_ARGS__)
#define vkCmdDraw(...) (DM_VK_DISPATCH_CONTEXT.vkCmdDraw)(__VA_ARGS__)
#define vkCmdDrawIndexed(...) (DM_VK_DISPATCH_CONTEXT.vkCmdDrawIndexed)(__VA_ARGS__)
#define vkCmdEndQuery(...) (DM_VK_DISPATCH_CONTEXT.vkCmdEndQuery)(__VA_ARGS__)
#define vkCmdEndRenderPass(...) (DM_VK_DISPATCH_CONTEXT.vkCmdEndRenderPass)(__VA_ARGS__)
#define vkCmdFillBuffer(...) (DM_VK_DISPATCH_CONTEXT.vkCmdFillBuffer)(__VA_ARGS__)
#define vkCmdPipelineBarrier(...) (DM_VK_DISPATCH_CONTEXT.vkCmdPipelineBarrier)(__VA_ARGS__)
#define vkCmdPushConstants(...) (DM_VK_DISPATCH_CONTEXT.vkCmdPushConstants)(__VA_ARGS__)
#define vkCmdResetQueryPool(...) (DM_VK_DISPATCH_CONTEXT.vkCmdResetQueryPool)(__VA_ARGS__)
#define vkCmdSetBlendConstants(...) (DM_VK_DISPATCH_CONTEXT.vkCmdSetBlendConstants)(__VA_ARGS__)
#define vkCmdSetDepthBias(...) (DM_VK_DISPATCH_CONTEXT.vkCmdSetDepthBias)(__VA_ARGS__)
#define vkCmdSetScissor(...) (DM_VK_DISPATCH_CONTEXT.vkCmdSetScissor)(__VA_ARGS__)
#define vkCmdSetStencilCompareMask(...) (DM_VK_DISPATCH_CONTEXT.vkCmdSetStencilCompareMask)(__VA_ARGS__)
#define vkCmdSetStencilReference(...) (DM_VK_DISPATCH_CONTEXT.vkCmdSetStencilReference)(__VA_ARGS__)
#define vkCmdSetStencilWriteMask(...) (DM_VK_DISPATCH_CONTEXT.vkCmdSetStencilWriteMask)(__VA_ARGS__)
#define vkCmdSetViewport(...) (DM_VK_DISPATCH_CONTEXT.vkCmdSetViewport)(__VA_ARGS__)
#define vkCmdWriteTimestamp(...) (DM_VK_DISPATCH_CONTEXT.vkCmdWriteTimestamp)(__VA_ARGS__)
#ifdef __ANDROID__
#define vkCreateAndroidSurfaceKHR(...) (DM_VK_DISPATCH_CONTEXT.vkCreateAndroidSurfaceKHR)(__VA_ARGS__)
#endif
#define vkCreateBuffer(...) (DM_VK_DISPATCH_CONTEXT.vkCreateBuffer)(__VA_ARGS__)
#define vkCreateBufferView(...) (DM_VK_DISPATCH_CONTEXT.vkCreateBufferView)(__VA_ARGS__)
#define vkCreateCommandPool(...) (DM_VK_DISPATCH_CONTEXT.vkCreateCommandPool)(__VA_ARGS__)
#define vkCreateComputePipelines(...) (DM_VK_DISPATCH_CONTEXT.vkCreateComputePipelines)(__VA_ARGS__)
#define vkCreateDescriptorPool(...) (DM_VK_DISPATCH_CONTEXT.vkCreateDescriptorPool)(__VA_ARGS__)
#define vkCreateDescriptorSetLayout(...) (DM_VK_DISPATCH_CONTEXT.vkCreateDescriptorSetLayout)(__VA_ARGS__)
#define vkCreateDevice(...) (DM_VK_DISPATCH_CONTEXT.vkCreateDevice)(__VA_ARGS__)
#define vkCreateFence(...) (DM_VK_DISPATCH_CONTEXT.vkCreateFence)(__VA_ARGS__)
#define vkCreateFramebuffer(...) (DM_VK_DISPATCH_CONTEXT.vkCreateFramebuffer)(__VA_ARGS__)
#define vkCreateGraphicsPipelines(...) (DM_VK_DISPATCH_CONTEXT.vkCreateGraphicsPipelines)(__VA_ARGS__)
#define vkCreateImage(...) (DM_VK_DISPATCH_CONTEXT.vkCreateImage)(__VA_ARGS__)
#define vkCreateImageView(...) (DM_VK_DISPATCH_CONTEXT.vkCreateImageView)(__VA_ARGS__)
#define vkCreateInstance(...) (DM_VK_DISPATCH_CONTEXT.vkCreateInstance)(__VA_ARGS__)
#define vkCreatePipelineCache(...) (DM_VK_DISPATCH_CONTEXT.vkCreatePipelineCache)(__VA_ARGS__)
#define vkCreatePipelineLayout(...) (DM_VK_DISPATCH_CONTEXT.vkCreatePipelineLayout)(__VA_ARGS__)
#define vkCreateQueryPool(...) (DM_VK_DISPATCH_CONTEXT.vkCreateQueryPool)(__VA_ARGS__)
#define vkCreateRenderPass(...) (DM_VK_DISPATCH_CONTEXT.vkCreateRenderPass)(__VA_ARGS__)
#define vkCreateSampler(...) (DM_VK_DISPATCH_CONTEXT.vkCreateSampler)(__VA_ARGS__)
#define vkCreateSemaphore(...) (DM_VK_DISPATCH_CONTEXT.vkCreateSemaphore)(__VA_ARGS__)
#define vkCreateShaderModule(...) (DM_VK_DISPATCH_CONTEXT.vkCreateShaderModule)(__VA_ARGS__)
#define vkCreateSwapchainKHR(...) (DM_VK_DISPATCH_CONTEXT.vkCreateSwapchainKHR)(__VA_ARGS__)
#define vkDestroyBuffer(...) (DM_VK_DISPATCH_CONTEXT.vkDestroyBuffer)(__VA_ARGS__)
#define vkDestroyBufferView(...) (DM_VK_DISPATCH_CONTEXT.vkDestroyBufferView)(__VA_ARGS__)
#define vkDestroyCommandPool(...) (DM_VK_DISPATCH_CONTEXT.vkDestroyCommandPool)(__VA_ARGS__)
#define vkDestroyDescriptorPool(...) (DM_VK_DISPATCH_CONTEXT.vkDestroyDescriptorPool)(__VA_ARGS__)
#define vkDestroyDevice(...) (DM_VK_DISPATCH_CONTEXT.vkDestroyDevice)(__VA_ARGS__)
#define vkDestroyFence(...) (DM_VK_DISPATCH_CONTEXT.vkDestroyFence)(__VA_ARGS__)
#define vkDestroyFramebuffer(...) (DM_VK_DISPATCH_CONTEXT.vkDestroyFramebuffer)(__VA_ARGS__)
#define vkDestroyImage(...) (DM_VK_DISPATCH_CONTEXT.vkDestroyImage)(__VA_ARGS__)
#define vkDestroyImageView(...) (DM_VK_DISPATCH_CONTEXT.vkDestroyImageView)(__VA_ARGS__)
#define vkDestroyInstance(...) (DM_VK_DISPATCH_CONTEXT.vkDestroyInstance)(__VA_ARGS__)
#define vkDestroyPipeline(...) (DM_VK_DISPATCH_CONTEXT.vkDestroyPipeline)(__VA_ARGS__)
#define vkDestroyPipelineLayout(...) (DM_VK_DISPATCH_CONTEXT.vkDestroyPipelineLayout)(__VA_ARGS__)
#define vkDestroyQueryPool(...) (DM_VK_DISPATCH_CONTEXT.vkDestroyQueryPool)(__VA_ARGS__)
#define vkDestroyRenderPass(...) (DM_VK_DISPATCH_CONTEXT.vkDestroyRenderPass)(__VA_ARGS__)
#define vkDestroySemaphore(...) (DM_VK_DISPATCH_CONTEXT.vkDestroySemaphore)(__VA_ARGS__)
#define vkDestroyShaderModule(...) (DM_VK_DISPATCH_CONTEXT.vkDestroyShaderModule)(__VA_ARGS__)
#define vkDestroySurfaceKHR(...) (DM_VK_DISPATCH_CONTEXT.vkDestroySurfaceKHR)(__VA_ARGS__)
#define vkDestroySwapchainKHR(...) (DM_VK_DISPATCH_CONTEXT.vkDestroySwapchainKHR)(__VA_ARGS__)
#define vkDeviceWaitIdle(...) (DM_VK_DISPATCH_CONTEXT.vkDeviceWaitIdle)(__VA_ARGS__)
#define vkEndCommandBuffer(...) (DM_VK_DISPATCH_CONTEXT.vkEndCommandBuffer)(__VA_ARGS__)
#define vkEnumerateDeviceExtensionProperties(...) (DM_VK_DISPATCH_CONTEXT.vkEnumerateDeviceExtensionProperties)(__VA_ARGS__)
#define vkEnumeratePhysicalDevices(...) (DM_VK_DISPATCH_CONTEXT.vkEnumeratePhysicalDevices)(__VA_ARGS__)
#define vkFreeCommandBuffers(...) (DM_VK_DISPATCH_CONTEXT.vkFreeCommandBuffers)(__VA_ARGS__)
#define vkFreeMemory(...) (DM_VK_DISPATCH_CONTEXT.vkFreeMemory)(__VA_ARGS__)
#define vkGetBufferMemoryRequirements(...) (DM_VK_DISPATCH_CONTEXT.vkGetBufferMemoryRequirements)(__VA_ARGS__)
#define vkGetDeviceProcAddr(...) (DM_VK_DISPATCH_CONTEXT.vkGetDeviceProcAddr)(__VA_ARGS__)
#define vkGetDeviceQueue(...) (DM_VK_DISPATCH_CONTEXT.vkGetDeviceQueue)(__VA_ARGS__)
#define vkGetFenceStatus(...) (DM_VK_DISPATCH_CONTEXT.vkGetFenceStatus)(__VA_ARGS__)
#define vkGetImageMemoryRequirements(...) (DM_VK_DISPATCH_CONTEXT.vkGetImageMemoryRequirements)(__VA_ARGS__)
#define vkGetPipelineCacheData(...) (DM_VK_DISPATCH_CONTEXT.vkGetPipelineCacheData)(__VA_ARGS__)
#define vkGetPhysicalDeviceFeatures(...) (DM_VK_DISPATCH_CONTEXT.vkGetPhysicalDeviceFeatures)(__VA_ARGS__)
#define vkGetPhysicalDeviceFeatures2(...) (DM_VK_DISPATCH_CONTEXT.vkGetPhysicalDeviceFeatures2)(__VA_ARGS__)
#define vkGetPhysicalDeviceFormatProperties(...) (DM_VK_DISPATCH_CONTEXT.vkGetPhysicalDeviceFormatProperties)(__VA_ARGS__)
#define vkGetPhysicalDeviceFormatProperties2(...) (DM_VK_DISPATCH_CONTEXT.vkGetPhysicalDeviceFormatProperties2)(__VA_ARGS__)
#define vkGetPhysicalDeviceMemoryProperties(...) (DM_VK_DISPATCH_CONTEXT.vkGetPhysicalDeviceMemoryProperties)(__VA_ARGS__)
#define vkGetPhysicalDeviceProperties(...) (DM_VK_DISPATCH_CONTEXT.vkGetPhysicalDeviceProperties)(__VA_ARGS__)
#define vkGetPhysicalDeviceProperties2(...) (DM_VK_DISPATCH_CONTEXT.vkGetPhysicalDeviceProperties2)(__VA_ARGS__)
#define vkGetPhysicalDeviceQueueFamilyProperties(...) (DM_VK_DISPATCH_CONTEXT.vkGetPhysicalDeviceQueueFamilyProperties)(__VA_ARGS__)
#define vkGetPhysicalDeviceSurfaceCapabilitiesKHR(...) (DM_VK_DISPATCH_CONTEXT.vkGetPhysicalDeviceSurfaceCapabilitiesKHR)(__VA_ARGS__)
#define vkGetPhysicalDeviceSurfaceFormatsKHR(...) (DM_VK_DISPATCH_CONTEXT.vkGetPhysicalDeviceSurfaceFormatsKHR)(__VA_ARGS__)
#define vkGetPhysicalDeviceSurfaceSupportKHR(...) (DM_VK_DISPATCH_CONTEXT.vkGetPhysicalDeviceSurfaceSupportKHR)(__VA_ARGS__)
#define vkGetSwapchainImagesKHR(...) (DM_VK_DISPATCH_CONTEXT.vkGetSwapchainImagesKHR)(__VA_ARGS__)
#define vkMapMemory(...) (DM_VK_DISPATCH_CONTEXT.vkMapMemory)(__VA_ARGS__)
#define vkQueuePresentKHR(...) (DM_VK_DISPATCH_CONTEXT.vkQueuePresentKHR)(__VA_ARGS__)
#define vkQueueSubmit(...) (DM_VK_DISPATCH_CONTEXT.vkQueueSubmit)(__VA_ARGS__)
#define vkQueueWaitIdle(...) (DM_VK_DISPATCH_CONTEXT.vkQueueWaitIdle)(__VA_ARGS__)
#define vkResetCommandBuffer(...) (DM_VK_DISPATCH_CONTEXT.vkResetCommandBuffer)(__VA_ARGS__)
#define vkResetDescriptorPool(...) (DM_VK_DISPATCH_CONTEXT.vkResetDescriptorPool)(__VA_ARGS__)
#define vkResetFences(...) (DM_VK_DISPATCH_CONTEXT.vkResetFences)(__VA_ARGS__)
#define vkUnmapMemory(...) (DM_VK_DISPATCH_CONTEXT.vkUnmapMemory)(__VA_ARGS__)
#define vkUpdateDescriptorSets(...) (DM_VK_DISPATCH_CONTEXT.vkUpdateDescriptorSets)(__VA_ARGS__)
#define vkWaitForFences(...) (DM_VK_DISPATCH_CONTEXT.vkWaitForFences)(__VA_ARGS__)
#endif
