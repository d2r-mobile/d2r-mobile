// NVN RGBA8_SRGB（0x38）的自有 Vulkan 回归：同一批存储字节，sRGB 映像采样得到线性值，UNORM 映像得到原始值；
// 原始字节读回不经编解码；不支持的 NVN 格式如实拒绝。期望值由本文件按 sRGB EOTF 公式独立计算。
// Self-owned Vulkan regression for NVN RGBA8_SRGB (0x38): the same stored bytes give linear values when sampled from an
// sRGB image and raw values from a UNORM image; raw byte readback involves no encode/decode; unsupported NVN formats are
// refused truthfully. Expectations are computed here from the sRGB EOTF formula.
#include "../vulkan/vk_host.h"
#include "../../gfx/tex/nvn_deswizzle.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);exit(1);}}while(0)
#define VK_CHECK(call) do{VkResult r=(call);if(r!=VK_SUCCESS){fprintf(stderr,"FAIL %s = %d\n",#call,r);exit(1);}}while(0)

enum { W = 2, H = 2 };
static unsigned checks, failures;
#define EXPECT(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #cond); } } while (0)

// 2×2 存储字节（sRGB 编码值）。 | 2x2 stored bytes (sRGB-encoded values).
static const unsigned char stored[W * H * 4] = {
    128, 64, 32, 255,
    255, 0, 128, 128,
    10, 20, 30, 40,
    200, 100, 50, 25,
};
// sRGB EOTF：编码值 → 线性；RGB 用，alpha 不用。 | sRGB EOTF: encoded -> linear; used for RGB, not alpha.
static double srgb_to_linear(double c) {
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}
// packUnorm4x8 会把 [0,1] 量化回 8 位；这里按同一量化给出期望字节（±1 容差覆盖驱动舍入）。
// packUnorm4x8 quantizes [0,1] back to 8 bits; the expectation uses the same quantization (+-1 covers driver rounding).
static unsigned quantize(double linear) {
    double v = linear * 255.0 + 0.5;
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    return (unsigned)v;
}

typedef struct { nvn_texdesc desc; nvn_texlayout layout; unsigned char *storage; } fixture;
static fixture make_fixture(int format) {
    fixture f = {.desc = {.target = 1, .format = format, .width = W, .height = H, .depth = 1, .levels = 1}};
    nvn_tex_layout(&f.desc, &f.layout);
    f.storage = calloc(1, f.layout.size);
    CHECK(f.storage);
    CHECK(!nvn_swizzle(&f.desc, 0, 0, stored, W * 4, f.storage));
    return f;
}

