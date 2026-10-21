// #13 NVN 采样器 → Vulkan：显式参数转换、拒绝未知/未实现组合、失败不修改 out，以及自有的采样绘制路径。
// #13 NVN sampler -> Vulkan: explicit parameter conversion, rejection of unknown/unimplemented combinations, failure
// without mutating out, and an owned sampled-draw path.
#include "vk_sampler.h"

#include <math.h>
#include <string.h>

// NVN 枚举（dm/gfx/nvn/include/nvn.h）：只接受本切片实现过的值；未知/未实现不默认回退。
// NVN enums (dm/gfx/nvn/include/nvn.h): accept only implemented values; unknown/unimplemented values never fall back.
static int map_wrap(int nvn, VkSamplerAddressMode *out) {
    switch (nvn) {
        case 0: case 7: *out = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; return 1;   // NVN CLAMP 与 CLAMP_TO_EDGE 在 Metal 中同为 clamp-to-edge
        case 1: *out = VK_SAMPLER_ADDRESS_MODE_REPEAT; return 1;
        case 6: *out = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT; return 1;
        default: return 0;   // 2/3 mirror-clamp 与 4/5 border 未实现 | mirror-clamp and border are not implemented
    }
}
// 公开枚举的穷举映射：未知/负值/超大值一律失败，绝不回退成 clamp（此前的三元表达式会静默映射）。
// Exhaustive mapping for the public enum: unknown/negative/large values fail and never fall back to clamp (the previous
// ternary silently mapped them).
static int map_address(dm_vk_address a, VkSamplerAddressMode *out) {
    switch (a) {
        case DM_VK_ADDRESS_REPEAT: *out = VK_SAMPLER_ADDRESS_MODE_REPEAT; return 1;
        case DM_VK_ADDRESS_MIRRORED_REPEAT: *out = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT; return 1;
        case DM_VK_ADDRESS_CLAMP_TO_EDGE: *out = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; return 1;
        case DM_VK_ADDRESS_CLAMP_TO_BORDER:
        case DM_VK_ADDRESS_MIRROR_CLAMP_TO_EDGE:
        default: return 0;   // 未实现或未知 | unimplemented or unknown
    }
}
static int from_vk_address(VkSamplerAddressMode m, dm_vk_address *out) {
    switch (m) {
        case VK_SAMPLER_ADDRESS_MODE_REPEAT: *out = DM_VK_ADDRESS_REPEAT; return 1;
        case VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT: *out = DM_VK_ADDRESS_MIRRORED_REPEAT; return 1;
        case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE: *out = DM_VK_ADDRESS_CLAMP_TO_EDGE; return 1;
        default: return 0;
    }
}
static int finite_float(float v) { return isfinite(v) && fabsf(v) <= 1.0e6f; }

