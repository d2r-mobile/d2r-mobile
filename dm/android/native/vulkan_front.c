// 显式交换链、队列与生命周期同步；每帧等待呈现队列，先验证正确性。
// Explicit swapchain, queue, and lifecycle synchronization; wait for presentation each frame for correctness first.
#define VK_USE_PLATFORM_ANDROID_KHR
#include "vulkan_front.h"
#include <android/log.h>
#define DM_VK_DISPATCH_CONTEXT (v->api)
#include "gpu_driver.h"
#include <vulkan/vulkan.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "strings_generated.h"

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "D2RAndroid", __VA_ARGS__)
#define VK_TRY(call) do { VkResult result = (call); if (result != VK_SUCCESS) { LOG("%s: %d", #call, result); goto fail; } } while (0)
struct dm_vk_front {
    dm_vk_driver api;
    VkInstance instance;
    VkPhysicalDevice physical;
    VkDevice device;
    VkQueue queue;
    uint32_t family;
    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;
    VkExtent2D extent;
    VkFormat format;
    uint32_t count;
    VkImageView *views;
    VkFramebuffer *frames;
    VkRenderPass pass;
    VkCommandPool pool;
    VkCommandBuffer command;
    VkSemaphore acquired, rendered;
    uint64_t presented;
};

static int diagnostic_driver_fallback(dm_vk_front *v,const char *reason) {
    if(!v->api.using_custom)return 0;
    if(v->device){if(!v->api.vkDestroyDevice)return 0;vkDestroyDevice(v->device,NULL);v->device=VK_NULL_HANDLE;}
    if(v->surface){if(!v->api.vkDestroySurfaceKHR)return 0;vkDestroySurfaceKHR(v->instance,v->surface,NULL);v->surface=VK_NULL_HANDLE;}
    if(v->instance){if(!v->api.vkDestroyInstance)return 0;vkDestroyInstance(v->instance,NULL);v->instance=VK_NULL_HANDLE;}
    v->physical=VK_NULL_HANDLE;v->queue=VK_NULL_HANDLE;
    return dm_vk_driver_fallback(&v->api,reason);
}

