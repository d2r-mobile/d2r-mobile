// Native offscreen HUD rendering over the frozen touch layout (#111).
#include "touch_hud.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DIM 16384u
#define SEGMENTS 28
#define BG_A (-1.0f)

typedef struct { float x, y, r, g, b, a; } vertex;
typedef struct { vertex v[DM_TOUCH_HUD_VERTEX_CAP]; uint32_t n; } batch;

static const float HUD_NORMAL[4] = {0.15f, 0.65f, 0.95f, 0.55f};
static const float HUD_PRESSED[4] = {0.95f, 0.35f, 0.15f, 0.75f};
static const float HUD_UNAVAILABLE[4] = {0.40f, 0.40f, 0.40f, 0.25f};
static const float HUD_JOY[4] = {0.15f, 0.85f, 0.55f, 0.30f};
static const float HUD_OVERLAY[4] = {0.95f, 0.15f, 0.75f, 0.95f};

static void push(batch *b, float x, float y, const float c[4])
{
    if (b->n >= DM_TOUCH_HUD_VERTEX_CAP) return;
    b->v[b->n++] = (vertex){x, y, c[0], c[1], c[2], c[3]};
}

static void ndc(const dm_touch_hud_snapshot *s, float x, float y, float *ox, float *oy)
{
    *ox = 2.0f * x / (float)s->pixel_width - 1.0f;
    *oy = 2.0f * y / (float)s->pixel_height - 1.0f;
}

static void tri(batch *b, const dm_touch_hud_snapshot *s,
                float ax, float ay, float bx, float by, float cx, float cy, const float c[4])
{
    float x, y;
    ndc(s, ax, ay, &x, &y); push(b, x, y, c);
    ndc(s, bx, by, &x, &y); push(b, x, y, c);
    ndc(s, cx, cy, &x, &y); push(b, x, y, c);
}

static void disk(batch *b, const dm_touch_hud_snapshot *s,
                 float cx, float cy, float r, const float c[4])
{
    for (int i = 0; i < SEGMENTS; i++) {
        float a0 = 6.28318530718f * (float)i / (float)SEGMENTS;
        float a1 = 6.28318530718f * (float)(i + 1) / (float)SEGMENTS;
        tri(b, s, cx, cy, cx + r * cosf(a0), cy + r * sinf(a0),
            cx + r * cosf(a1), cy + r * sinf(a1), c);
    }
}

static void line(batch *b, const dm_touch_hud_snapshot *s,
                 float x0, float y0, float x1, float y1, float w, const float c[4])
{
    float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
    if (len < 0.0001f) return;
    float nx = -dy / len * w * 0.5f, ny = dx / len * w * 0.5f;
    tri(b, s, x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, c);
    tri(b, s, x0 + nx, y0 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, c);
}

const char *dm_touch_hud_art_status(void)
{
    return "unavailable: no licensed glyph atlas; owned geometric shapes only";
}

int dm_touch_hud_validate(const dm_touch_hud_snapshot *s)
{
    if (!s) return 0;
    if (!isfinite(s->width_pt) || !isfinite(s->height_pt) || !isfinite(s->density)) return 0;
    if (!(s->width_pt > 0.0f) || !(s->height_pt > 0.0f)) return 0;
    if (!(s->density > 0.0f)) return 0;
    if (s->width_pt > (float)MAX_DIM || s->height_pt > (float)MAX_DIM) return 0;
    if (!s->pixel_width || !s->pixel_height) return 0;
    if (s->pixel_width > MAX_DIM || s->pixel_height > MAX_DIM) return 0;
    // Validate the product in double before any float-to-integer conversion.
    double expected_w = (double)s->width_pt * (double)s->density;
    double expected_h = (double)s->height_pt * (double)s->density;
    if (!isfinite(expected_w) || !isfinite(expected_h)) return 0;
    if (expected_w > (double)MAX_DIM + 1.0 || expected_h > (double)MAX_DIM + 1.0) return 0;
    uint32_t rounded_w = (uint32_t)(expected_w + 0.5);
    uint32_t rounded_h = (uint32_t)(expected_h + 0.5);
    if (s->pixel_width > rounded_w || s->pixel_width + 1u < rounded_w) return 0;
    if (s->pixel_height > rounded_h || s->pixel_height + 1u < rounded_h) return 0;
    if (s->enabled_mask & ~((1u << DM_TC_COUNT) - 1u)) return 0;
    if (s->available_mask & ~((1u << DM_TC_COUNT) - 1u)) return 0;
    if (s->pressed_mask & ~((1u << DM_TC_COUNT) - 1u)) return 0;
    return 1;
}

