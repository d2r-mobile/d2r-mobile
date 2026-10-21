// GPU-free lifecycle oracle for #111. Mocks the actual Vulkan/dm_vk calls the
// renderer makes and counts real call pairs, so a missing destroy/free fails
// even when the renderer returns successfully.
#include "touch_hud.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks;
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c); exit(1); } checks++; } while (0)

#define H(t,v) ((t)(uintptr_t)(v))
static int shader_live, image_live, memory_live, view_live, renderpass_live;
static int framebuffer_live, layout_live, pipeline_live, buffer_live, map_live;
static int command_live, fence_live;
static int shader_create, shader_destroy, image_create, image_destroy;
static int memory_alloc, memory_free, buffer_create, buffer_destroy, map_count, unmap_count;
static int command_begin, command_free, submit_count, fence_waits;
static int fail_begin, fail_submit, fail_buffer_at, buffer_calls;

static int all_balanced(void)
{
    if (shader_live || image_live || memory_live || view_live || renderpass_live ||
        framebuffer_live || layout_live || pipeline_live || buffer_live || map_live ||
        command_live || fence_live) return 0;
    if (shader_create != shader_destroy || image_create != image_destroy ||
        memory_alloc != memory_free || buffer_create != buffer_destroy ||
        map_count != unmap_count || command_begin != command_free ||
        submit_count != fence_waits) return 0;
    return 1;
}