dm_vk_front *dm_vk_front_create(ANativeWindow *window) {
    dm_vk_front *v = calloc(1, sizeof *v);
    if (!v) return NULL;
    if(!dm_vk_driver_open(&v->api)){LOG("Vulkan loader: %s",dm_vk_driver_error(&v->api));goto fail;}
driver_retry:;
    const char *extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME};
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "D2R Android port", .apiVersion = VK_API_VERSION_1_1};
    VkInstanceCreateInfo instance = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app, .enabledExtensionCount = 2, .ppEnabledExtensionNames = extensions};
    VkResult created=vkCreateInstance(&instance,NULL,&v->instance);
    if(created!=VK_SUCCESS&&diagnostic_driver_fallback(v,"vkCreateInstance failed"))goto driver_retry;VK_TRY(created);
    if(!dm_vk_driver_load_instance(&v->api,v->instance,1)){if(diagnostic_driver_fallback(v,dm_vk_driver_error(&v->api)))goto driver_retry;goto fail;}
    VkAndroidSurfaceCreateInfoKHR surface = {.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR, .window = window};
    created=vkCreateAndroidSurfaceKHR(v->instance,&surface,NULL,&v->surface);
    if(created!=VK_SUCCESS&&diagnostic_driver_fallback(v,"vkCreateAndroidSurfaceKHR failed"))goto driver_retry;VK_TRY(created);
    uint32_t count = 0;
    VK_TRY(vkEnumeratePhysicalDevices(v->instance, &count, NULL));
    if (!count){if(diagnostic_driver_fallback(v,"custom Vulkan has no physical device"))goto driver_retry;goto fail;}
    VkPhysicalDevice *devices = calloc(count, sizeof *devices);
    if (!devices) goto fail;
    VkResult result = vkEnumeratePhysicalDevices(v->instance, &count, devices);
    if (result != VK_SUCCESS) { free(devices); goto fail; }
    for (unsigned d = 0; d < count && !v->physical; ++d) {
        uint32_t nq = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &nq, NULL);
        VkQueueFamilyProperties *properties = calloc(nq, sizeof *properties);
        if (!properties) continue;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &nq, properties);
        for (unsigned q = 0; q < nq; ++q) {
            VkBool32 supported = VK_FALSE;
            result = vkGetPhysicalDeviceSurfaceSupportKHR(devices[d], q, v->surface, &supported);
            if (result == VK_SUCCESS && supported && (properties[q].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                v->physical = devices[d]; v->family = q; break;
            }
        }
        free(properties);
    }
    free(devices);
    if (!v->physical){if(diagnostic_driver_fallback(v,"custom Vulkan has no graphics/present queue"))goto driver_retry;goto fail;}
    if(!dm_vk_driver_validate(&v->api,v->physical,NULL)){if(diagnostic_driver_fallback(v,dm_vk_driver_error(&v->api)))goto driver_retry;goto fail;}
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(v->physical, &properties);
    LOG("Vulkan GPU: %s API %u.%u", properties.deviceName, VK_VERSION_MAJOR(properties.apiVersion), VK_VERSION_MINOR(properties.apiVersion));
    const char *device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = v->family, .queueCount = 1, .pQueuePriorities = &priority};
    VkDeviceCreateInfo device = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1, .pQueueCreateInfos = &queue, .enabledExtensionCount = 1, .ppEnabledExtensionNames = device_extensions};
    if(!dm_vk_driver_prepare_device(&v->api,&device)){LOG("Vulkan subgroup device policy: %s",dm_vk_driver_error(&v->api));goto fail;}
    created=vkCreateDevice(v->physical,&device,NULL,&v->device);
    if(created!=VK_SUCCESS&&diagnostic_driver_fallback(v,"vkCreateDevice failed"))goto driver_retry;VK_TRY(created);
    if(!dm_vk_driver_load_device(&v->api,v->device)){if(diagnostic_driver_fallback(v,dm_vk_driver_error(&v->api)))goto driver_retry;goto fail;}
    vkGetDeviceQueue(v->device, v->family, 0, &v->queue);
    VkSurfaceCapabilitiesKHR capabilities;
    VK_TRY(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(v->physical, v->surface, &capabilities));
    v->extent = capabilities.currentExtent;
    if (v->extent.width == UINT32_MAX) {
        v->extent.width = ANativeWindow_getWidth(window); v->extent.height = ANativeWindow_getHeight(window);
        if (v->extent.width < capabilities.minImageExtent.width) v->extent.width = capabilities.minImageExtent.width;
        if (v->extent.height < capabilities.minImageExtent.height) v->extent.height = capabilities.minImageExtent.height;
        if (v->extent.width > capabilities.maxImageExtent.width) v->extent.width = capabilities.maxImageExtent.width;
        if (v->extent.height > capabilities.maxImageExtent.height) v->extent.height = capabilities.maxImageExtent.height;
    }
    if (!(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) goto fail;
    uint32_t formats_count = 0;
    VK_TRY(vkGetPhysicalDeviceSurfaceFormatsKHR(v->physical, v->surface, &formats_count, NULL));
    if (!formats_count) goto fail;
    VkSurfaceFormatKHR *formats = calloc(formats_count, sizeof *formats);
    if (!formats) goto fail;
    result = vkGetPhysicalDeviceSurfaceFormatsKHR(v->physical, v->surface, &formats_count, formats);
    VkSurfaceFormatKHR format = formats[0]; free(formats);
    if (result != VK_SUCCESS) goto fail;
    if (format.format == VK_FORMAT_UNDEFINED) format.format = VK_FORMAT_R8G8B8A8_UNORM;
    v->format = format.format;
    uint32_t images = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount && images > capabilities.maxImageCount) images = capabilities.maxImageCount;
    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(capabilities.supportedCompositeAlpha & alpha)) alpha = capabilities.supportedCompositeAlpha & -capabilities.supportedCompositeAlpha;
    VkSwapchainCreateInfoKHR swap = {.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, .surface = v->surface, .minImageCount = images,
        .imageFormat = format.format, .imageColorSpace = format.colorSpace, .imageExtent = v->extent, .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = capabilities.currentTransform, .compositeAlpha = alpha, .presentMode = VK_PRESENT_MODE_FIFO_KHR, .clipped = VK_TRUE};
    VK_TRY(vkCreateSwapchainKHR(v->device, &swap, NULL, &v->swapchain));
    VK_TRY(vkGetSwapchainImagesKHR(v->device, v->swapchain, &v->count, NULL));
    VkImage *image = calloc(v->count, sizeof *image);
    v->views = calloc(v->count, sizeof *v->views); v->frames = calloc(v->count, sizeof *v->frames);
    if (!image || !v->views || !v->frames) { free(image); goto fail; }
    result = vkGetSwapchainImagesKHR(v->device, v->swapchain, &v->count, image);
    if (result != VK_SUCCESS) { free(image); goto fail; }
    for (unsigned i = 0; i < v->count; ++i) {
        VkImageViewCreateInfo view = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = image[i], .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = v->format, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
        result = vkCreateImageView(v->device, &view, NULL, &v->views[i]);
        if (result != VK_SUCCESS) { free(image); goto fail; }
    }
    free(image);
    VkAttachmentDescription attachment = {.format = v->format, .samples = VK_SAMPLE_COUNT_1_BIT, .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};
    VkAttachmentReference reference = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, .colorAttachmentCount = 1, .pColorAttachments = &reference};
    VkSubpassDependency dependency = {.srcSubpass = VK_SUBPASS_EXTERNAL, .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT};
    VkRenderPassCreateInfo pass = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, .attachmentCount = 1, .pAttachments = &attachment,
        .subpassCount = 1, .pSubpasses = &subpass, .dependencyCount = 1, .pDependencies = &dependency};
    VK_TRY(vkCreateRenderPass(v->device, &pass, NULL, &v->pass));
    for (unsigned i = 0; i < v->count; ++i) {
        VkFramebufferCreateInfo frame = {.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = v->pass, .attachmentCount = 1,
            .pAttachments = &v->views[i], .width = v->extent.width, .height = v->extent.height, .layers = 1};
        VK_TRY(vkCreateFramebuffer(v->device, &frame, NULL, &v->frames[i]));
    }
    VkCommandPoolCreateInfo pool = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = v->family};
    VK_TRY(vkCreateCommandPool(v->device, &pool, NULL, &v->pool));
    VkCommandBufferAllocateInfo allocate = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = v->pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    VK_TRY(vkAllocateCommandBuffers(v->device, &allocate, &v->command));
    VkSemaphoreCreateInfo semaphore = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VK_TRY(vkCreateSemaphore(v->device, &semaphore, NULL, &v->acquired));
    VK_TRY(vkCreateSemaphore(v->device, &semaphore, NULL, &v->rendered));
    LOG("Vulkan surface ready: %ux%u", v->extent.width, v->extent.height);
    return v;