VkResult dm_vk_sampler_create(dm_vk *vk, const dm_vk_sampler_desc *desc, dm_vk_sampler *out) {
    if (!vk || !vk->device || !out) return VK_ERROR_INITIALIZATION_FAILED;
    if (!desc) return VK_ERROR_INITIALIZATION_FAILED;
    if (desc->min_filter != DM_VK_FILTER_NEAREST && desc->min_filter != DM_VK_FILTER_LINEAR) return VK_ERROR_INITIALIZATION_FAILED;
    if (desc->mag_filter != DM_VK_FILTER_NEAREST && desc->mag_filter != DM_VK_FILTER_LINEAR) return VK_ERROR_INITIALIZATION_FAILED;
    if (desc->mipmap != DM_VK_MIP_NEAREST && desc->mipmap != DM_VK_MIP_LINEAR) return VK_ERROR_INITIALIZATION_FAILED;
    VkSamplerAddressMode addr[3];
    const dm_vk_address modes[3] = {desc->address_u, desc->address_v, desc->address_w};
    for (int i = 0; i < 3; i++) {
        if (modes[i] == DM_VK_ADDRESS_CLAMP_TO_BORDER) return VK_ERROR_FEATURE_NOT_PRESENT;   // 未实现 | unimplemented
        if (modes[i] == DM_VK_ADDRESS_MIRROR_CLAMP_TO_EDGE) return VK_ERROR_FEATURE_NOT_PRESENT;
        if (!map_address(modes[i], &addr[i])) return VK_ERROR_INITIALIZATION_FAILED;   // 未知/负值/超大值 | unknown/negative/large
    }
    if (!finite_float(desc->lod_min) || !finite_float(desc->lod_max) || !finite_float(desc->lod_bias))
        return VK_ERROR_INITIALIZATION_FAILED;
    if (desc->lod_min > desc->lod_max) return VK_ERROR_INITIALIZATION_FAILED;
    if (!finite_float(desc->max_aniso) || desc->max_aniso != 1.0f) return VK_ERROR_FEATURE_NOT_PRESENT;
    if (desc->compare_enable) return VK_ERROR_FEATURE_NOT_PRESENT;
    if (desc->has_border) return VK_ERROR_FEATURE_NOT_PRESENT;
    if (fabsf(desc->lod_bias) > (float)vk->properties.limits.maxSamplerLodBias) return VK_ERROR_INITIALIZATION_FAILED;

    VkSamplerCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = desc->mag_filter == DM_VK_FILTER_LINEAR ? VK_FILTER_LINEAR : VK_FILTER_NEAREST,
        .minFilter = desc->min_filter == DM_VK_FILTER_LINEAR ? VK_FILTER_LINEAR : VK_FILTER_NEAREST,
        .mipmapMode = desc->mipmap == DM_VK_MIP_LINEAR ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = addr[0], .addressModeV = addr[1], .addressModeW = addr[2],
        // 非 mipmap 的 NVN min filter（0/1）在 Metal 中是 NotMipmapped：等效为 lod 上限 0。 | NotMipmapped is equivalent to lod_max 0.
        .mipLodBias = desc->lod_bias,
        .minLod = desc->lod_min,
        .maxLod = desc->lod_max,
        .anisotropyEnable = VK_FALSE,
        .compareEnable = VK_FALSE,
        .borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
        .unnormalizedCoordinates = VK_FALSE};
    VkSampler sampler = VK_NULL_HANDLE;
    VkResult r = vkCreateSampler(vk->device, &ci, NULL, &sampler);
    if (r != VK_SUCCESS) return r;
    out->handle = sampler;
    return VK_SUCCESS;
}

void dm_vk_sampler_destroy(dm_vk *vk, dm_vk_sampler *sampler) {
    if (!sampler) return;
    if (sampler->handle && vk && vk->device) vkDestroySampler(vk->device, sampler->handle, NULL);
    memset(sampler, 0, sizeof *sampler);
}

