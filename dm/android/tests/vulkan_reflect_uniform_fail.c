// #109 uniform 消费后创建失败注入：真实 vk_shader_reflect.c/vk_memory.c/vk_host.c + mock Vulkan。
// 覆盖 descriptor-pool 创建、命令结束、读回失效；检查画布/调用者输入不变、全部自有对象平衡、调用者所有权。
// #109 post-creation failure injection against the real uniform consumer with mock Vulkan: descriptor-pool creation,
// command end and readback invalidation; canvas/caller input unchanged, all owned objects balanced, no caller ownership.
#include "../vulkan/vk_shader_reflect.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)
#define HANDLE(type, v) ((type)(uintptr_t)(v))

static int fail_stage;   // 1 descriptor pool, 2 command end, 3 readback invalidate
static unsigned buffers, memories, commands, fences, mapped, pending, shaders, dsls, layouts, pipelines, pools, sets;
static unsigned flushes, invalidates;
static unsigned fail_hit, hit_shader, hit_dsl, hit_layout, hit_pipeline, hit_pool, hit_set, hit_commands, hit_buffers, hit_memories, hit_mapped;
static uint8_t staging[128];

static void reset(void) {
    buffers = memories = commands = fences = mapped = pending = 0;
    shaders = dsls = layouts = pipelines = pools = sets = 0;
    flushes = invalidates = 0;
    fail_hit = hit_shader = hit_dsl = hit_layout = hit_pipeline = hit_pool = hit_set = 0;
    hit_commands = hit_buffers = hit_memories = hit_mapped = 0;
    memset(staging, 0x7f, sizeof staging);
}
static void balanced(void) {
    CHECK(!buffers && !memories && !commands && !fences && !mapped && !pending);
    CHECK(!shaders && !dsls && !layouts && !pipelines && !pools && !sets);
}
static void record_hit(void) {
    hit_shader = shaders; hit_dsl = dsls; hit_layout = layouts; hit_pipeline = pipelines;
    hit_pool = pools; hit_set = sets; hit_commands = commands; hit_buffers = buffers;
    hit_memories = memories; hit_mapped = mapped;
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateBuffer(VkDevice d, const VkBufferCreateInfo *i, const VkAllocationCallbacks *a, VkBuffer *o) {
    (void)d; (void)a;
    CHECK(i->size == 16 || i->usage == VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    buffers++; *o = HANDLE(VkBuffer, 2 + buffers); return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements(VkDevice d, VkBuffer b, VkMemoryRequirements *o) {
    (void)d; (void)b; *o = (VkMemoryRequirements){64, 16, 2};
}
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(VkDevice d, const VkMemoryAllocateInfo *i, const VkAllocationCallbacks *a, VkDeviceMemory *o) {
    (void)d; (void)a; CHECK(i->memoryTypeIndex == 1); memories++; *o = HANDLE(VkDeviceMemory, 10 + memories); return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory(VkDevice d, VkBuffer b, VkDeviceMemory m, VkDeviceSize off) {
    (void)d; (void)b; (void)m; CHECK(!off); return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory(VkDevice d, VkDeviceMemory m, VkDeviceSize o, VkDeviceSize s, VkMemoryMapFlags f, void **p) {
    (void)d; (void)m; CHECK(!o && s == VK_WHOLE_SIZE && !f); mapped++; *p = staging; return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkFlushMappedMemoryRanges(VkDevice d, uint32_t n, const VkMappedMemoryRange *r) {
    (void)d; CHECK(n == 1 && !r->offset && r->size == VK_WHOLE_SIZE); flushes++; return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkInvalidateMappedMemoryRanges(VkDevice d, uint32_t n, const VkMappedMemoryRange *r) {
    (void)d; CHECK(n == 1 && !r->offset && r->size == VK_WHOLE_SIZE); invalidates++;
    if (fail_stage == 3) { fail_hit = 3; record_hit(); return VK_ERROR_OUT_OF_HOST_MEMORY; }
    return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateShaderModule(VkDevice d, const VkShaderModuleCreateInfo *i, const VkAllocationCallbacks *a, VkShaderModule *o) {
    (void)d; (void)i; (void)a; shaders++; *o = HANDLE(VkShaderModule, 4); return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyShaderModule(VkDevice d, VkShaderModule m, const VkAllocationCallbacks *a) { (void)d; (void)m; (void)a; CHECK(shaders == 1); shaders--; }
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorSetLayout(VkDevice d, const VkDescriptorSetLayoutCreateInfo *i, const VkAllocationCallbacks *a, VkDescriptorSetLayout *o) {
    (void)d; (void)i; (void)a; dsls++; *o = HANDLE(VkDescriptorSetLayout, 5); return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorSetLayout(VkDevice d, VkDescriptorSetLayout l, const VkAllocationCallbacks *a) { (void)d; (void)l; (void)a; CHECK(dsls == 1); dsls--; }
VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineLayout(VkDevice d, const VkPipelineLayoutCreateInfo *i, const VkAllocationCallbacks *a, VkPipelineLayout *o) {
    (void)d; (void)i; (void)a; layouts++; *o = HANDLE(VkPipelineLayout, 6); return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyPipelineLayout(VkDevice d, VkPipelineLayout l, const VkAllocationCallbacks *a) { (void)d; (void)l; (void)a; CHECK(layouts == 1); layouts--; }
VKAPI_ATTR VkResult VKAPI_CALL vkCreateComputePipelines(VkDevice d, VkPipelineCache c, uint32_t n, const VkComputePipelineCreateInfo *i, const VkAllocationCallbacks *a, VkPipeline *o) {
    (void)d; (void)c; (void)n; (void)i; (void)a; pipelines++; *o = HANDLE(VkPipeline, 7); return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyPipeline(VkDevice d, VkPipeline p, const VkAllocationCallbacks *a) { (void)d; (void)p; (void)a; CHECK(pipelines == 1); pipelines--; }
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorPool(VkDevice d, const VkDescriptorPoolCreateInfo *i, const VkAllocationCallbacks *a, VkDescriptorPool *o) {
    (void)d; (void)i; (void)a;
    if (fail_stage == 1) { fail_hit = 1; record_hit(); return VK_ERROR_OUT_OF_HOST_MEMORY; }
    pools++; *o = HANDLE(VkDescriptorPool, 8); return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorPool(VkDevice d, VkDescriptorPool p, const VkAllocationCallbacks *a) {
    (void)d; (void)p; (void)a; CHECK(pools == 1); pools--; sets = 0;
}
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateDescriptorSets(VkDevice d, const VkDescriptorSetAllocateInfo *i, VkDescriptorSet *o) {
    (void)d; CHECK(i->descriptorSetCount == 1); sets++; *o = HANDLE(VkDescriptorSet, 9); return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSets(VkDevice d, uint32_t n, const VkWriteDescriptorSet *w, uint32_t c, const VkCopyDescriptorSet *p) {
    (void)d; (void)c; (void)p;
    CHECK(n == 2 && w[0].descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER &&
        w[1].descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    CHECK(w[0].pBufferInfo->range == 16);
}
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateCommandBuffers(VkDevice d, const VkCommandBufferAllocateInfo *i, VkCommandBuffer *o) {
    (void)d; CHECK(i->commandBufferCount == 1); commands++; *o = HANDLE(VkCommandBuffer, 11); return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkBeginCommandBuffer(VkCommandBuffer c, const VkCommandBufferBeginInfo *i) { (void)c; CHECK(i->flags == VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT); return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL vkEndCommandBuffer(VkCommandBuffer c) {
    (void)c;
    if (fail_stage == 2) { fail_hit = 2; record_hit(); return VK_ERROR_OUT_OF_HOST_MEMORY; }
    return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateFence(VkDevice d, const VkFenceCreateInfo *i, const VkAllocationCallbacks *a, VkFence *o) {
    (void)d; (void)i; (void)a; fences++; *o = HANDLE(VkFence, 12); return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(VkQueue q, uint32_t n, const VkSubmitInfo *i, VkFence f) { (void)q; (void)i; (void)f; CHECK(n == 1); pending = 1; return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL vkWaitForFences(VkDevice d, uint32_t n, const VkFence *f, VkBool32 a, uint64_t t) {
    (void)d; (void)f; CHECK(n == 1 && a && t == UINT64_MAX && pending); pending = 0; return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkDeviceWaitIdle(VkDevice d) { (void)d; pending = 0; return VK_SUCCESS; }
VKAPI_ATTR void VKAPI_CALL vkDestroyFence(VkDevice d, VkFence f, const VkAllocationCallbacks *a) { (void)d; (void)f; (void)a; CHECK(fences == 1 && !pending); fences--; }
VKAPI_ATTR void VKAPI_CALL vkFreeCommandBuffers(VkDevice d, VkCommandPool p, uint32_t n, const VkCommandBuffer *c) {
    (void)d; (void)p; (void)c; CHECK(n == 1 && commands == 1 && !pending); commands--;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(VkDevice d, VkBuffer b, const VkAllocationCallbacks *a) { (void)d; (void)b; (void)a; CHECK(buffers >= 1); buffers--; }
VKAPI_ATTR void VKAPI_CALL vkUnmapMemory(VkDevice d, VkDeviceMemory m) { (void)d; (void)m; CHECK(mapped >= 1); mapped--; }
VKAPI_ATTR void VKAPI_CALL vkFreeMemory(VkDevice d, VkDeviceMemory m, const VkAllocationCallbacks *a) { (void)d; (void)m; (void)a; CHECK(memories >= 1); memories--; }
VKAPI_ATTR void VKAPI_CALL vkCmdBindPipeline(VkCommandBuffer c, VkPipelineBindPoint b, VkPipeline p) { (void)c; (void)b; (void)p; }
VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorSets(VkCommandBuffer c, VkPipelineBindPoint b, VkPipelineLayout l, uint32_t f, uint32_t n, const VkDescriptorSet *s, uint32_t d, const uint32_t *o) {
    (void)c; (void)b; (void)l; (void)s; (void)d; (void)o; CHECK(f == 0 && n == 1);
}
VKAPI_ATTR void VKAPI_CALL vkCmdPushConstants(VkCommandBuffer c, VkPipelineLayout l, VkShaderStageFlags s, uint32_t o, uint32_t n, const void *v) { (void)c; (void)l; (void)s; (void)o; (void)n; (void)v; }
VKAPI_ATTR void VKAPI_CALL vkCmdDispatch(VkCommandBuffer c, uint32_t x, uint32_t y, uint32_t z) { (void)c; CHECK(x == 1 && y == 1 && z == 1); }
VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer c, VkPipelineStageFlags s, VkPipelineStageFlags d, VkDependencyFlags f,
    uint32_t mc, const VkMemoryBarrier *m, uint32_t bc, const VkBufferMemoryBarrier *b, uint32_t ic, const VkImageMemoryBarrier *i) {
    (void)c; (void)s; (void)d; (void)f; (void)m; (void)i; CHECK(!mc && bc == 1 && !ic && b->buffer == HANDLE(VkBuffer, 3));
}

int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "usage: %s uniform.spv uniform.res.json\n", argv[0]); return 2; }
    dm_vk_shader_plan plan; char why[DM_VK_REFLECT_WHY] = {0};
    CHECK(dm_vk_reflect_load(argv[1], argv[2], &plan, why, sizeof why) == VK_SUCCESS);
    dm_vk vk = {.device = HANDLE(VkDevice, 30), .queue = HANDLE(VkQueue, 31), .pool = HANDLE(VkCommandPool, 32)};
    vk.memory.memoryTypeCount = 2;
    vk.memory.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    vk.memory.memoryTypes[1].propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;   // non-coherent: exercise upload flush
    vk.properties.limits.maxComputeWorkGroupSize[0] = 1024;
    vk.properties.limits.maxComputeWorkGroupSize[1] = 1024;
    vk.properties.limits.maxComputeWorkGroupSize[2] = 64;
    vk.properties.limits.maxComputeWorkGroupInvocations = 1024;
    vk.properties.limits.maxComputeWorkGroupCount[0] = 65535;
    vk.properties.limits.maxComputeWorkGroupCount[1] = 65535;
    vk.properties.limits.maxComputeWorkGroupCount[2] = 65535;
    vk.properties.limits.maxPushConstantsSize = 128;
    vk.properties.limits.maxStorageBufferRange = 1u << 20;
    vk.properties.limits.maxUniformBufferRange = 1u << 16;
    uint32_t input[4] = {0x12345678u, 0x9abcdef0u, 0x0f0f0f0fu, 0xdeadbeefu};
    uint32_t input_before[4]; memcpy(input_before, input, sizeof input);
    uint32_t words[4] = {0x5a5a5a5au, 0x5a5a5a5au, 0x5a5a5a5au, 0x5a5a5a5au};
    dm_vk_pixels canvas = {.width = 4, .height = 1, .bytes = (uint8_t *)words, .capacity = sizeof words};

    // 设备 uniform 上限与调用者短输入在创建任何对象前拒绝。
    // Device uniform range and short caller input are rejected before any object is created.
    reset();
    vk.properties.limits.maxUniformBufferRange = 8;
    CHECK(dm_vk_reflect_compute_uniform(&vk, &plan, &canvas, input, 16, NULL, 0) != VK_SUCCESS);
    balanced();
    vk.properties.limits.maxUniformBufferRange = 1u << 16;
    reset();
    CHECK(dm_vk_reflect_compute_uniform(&vk, &plan, &canvas, input, 15, NULL, 0) != VK_SUCCESS);
    balanced();
    for (unsigned i = 0; i < 4; i++) CHECK(words[i] == 0x5a5a5a5a);

    for (fail_stage = 0; fail_stage <= 3; fail_stage++) {
        reset(); memset(words, 0x5a, sizeof words); memcpy(input, input_before, sizeof input);
        VkResult r = dm_vk_reflect_compute_uniform(&vk, &plan, &canvas, input, 16, NULL, 0);
        if (!fail_stage) {
            CHECK(r == VK_SUCCESS);
            CHECK(flushes == 1 && invalidates == 1);
        } else {
            CHECK(r != VK_SUCCESS);
            CHECK(fail_hit == (unsigned)fail_stage);
            for (unsigned i = 0; i < 4; i++) CHECK(words[i] == 0x5a5a5a5a);
            if (fail_stage == 1) {
                CHECK(hit_shader == 1 && hit_dsl == 1 && hit_layout == 1 && hit_pipeline == 1);
                CHECK(hit_pool == 0 && hit_set == 0 && hit_buffers == 2);
            } else if (fail_stage == 2) {
                CHECK(hit_shader == 1 && hit_dsl == 1 && hit_layout == 1 && hit_pipeline == 1);
                CHECK(hit_pool == 1 && hit_set == 1 && hit_commands == 1 && hit_buffers == 2);
            } else {
                CHECK(hit_shader == 1 && hit_dsl == 1 && hit_layout == 1 && hit_pipeline == 1);
                CHECK(hit_pool == 1 && hit_set == 1 && hit_buffers == 2 && hit_memories == 2 && hit_mapped == 2);
                CHECK(hit_commands == 0);
            }
        }
        CHECK(!memcmp(input, input_before, sizeof input));
        balanced();
    }
    dm_vk_reflect_free(&plan);
    printf("vulkan_reflect_uniform_fail: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