fail:
    dm_vk_front_destroy(v);
    return NULL;
}

static void rect(dm_vk_front *v, int x, int y, unsigned width, unsigned height, float r, float g, float b) {
    if (x < 0 || y < 0 || (unsigned)x >= v->extent.width || (unsigned)y >= v->extent.height) return;
    if (width > v->extent.width - x) width = v->extent.width - x;
    if (height > v->extent.height - y) height = v->extent.height - y;
    VkClearAttachment attachment = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .colorAttachment = 0, .clearValue.color = {{r, g, b, 1}}};
    VkClearRect clear = {.rect = {{x, y}, {width, height}}, .baseArrayLayer = 0, .layerCount = 1};
    vkCmdClearAttachments(v->command, 1, &attachment, 1, &clear);
}

// 诊断页的 ASCII 点阵；游戏 UI 仍需接入原有资源和 NVN 绘制。
// ASCII diagnostic glyphs; game UI still needs original assets and NVN drawing.
static const uint8_t glyphs[36][7] = {
 {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},{30,17,17,17,17,17,30},
 {31,16,16,30,16,16,31},{31,16,16,30,16,16,16},{14,17,16,23,17,17,15},{17,17,17,31,17,17,17},
 {31,4,4,4,4,4,31},{7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
 {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},{30,17,17,30,16,16,16},
 {14,17,17,17,21,18,13},{30,17,17,30,20,18,17},{15,16,16,14,1,1,30},{31,4,4,4,4,4,4},
 {17,17,17,17,17,17,14},{17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
 {17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
 {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},{30,1,1,14,1,1,30},
 {2,6,10,18,31,2,2},{31,16,16,30,1,1,30},{14,16,16,30,17,17,14},{31,1,2,4,8,8,8},
 {14,17,17,14,17,17,14},{14,17,17,15,1,1,14}
};
static void text(dm_vk_front *v, int x, int y, const char *s, unsigned scale, float r, float g, float b) {
    for (; *s; s++, x += 6 * scale) {
        char c = *s >= 'a' && *s <= 'z' ? *s - 32 : *s;
        int index = c >= 'A' && c <= 'Z' ? c - 'A' : c >= '0' && c <= '9' ? c - '0' + 26 : -1;
        if (index < 0) continue;
        for (unsigned row = 0; row < 7; ++row) for (unsigned col = 0; col < 5; ++col)
            if (glyphs[index][row] & (1 << (4 - col))) rect(v, x + col * scale, y + row * scale, scale, scale, r, g, b);
    }
}

int dm_vk_front_draw(dm_vk_front *v, const char *status, int ok, int32_t lx, int32_t ly, uint64_t buttons) {
    uint32_t image;
    VkResult result = vkAcquireNextImageKHR(v->device, v->swapchain, UINT64_MAX, v->acquired, VK_NULL_HANDLE, &image);
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) return -1;
    VK_TRY(vkResetCommandBuffer(v->command, 0));
    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    VK_TRY(vkBeginCommandBuffer(v->command, &begin));
    VkClearValue color = {.color = {{0.035f, 0.045f, 0.07f, 1}}};
    VkRenderPassBeginInfo pass = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = v->pass, .framebuffer = v->frames[image],
        .renderArea = {{0, 0}, v->extent}, .clearValueCount = 1, .pClearValues = &color};
    vkCmdBeginRenderPass(v->command, &pass, VK_SUBPASS_CONTENTS_INLINE);
    unsigned scale = v->extent.width / 420; if (scale < 2) scale = 2;
    text(v, 40, 40, dmAndroidTitle, scale, 0.8, 0.7, 0.4);
    text(v, 40, 40 + 14 * scale, status, scale, ok ? 0.3 : 1, ok ? 0.9 : 0.2, 0.5);
    text(v, 40, 40 + 28 * scale, dmAndroidVulkanActive, scale, 0.3, 0.7, 1);
    text(v, 40, 40 + 42 * scale, dmAndroidBackendPending, scale, 0.8, 0.8, 0.8);
    char input[80]; snprintf(input, sizeof input, dmAndroidTouchStatus, lx, ly, (unsigned long long)buttons);
    text(v, 40, 40 + 56 * scale, input, scale, 0.6, 0.7, 0.8);
    int x = (int)(v->extent.width * 0.15f), y = (int)(v->extent.height * 0.7f);
    rect(v, x - 40, y - 40, 80, 80, 0.15, 0.2, 0.3);
    rect(v, x - 12 + lx * 35 / 32767, y - 12 - ly * 35 / 32767, 24, 24, 0.3, 0.8, 1);
    rect(v, (int)(v->extent.width * 0.85f) - 30, y - 30, 60, 60, buttons ? 0.9 : 0.3, 0.35, 0.2);
    vkCmdEndRenderPass(v->command);
    VK_TRY(vkEndCommandBuffer(v->command));
    VkPipelineStageFlags wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .waitSemaphoreCount = 1, .pWaitSemaphores = &v->acquired,
        .pWaitDstStageMask = &wait, .commandBufferCount = 1, .pCommandBuffers = &v->command, .signalSemaphoreCount = 1, .pSignalSemaphores = &v->rendered};
    VK_TRY(vkQueueSubmit(v->queue, 1, &submit, VK_NULL_HANDLE));
    VkPresentInfoKHR present = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, .waitSemaphoreCount = 1, .pWaitSemaphores = &v->rendered,
        .swapchainCount = 1, .pSwapchains = &v->swapchain, .pImageIndices = &image};
    result = vkQueuePresentKHR(v->queue, &present);
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) return -1;
    VK_TRY(vkQueueWaitIdle(v->queue));
    if (++v->presented == 1 || v->presented == 120) LOG("Vulkan presented %llu frames", (unsigned long long)v->presented);
    return 0;