// 用生产采样着色器（sample_texture.comp 的 2D 变体）逐像素 texelFetch，回读 packUnorm4x8 的结果。
// Fetch every pixel with the production sampling shader (2D variant of sample_texture.comp) and read back packUnorm4x8.
static void sample(dm_vk *vk, const dm_vk_texture *texture, const char *shader_path, unsigned char out[W * H * 4]) {
    VkShaderModule shader; VK_CHECK(dm_vk_shader_load(vk, shader_path, &shader));
    VkDescriptorSetLayoutBinding bindings[2] = {
        {.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT}};
    VkDescriptorSetLayoutCreateInfo di = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 2, .pBindings = bindings};
    VkDescriptorSetLayout descriptor; VK_CHECK(vkCreateDescriptorSetLayout(vk->device, &di, NULL, &descriptor));
    VkPushConstantRange push = {.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .size = 16};
    VkPipelineLayoutCreateInfo li = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &descriptor,
        .pushConstantRangeCount = 1, .pPushConstantRanges = &push};
    VkPipelineLayout layout; VK_CHECK(vkCreatePipelineLayout(vk->device, &li, NULL, &layout));
    VkComputePipelineCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, .layout = layout,
        .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = shader, .pName = "main"}};
    VkPipeline pipeline; VK_CHECK(vkCreateComputePipelines(vk->device, VK_NULL_HANDLE, 1, &ci, NULL, &pipeline));
    VkDescriptorPoolSize sizes[2] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}};
    VkDescriptorPoolCreateInfo pi = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 1, .poolSizeCount = 2, .pPoolSizes = sizes};
    VkDescriptorPool pool; VK_CHECK(vkCreateDescriptorPool(vk->device, &pi, NULL, &pool));
    VkDescriptorSetAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = pool, .descriptorSetCount = 1, .pSetLayouts = &descriptor};
    VkDescriptorSet set; VK_CHECK(vkAllocateDescriptorSets(vk->device, &ai, &set));
    VkSamplerCreateInfo si = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .magFilter = VK_FILTER_NEAREST, .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST, .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .maxLod = 0};
    VkSampler sampler; VK_CHECK(vkCreateSampler(vk->device, &si, NULL, &sampler));
    dm_vk_buffer buffer = {0}; dm_vk_buffer_info bi = {.size = W * H * 4, .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT};
    VK_CHECK(dm_vk_buffer_create(vk, &bi, &buffer));
    VkDescriptorImageInfo image = {.sampler = sampler, .imageView = texture->view, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorBufferInfo storage = {.buffer = buffer.handle, .range = W * H * 4};
    VkWriteDescriptorSet writes[2] = {
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = 0, .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &image},
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = 1, .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &storage}};
    vkUpdateDescriptorSets(vk->device, 2, writes, 0, NULL);
    memset(buffer.mapped, 0xA6, W * H * 4);
    VK_CHECK(dm_vk_buffer_sync(vk, &buffer, 1));
    VkCommandBuffer command; VK_CHECK(dm_vk_begin(vk, &command));
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set, 0, NULL);
    uint32_t shape[4] = {W, H, 0, 0};
    vkCmdPushConstants(command, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof shape, shape);
    vkCmdDispatch(command, 1, 1, 1);
    VkBufferMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_HOST_READ_BIT, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .buffer = buffer.handle, .size = VK_WHOLE_SIZE};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 1, &barrier, 0, NULL);
    VK_CHECK(dm_vk_submit(vk, command));
    VK_CHECK(dm_vk_buffer_sync(vk, &buffer, 0));
    memcpy(out, buffer.mapped, W * H * 4);
    dm_vk_buffer_destroy(vk, &buffer);
    vkDestroySampler(vk->device, sampler, NULL);
    vkDestroyDescriptorPool(vk->device, pool, NULL);
    vkDestroyPipeline(vk->device, pipeline, NULL);
    vkDestroyPipelineLayout(vk->device, layout, NULL);
    vkDestroyDescriptorSetLayout(vk->device, descriptor, NULL);
    vkDestroyShaderModule(vk->device, shader, NULL);
}

