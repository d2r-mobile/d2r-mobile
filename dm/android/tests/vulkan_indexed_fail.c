// #112 indexed 离屏绘制失败注入：真实 vk_indexed.c/vk_memory.c/vk_host.c + mock Vulkan。
// 覆盖 pipeline 创建、命令结束、读回失效；验证错误、调用者输入/画布不变、全部自有对象计数平衡。
// #112 indexed offscreen failure injection against the real module with mock Vulkan: pipeline creation, command end
// and readback invalidation; error, borrowed inputs/canvas unchanged, and every owned object balanced.
#include "../vulkan/vk_indexed.h"
#include "../vulkan/vk_graphics_interface.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)
#define HANDLE(type, v) ((type)(uintptr_t)(v))

static int fail_stage;
static unsigned shader_calls, buffer_calls, memory_calls, submit_calls;
static unsigned fail_shader_call, fail_buffer_call, fail_memory_call, fail_submit_call;
static unsigned format_queries;
static unsigned buffers, memories, images, views, renderpasses, framebuffers, shaders, layouts, pipelines, commands, fences, mapped;
static unsigned fail_hit, hit_buffers, hit_memories, hit_images, hit_views, hit_renderpasses, hit_framebuffers;
static unsigned hit_shaders, hit_layouts, hit_pipelines, hit_commands, hit_fences, hit_mapped;
static uint8_t staging[4096];