VkResult dm_vk_sampler_from_nvn(dm_vk *vk, const dm_vk_nvn_sampler *nvn, dm_vk_sampler *out) {
    if (!vk || !nvn || !out) return VK_ERROR_INITIALIZATION_FAILED;
    if (nvn->minf < 0 || nvn->minf > 5) return VK_ERROR_INITIALIZATION_FAILED;   // 未知枚举不默认 | unknown enum never defaults
    if (nvn->magf < 0 || nvn->magf > 1) return VK_ERROR_INITIALIZATION_FAILED;
    if (nvn->cmp_mode != 0) return VK_ERROR_FEATURE_NOT_PRESENT;                 // 比较采样未实现 | unimplemented
    // 原始 NVN LOD 必须是有限值且 min≤max：必须在校验前拒绝，不能因为非 mipmap 归一化而被绕过。
    // The original NVN LOD must be finite with min<=max: reject before normalization so non-mipmapped normalization
    // cannot bypass the check promised in the header.
    if (!finite_float(nvn->lod_min) || !finite_float(nvn->lod_max) || nvn->lod_min > nvn->lod_max)
        return VK_ERROR_INITIALIZATION_FAILED;
    dm_vk_sampler_desc d;
    memset(&d, 0, sizeof d);
    // 显式穷举 minf -> minFilter/mipmapMode/LOD：0/1 非 mipmap（Metal NotMipmapped，LOD 恒 0）；2/3 mip nearest；
    // 4/5 mip linear，其中 4 的 minFilter 是 nearest、5 是 linear（与 nvn_mtl.m:2552-2553 一致）。
    // Explicit minf -> minFilter/mipmapMode/LOD: 0/1 non-mipmapped (Metal NotMipmapped, LOD always 0); 2/3 mip nearest;
    // 4/5 mip linear, where 4 keeps a nearest minFilter and 5 a linear one (matching nvn_mtl.m:2552-2553).
    switch (nvn->minf) {
        case 0: d.min_filter = DM_VK_FILTER_NEAREST; d.mipmap = DM_VK_MIP_NEAREST; d.lod_min = 0.0f; d.lod_max = 0.0f; break;
        case 1: d.min_filter = DM_VK_FILTER_LINEAR;  d.mipmap = DM_VK_MIP_NEAREST; d.lod_min = 0.0f; d.lod_max = 0.0f; break;
        case 2: d.min_filter = DM_VK_FILTER_NEAREST; d.mipmap = DM_VK_MIP_NEAREST; d.lod_min = nvn->lod_min; d.lod_max = nvn->lod_max; break;
        case 3: d.min_filter = DM_VK_FILTER_LINEAR;  d.mipmap = DM_VK_MIP_NEAREST; d.lod_min = nvn->lod_min; d.lod_max = nvn->lod_max; break;
        case 4: d.min_filter = DM_VK_FILTER_NEAREST; d.mipmap = DM_VK_MIP_LINEAR;  d.lod_min = nvn->lod_min; d.lod_max = nvn->lod_max; break;
        case 5: d.min_filter = DM_VK_FILTER_LINEAR;  d.mipmap = DM_VK_MIP_LINEAR;  d.lod_min = nvn->lod_min; d.lod_max = nvn->lod_max; break;
        default: return VK_ERROR_INITIALIZATION_FAILED;   // 未知不默认 | unknown never defaults
    }
    d.mag_filter = nvn->magf == 1 ? DM_VK_FILTER_LINEAR : DM_VK_FILTER_NEAREST;
    d.lod_bias = nvn->lod_bias;
    d.max_aniso = nvn->aniso;
    d.compare_enable = nvn->cmp_mode;
    d.has_border = 0;
    VkSamplerAddressMode mapped;
    if (!map_wrap(nvn->ws, &mapped) || !from_vk_address(mapped, &d.address_u)) return VK_ERROR_FEATURE_NOT_PRESENT;
    if (!map_wrap(nvn->wt, &mapped) || !from_vk_address(mapped, &d.address_v)) return VK_ERROR_FEATURE_NOT_PRESENT;
    if (!map_wrap(nvn->wr, &mapped) || !from_vk_address(mapped, &d.address_w)) return VK_ERROR_FEATURE_NOT_PRESENT;
    return dm_vk_sampler_create(vk, &d, out);
}