int main(int argc, char **argv) {
    CHECK(argc == 2);   // sample_texture 2D 变体的 SPIR-V | SPIR-V of the 2D variant of sample_texture
    dm_vk vk = {0};
    VK_CHECK(dm_vk_create(&vk));
    printf("GPU: %s Vulkan %u.%u.%u\n", vk.properties.deviceName, VK_VERSION_MAJOR(vk.properties.apiVersion),
        VK_VERSION_MINOR(vk.properties.apiVersion), VK_VERSION_PATCH(vk.properties.apiVersion));

    // 1. sRGB 映像：上传原始字节、读回原始字节（不经编解码）。
    // 1. sRGB image: upload the raw bytes, read the raw bytes back (no encode/decode).
    fixture s = make_fixture(0x38);
    dm_vk_texture srgb = {0};
    VK_CHECK(dm_vk_texture_upload(&vk, &s.desc, s.storage, s.layout.size, &srgb));
    EXPECT(srgb.format == VK_FORMAT_R8G8B8A8_SRGB);
    unsigned char raw[W * H * 4];
    const dm_vk_region full = {0, 0, W, H};
    EXPECT(dm_vk_texture_read_rows(&vk, &srgb, 0, 0, &full, 0, raw, sizeof raw) == VK_SUCCESS);
    EXPECT(!memcmp(raw, stored, sizeof raw));

    // 2. 采样 sRGB 映像：RGB 必须等于 sRGB EOTF 的量化值，alpha 保持线性原值。
    // 2. Sample the sRGB image: RGB must equal the quantized sRGB EOTF value, alpha stays linear.
    unsigned char sampled_srgb[W * H * 4];
    sample(&vk, &srgb, argv[1], sampled_srgb);
    unsigned rgb_bad = 0, alpha_bad = 0;
    for (unsigned p = 0; p < W * H; p++) {
        for (unsigned c = 0; c < 3; c++) {
            unsigned want = quantize(srgb_to_linear(stored[p * 4 + c] / 255.0));
            unsigned got = sampled_srgb[p * 4 + c];
            unsigned diff = got > want ? got - want : want - got;
            if (diff > 1) { rgb_bad++; fprintf(stderr, "  pixel %u channel %u: got %u want %u\n", p, c, got, want); }
        }
        alpha_bad += sampled_srgb[p * 4 + 3] != stored[p * 4 + 3];
    }
    EXPECT(rgb_bad == 0);
    EXPECT(alpha_bad == 0);

    // 3. 同一批字节按 UNORM 上传：采样得到原始值，且与 sRGB 期望明显不同（错误映射会被抓住）。
    // 3. The same bytes uploaded as UNORM: sampling returns the raw values, clearly different from the sRGB expectation
    //    (a wrong mapping is caught).
    fixture u = make_fixture(0x25);
    dm_vk_texture unorm = {0};
    VK_CHECK(dm_vk_texture_upload(&vk, &u.desc, u.storage, u.layout.size, &unorm));
    EXPECT(unorm.format == VK_FORMAT_R8G8B8A8_UNORM);
    unsigned char sampled_unorm[W * H * 4];
    sample(&vk, &unorm, argv[1], sampled_unorm);
    unsigned unorm_bad = 0, distinct = 0;
    for (unsigned i = 0; i < W * H * 4; i++) {
        unorm_bad += sampled_unorm[i] != stored[i];
        unsigned d = sampled_unorm[i] > sampled_srgb[i] ? sampled_unorm[i] - sampled_srgb[i] : sampled_srgb[i] - sampled_unorm[i];
        distinct += d;
    }
    EXPECT(unorm_bad == 0);
    EXPECT(distinct >= 100);   // 同一字节按 UNORM 读与按 sRGB 读至少差这么多级 | UNORM vs sRGB reads of the same bytes differ by at least this much in total
    printf("PASS: sRGB sampled RGB differs from UNORM by %u levels in total (same stored bytes)\n", distinct);

    // 4. 行更新在 sRGB 映像上按原始字节生效：改一个像素，读回只变该像素。
    // 4. Row update on the sRGB image works on raw bytes: change one pixel; only that pixel changes on readback.
    const dm_vk_region one = {1, 1, 1, 1};
    unsigned char patch[4] = {9, 8, 7, 6};
    EXPECT(dm_vk_texture_update_rows(&vk, &srgb, 0, 0, &one, patch, sizeof patch, 0) == VK_SUCCESS);
    EXPECT(dm_vk_texture_read_rows(&vk, &srgb, 0, 0, &full, 0, raw, sizeof raw) == VK_SUCCESS);
    EXPECT(!memcmp(raw, stored, 3 * 4));
    EXPECT(!memcmp(raw + 3 * 4, patch, 4));

    // 5. 不支持的 NVN 格式如实拒绝：0x37 RGBX8_SRGB、0x26 RGBA8SN、0x27 RGBA8UI、0x3D RGB10A2 都不猜映射。
    // 5. Unsupported NVN formats are refused truthfully: 0x37 RGBX8_SRGB, 0x26 RGBA8SN, 0x27 RGBA8UI and 0x3D RGB10A2 are not guessed.
    const int unsupported[] = {0x37, 0x26, 0x27, 0x3D};
    for (unsigned i = 0; i < sizeof unsupported / sizeof *unsupported; i++) {
        nvn_texdesc d = s.desc; d.format = unsupported[i];
        dm_vk_texture out; memset(&out, 0x5A, sizeof out); dm_vk_texture before = out;
        EXPECT(dm_vk_texture_upload(&vk, &d, s.storage, s.layout.size, &out) == VK_ERROR_FORMAT_NOT_SUPPORTED);
        EXPECT(!memcmp(&out, &before, sizeof out));
    }

    dm_vk_texture_destroy(&vk, &srgb);
    dm_vk_texture_destroy(&vk, &unorm);
    free(s.storage); free(u.storage);
    printf("vulkan_texture_srgb: %u checks, %u failures\n", checks, failures);
    dm_vk_destroy(&vk);
    CHECK(!vk.device && !vk.instance);
    return failures ? 1 : 0;
}