int dm_touch_hud_build_geometry(const dm_touch_hud_snapshot *s,
                          dm_touch_hud_geometry out[DM_TC_COUNT],
                          dm_touch_hud_joy_geometry *joy)
{
    if (!dm_touch_hud_validate(s) || !out || !joy) return 0;
    dm_tc_layout layout;
    dm_tc_layout_make(&layout, s->width_pt, s->height_pt);
    for (int i = 0; i < DM_TC_COUNT; i++) {
        out[i].target = i;
        out[i].center_x_pt = layout.target[i].x;
        out[i].center_y_pt = layout.target[i].y;
        out[i].radius_pt = layout.target[i].r;
        out[i].center_x_px = layout.target[i].x * s->density;
        out[i].center_y_px = layout.target[i].y * s->density;
        out[i].radius_px = layout.target[i].r * s->density;
    }
    joy->center_x_pt = layout.joy_x; joy->center_y_pt = layout.joy_y;
    joy->radius_pt = layout.joy_radius;
    joy->center_x_px = layout.joy_x * s->density;
    joy->center_y_px = layout.joy_y * s->density;
    joy->radius_px = layout.joy_radius * s->density;
    return 1;
}

static void background(batch *b, const dm_touch_hud_snapshot *s)
{
    float x, y;
    ndc(s, 0, 0, &x, &y);
    const float bg[4] = {0, 0, 0, BG_A};
    push(b, x, y, bg);
    ndc(s, (float)s->pixel_width, 0, &x, &y); push(b, x, y, bg);
    ndc(s, 0, (float)s->pixel_height, &x, &y); push(b, x, y, bg);
    ndc(s, (float)s->pixel_width, 0, &x, &y); push(b, x, y, bg);
    ndc(s, (float)s->pixel_width, (float)s->pixel_height, &x, &y); push(b, x, y, bg);
    ndc(s, 0, (float)s->pixel_height, &x, &y); push(b, x, y, bg);
}

static void build(batch *b, const dm_touch_hud_snapshot *s,
                  const dm_touch_hud_geometry g[DM_TC_COUNT],
                  const dm_touch_hud_joy_geometry *joy, int overlay)
{
    background(b, s);
    for (int i = 0; i < DM_TC_COUNT; i++) {
        if (!(s->enabled_mask & (1u << i))) continue;
        int available = (s->available_mask & (1u << i)) != 0;
        if (overlay) {
            float x0 = g[i].center_x_px - g[i].radius_px, x1 = g[i].center_x_px + g[i].radius_px;
            float y0 = g[i].center_y_px - g[i].radius_px, y1 = g[i].center_y_px + g[i].radius_px;
            line(b, s, x0, y0, x1, y0, 2, HUD_OVERLAY); line(b, s, x1, y0, x1, y1, 2, HUD_OVERLAY);
            line(b, s, x1, y1, x0, y1, 2, HUD_OVERLAY); line(b, s, x0, y1, x0, y0, 2, HUD_OVERLAY);
            line(b, s, g[i].center_x_px - 8, g[i].center_y_px, g[i].center_x_px + 8, g[i].center_y_px, 1.5f, HUD_OVERLAY);
            line(b, s, g[i].center_x_px, g[i].center_y_px - 8, g[i].center_x_px, g[i].center_y_px + 8, 1.5f, HUD_OVERLAY);
        } else {
            int pressed = available && (s->pressed_mask & (1u << i));
            disk(b, s, g[i].center_x_px, g[i].center_y_px, g[i].radius_px,
                 !available ? HUD_UNAVAILABLE : pressed ? HUD_PRESSED : HUD_NORMAL);
        }
    }
    if (overlay) {
        const float c[4] = {0.15f, 0.95f, 0.35f, 0.95f};
        float cx = joy->center_x_px, cy = joy->center_y_px, r = joy->radius_px;
        line(b, s, cx - r, cy, cx + r, cy, 2, c);
        line(b, s, cx, cy - r, cx, cy + r, 2, c);
        for (int i = 0; i < 32; i++) {
            float a0 = 6.28318530718f * (float)i / 32.0f;
            float a1 = 6.28318530718f * (float)(i + 1) / 32.0f;
            line(b, s, cx + r * cosf(a0), cy + r * sinf(a0), cx + r * cosf(a1), cy + r * sinf(a1), 2, c);
        }
    } else {
        disk(b, s, joy->center_x_px, joy->center_y_px, joy->radius_px, HUD_JOY);
    }
}