// --- actual Vulkan entry points used by touch_hud.c and its dm_vk helpers ---
VKAPI_ATTR VkResult VKAPI_CALL vkCreateShaderModule(VkDevice d, const VkShaderModuleCreateInfo *ci,
    const VkAllocationCallbacks *a, VkShaderModule *out)
{
    (void)d; (void)ci; (void)a;
    shader_create++; shader_live++; *out = H(VkShaderModule, 0x100 + shader_create);
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyShaderModule(VkDevice d, VkShaderModule m, const VkAllocationCallbacks *a)
{
    (void)d; (void)m; (void)a; CHECK(shader_live > 0); shader_live--; shader_destroy++;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateImage(VkDevice d, const VkImageCreateInfo *ci,
    const VkAllocationCallbacks *a, VkImage *out)
{
    (void)d; (void)a; CHECK(ci->extent.width == 1334 && ci->extent.height == 750);
    image_create++; image_live++; *out = H(VkImage, 0x200 + image_create); return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyImage(VkDevice d, VkImage i, const VkAllocationCallbacks *a)
{
    (void)d; (void)i; (void)a; CHECK(image_live > 0); image_live--; image_destroy++;
}
VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements(VkDevice d, VkImage i, VkMemoryRequirements *r)
{
    (void)d; (void)i; *r = (VkMemoryRequirements){.size = 4096, .alignment = 16, .memoryTypeBits = 1};
}
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(VkDevice d, const VkMemoryAllocateInfo *ai,
    const VkAllocationCallbacks *a, VkDeviceMemory *out)
{
    (void)d; (void)a; CHECK(ai->memoryTypeIndex == 0);
    memory_alloc++; memory_live++; *out = H(VkDeviceMemory, 0x300 + memory_alloc); return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkFreeMemory(VkDevice d, VkDeviceMemory m, const VkAllocationCallbacks *a)
{
    (void)d; (void)m; (void)a; CHECK(memory_live > 0); memory_live--; memory_free++;
}
VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory(VkDevice d, VkImage i, VkDeviceMemory m, VkDeviceSize o)
{
    (void)d; (void)i; (void)m; CHECK(o == 0); return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateImageView(VkDevice d, const VkImageViewCreateInfo *ci,
    const VkAllocationCallbacks *a, VkImageView *out)
{
    (void)d; (void)ci; (void)a; view_live++; *out = H(VkImageView, 0x400); return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyImageView(VkDevice d, VkImageView v, const VkAllocationCallbacks *a)
{
    (void)d; (void)v; (void)a; CHECK(view_live > 0); view_live--;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateRenderPass(VkDevice d, const VkRenderPassCreateInfo *ci,
    const VkAllocationCallbacks *a, VkRenderPass *out)
{
    (void)d; (void)ci; (void)a; renderpass_live++; *out = H(VkRenderPass, 0x500); return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyRenderPass(VkDevice d, VkRenderPass p, const VkAllocationCallbacks *a)
{
    (void)d; (void)p; (void)a; CHECK(renderpass_live > 0); renderpass_live--;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateFramebuffer(VkDevice d, const VkFramebufferCreateInfo *ci,
    const VkAllocationCallbacks *a, VkFramebuffer *out)
{
    (void)d; (void)ci; (void)a; framebuffer_live++; *out = H(VkFramebuffer, 0x600); return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyFramebuffer(VkDevice d, VkFramebuffer f, const VkAllocationCallbacks *a)
{
    (void)d; (void)f; (void)a; CHECK(framebuffer_live > 0); framebuffer_live--;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineLayout(VkDevice d, const VkPipelineLayoutCreateInfo *ci,
    const VkAllocationCallbacks *a, VkPipelineLayout *out)
{
    (void)d; (void)ci; (void)a; layout_live++; *out = H(VkPipelineLayout, 0x700); return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyPipelineLayout(VkDevice d, VkPipelineLayout p, const VkAllocationCallbacks *a)
{
    (void)d; (void)p; (void)a; CHECK(layout_live > 0); layout_live--;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateGraphicsPipelines(VkDevice d, VkPipelineCache c, uint32_t n,
    const VkGraphicsPipelineCreateInfo *ci, const VkAllocationCallbacks *a, VkPipeline *out)
{
    (void)d; (void)c; (void)ci; (void)a; CHECK(n == 1);
    pipeline_live++; *out = H(VkPipeline, 0x800); return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyPipeline(VkDevice d, VkPipeline p, const VkAllocationCallbacks *a)
{
    (void)d; (void)p; (void)a; CHECK(pipeline_live > 0); pipeline_live--;
}
VKAPI_ATTR void VKAPI_CALL vkFreeCommandBuffers(VkDevice d, VkCommandPool p, uint32_t n, const VkCommandBuffer *c)
{
    (void)d; (void)p; (void)c; CHECK(n == 1 && command_live > 0); command_live--; command_free++;
}
VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderPass(VkCommandBuffer c, const VkRenderPassBeginInfo *b, VkSubpassContents s)
{ (void)c; (void)b; CHECK(s == VK_SUBPASS_CONTENTS_INLINE); }
VKAPI_ATTR void VKAPI_CALL vkCmdBindPipeline(VkCommandBuffer c, VkPipelineBindPoint b, VkPipeline p)
{ (void)c; (void)b; (void)p; }
VKAPI_ATTR void VKAPI_CALL vkCmdBindVertexBuffers(VkCommandBuffer c, uint32_t f, uint32_t n,
    const VkBuffer *b, const VkDeviceSize *o) { (void)c; (void)f; (void)n; (void)b; (void)o; }
VKAPI_ATTR void VKAPI_CALL vkCmdPushConstants(VkCommandBuffer c, VkPipelineLayout l, VkShaderStageFlags s,
    uint32_t o, uint32_t n, const void *p) { (void)c; (void)l; (void)s; (void)o; (void)n; (void)p; }
VKAPI_ATTR void VKAPI_CALL vkCmdDraw(VkCommandBuffer c, uint32_t v, uint32_t i, uint32_t a, uint32_t b)
{ (void)c; (void)v; (void)i; (void)a; (void)b; }
VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderPass(VkCommandBuffer c) { (void)c; }
VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer c, VkPipelineStageFlags s,
    VkPipelineStageFlags d, VkDependencyFlags f, uint32_t m, const VkMemoryBarrier *mb,
    uint32_t b, const VkBufferMemoryBarrier *bb, uint32_t i, const VkImageMemoryBarrier *ib)
{ (void)c; (void)s; (void)d; (void)f; (void)m; (void)mb; (void)b; (void)bb; (void)i; (void)ib; }
VKAPI_ATTR void VKAPI_CALL vkCmdCopyImageToBuffer(VkCommandBuffer c, VkImage i, VkImageLayout l,
    VkBuffer b, uint32_t n, const VkBufferImageCopy *r)
{ (void)c; (void)i; (void)l; (void)b; CHECK(n == 1 && r->imageExtent.width == 1334); }

// --- dm_vk helpers (mocked at the seam touch_hud.c calls) ---
VkResult dm_vk_shader_load(dm_vk *vk, const char *path, VkShaderModule *out)
{
    if (!path || strstr(path, "missing")) return VK_ERROR_INITIALIZATION_FAILED;
    VkShaderModuleCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = 4, .pCode = (const uint32_t *)"\x03\x02\x23\x07"};
    return vkCreateShaderModule(vk->device, &ci, NULL, out);
}
VkResult dm_vk_buffer_create(dm_vk *vk, const dm_vk_buffer_info *info, dm_vk_buffer *out)
{
    (void)vk;
    if (fail_buffer_at && ++buffer_calls == fail_buffer_at) return VK_ERROR_OUT_OF_HOST_MEMORY;
    if (!info->size) return VK_ERROR_INITIALIZATION_FAILED;
    out->mapped = malloc((size_t)info->size);
    if (!out->mapped) return VK_ERROR_OUT_OF_HOST_MEMORY;
    out->handle = H(VkBuffer, 0x900 + buffer_create);
    out->memory = H(VkDeviceMemory, 0xa00 + buffer_create);
    out->size = info->size; out->coherent = 1;
    buffer_create++; buffer_live++; map_count++; map_live++; memory_alloc++; memory_live++;
    return VK_SUCCESS;
}
void dm_vk_buffer_destroy(dm_vk *vk, dm_vk_buffer *b)
{
    (void)vk;
    if (!b->handle && !b->memory && !b->mapped) return;   // early failure: never created
    if (!(b->handle && map_live > 0 && buffer_live > 0 && memory_live > 0)) {
        fprintf(stderr,"buffer destroy mismatch handle=%p map=%d buf=%d mem=%d\n",
                (void*)b->handle, map_live, buffer_live, memory_live);
        exit(1);
    }
    free(b->mapped); b->mapped = NULL;
    buffer_live--; map_live--; unmap_count++; memory_live--; memory_free++;
    buffer_destroy++; b->handle = VK_NULL_HANDLE; b->memory = VK_NULL_HANDLE; b->size = 0;
}
VkResult dm_vk_buffer_sync(dm_vk *vk, dm_vk_buffer *b, int upload) { (void)vk; (void)b; (void)upload; return VK_SUCCESS; }
VkResult dm_vk_begin(dm_vk *vk, VkCommandBuffer *out)
{
    (void)vk;
    if (fail_begin) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    command_begin++; command_live++; *out = H(VkCommandBuffer, 0xb00); return VK_SUCCESS;
}
VkResult dm_vk_submit(dm_vk *vk, VkCommandBuffer cmd)
{
    (void)vk; (void)cmd;
    CHECK(command_live > 0);
    submit_count++; fence_waits++; fence_live++; command_live--; command_free++; fence_live--;
    return fail_submit ? VK_ERROR_DEVICE_LOST : VK_SUCCESS;
}

static unsigned char *alloc_pixels(dm_vk_pixels *p)
{
    p->width = 1334; p->height = 750; p->capacity = 1334u * 750u * 4u;
    p->bytes = malloc(p->capacity);
    if (p->bytes) memset(p->bytes, 0x5a, p->capacity);
    return p->bytes;
}

static int unchanged(const dm_vk_pixels *p, const unsigned char *s)
{ return memcmp(p->bytes, s, p->capacity) == 0; }

static int bad_case(const char *name, dm_touch_hud_snapshot s, dm_vk *vk, dm_vk_pixels *p,
                    const unsigned char *before, const char *vert, const char *frag)
{
    if (dm_touch_hud_validate(&s)) { fprintf(stderr,"FAIL %s validate\n",name); return 0; }
    dm_touch_hud_geometry g[DM_TC_COUNT]; dm_touch_hud_joy_geometry j;
    if (dm_touch_hud_build_geometry(&s,g,&j)) { fprintf(stderr,"FAIL %s geometry\n",name); return 0; }
    if (dm_touch_hud_render(vk,&s,p,0,vert,frag)) { fprintf(stderr,"FAIL %s render\n",name); return 0; }
    if (!unchanged(p,before)) { fprintf(stderr,"FAIL %s output changed\n",name); return 0; }
    if (!all_balanced()) { fprintf(stderr,"FAIL %s lifecycle unbalanced\n",name); return 0; }
    checks++; return 1;
}

int main(void)
{
    dm_touch_hud_snapshot s = {
        .width_pt = 667, .height_pt = 375, .density = 2,
        .pixel_width = 1334, .pixel_height = 750,
        .available_mask = (1u << DM_TC_COUNT) - 1u,
        .enabled_mask = (1u << DM_TC_COUNT) - 1u,
        .pressed_mask = 1u << DM_TC_MAIN,
    };
    dm_vk vk = {.device = H(VkDevice,1), .pool = H(VkCommandPool,2)};
    vk.memory.memoryTypeCount = 1;
    vk.memory.memoryTypes[0].propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    dm_vk_pixels px = {0}; CHECK(alloc_pixels(&px));
    unsigned char *before = malloc(px.capacity); CHECK(before);
    memcpy(before, px.bytes, px.capacity);

    // Success: every actual create/allocate/map/begin has a matching release.
    CHECK(dm_touch_hud_render(&vk, &s, &px, 0, "hud.vert.spv", "hud.frag.spv"));
    CHECK(!unchanged(&px, before));
    CHECK(shader_create == 2 && shader_destroy == 2);
    CHECK(image_create == image_destroy && memory_alloc == memory_free);
    CHECK(buffer_create == buffer_destroy && map_count == unmap_count);
    CHECK(command_begin == command_free && submit_count == 1 && fence_waits == 1);
    CHECK(all_balanced());

    // Missing shader stages: actual load failure, unchanged output, no leak.
    memset(px.bytes, 0x5a, px.capacity); memcpy(before, px.bytes, px.capacity);
    CHECK(!dm_touch_hud_render(&vk,&s,&px,0,"missing.vert.spv","hud.frag.spv"));
    CHECK(unchanged(&px,before)); CHECK(all_balanced());
    CHECK(!dm_touch_hud_render(&vk,&s,&px,0,"hud.vert.spv","missing.frag.spv"));
    CHECK(unchanged(&px,before)); CHECK(all_balanced());

    // Post-creation failures: buffers, command begin, fence/submit.
    fail_buffer_at = 1;
    CHECK(!dm_touch_hud_render(&vk,&s,&px,0,"hud.vert.spv","hud.frag.spv"));
    CHECK(unchanged(&px,before)); CHECK(all_balanced()); fail_buffer_at = 0;

    fail_begin = 1;
    CHECK(!dm_touch_hud_render(&vk,&s,&px,0,"hud.vert.spv","hud.frag.spv"));
    CHECK(unchanged(&px,before)); CHECK(all_balanced()); fail_begin = 0;

    fail_submit = 1;
    CHECK(!dm_touch_hud_render(&vk,&s,&px,0,"hud.vert.spv","hud.frag.spv"));
    CHECK(unchanged(&px,before)); CHECK(all_balanced());
    CHECK(submit_count == 2 && fence_waits == 2); fail_submit = 0;

    // Numeric boundaries never reach GPU calls.
    dm_touch_hud_snapshot b = s;
    b.width_pt = INFINITY; CHECK(bad_case("+inf width",b,&vk,&px,before,"hud.vert.spv","hud.frag.spv"));
    b = s; b.width_pt = -INFINITY; CHECK(bad_case("-inf width",b,&vk,&px,before,"hud.vert.spv","hud.frag.spv"));
    b = s; b.width_pt = NAN; CHECK(bad_case("nan width",b,&vk,&px,before,"hud.vert.spv","hud.frag.spv"));
    b = s; b.height_pt = INFINITY; CHECK(bad_case("+inf height",b,&vk,&px,before,"hud.vert.spv","hud.frag.spv"));
    b = s; b.height_pt = NAN; CHECK(bad_case("nan height",b,&vk,&px,before,"hud.vert.spv","hud.frag.spv"));
    b = s; b.width_pt = FLT_MAX; CHECK(bad_case("FLT_MAX width",b,&vk,&px,before,"hud.vert.spv","hud.frag.spv"));
    b = s; b.height_pt = FLT_MAX; CHECK(bad_case("FLT_MAX height",b,&vk,&px,before,"hud.vert.spv","hud.frag.spv"));
    b = s; b.width_pt = 1.0e10f; b.pixel_width = 1; CHECK(bad_case("huge point width",b,&vk,&px,before,"hud.vert.spv","hud.frag.spv"));
    b = s; b.width_pt = 1.0f; b.density = FLT_MAX; b.pixel_width = 1; CHECK(bad_case("product overflow",b,&vk,&px,before,"hud.vert.spv","hud.frag.spv"));
    b = s; b.width_pt = 20000.0f; b.pixel_width = 1; CHECK(bad_case("width over MAX_DIM",b,&vk,&px,before,"hud.vert.spv","hud.frag.spv"));
    b = s; b.height_pt = 20000.0f; b.pixel_height = 1; CHECK(bad_case("height over MAX_DIM",b,&vk,&px,before,"hud.vert.spv","hud.frag.spv"));

    free(before); free(px.bytes);
    printf("touch HUD mock lifecycle: %d checks\n", checks);
    return 0;
}