fail:
    return -1;
}

void dm_vk_front_destroy(dm_vk_front *v) {
    if (!v) return;
    if (v->device) {
        if(v->api.vkDeviceWaitIdle)vkDeviceWaitIdle(v->device);
        if (v->rendered) vkDestroySemaphore(v->device, v->rendered, NULL);
        if (v->acquired) vkDestroySemaphore(v->device, v->acquired, NULL);
        if (v->pool) vkDestroyCommandPool(v->device, v->pool, NULL);
        for (unsigned i = 0; i < v->count; ++i) {
            if (v->frames && v->frames[i]) vkDestroyFramebuffer(v->device, v->frames[i], NULL);
            if (v->views && v->views[i]) vkDestroyImageView(v->device, v->views[i], NULL);
        }
        if (v->pass) vkDestroyRenderPass(v->device, v->pass, NULL);
        if (v->swapchain) vkDestroySwapchainKHR(v->device, v->swapchain, NULL);
        if(v->api.vkDestroyDevice)vkDestroyDevice(v->device, NULL);
    }
    if (v->surface) vkDestroySurfaceKHR(v->instance, v->surface, NULL);
    if (v->instance&&v->api.vkDestroyInstance) vkDestroyInstance(v->instance, NULL);
    dm_vk_driver_close(&v->api);
    free(v->frames); free(v->views); free(v);
}