// ---- 自有采样绘制 | owned sampled draw ---------------------------------------------------------------
VkResult dm_vk_sample_draw(dm_vk *vk, const char *vertex_path, const char *fragment_path,
                           const dm_vk_texture *texture, const dm_vk_sampler *sampler,
                           uint32_t width, uint32_t height, dm_vk_pixels *pixels, float uv_scale, float lod) {
    if (!vk || !vk->device || !vertex_path || !fragment_path || !texture || !texture->view || !sampler || !sampler->handle || !pixels || !pixels->bytes)
        return VK_ERROR_INITIALIZATION_FAILED;
    if (!width || !height || width > 16384 || height > 16384) return VK_ERROR_INITIALIZATION_FAILED;
    // sampler2D 输入契约：只接受自有管线验证过的 2D 单层 RGBA8 UNORM 视图，且在创建任何 GPU 对象之前拒绝其余形态。
    // sampler2D input contract: only the owned pipeline's verified 2D single-layer RGBA8 UNORM view is accepted; all
    // other shapes are rejected before any GPU object is created.
    if (texture->view_type != VK_IMAGE_VIEW_TYPE_2D || texture->layers != 1 || texture->levels < 1)
        return VK_ERROR_FEATURE_NOT_PRESENT;
    if (texture->format != VK_FORMAT_R8G8B8A8_UNORM) return VK_ERROR_FORMAT_NOT_SUPPORTED;
    if (texture->width > vk->properties.limits.maxImageDimension2D ||
        texture->height > vk->properties.limits.maxImageDimension2D) return VK_ERROR_FORMAT_NOT_SUPPORTED;
    VkFormatProperties tex_format;
    vkGetPhysicalDeviceFormatProperties(vk->physical, VK_FORMAT_R8G8B8A8_UNORM, &tex_format);
    if ((tex_format.optimalTilingFeatures & (VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                             VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) !=
        (VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    if (!finite_float(uv_scale) || uv_scale <= 0 || !finite_float(lod)) return VK_ERROR_INITIALIZATION_FAILED;
    uint64_t bytes = (uint64_t)width * height * 4;
    if (bytes > pixels->capacity) return VK_ERROR_INITIALIZATION_FAILED;
    VkFormatProperties format;
    vkGetPhysicalDeviceFormatProperties(vk->physical, VK_FORMAT_R8G8B8A8_UNORM, &format);
    VkFormatFeatureFlags need = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
    if ((format.optimalTilingFeatures & need) != need) return VK_ERROR_FORMAT_NOT_SUPPORTED;

    VkImage image = VK_NULL_HANDLE; VkDeviceMemory memory = VK_NULL_HANDLE; VkImageView view = VK_NULL_HANDLE;
    VkRenderPass render = VK_NULL_HANDLE; VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkShaderModule vertex = VK_NULL_HANDLE, fragment = VK_NULL_HANDLE;
    VkDescriptorSetLayout dsl = VK_NULL_HANDLE; VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE; VkDescriptorPool pool = VK_NULL_HANDLE; VkDescriptorSet set = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE; dm_vk_buffer buffer = {0};
    VkResult result = VK_ERROR_INITIALIZATION_FAILED;
    VkImageCreateInfo create = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM, .extent = {width, height, 1}, .mipLevels = 1, .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    result = vkCreateImage(vk->device, &create, NULL, &image);
    if (result != VK_SUCCESS) goto done;
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(vk->device, image, &req);
    uint32_t selected = UINT32_MAX;
    for (uint32_t i = 0; i < vk->memory.memoryTypeCount; i++)
        if (req.memoryTypeBits & (1u << i)) { selected = i; if (vk->memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) break; }
    if (selected == UINT32_MAX) { result = VK_ERROR_FEATURE_NOT_PRESENT; goto done; }
    VkMemoryAllocateInfo alloc = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = req.size, .memoryTypeIndex = selected};
    result = vkAllocateMemory(vk->device, &alloc, NULL, &memory);
    if (result != VK_SUCCESS) goto done;
    result = vkBindImageMemory(vk->device, image, memory, 0);
    if (result != VK_SUCCESS) goto done;
    VkImageViewCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = image, .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    result = vkCreateImageView(vk->device, &vi, NULL, &view);
    if (result != VK_SUCCESS) goto done;
    VkAttachmentDescription attachment = {.format = VK_FORMAT_R8G8B8A8_UNORM, .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference reference = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, .colorAttachmentCount = 1, .pColorAttachments = &reference};
    VkSubpassDependency dependency = {.srcSubpass = VK_SUBPASS_EXTERNAL, .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT};
    VkRenderPassCreateInfo pass = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, .attachmentCount = 1, .pAttachments = &attachment,
        .subpassCount = 1, .pSubpasses = &subpass, .dependencyCount = 1, .pDependencies = &dependency};
    result = vkCreateRenderPass(vk->device, &pass, NULL, &render);
    if (result != VK_SUCCESS) goto done;
    VkFramebufferCreateInfo fb = {.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = render,
        .attachmentCount = 1, .pAttachments = &view, .width = width, .height = height, .layers = 1};
    result = vkCreateFramebuffer(vk->device, &fb, NULL, &framebuffer);
    if (result != VK_SUCCESS) goto done;
    result = dm_vk_shader_load(vk, vertex_path, &vertex);
    if (result != VK_SUCCESS) goto done;
    result = dm_vk_shader_load(vk, fragment_path, &fragment);
    if (result != VK_SUCCESS) goto done;
    VkDescriptorSetLayoutBinding binding = {.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT};
    VkDescriptorSetLayoutCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1, .pBindings = &binding};
    result = vkCreateDescriptorSetLayout(vk->device, &dci, NULL, &dsl);
    if (result != VK_SUCCESS) goto done;
    VkPushConstantRange range = {.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT, .offset = 0, .size = sizeof(float) * 4};
    VkPipelineLayoutCreateInfo lci = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1, .pSetLayouts = &dsl, .pushConstantRangeCount = 1, .pPushConstantRanges = &range};
    result = vkCreatePipelineLayout(vk->device, &lci, NULL, &layout);
    if (result != VK_SUCCESS) goto done;
    VkPipelineShaderStageCreateInfo stages[2] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vertex, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fragment, .pName = "main"}};
    VkPipelineVertexInputStateCreateInfo input = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkViewport viewport = {.width = (float)width, .height = (float)height, .maxDepth = 1};
    VkRect2D scissor = {.extent = {width, height}};
    VkPipelineViewportStateCreateInfo vp = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .pViewports = &viewport, .scissorCount = 1, .pScissors = &scissor};
    VkPipelineRasterizationStateCreateInfo raster = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE, .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1};
    VkPipelineMultisampleStateCreateInfo samples = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState blend = {.colorWriteMask = 0xf};
    VkPipelineColorBlendStateCreateInfo blending = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &blend};
    VkGraphicsPipelineCreateInfo graphics = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2, .pStages = stages, .pVertexInputState = &input, .pInputAssemblyState = &assembly,
        .pViewportState = &vp, .pRasterizationState = &raster, .pMultisampleState = &samples,
        .pColorBlendState = &blending, .layout = layout, .renderPass = render, .basePipelineIndex = -1};
    result = vkCreateGraphicsPipelines(vk->device, VK_NULL_HANDLE, 1, &graphics, NULL, &pipeline);
    if (result != VK_SUCCESS) goto done;
    VkDescriptorPoolSize psz = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo pci = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 1,
        .poolSizeCount = 1, .pPoolSizes = &psz};
    result = vkCreateDescriptorPool(vk->device, &pci, NULL, &pool);
    if (result != VK_SUCCESS) goto done;
    VkDescriptorSetAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = pool, .descriptorSetCount = 1, .pSetLayouts = &dsl};
    result = vkAllocateDescriptorSets(vk->device, &ai, &set);
    if (result != VK_SUCCESS) goto done;
    VkDescriptorImageInfo dii = {.sampler = sampler->handle, .imageView = texture->view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet wds = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = 0,
        .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &dii};
    vkUpdateDescriptorSets(vk->device, 1, &wds, 0, NULL);
    dm_vk_buffer_info bi = {.size = bytes, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    result = dm_vk_buffer_create(vk, &bi, &buffer);
    if (result != VK_SUCCESS) goto done;
    result = dm_vk_begin(vk, &command);
    if (result != VK_SUCCESS) goto done;
    VkClearValue clear = {.color = {.float32 = {0, 0, 0, 1}}};
    VkRenderPassBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = render,
        .framebuffer = framebuffer, .renderArea = {.extent = {width, height}}, .clearValueCount = 1, .pClearValues = &clear};
    vkCmdBeginRenderPass(command, &begin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 0, NULL);
    float params[4] = {1.0f / (float)width, 1.0f / (float)height, uv_scale, lod};
    vkCmdPushConstants(command, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof params, params);
    vkCmdDraw(command, 3, 1, 0, 0);
    vkCmdEndRenderPass(command);
    VkImageMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, NULL, 0, NULL, 1, &barrier);
    VkBufferImageCopy copy = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {width, height, 1}};
    vkCmdCopyImageToBuffer(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer.handle, 1, &copy);
    VkBufferMemoryBarrier host = {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = buffer.handle, .size = VK_WHOLE_SIZE};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 1, &host, 0, NULL);
    result = dm_vk_submit(vk, command);
    command = VK_NULL_HANDLE;
    if (result != VK_SUCCESS) goto done;
    result = dm_vk_buffer_sync(vk, &buffer, 0);
    if (result != VK_SUCCESS) goto done;
    memcpy(pixels->bytes, buffer.mapped, (size_t)bytes);
done:
    if (command) vkFreeCommandBuffers(vk->device, vk->pool, 1, &command);
    dm_vk_buffer_destroy(vk, &buffer);
    if (pipeline) vkDestroyPipeline(vk->device, pipeline, NULL);
    if (pool) vkDestroyDescriptorPool(vk->device, pool, NULL);
    if (layout) vkDestroyPipelineLayout(vk->device, layout, NULL);
    if (dsl) vkDestroyDescriptorSetLayout(vk->device, dsl, NULL);
    if (fragment) vkDestroyShaderModule(vk->device, fragment, NULL);
    if (vertex) vkDestroyShaderModule(vk->device, vertex, NULL);
    if (framebuffer) vkDestroyFramebuffer(vk->device, framebuffer, NULL);
    if (render) vkDestroyRenderPass(vk->device, render, NULL);
    if (view) vkDestroyImageView(vk->device, view, NULL);
    if (image) vkDestroyImage(vk->device, image, NULL);
    if (memory) vkFreeMemory(vk->device, memory, NULL);
    return result;
}