int dm_touch_hud_render(dm_vk *vk, const dm_touch_hud_snapshot *s, dm_vk_pixels *out,
                        int overlay, const char *vert, const char *frag)
{
    if (!vk || !vk->device || !out || !out->bytes || !vert || !frag) return 0;
    if (!dm_touch_hud_validate(s)) return 0;
    if (out->width != s->pixel_width || out->height != s->pixel_height) return 0;
    uint64_t bytes = (uint64_t)s->pixel_width * s->pixel_height * 4u;
    if (bytes > out->capacity) return 0;

    dm_touch_hud_geometry g[DM_TC_COUNT];
    dm_touch_hud_joy_geometry joy;
    if (!dm_touch_hud_build_geometry(s, g, &joy)) return 0;
    batch *b = calloc(1, sizeof(*b));
    if (!b) return 0;
    build(b, s, g, &joy, overlay);

    VkResult result = VK_SUCCESS;
    VkImage image = VK_NULL_HANDLE, depth = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkRenderPass pass = VK_NULL_HANDLE;
    VkFramebuffer fb = VK_NULL_HANDLE;
    VkShaderModule vs = VK_NULL_HANDLE, fs = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    dm_vk_buffer readback = {0};
    (void)depth;
    if (b->n == 0) { free(b); return 0; }

    VkResult shader_result = dm_vk_shader_load(vk, vert, &vs);
    if (shader_result != VK_SUCCESS) { result = shader_result; goto done; }
    shader_result = dm_vk_shader_load(vk, frag, &fs);
    if (shader_result != VK_SUCCESS) { result = shader_result; goto done; }
    VkImageCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {s->pixel_width, s->pixel_height, 1}, .mipLevels = 1, .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    if ((result = vkCreateImage(vk->device, &ci, NULL, &image)) != VK_SUCCESS) goto done;
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(vk->device, image, &req);
    uint32_t type = UINT32_MAX;
    for (uint32_t i = 0; i < vk->memory.memoryTypeCount; i++)
        if (req.memoryTypeBits & (1u << i)) {
            type = i;
            if (vk->memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) break;
        }
    if (type == UINT32_MAX) { result = VK_ERROR_FEATURE_NOT_PRESENT; goto done; }
    VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size, .memoryTypeIndex = type};
    if ((result = vkAllocateMemory(vk->device, &ai, NULL, &memory)) != VK_SUCCESS) goto done;
    if ((result = vkBindImageMemory(vk->device, image, memory, 0)) != VK_SUCCESS) goto done;
    VkImageViewCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1}};
    if ((result = vkCreateImageView(vk->device, &vi, NULL, &view)) != VK_SUCCESS) goto done;
    VkAttachmentDescription att = {.format = VK_FORMAT_R8G8B8A8_UNORM,
        .samples = VK_SAMPLE_COUNT_1_BIT, .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference ref = {.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1, .pColorAttachments = &ref};
    VkSubpassDependency dep = {.srcSubpass = VK_SUBPASS_EXTERNAL, .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT};
    VkRenderPassCreateInfo rp = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &att, .subpassCount = 1, .pSubpasses = &sub,
        .dependencyCount = 1, .pDependencies = &dep};
    if ((result = vkCreateRenderPass(vk->device, &rp, NULL, &pass)) != VK_SUCCESS) goto done;
    VkFramebufferCreateInfo fi = {.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = pass, .attachmentCount = 1, .pAttachments = &view,
        .width = s->pixel_width, .height = s->pixel_height, .layers = 1};
    if ((result = vkCreateFramebuffer(vk->device, &fi, NULL, &fb)) != VK_SUCCESS) goto done;
    VkPushConstantRange range = {.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT, .offset = 0, .size = 8};
    VkPipelineLayoutCreateInfo pl = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1, .pPushConstantRanges = &range};
    if ((result = vkCreatePipelineLayout(vk->device, &pl, NULL, &layout)) != VK_SUCCESS) goto done;
    VkPipelineShaderStageCreateInfo stages[2] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vs, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fs, .pName = "main"}};
    VkVertexInputBindingDescription binding = {.binding = 0, .stride = sizeof(vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attr[2] = {
        {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = 0},
        {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = 8}};
    VkPipelineVertexInputStateCreateInfo vin = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = 2, .pVertexAttributeDescriptions = attr};
    VkPipelineInputAssemblyStateCreateInfo ia = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkViewport vp = {.width = (float)s->pixel_width, .height = (float)s->pixel_height, .maxDepth = 1};
    VkRect2D sc = {.extent = {s->pixel_width, s->pixel_height}};
    VkPipelineViewportStateCreateInfo vps = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .pViewports = &vp, .scissorCount = 1, .pScissors = &sc};
    VkPipelineRasterizationStateCreateInfo rs = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1};
    VkPipelineMultisampleStateCreateInfo ms = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState ba = {
        .blendEnable = VK_TRUE, .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE, .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};
    VkPipelineColorBlendStateCreateInfo bs = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &ba};
    VkGraphicsPipelineCreateInfo gp = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2, .pStages = stages, .pVertexInputState = &vin, .pInputAssemblyState = &ia,
        .pViewportState = &vps, .pRasterizationState = &rs, .pMultisampleState = &ms,
        .pColorBlendState = &bs, .layout = layout, .renderPass = pass, .basePipelineIndex = -1};
    if ((result = vkCreateGraphicsPipelines(vk->device, VK_NULL_HANDLE, 1, &gp, NULL, &pipeline)) != VK_SUCCESS) goto done;
    dm_vk_buffer_info bi = {.size = (VkDeviceSize)b->n * sizeof(vertex),
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    dm_vk_buffer_info ri = {.size = (VkDeviceSize)bytes, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    if ((result = dm_vk_buffer_create(vk, &ri, &readback)) != VK_SUCCESS) goto done;
    // readback buffer is reused below as the vertex staging buffer: create a separate one.
    {
        dm_vk_buffer vb = {0};
        if ((result = dm_vk_buffer_create(vk, &bi, &vb)) != VK_SUCCESS) goto done;
        memcpy(vb.mapped, b->v, (size_t)bi.size);
        if ((result = dm_vk_buffer_sync(vk, &vb, 1)) != VK_SUCCESS) { dm_vk_buffer_destroy(vk, &vb); goto done; }
        result = dm_vk_begin(vk, &cmd);
        if (result != VK_SUCCESS) { dm_vk_buffer_destroy(vk, &vb); goto done; }
        VkClearValue clear = {.color = {.float32 = {0, 0, 0, 0}}};
        VkRenderPassBeginInfo rbi = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = pass, .framebuffer = fb, .renderArea = {.extent = {s->pixel_width, s->pixel_height}},
            .clearValueCount = 1, .pClearValues = &clear};
        vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vb.handle, &offset);
        float dims[2] = {(float)s->pixel_width, (float)s->pixel_height};
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof dims, dims);
        vkCmdDraw(cmd, b->n, 1, 0, 0);
        vkCmdEndRenderPass(cmd);
        VkImageMemoryBarrier ib = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image, .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1}};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &ib);
        VkBufferImageCopy copy = {.imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1},
            .imageExtent = {s->pixel_width, s->pixel_height, 1}};
        vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.handle, 1, &copy);
        VkBufferMemoryBarrier hb = {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = readback.handle, .size = VK_WHOLE_SIZE};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
            0, 0, NULL, 1, &hb, 0, NULL);
        result = dm_vk_submit(vk, cmd);
        cmd = VK_NULL_HANDLE;
        dm_vk_buffer_destroy(vk, &vb);
        if (result != VK_SUCCESS) goto done;
        if (dm_vk_buffer_sync(vk, &readback, 0) != VK_SUCCESS) { result = VK_ERROR_UNKNOWN; goto done; }
        memcpy(out->bytes, readback.mapped, (size_t)bytes);
    }
done:
    if (cmd) vkFreeCommandBuffers(vk->device, vk->pool, 1, &cmd);
    dm_vk_buffer_destroy(vk, &readback);
    if (pipeline) vkDestroyPipeline(vk->device, pipeline, NULL);
    if (layout) vkDestroyPipelineLayout(vk->device, layout, NULL);
    if (fb) vkDestroyFramebuffer(vk->device, fb, NULL);
    if (pass) vkDestroyRenderPass(vk->device, pass, NULL);
    if (vs) vkDestroyShaderModule(vk->device, vs, NULL);
    if (fs) vkDestroyShaderModule(vk->device, fs, NULL);
    if (view) vkDestroyImageView(vk->device, view, NULL);
    if (image) vkDestroyImage(vk->device, image, NULL);
    if (memory) vkFreeMemory(vk->device, memory, NULL);
    free(b);
    return result == VK_SUCCESS ? 1 : 0;
}