static void reset(void) {
    format_queries = 0;
    shader_calls = buffer_calls = memory_calls = submit_calls = 0;
    fail_shader_call = fail_buffer_call = fail_memory_call = fail_submit_call = 0;
    buffers = memories = images = views = renderpasses = framebuffers = shaders = layouts = pipelines = commands = fences = mapped = 0;
    fail_hit = hit_buffers = hit_memories = hit_images = hit_views = hit_renderpasses = hit_framebuffers = 0;
    hit_shaders = hit_layouts = hit_pipelines = hit_commands = hit_fences = hit_mapped = 0;
    memset(staging, 0x7f, sizeof staging);
}
static void balanced(void) {
    CHECK(!buffers && !memories && !images && !views && !renderpasses && !framebuffers);
    CHECK(!shaders && !layouts && !pipelines && !commands && !fences && !mapped);
}
static void record_hit(void) {
    hit_buffers = buffers; hit_memories = memories; hit_images = images; hit_views = views;
    hit_renderpasses = renderpasses; hit_framebuffers = framebuffers; hit_shaders = shaders;
    hit_layouts = layouts; hit_pipelines = pipelines; hit_commands = commands; hit_fences = fences; hit_mapped = mapped;
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice p, VkFormat f, VkFormatProperties *o) {
    (void)p; CHECK(f == VK_FORMAT_R8G8B8A8_UNORM); format_queries++;
    o->optimalTilingFeatures = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateBuffer(VkDevice d, const VkBufferCreateInfo *i, const VkAllocationCallbacks *a, VkBuffer *o) {
    (void)d; (void)a; CHECK(i->size > 0 && i->sharingMode == VK_SHARING_MODE_EXCLUSIVE);
    buffer_calls++;
    if (fail_buffer_call == buffer_calls) { fail_hit = 11; record_hit(); fail_buffer_call = 0; return VK_ERROR_OUT_OF_HOST_MEMORY; }
    buffers++; *o = HANDLE(VkBuffer, 2 + buffers); return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements(VkDevice d, VkBuffer b, VkMemoryRequirements *o) {
    (void)d; (void)b; *o = (VkMemoryRequirements){.size = 256, .alignment = 16, .memoryTypeBits = 2};
}
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(VkDevice d, const VkMemoryAllocateInfo *i, const VkAllocationCallbacks *a, VkDeviceMemory *o) {
    (void)d; (void)a; CHECK((i->memoryTypeIndex == 0 || i->memoryTypeIndex == 1) && i->allocationSize);
    memory_calls++;
    if (fail_memory_call == memory_calls) { fail_hit = 12; record_hit(); fail_memory_call = 0; return VK_ERROR_OUT_OF_HOST_MEMORY; }
    memories++; *o = HANDLE(VkDeviceMemory, 20 + memories); return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory(VkDevice d, VkBuffer b, VkDeviceMemory m, VkDeviceSize off) { (void)d; (void)b; (void)m; CHECK(!off); return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory(VkDevice d, VkDeviceMemory m, VkDeviceSize o, VkDeviceSize s, VkMemoryMapFlags f, void **p) {
    (void)d; (void)m; CHECK(!o && s == VK_WHOLE_SIZE && !f); mapped++; *p = staging; return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkFlushMappedMemoryRanges(VkDevice d, uint32_t n, const VkMappedMemoryRange *r) {
    (void)d; CHECK(n == 1 && !r->offset && r->size == VK_WHOLE_SIZE); return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkInvalidateMappedMemoryRanges(VkDevice d, uint32_t n, const VkMappedMemoryRange *r) {
    (void)d; CHECK(n == 1 && !r->offset && r->size == VK_WHOLE_SIZE);
    if (fail_stage == 3) { fail_hit = 3; record_hit(); return VK_ERROR_OUT_OF_HOST_MEMORY; }
    return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateImage(VkDevice d, const VkImageCreateInfo *i, const VkAllocationCallbacks *a, VkImage *o) {
    (void)d; (void)a; CHECK(i->format == VK_FORMAT_R8G8B8A8_UNORM && i->extent.width == 16 && i->extent.height == 16);
    images++; *o = HANDLE(VkImage, 30); return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements(VkDevice d, VkImage i, VkMemoryRequirements *o) { (void)d; (void)i; *o = (VkMemoryRequirements){.size = 4096, .alignment = 16, .memoryTypeBits = 3}; }
VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory(VkDevice d, VkImage i, VkDeviceMemory m, VkDeviceSize o) { (void)d; (void)i; (void)m; CHECK(!o); return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL vkCreateImageView(VkDevice d, const VkImageViewCreateInfo *i, const VkAllocationCallbacks *a, VkImageView *o) {
    (void)d; (void)a; CHECK(i->image == HANDLE(VkImage, 30) && i->format == VK_FORMAT_R8G8B8A8_UNORM);
    views++; *o = HANDLE(VkImageView, 31); return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateRenderPass(VkDevice d, const VkRenderPassCreateInfo *i, const VkAllocationCallbacks *a, VkRenderPass *o) {
    (void)d; (void)a; CHECK(i->attachmentCount == 1 && i->subpassCount == 1); renderpasses++; *o = HANDLE(VkRenderPass, 32); return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateFramebuffer(VkDevice d, const VkFramebufferCreateInfo *i, const VkAllocationCallbacks *a, VkFramebuffer *o) {
    (void)d; (void)a; CHECK(i->width == 16 && i->height == 16 && i->layers == 1); framebuffers++; *o = HANDLE(VkFramebuffer, 33); return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateShaderModule(VkDevice d, const VkShaderModuleCreateInfo *i, const VkAllocationCallbacks *a, VkShaderModule *o) {
    (void)d; (void)a; CHECK(i && i->codeSize >= 4 && i->pCode);
    shader_calls++;
    if ((fail_stage == 4 && shader_calls == 1) || fail_shader_call == shader_calls) {
        fail_hit = 4; record_hit(); fail_shader_call = 0; return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    shaders++; *o = HANDLE(VkShaderModule, 34 + shaders); return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyShaderModule(VkDevice d, VkShaderModule m, const VkAllocationCallbacks *a) { (void)d; (void)m; (void)a; CHECK(shaders >= 1); shaders--; }
VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineLayout(VkDevice d, const VkPipelineLayoutCreateInfo *i, const VkAllocationCallbacks *a, VkPipelineLayout *o) {
    (void)d; (void)i; (void)a; layouts++; *o = HANDLE(VkPipelineLayout, 36); return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyPipelineLayout(VkDevice d, VkPipelineLayout l, const VkAllocationCallbacks *a) { (void)d; (void)l; (void)a; CHECK(layouts == 1); layouts--; }
VKAPI_ATTR VkResult VKAPI_CALL vkCreateGraphicsPipelines(VkDevice d, VkPipelineCache c, uint32_t n, const VkGraphicsPipelineCreateInfo *i, const VkAllocationCallbacks *a, VkPipeline *o) {
    (void)d; (void)c; (void)a; CHECK(n == 1 && i->stageCount == 2 && i->pVertexInputState->vertexAttributeDescriptionCount == 2);
    if (fail_stage == 1) { fail_hit = 1; record_hit(); return VK_ERROR_OUT_OF_HOST_MEMORY; }
    pipelines++; *o = HANDLE(VkPipeline, 37); return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyPipeline(VkDevice d, VkPipeline p, const VkAllocationCallbacks *a) { (void)d; (void)p; (void)a; CHECK(pipelines == 1); pipelines--; }
VKAPI_ATTR void VKAPI_CALL vkDestroyFramebuffer(VkDevice d, VkFramebuffer f, const VkAllocationCallbacks *a) { (void)d; (void)f; (void)a; CHECK(framebuffers == 1); framebuffers--; }
VKAPI_ATTR void VKAPI_CALL vkDestroyRenderPass(VkDevice d, VkRenderPass r, const VkAllocationCallbacks *a) { (void)d; (void)r; (void)a; CHECK(renderpasses == 1); renderpasses--; }
VKAPI_ATTR void VKAPI_CALL vkDestroyImageView(VkDevice d, VkImageView v, const VkAllocationCallbacks *a) { (void)d; (void)v; (void)a; CHECK(views == 1); views--; }
VKAPI_ATTR void VKAPI_CALL vkDestroyImage(VkDevice d, VkImage i, const VkAllocationCallbacks *a) { (void)d; (void)i; (void)a; CHECK(images == 1); images--; }
VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(VkDevice d, VkBuffer b, const VkAllocationCallbacks *a) { (void)d; (void)b; (void)a; CHECK(buffers >= 1); buffers--; }
VKAPI_ATTR void VKAPI_CALL vkUnmapMemory(VkDevice d, VkDeviceMemory m) { (void)d; (void)m; CHECK(mapped >= 1); mapped--; }
VKAPI_ATTR void VKAPI_CALL vkFreeMemory(VkDevice d, VkDeviceMemory m, const VkAllocationCallbacks *a) { (void)d; (void)m; (void)a; CHECK(memories >= 1); memories--; }
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateCommandBuffers(VkDevice d, const VkCommandBufferAllocateInfo *i, VkCommandBuffer *o) {
    (void)d; CHECK(i->commandBufferCount == 1); commands++; *o = HANDLE(VkCommandBuffer, 40); return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkBeginCommandBuffer(VkCommandBuffer c, const VkCommandBufferBeginInfo *i) { (void)c; CHECK(i->flags == VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT); return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL vkEndCommandBuffer(VkCommandBuffer c) {
    (void)c;
    if (fail_stage == 2) { fail_hit = 2; record_hit(); return VK_ERROR_OUT_OF_HOST_MEMORY; }
    return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateFence(VkDevice d, const VkFenceCreateInfo *i, const VkAllocationCallbacks *a, VkFence *o) {
    (void)d; (void)i; (void)a; fences++; *o = HANDLE(VkFence, 41); return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(VkQueue q, uint32_t n, const VkSubmitInfo *i, VkFence f) {
    (void)q; (void)i; (void)f; CHECK(n == 1);
    submit_calls++;
    if (fail_submit_call == submit_calls) { fail_hit = 13; record_hit(); fail_submit_call = 0; return VK_ERROR_OUT_OF_HOST_MEMORY; }
    return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkWaitForFences(VkDevice d, uint32_t n, const VkFence *f, VkBool32 a, uint64_t t) { (void)d; (void)f; CHECK(n == 1 && a && t == UINT64_MAX); return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL vkDeviceWaitIdle(VkDevice d) { (void)d; return VK_SUCCESS; }
VKAPI_ATTR void VKAPI_CALL vkDestroyFence(VkDevice d, VkFence f, const VkAllocationCallbacks *a) { (void)d; (void)f; (void)a; CHECK(fences == 1); fences--; }
VKAPI_ATTR void VKAPI_CALL vkFreeCommandBuffers(VkDevice d, VkCommandPool p, uint32_t n, const VkCommandBuffer *c) { (void)d; (void)p; (void)c; CHECK(n == 1 && commands == 1); commands--; }
VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderPass(VkCommandBuffer c, const VkRenderPassBeginInfo *i, VkSubpassContents s) { (void)c; (void)i; CHECK(s == VK_SUBPASS_CONTENTS_INLINE); }
VKAPI_ATTR void VKAPI_CALL vkCmdBindPipeline(VkCommandBuffer c, VkPipelineBindPoint b, VkPipeline p) { (void)c; (void)p; CHECK(b == VK_PIPELINE_BIND_POINT_GRAPHICS); }
VKAPI_ATTR void VKAPI_CALL vkCmdBindVertexBuffers(VkCommandBuffer c, uint32_t first, uint32_t n, const VkBuffer *b, const VkDeviceSize *o) {
    (void)c; (void)b; CHECK(first == 0 && n == 1 && o && !*o);
}
VKAPI_ATTR void VKAPI_CALL vkCmdBindIndexBuffer(VkCommandBuffer c, VkBuffer b, VkDeviceSize o, VkIndexType t) { (void)c; (void)b; CHECK(!o && t == VK_INDEX_TYPE_UINT16); }
VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexed(VkCommandBuffer c, uint32_t count, uint32_t inst, uint32_t first, int32_t base, uint32_t first_inst) {
    (void)c; CHECK(count == 6 && inst == 1 && first == 0 && base == 0 && first_inst == 0);
}
VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderPass(VkCommandBuffer c) { (void)c; }
VKAPI_ATTR void VKAPI_CALL vkCmdCopyImageToBuffer(VkCommandBuffer c, VkImage i, VkImageLayout l, VkBuffer b, uint32_t n, const VkBufferImageCopy *r) {
    (void)c; (void)i; (void)l; (void)b; CHECK(n == 1 && r->imageExtent.width == 16 && r->imageExtent.height == 16);
}
VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer c, VkPipelineStageFlags s, VkPipelineStageFlags d, VkDependencyFlags f,
    uint32_t mc, const VkMemoryBarrier *m, uint32_t bc, const VkBufferMemoryBarrier *b, uint32_t ic, const VkImageMemoryBarrier *i) {
    (void)c; (void)s; (void)d; (void)f; (void)m; (void)mc; (void)b; (void)i;
    CHECK((ic == 1 && !bc) || (bc == 1 && !ic));
}

int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "usage: %s indexed.vert.spv indexed.frag.spv\n", argv[0]); return 2; }
    dm_vk_indexed_vertex vertices[6];
    for (unsigned i = 0; i < 6; i++) {
        vertices[i] = (dm_vk_indexed_vertex){{-0.5f + (float)i * 0.2f, -0.5f}, {1, 1, 1, 1}};
    }
    uint16_t indices[6] = {0, 1, 2, 3, 4, 5};
    dm_vk_indexed_vertex vertices_before[6]; uint16_t indices_before[6];
    memcpy(vertices_before, vertices, sizeof vertices); memcpy(indices_before, indices, sizeof indices);
    uint8_t output[16 * 16 * 4];
    dm_vk_pixels pixels = {.width = 16, .height = 16, .bytes = output, .capacity = sizeof output};
    dm_vk_indexed_draw draw = {.vertices = vertices, .vertex_count = 6, .vertex_stride = DM_VK_INDEXED_VERTEX_STRIDE,
        .indices = indices, .index_count = 6, .vertex_spv = argv[1], .fragment_spv = argv[2]};
    dm_vk vk = {.device = HANDLE(VkDevice, 50), .physical = HANDLE(VkPhysicalDevice, 51), .queue = HANDLE(VkQueue, 52),
        .pool = HANDLE(VkCommandPool, 53)};
    vk.memory.memoryTypeCount = 2;
    vk.memory.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    vk.memory.memoryTypes[1].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    vk.properties.limits.maxImageDimension2D = 16384;
    vk.properties.limits.maxViewportDimensions[0] = 16384;
    vk.properties.limits.maxViewportDimensions[1] = 16384;
    vk.properties.limits.maxFramebufferWidth = 16384;
    vk.properties.limits.maxFramebufferHeight = 16384;
    vk.properties.limits.maxVertexInputBindings = 4;
    vk.properties.limits.maxVertexInputAttributes = 4;
    vk.properties.limits.maxStorageBufferRange = 1u << 24;

    for (fail_stage = 0; fail_stage <= 3; fail_stage++) {
        reset(); memset(output, 0x5a, sizeof output);
        VkResult r = dm_vk_draw_indexed_offscreen(&vk, &draw, &pixels);
        if (!fail_stage) {
            CHECK(r == VK_SUCCESS);
        } else {
            CHECK(r != VK_SUCCESS);
            CHECK(fail_hit == (unsigned)fail_stage);
            for (size_t i = 0; i < sizeof output; i++) CHECK(output[i] == 0x5a);
            if (fail_stage == 1) {
                CHECK(hit_buffers == 3 && hit_memories == 4 && hit_images == 1 && hit_views == 1);
                CHECK(hit_renderpasses == 1 && hit_framebuffers == 1 && hit_shaders == 2 && hit_layouts == 1);
                CHECK(hit_pipelines == 0 && hit_commands == 0);
            } else if (fail_stage == 2) {
                CHECK(hit_buffers == 3 && hit_memories == 4 && hit_shaders == 2 && hit_layouts == 1 && hit_pipelines == 1);
                CHECK(hit_commands == 1 && hit_fences == 0);
            } else {
                CHECK(hit_buffers == 3 && hit_memories == 4 && hit_images == 1 && hit_shaders == 2 && hit_layouts == 1 && hit_pipelines == 1);
                CHECK(hit_commands == 0 && hit_fences == 0 && hit_mapped == 3);
            }
        }
        CHECK(!memcmp(vertices, vertices_before, sizeof vertices));
        CHECK(!memcmp(indices, indices_before, sizeof indices));
        balanced();
    }
    // #137 实际 plan 路径：plan-owned vkCreateShaderModule、失败回滚、成功/失败/重试。
    dm_vk_graphics_plan plan; char why[DM_VK_GRAPHICS_WHY] = {0};
    CHECK(dm_vk_graphics_plan_load(argv[1], argv[2], NULL, &plan, why, sizeof why) == VK_SUCCESS);
    draw.vertex_spv = NULL; draw.fragment_spv = NULL;
    for (fail_stage = 0; fail_stage <= 4; fail_stage++) {
        reset(); memset(output, 0x5a, sizeof output);
        VkResult r = dm_vk_draw_indexed_plan_offscreen(&vk, &draw, &plan, &pixels);
        if (!fail_stage) {
            CHECK(r == VK_SUCCESS);
        } else {
            CHECK(r != VK_SUCCESS);
            CHECK(fail_hit == (unsigned)fail_stage);
            for (size_t i = 0; i < sizeof output; i++) CHECK(output[i] == 0x5a);
            if (fail_stage == 4) {
                CHECK(hit_buffers == 2 && hit_memories == 2 && hit_images == 0 && hit_views == 0);
                CHECK(hit_shaders == 0 && hit_layouts == 0 && hit_pipelines == 0 && hit_commands == 0);
            }
        }
        CHECK(!memcmp(vertices, vertices_before, sizeof vertices));
        CHECK(!memcmp(indices, indices_before, sizeof indices));
        balanced();
    }
    // 第二个 shader 在第一个 owned shader 已存在后失败。
    reset(); fail_shader_call = 2; memset(output, 0x5a, sizeof output);
    CHECK(dm_vk_draw_indexed_plan_offscreen(&vk, &draw, &plan, &pixels) != VK_SUCCESS);
    CHECK(fail_hit == 4 && hit_shaders == 1);
    for (size_t i = 0; i < sizeof output; i++) CHECK(output[i] == 0x5a);
    balanced();
    reset(); CHECK(dm_vk_draw_indexed_plan_offscreen(&vk, &draw, &plan, &pixels) == VK_SUCCESS); balanced();
    // 第 1..3 次 buffer 创建失败（包含已有 owned buffer 后）。
    for (unsigned call = 1; call <= 3; call++) {
        reset(); fail_buffer_call = call; memset(output, 0x5a, sizeof output);
        CHECK(dm_vk_draw_indexed_plan_offscreen(&vk, &draw, &plan, &pixels) != VK_SUCCESS);
        CHECK(fail_hit == 11); for (size_t i = 0; i < sizeof output; i++) CHECK(output[i] == 0x5a); balanced();
        reset(); CHECK(dm_vk_draw_indexed_plan_offscreen(&vk, &draw, &plan, &pixels) == VK_SUCCESS); balanced();
    }
    // 第 1..3 次 image/buffer memory 分配失败。
    for (unsigned call = 1; call <= 3; call++) {
        reset(); fail_memory_call = call; memset(output, 0x5a, sizeof output);
        CHECK(dm_vk_draw_indexed_plan_offscreen(&vk, &draw, &plan, &pixels) != VK_SUCCESS);
        CHECK(fail_hit == 12); for (size_t i = 0; i < sizeof output; i++) CHECK(output[i] == 0x5a); balanced();
        reset(); CHECK(dm_vk_draw_indexed_plan_offscreen(&vk, &draw, &plan, &pixels) == VK_SUCCESS); balanced();
    }
    // 实际 draw submission 失败。
    reset(); fail_submit_call = 1; memset(output, 0x5a, sizeof output);
    CHECK(dm_vk_draw_indexed_plan_offscreen(&vk, &draw, &plan, &pixels) != VK_SUCCESS);
    CHECK(fail_hit == 13); for (size_t i = 0; i < sizeof output; i++) CHECK(output[i] == 0x5a); balanced();
    reset(); CHECK(dm_vk_draw_indexed_plan_offscreen(&vk, &draw, &plan, &pixels) == VK_SUCCESS); balanced();
    // 错误 kind 必须在任何 Vulkan format query 前拒绝。
    reset();
    plan.kind = DM_VK_PLAN_TEXTURED;
    CHECK(dm_vk_draw_indexed_plan_offscreen(&vk, &draw, &plan, &pixels) != VK_SUCCESS);
    CHECK(format_queries == 0);
    balanced();
    plan.kind = DM_VK_PLAN_COLORED;
    // 失败后再次成功，证明可重试且 plan/借用输入未被破坏。
    reset(); CHECK(dm_vk_draw_indexed_plan_offscreen(&vk, &draw, &plan, &pixels) == VK_SUCCESS);
    balanced();
    dm_vk_graphics_plan_free(&plan);
    printf("vulkan_indexed_fail: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
