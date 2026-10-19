// 贴图创建、Tegra 解排布、上传与渲染目标清除。| Texture creation, Tegra deswizzling, uploads and render-target clears.
typedef struct cached_image_view { VkImageViewType type; VkImageSubresourceRange range; VkImageView view; struct cached_image_view *next; } cached_image_view;
typedef struct image_entry {
    uint64_t serial; tex_t texture; VkImage image; VkImageView view; VkDeviceMemory memory;
    VkFormat format; VkImageAspectFlags aspect; uint64_t stamp; int populated;
    uint64_t source_stamp,source_hash,source_seen;int source_valid,source_tracked;
    VkPipelineStageFlags last_stage;VkAccessFlags last_access;int gpu_written;
    cached_image_view *views;
    struct image_entry *next,*hash_next;
} image_entry;
typedef struct { int valid; tex_t texture; view_t view; } texture_descriptor;
typedef struct { int valid; samp_par parameters; VkSampler sampler; } sampler_descriptor;
static image_entry *image_list,*image_index[4096];
static texture_descriptor *texture_descriptors;
static sampler_descriptor *sampler_descriptors;
static int texture_capacity, sampler_capacity;
static pthread_mutex_t descriptor_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t texture_uploads, render_passes, draw_count;
static uint64_t image_barriers;
static VkImageView image_view_get(image_entry *e,VkImageViewType type,VkImageSubresourceRange range) {
    for(cached_image_view *v=e->views;v;v=v->next)if(v->type==type&&!memcmp(&v->range,&range,sizeof range))return v->view;
    cached_image_view *v=calloc(1,sizeof *v);if(!v)die("Vulkan image view cache allocation");v->type=type;v->range=range;
    VkImageViewCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=e->image,.viewType=type,.format=e->format,.subresourceRange=range};
    VK_CHECK(vkCreateImageView(device,&ci,NULL,&v->view));v->next=e->views;e->views=v;return v->view;
}

static VkFormat vk_format(int format) {
    static const VkFormat formats[] = {
        [0x01]=VK_FORMAT_R8_UNORM, [0x02]=VK_FORMAT_R8_SNORM, [0x03]=VK_FORMAT_R8_UINT, [0x04]=VK_FORMAT_R8_SINT,
        [0x05]=VK_FORMAT_R16_SFLOAT, [0x06]=VK_FORMAT_R16_UNORM, [0x07]=VK_FORMAT_R16_SNORM, [0x08]=VK_FORMAT_R16_UINT, [0x09]=VK_FORMAT_R16_SINT,
        [0x0a]=VK_FORMAT_R32_SFLOAT, [0x0b]=VK_FORMAT_R32_UINT, [0x0c]=VK_FORMAT_R32_SINT,
        [0x0d]=VK_FORMAT_R8G8_UNORM, [0x0e]=VK_FORMAT_R8G8_SNORM, [0x0f]=VK_FORMAT_R8G8_UINT, [0x10]=VK_FORMAT_R8G8_SINT,
        [0x11]=VK_FORMAT_R16G16_SFLOAT, [0x12]=VK_FORMAT_R16G16_UNORM, [0x13]=VK_FORMAT_R16G16_SNORM, [0x14]=VK_FORMAT_R16G16_UINT, [0x15]=VK_FORMAT_R16G16_SINT,
        [0x16]=VK_FORMAT_R32G32_SFLOAT, [0x17]=VK_FORMAT_R32G32_UINT, [0x18]=VK_FORMAT_R32G32_SINT,
        [0x19]=VK_FORMAT_R8G8B8_UNORM, [0x1a]=VK_FORMAT_R8G8B8_SNORM, [0x1b]=VK_FORMAT_R8G8B8_UINT, [0x1c]=VK_FORMAT_R8G8B8_SINT,
        [0x1d]=VK_FORMAT_R16G16B16_SFLOAT, [0x1e]=VK_FORMAT_R16G16B16_UNORM, [0x1f]=VK_FORMAT_R16G16B16_SNORM, [0x20]=VK_FORMAT_R16G16B16_UINT, [0x21]=VK_FORMAT_R16G16B16_SINT,
        [0x22]=VK_FORMAT_R32G32B32_SFLOAT, [0x23]=VK_FORMAT_R32G32B32_UINT, [0x24]=VK_FORMAT_R32G32B32_SINT,
        [0x25]=VK_FORMAT_R8G8B8A8_UNORM, [0x26]=VK_FORMAT_R8G8B8A8_SNORM, [0x27]=VK_FORMAT_R8G8B8A8_UINT, [0x28]=VK_FORMAT_R8G8B8A8_SINT,
        [0x29]=VK_FORMAT_R16G16B16A16_SFLOAT, [0x2a]=VK_FORMAT_R16G16B16A16_UNORM, [0x2b]=VK_FORMAT_R16G16B16A16_SNORM, [0x2c]=VK_FORMAT_R16G16B16A16_UINT, [0x2d]=VK_FORMAT_R16G16B16A16_SINT,
        [0x2e]=VK_FORMAT_R32G32B32A32_SFLOAT, [0x2f]=VK_FORMAT_R32G32B32A32_UINT, [0x30]=VK_FORMAT_R32G32B32A32_SINT,
        [0x31]=VK_FORMAT_S8_UINT, [0x32]=VK_FORMAT_D16_UNORM, [0x33]=VK_FORMAT_X8_D24_UNORM_PACK32, [0x34]=VK_FORMAT_D32_SFLOAT, [0x35]=VK_FORMAT_D24_UNORM_S8_UINT, [0x36]=VK_FORMAT_D32_SFLOAT_S8_UINT,
        [0x37]=VK_FORMAT_R8G8B8A8_SRGB, [0x38]=VK_FORMAT_R8G8B8A8_SRGB,
        [0x39]=VK_FORMAT_R4G4B4A4_UNORM_PACK16, [0x3b]=VK_FORMAT_R5G5B5A1_UNORM_PACK16, [0x3c]=VK_FORMAT_R5G6B5_UNORM_PACK16,
        [0x3d]=VK_FORMAT_A2B10G10R10_UNORM_PACK32, [0x3e]=VK_FORMAT_A2B10G10R10_UINT_PACK32, [0x3f]=VK_FORMAT_B10G11R11_UFLOAT_PACK32, [0x40]=VK_FORMAT_E5B9G9R9_UFLOAT_PACK32,
        [0x41]=VK_FORMAT_BC1_RGB_UNORM_BLOCK, [0x42]=VK_FORMAT_BC1_RGBA_UNORM_BLOCK, [0x43]=VK_FORMAT_BC2_UNORM_BLOCK, [0x44]=VK_FORMAT_BC3_UNORM_BLOCK,
        [0x45]=VK_FORMAT_BC1_RGB_SRGB_BLOCK, [0x46]=VK_FORMAT_BC1_RGBA_SRGB_BLOCK, [0x47]=VK_FORMAT_BC2_SRGB_BLOCK, [0x48]=VK_FORMAT_BC3_SRGB_BLOCK,
        [0x49]=VK_FORMAT_BC4_UNORM_BLOCK, [0x4a]=VK_FORMAT_BC4_SNORM_BLOCK, [0x4b]=VK_FORMAT_BC5_UNORM_BLOCK, [0x4c]=VK_FORMAT_BC5_SNORM_BLOCK,
        [0x4d]=VK_FORMAT_BC7_UNORM_BLOCK, [0x4e]=VK_FORMAT_BC7_SRGB_BLOCK, [0x4f]=VK_FORMAT_BC6H_SFLOAT_BLOCK, [0x50]=VK_FORMAT_BC6H_UFLOAT_BLOCK,
        [0x6d]=VK_FORMAT_R8G8B8A8_UNORM, [0x6e]=VK_FORMAT_R8G8B8A8_SNORM, [0x6f]=VK_FORMAT_R8G8B8A8_UINT, [0x70]=VK_FORMAT_R8G8B8A8_SINT,
        [0x71]=VK_FORMAT_R16G16B16A16_SFLOAT, [0x72]=VK_FORMAT_R16G16B16A16_UNORM, [0x73]=VK_FORMAT_R16G16B16A16_SNORM, [0x74]=VK_FORMAT_R16G16B16A16_UINT, [0x75]=VK_FORMAT_R16G16B16A16_SINT,
        [0x76]=VK_FORMAT_R32G32B32A32_SFLOAT, [0x77]=VK_FORMAT_R32G32B32A32_UINT, [0x78]=VK_FORMAT_R32G32B32A32_SINT,
    };
    if (format >= 0x79 && format <= 0x94) return VK_FORMAT_ASTC_4x4_UNORM_BLOCK + (format - 0x79) % 14 * 2 + (format >= 0x87);
    return format > 0 && format < sizeof formats / sizeof *formats ? formats[format] : VK_FORMAT_UNDEFINED;
}
// 按真正的读写风险同步；连续只读与通道内同附件写入不加全局屏障。
// Synchronize actual read/write hazards; avoid global barriers for consecutive reads or same-pass attachment writes.
static void image_access(image_entry *e,VkPipelineStageFlags stage,VkAccessFlags access) {
    VkAccessFlags writes=VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_HOST_WRITE_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    if(graphics_image_is_attachment(e)&&stage==e->last_stage&&access==e->last_access&&(access&(VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT)))return;
    if(e->last_access&&((e->last_access|access)&writes)){
        graphics_pass_end();VkImageMemoryBarrier b={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.srcAccessMask=e->last_access,.dstAccessMask=access,.oldLayout=VK_IMAGE_LAYOUT_GENERAL,.newLayout=VK_IMAGE_LAYOUT_GENERAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=e->image,.subresourceRange={e->aspect,0,e->texture.d.levels,0,e->texture.L.layers}};
        vkCmdPipelineBarrier(command,e->last_stage,stage,0,0,NULL,0,NULL,1,&b);image_barriers++;
        e->last_stage=stage;e->last_access=access;
    }else{e->last_stage|=stage;e->last_access|=access;}
}
static uint32_t dimension(uint32_t v, unsigned level) { v >>= level; return v ? v : 1; }
static image_entry *image_get(const tex_t *t) {
    unsigned bucket=t->serial&4095;for(image_entry *e=image_index[bucket];e;e=e->hash_next)if(e->serial==t->serial)return e;
    if (t->d.target==NVN_TEXTURE_TARGET_BUFFER || t->d.samples>1) die("Vulkan texture target/samples pending: %d/%d",t->d.target,t->d.samples);
    image_entry *e=calloc(1,sizeof *e); if(!e) die("Vulkan texture allocation"); e->serial=t->serial;e->texture=*t;e->format=vk_format(t->d.format);graphics_pass_end();
    if(e->format==VK_FORMAT_UNDEFINED) die("Unknown Vulkan NVN format %#x",t->d.format);
    e->aspect=VK_IMAGE_ASPECT_COLOR_BIT;
    if(t->d.format>=0x31 && t->d.format<=0x36) {e->aspect=0;if(t->d.format!=0x31)e->aspect|=VK_IMAGE_ASPECT_DEPTH_BIT;if(t->d.format==0x31||t->d.format==0x35||t->d.format==0x36)e->aspect|=VK_IMAGE_ASPECT_STENCIL_BIT;}
    VkFormatProperties fp;vkGetPhysicalDeviceFormatProperties(physical,e->format,&fp);
    VkImageUsageFlags usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
    if(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)usage|=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)usage|=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)usage|=VK_IMAGE_USAGE_STORAGE_BIT;
    int is3d=t->d.target==NVN_TEXTURE_TARGET_3D,is1d=t->d.target==NVN_TEXTURE_TARGET_1D||t->d.target==NVN_TEXTURE_TARGET_1D_ARRAY;
    VkImageCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=is3d?VK_IMAGE_TYPE_3D:is1d?VK_IMAGE_TYPE_1D:VK_IMAGE_TYPE_2D,.format=e->format,.extent={t->d.width,is1d?1:t->d.height,is3d?t->d.depth:1},.mipLevels=t->d.levels,.arrayLayers=t->L.layers,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,.usage=usage,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
    if(t->d.target==NVN_TEXTURE_TARGET_CUBEMAP||t->d.target==NVN_TEXTURE_TARGET_CUBEMAP_ARRAY)ci.flags=VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    VK_CHECK(vkCreateImage(device,&ci,NULL,&e->image));VkMemoryRequirements req;vkGetImageMemoryRequirements(device,e->image,&req);
    uint32_t type=UINT32_MAX;for(uint32_t i=0;i<memory_properties.memoryTypeCount;i++)if((req.memoryTypeBits & (1u<<i)) && (memory_properties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)){type=i;break;}
    if(type==UINT32_MAX)die("No Vulkan image memory");VkMemoryAllocateInfo mi={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=type};
    VK_CHECK(vkAllocateMemory(device,&mi,NULL,&e->memory));VK_CHECK(vkBindImageMemory(device,e->image,e->memory,0));
    VkImageViewType view= is3d?VK_IMAGE_VIEW_TYPE_3D:is1d?(t->L.layers>1?VK_IMAGE_VIEW_TYPE_1D_ARRAY:VK_IMAGE_VIEW_TYPE_1D):(t->L.layers>1?VK_IMAGE_VIEW_TYPE_2D_ARRAY:VK_IMAGE_VIEW_TYPE_2D);
    if(ci.flags) view=t->d.target==NVN_TEXTURE_TARGET_CUBEMAP?VK_IMAGE_VIEW_TYPE_CUBE:VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
    VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=e->image,.viewType=view,.format=e->format,.subresourceRange={e->aspect,0,t->d.levels,0,t->L.layers}};
    VK_CHECK(vkCreateImageView(device,&vi,NULL,&e->view));
    VkImageMemoryBarrier b={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_GENERAL,.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=e->image,.subresourceRange=vi.subresourceRange};
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,NULL,0,NULL,1,&b);
    e->next=image_list;image_list=e;e->hash_next=image_index[bucket];image_index[bucket]=e;return e;
}
// GPU 内容与 packaged CPU 源是两条独立版本；GPU 写不能把后来的 CPU 脏写永久屏蔽。
// Track GPU contents separately from packaged CPU sources; GPU writes must not suppress subsequent real CPU changes.
static uint64_t image_source_hash(const uint8_t *source,uint64_t size) {
    uint64_t hash=14695981039346656037ull;for(uint64_t i=0;i<size;i++)hash=(hash^source[i])*1099511628211ull;return hash;
}
static void image_source_baseline(image_entry *e,const uint8_t *source,uint64_t stamp) {
    const tex_t *t=&e->texture;if(!t->has_layout||!t->mem)return;
    e->source_stamp=stamp;e->source_seen=stamp;e->source_tracked=nvn_dirty_tracked(t->mem,t->L.size);e->source_valid=1;
    // 未跟踪来源没有可靠代数，用内容变化辨别后来 CPU 写，仍保留未改源时的 GPU 拷贝。
    // Untracked sources have no reliable generation; compare their bytes while retaining GPU copies if the source is unchanged.
    if(!e->source_tracked)e->source_hash=image_source_hash(source,t->L.size);
}
static void image_gpu_written(image_entry *e) {
    const tex_t *t=&e->texture;
    // 已存在的基线代表真正读过的 CPU 版本，重复 GPU 标记不能吞掉尚未读的 CPU 脏写。
    // Existing baselines represent CPU versions actually read; repeated GPU marking cannot consume unread CPU changes.
    if(t->has_layout&&t->mem&&!e->source_valid){nvn_dirty_protect(t->mem,t->L.size);image_source_baseline(e,t->mem,nvn_dirty_now());}
    e->populated=1;e->stamp=0;e->gpu_written=1;
}
// 只读计数：GPU 渲染过的贴图又被整张从客体内存上传回去，等于用旧字节覆盖刚渲染出来的内容。
// Read-only counter: an image the GPU rendered is uploaded whole from guest memory again, overwriting freshly
// rendered content with older bytes.
static uint64_t image_gpu_overwrites;
// 游戏绑定了句柄、端口却查不到登记的采样贴图次数（会退回 1x1 替身贴图）。
// How often the game bound a handle whose sampled texture the port cannot resolve (falls back to the 1x1 stand-in).
static uint64_t gpu_unregistered_textures;
static void image_upload(image_entry *e) {
    const tex_t *t=&e->texture;
    int pending=t->mem && snapshot_pending((uintptr_t)t->mem,t->L.size);
    if(e->populated&&!pending){
        if(t->has_layout&&t->mem){
            if(e->source_valid){int tracked=nvn_dirty_tracked(t->mem,t->L.size);
                if(tracked&&e->source_tracked){uint64_t now=nvn_dirty_now();int clean=now==e->source_seen||!nvn_dirty_since(t->mem,t->L.size,e->source_stamp);e->source_seen=now;if(clean)return;}
                else if(!tracked&&!e->source_tracked&&image_source_hash(t->mem,t->L.size)==e->source_hash)return;
            }
        }else if(!e->stamp||!nvn_dirty_since(t->mem,t->L.size,e->stamp))return;
    }
    if(!t->mem)die("Vulkan texture has no source storage");
    if((e->aspect & VK_IMAGE_ASPECT_STENCIL_BIT) && (e->aspect & VK_IMAGE_ASPECT_DEPTH_BIT))die("Vulkan combined depth/stencil CPU upload pending");
    // 与原后端一样先保护再取代数和读取，减少并发写入漏记的窗口。
    // Like the original backend, protect before taking the generation/reading to avoid a window for untracked concurrent writes.
    graphics_pass_end();nvn_dirty_protect(t->mem,t->L.size);uint64_t stamp=nvn_dirty_now();image_access(e,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);uint8_t *shadow=NULL;const uint8_t *storage=t->mem;
    // 上传必须看到同批前面的复制，不提前发布客体内存。
    // Uploads must see preceding copies in the batch without publishing guest memory early.
    // 未跟踪 packaged 源也冻结一份快照，不能上传旧字节后把并发改写的新字节记为已上传。
    // Freeze untracked packaged sources too; never upload old bytes and then baseline a concurrent newer write.
    if(pending||(t->has_layout&&!nvn_dirty_tracked(t->mem,t->L.size))){shadow=malloc(t->L.size);if(!shadow||snapshot_read((uintptr_t)t->mem,shadow,t->L.size))die("Vulkan texture snapshot read");storage=shadow;}
    for(unsigned layer=0;layer<t->L.layers;layer++)for(unsigned level=0;level<t->d.levels;level++){
        int bw,bh;uint32_t bpb=nvn_fmt_bpb(t->d.format,&bw,&bh),w=dimension(t->d.width,level),h=dimension(t->d.height,level),d=t->d.target==NVN_TEXTURE_TARGET_3D?dimension(t->d.depth,level):1;
        uint32_t pitch=(w+bw-1)/bw*bpb,rows=(h+bh-1)/bh;uint64_t bytes=(uint64_t)pitch*rows*d;
        post_op *p=post_new(3);p->source=buffer_create(bytes);
        if(t->d.flags & (NVN_TEXTURE_FLAGS_LINEAR|NVN_TEXTURE_FLAGS_LINEAR_RENDER_TARGET)){
            const uint8_t *src=storage+nvn_tex_level_offset(&t->d,level)+layer*t->L.layer_size;size_t stride=t->d.stride?t->d.stride:pitch;
            for(unsigned z=0;z<d;z++)for(unsigned y=0;y<rows;y++)memcpy((uint8_t*)p->source.mapped+(z*rows+y)*pitch,src+(z*rows+y)*stride,pitch);
        }else if(nvn_deswizzle(&t->d,level,layer,storage,p->source.mapped,pitch))die("Vulkan texture deswizzle failed");
        VkBufferImageCopy r={.imageSubresource={e->aspect,level,layer,1},.imageExtent={w,h,d}};
        vkCmdCopyBufferToImage(command,p->source.buffer,e->image,VK_IMAGE_LAYOUT_GENERAL,1,&r);
    }
    if(e->gpu_written){image_gpu_overwrites++;if(image_gpu_overwrites<=6||!(image_gpu_overwrites%64))LOG("[vulkan-upload] 覆盖 GPU 已渲染内容 serial=%llu %ux%u format=%#x bytes=%llu 第 %llu 次（客体内存里没有 GPU 那次渲染的结果）",(unsigned long long)t->serial,t->d.width,t->d.height,t->d.format,(unsigned long long)t->L.size,(unsigned long long)image_gpu_overwrites);}
    image_source_baseline(e,storage,stamp);free(shadow);e->populated=1;e->stamp=stamp;texture_uploads++;
}
static VkImageSubresourceRange texture_range(image_entry *e,const view_t *v) {
    VkImageSubresourceRange r={e->aspect,v&&v->set_levels?v->base_level:0,1,v&&v->set_layers?v->min_layer:0,1};
    if(r.baseMipLevel>=e->texture.d.levels||r.baseArrayLayer>=e->texture.L.layers)die("Vulkan texture view bounds");return r;
}
static void clear_color(const p_clear_color *c) {
    if(c->index<0||c->index>=state.rt.n||!state.rt.color[c->index])return;
    if((c->mask & 15)!=15)die("Vulkan masked color clear pending");
    image_entry *e=image_get(state.rt.color[c->index]);VkImageSubresourceRange r=texture_range(e,state.rt.has_cview[c->index]?&state.rt.cview[c->index]:NULL);
    // 非完整剪裁必须保留未覆盖像素；不能用整张清除替代。
    // A partial scissor must preserve uncovered pixels; never substitute a whole-image clear.
    if(state.scissor.w>0&&(state.scissor.x||state.scissor.y||state.scissor.w<(int)dimension(e->texture.d.width,r.baseMipLevel)||state.scissor.h<(int)dimension(e->texture.d.height,r.baseMipLevel)))die("Vulkan scissored clear pending");
    graphics_pass_end();VkClearColorValue color;memcpy(color.float32,c->rgba,sizeof c->rgba);image_access(e,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);vkCmdClearColorImage(command,e->image,VK_IMAGE_LAYOUT_GENERAL,&color,1,&r);image_gpu_written(e);
}
static void clear_depth(const p_clear_ds *c) {
    if(!state.rt.depth)return;image_entry *e=image_get(state.rt.depth);VkImageSubresourceRange r=texture_range(e,state.rt.has_dview?&state.rt.dview:NULL);r.aspectMask=0;
    if(c->depth_mask)r.aspectMask|=VK_IMAGE_ASPECT_DEPTH_BIT;if(c->stencil_mask){if(c->stencil_mask!=255)die("Vulkan partial stencil clear pending");r.aspectMask|=VK_IMAGE_ASPECT_STENCIL_BIT;}
    r.aspectMask&=e->aspect;if(!r.aspectMask)return;graphics_pass_end();VkClearDepthStencilValue value={c->depth,c->stencil};image_access(e,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);vkCmdClearDepthStencilImage(command,e->image,VK_IMAGE_LAYOUT_GENERAL,&value,1,&r);image_gpu_written(e);
}
static void copy_buffer_texture(const p_copy_b2t *c) {
    if(c->r.width<=0||c->r.height<=0||c->r.depth<=0)return;
    snapshot_prepare();
    dependencies_prepare_copy_texture(c);
    graphics_pass_end();image_entry *e=image_get(c->tex);image_upload(e);image_access(e,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);VkImageSubresourceRange range=texture_range(e,c->has_view?&c->view:NULL);
    int bw,bh;unsigned bpb=nvn_fmt_bpb(c->tex->d.format,&bw,&bh),pitch=(c->r.width+bw-1)/bw*bpb,rows=(c->r.height+bh-1)/bh;uint64_t bytes=(uint64_t)pitch*rows*c->r.depth;
    uint64_t stride=state.copy_stride>0?state.copy_stride:pitch;if(stride<pitch)die("Vulkan texture copy stride");post_op *p=post_new(3);p->source=buffer_create(bytes);
    for(int z=0;z<c->r.depth;z++)for(unsigned y=0;y<rows;y++)if(snapshot_read(c->src+(z*rows+y)*stride,(uint8_t*)p->source.mapped+(z*rows+y)*pitch,pitch))die("Vulkan texture copy source");
    int is3d=c->tex->d.target==NVN_TEXTURE_TARGET_3D;VkBufferImageCopy copy={.imageSubresource={e->aspect,range.baseMipLevel,range.baseArrayLayer+(is3d?0:c->r.zoffset),is3d?1:c->r.depth},.imageOffset={c->r.xoffset,c->r.yoffset,is3d?c->r.zoffset:0},.imageExtent={c->r.width,c->r.height,is3d?c->r.depth:1}};
    vkCmdCopyBufferToImage(command,p->source.buffer,e->image,VK_IMAGE_LAYOUT_GENERAL,1,&copy);image_gpu_written(e);
}
enum { TEXTURE_CLEAR_UNORM, TEXTURE_CLEAR_SNORM, TEXTURE_CLEAR_UINT, TEXTURE_CLEAR_SINT, TEXTURE_CLEAR_FLOAT };
typedef struct { unsigned channels,bits,kind; } texture_clear_format;
static texture_clear_format texture_clear_format_get(int format) {
    if(format>=0x6d&&format<=0x78)format-=0x48;
    if(format>=1&&format<=0x30){unsigned index=(format-1)%12;static const uint8_t kinds[]={TEXTURE_CLEAR_UNORM,TEXTURE_CLEAR_SNORM,TEXTURE_CLEAR_UINT,TEXTURE_CLEAR_SINT,TEXTURE_CLEAR_FLOAT,TEXTURE_CLEAR_UNORM,TEXTURE_CLEAR_SNORM,TEXTURE_CLEAR_UINT,TEXTURE_CLEAR_SINT,TEXTURE_CLEAR_FLOAT,TEXTURE_CLEAR_UINT,TEXTURE_CLEAR_SINT};return (texture_clear_format){(format-1)/12+1,index<4?8:index<9?16:32,kinds[index]};}
    if(format==NVN_FORMAT_RGB10A2UI)return (texture_clear_format){4,0,TEXTURE_CLEAR_UINT};
    return (texture_clear_format){0};
}
// float16 位模式按最近偶数舍入，避免以底层 UNORM 的数值清除代替浮点视图的字节。
// Round float16 bits to nearest even; never substitute a base UNORM numeric clear for a float view's bytes.
static uint16_t texture_clear_half(float value) {
    uint32_t bits;memcpy(&bits,&value,4);unsigned sign=(bits>>16)&0x8000,fraction=bits&0x7fffff,exponent=(bits>>23)&255;
    if(exponent==255)return sign|0x7c00|(fraction?0x200|(fraction>>13):0);
    int half_exponent=(int)exponent-127+15;if(half_exponent>=31)return sign|0x7c00;
    if(half_exponent<=0){if(half_exponent<-10)return sign;unsigned shift=14-half_exponent,mantissa=fraction|0x800000,result=mantissa>>shift,remainder=mantissa&((1u<<shift)-1),half=1u<<(shift-1);return sign|(result+(remainder>half||(remainder==half&&(result&1))));}
    unsigned result=(half_exponent<<10)|(fraction>>13),remainder=fraction&0x1fff;return sign|(result+(remainder>0x1000||(remainder==0x1000&&(result&1))));
}
static uint64_t texture_clear_round(double value) {
    uint64_t result=(uint64_t)value;double remainder=value-(double)result;return result+(remainder>.5||(remainder==.5&&(result&1)));
}
static unsigned texture_clear_pattern(const p_clear_tex *c,int format,uint8_t pattern[16]) {
    texture_clear_format f=texture_clear_format_get(format);if(!f.channels)die("Vulkan typed texture clear format pending: %#x",format);
    uint32_t packed=0;
    for(unsigned i=0;i<f.channels;i++){
        unsigned bits=f.bits?f.bits:i==3?2:10;uint64_t mask=(1ull<<bits)-1,raw=0;double value=c->is_uint?(double)c->c.u[i]:(double)c->c.f[i];
        if(f.kind==TEXTURE_CLEAR_FLOAT){float converted=(float)value;if(bits==16)raw=texture_clear_half(converted);else{uint32_t word;memcpy(&word,&converted,4);raw=word;}}
        else if(f.kind==TEXTURE_CLEAR_UINT){if(c->is_uint)raw=c->c.u[i]&mask;else{if(!(value>=0&&value<=4294967295.0))die("Vulkan float-to-UINT texture clear value");raw=(uint64_t)value&mask;}}
        else if(f.kind==TEXTURE_CLEAR_SINT){int64_t minimum=-(1ll<<(bits-1)),maximum=(1ll<<(bits-1))-1;if(!(value>=(double)minimum&&value<=(double)maximum))die("Vulkan SINT texture clear value outside view range");raw=(uint64_t)(int64_t)value&mask;}
        else if(f.kind==TEXTURE_CLEAR_UNORM){if(value!=value)die("Vulkan NaN UNORM texture clear");value=value<0?0:value>1?1:value;raw=texture_clear_round(value*(double)mask);}
        else {if(value!=value)die("Vulkan NaN SNORM texture clear");value=value<-1?-1:value>1?1:value;uint64_t magnitude=texture_clear_round((value<0?-value:value)*(double)(mask>>1));raw=(value<0?-(int64_t)magnitude:(int64_t)magnitude)&mask;}
        if(!f.bits)packed|=(uint32_t)raw<<(i*10);else for(unsigned byte=0;byte<bits/8;byte++)pattern[i*(bits/8)+byte]=raw>>(byte*8);
    }
    if(!f.bits){memcpy(pattern,&packed,4);return 4;}return f.channels*f.bits/8;
}
static void clear_texture(const p_clear_tex *c) {
    if(c->r.width<=0||c->r.height<=0||c->r.depth<=0||!(c->mask&15))return;
    const view_t *v=c->has_view?&c->view:NULL;int format=v&&v->set_format?v->format:c->tex->d.format;
    // 初始化只记录前五次真实清除，不给每帧绘制增加日志或同步。
    // Log only the first five actual initialization clears, without adding per-frame draw logging or synchronization.
    static unsigned diagnostics;if(diagnostics++<5)LOG("[vulkan] typed texture clear serial=%llu base=%#x view=%#x ui=%u raw=%08x,%08x,%08x,%08x mip=%d layer=%d+%d region=%d,%d,%d %dx%dx%d mask=%#x",(unsigned long long)c->tex->serial,c->tex->d.format,format,c->is_uint,c->c.u[0],c->c.u[1],c->c.u[2],c->c.u[3],v&&v->set_levels?v->base_level:0,v&&v->set_layers?v->min_layer:0,v&&v->set_layers?v->nlayers:c->tex->L.layers,c->r.xoffset,c->r.yoffset,c->r.zoffset,c->r.width,c->r.height,c->r.depth,c->mask);
    if(c->tex->has_layout&&c->tex->mem)dependencies_prepare_texture(c->tex);
    graphics_pass_end();image_entry *e=image_get(c->tex);VkImageSubresourceRange range=texture_range(e,v);int is3d=c->tex->d.target==NVN_TEXTURE_TARGET_3D;
    unsigned w=dimension(c->tex->d.width,range.baseMipLevel),h=(c->tex->d.target==NVN_TEXTURE_TARGET_1D||c->tex->d.target==NVN_TEXTURE_TARGET_1D_ARRAY)?1:dimension(c->tex->d.height,range.baseMipLevel),d=is3d?dimension(c->tex->d.depth,range.baseMipLevel):c->tex->L.layers-range.baseArrayLayer;
    if(v&&v->set_levels&&(v->nlevels<=0||(uint32_t)v->nlevels>c->tex->d.levels-range.baseMipLevel))die("Vulkan texture clear mip view bounds");
    if(v&&v->set_layers){if(is3d||v->nlayers<=0||(uint32_t)v->nlayers>d)die("Vulkan texture clear layer view bounds");d=v->nlayers;}
    if(c->r.xoffset<0||c->r.yoffset<0||c->r.zoffset<0||(uint64_t)c->r.xoffset+c->r.width>w||(uint64_t)c->r.yoffset+c->r.height>h||(uint64_t)c->r.zoffset+c->r.depth>d)die("Vulkan texture clear region bounds");
    texture_clear_format f=texture_clear_format_get(format);unsigned channel_mask=f.channels?(1u<<f.channels)-1:15;
    if((c->mask&channel_mask)!=channel_mask)die("Vulkan masked texture clear pending: mask=%#x format=%#x",c->mask,format);
    int bw,bh,vbw,vbh;unsigned bytes=nvn_fmt_bpb(c->tex->d.format,&bw,&bh),view_bytes=nvn_fmt_bpb(format,&vbw,&vbh);
    if(e->aspect!=VK_IMAGE_ASPECT_COLOR_BIT||!bytes||bytes!=view_bytes||bw!=1||bh!=1||vbw!=1||vbh!=1||vk_format(format)==VK_FORMAT_UNDEFINED||(format>=0x31&&format<=0x36))die("Vulkan incompatible typed texture clear base=%#x view=%#x",c->tex->d.format,format);
    // 与 Metal tex_mtl 一样先保留 packaged 源：区域外、其余 mip 和层不能被清除值覆盖。
    // Like Metal tex_mtl, preserve packaged sources first; the region exterior, other mips and layers must retain their contents.
    if(c->tex->has_layout&&c->tex->mem)image_upload(e);
    image_access(e,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
    int whole_xy=!c->r.xoffset&&!c->r.yoffset&&c->r.width==(int)w&&c->r.height==(int)h;
    if(!is3d&&whole_xy&&vk_format(format)==e->format&&((c->is_uint&&f.channels&&f.kind==TEXTURE_CLEAR_UINT)||(!c->is_uint&&(!f.channels||f.kind==TEXTURE_CLEAR_UNORM||f.kind==TEXTURE_CLEAR_SNORM||f.kind==TEXTURE_CLEAR_FLOAT)))){
        range.baseArrayLayer+=c->r.zoffset;range.layerCount=c->r.depth;VkClearColorValue value;memcpy(&value,&c->c,sizeof value);vkCmdClearColorImage(command,e->image,VK_IMAGE_LAYOUT_GENERAL,&value,1,&range);
    }else{
        // VkClearColorImage 只接受 image，不接受 typed view；编码视图像素后原位 GPU 复制，避免非法格式别名。
        // VkClearColorImage accepts an image, not a typed view; encode view pixels and copy raw GPU bytes in place, avoiding illegal format aliases.
        uint8_t pattern[16]={0};if(texture_clear_pattern(c,format,pattern)!=bytes)die("Vulkan typed texture clear pixel size");
        uint64_t count=(uint64_t)c->r.width*c->r.height;if(count>((1ull<<32)-3)/bytes/(unsigned)c->r.depth)die("Vulkan texture clear staging size");uint64_t size=count*c->r.depth*bytes,padded=(size+3)&~3ull;
        post_op *p=post_new(3);p->source=buffer_create(padded);
        if(bytes==1||bytes==2||bytes==4){uint8_t repeated[4];for(unsigned i=0;i<4;i++)repeated[i]=pattern[i%bytes];uint32_t word;memcpy(&word,repeated,4);vkCmdFillBuffer(command,p->source.buffer,0,padded,word);
            VkBufferMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.buffer=p->source.buffer,.offset=0,.size=padded};vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,1,&barrier,0,NULL);
        }else for(uint64_t offset=0;offset<size;offset+=bytes)memcpy((uint8_t*)p->source.mapped+offset,pattern,bytes);
        VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,range.baseMipLevel,range.baseArrayLayer+(is3d?0:c->r.zoffset),is3d?1:c->r.depth},.imageOffset={c->r.xoffset,c->r.yoffset,is3d?c->r.zoffset:0},.imageExtent={c->r.width,c->r.height,is3d?c->r.depth:1}};vkCmdCopyBufferToImage(command,p->source.buffer,e->image,VK_IMAGE_LAYOUT_GENERAL,1,&copy);
    }
    image_gpu_written(e);
}
static void copy_texture_texture(const p_copy_t2t *c) {
    snapshot_prepare();
    dependencies_prepare_texture(c->src);
    if(c->dst->has_layout&&c->dst->mem)dependencies_prepare_texture(c->dst);
    graphics_pass_end();image_entry *src=image_get(c->src),*dst=image_get(c->dst);image_upload(src);
    // 原 Metal 的目标也经 tex_mtl：packaged 目标未被覆盖的块和 mip 必须先有真实源内容。
    // Metal also resolves destinations through tex_mtl; preserve untouched blocks/mips of packaged destinations from their real source.
    if(dst->texture.has_layout&&dst->texture.mem)image_upload(dst);
    image_access(src,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT);image_access(dst,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT);
    VkImageSubresourceRange sr=texture_range(src,c->has_sv?&c->sv:NULL),dr=texture_range(dst,c->has_dv?&c->dv:NULL);int s3=c->src->d.target==NVN_TEXTURE_TARGET_3D,d3=c->dst->d.target==NVN_TEXTURE_TARGET_3D;
    VkImageAspectFlags aspects=src->aspect & dst->aspect;if(!aspects)die("Vulkan incompatible copy aspects");
    for(unsigned aspect=1;aspect<=VK_IMAGE_ASPECT_STENCIL_BIT;aspect<<=1)if(aspects & aspect){
        VkImageSubresourceLayers sl={aspect,sr.baseMipLevel,sr.baseArrayLayer+(s3?0:c->sr.zoffset),s3?1:c->sr.depth},dl={aspect,dr.baseMipLevel,dr.baseArrayLayer+(d3?0:c->dr.zoffset),d3?1:c->dr.depth};
        if(src->format==dst->format&&c->sr.width==c->dr.width&&c->sr.height==c->dr.height&&c->sr.depth==c->dr.depth&&!(c->flags & (1|4|8|16))){VkImageCopy region={.srcSubresource=sl,.srcOffset={c->sr.xoffset,c->sr.yoffset,s3?c->sr.zoffset:0},.dstSubresource=dl,.dstOffset={c->dr.xoffset,c->dr.yoffset,d3?c->dr.zoffset:0},.extent={c->sr.width,c->sr.height,s3||d3?c->sr.depth:1}};vkCmdCopyImage(command,src->image,VK_IMAGE_LAYOUT_GENERAL,dst->image,VK_IMAGE_LAYOUT_GENERAL,1,&region);}
        else {VkImageBlit region={.srcSubresource=sl,.srcOffsets={{c->sr.xoffset,c->sr.yoffset,s3?c->sr.zoffset:0},{c->sr.xoffset+c->sr.width,c->sr.yoffset+c->sr.height,s3?c->sr.zoffset+c->sr.depth:1}},.dstSubresource=dl,.dstOffsets={{c->dr.xoffset,c->dr.yoffset,d3?c->dr.zoffset:0},{c->dr.xoffset+c->dr.width,c->dr.yoffset+c->dr.height,d3?c->dr.zoffset+c->dr.depth:1}}};for(int axis=0;axis<3;axis++)if(c->flags & (4<<axis)){int32_t *a=(int32_t*)&region.srcOffsets[0],*b=(int32_t*)&region.srcOffsets[1];int32_t t=a[axis];a[axis]=b[axis];b[axis]=t;}vkCmdBlitImage(command,src->image,VK_IMAGE_LAYOUT_GENERAL,dst->image,VK_IMAGE_LAYOUT_GENERAL,1,&region,(c->flags&1)&&aspect==VK_IMAGE_ASPECT_COLOR_BIT?VK_FILTER_LINEAR:VK_FILTER_NEAREST);}
    }
    image_gpu_written(dst);
}
void mtl_set_tex_pool_n(int n) {
    if(n<0||n>1048576)die("Vulkan texture descriptor capacity %d",n);pthread_mutex_lock(&descriptor_lock);
    if(n>texture_capacity){texture_descriptor *next=realloc(texture_descriptors,n*sizeof *next);if(!next)die("Vulkan descriptor allocation");memset(next+texture_capacity,0,(n-texture_capacity)*sizeof *next);texture_descriptors=next;texture_capacity=n;}pthread_mutex_unlock(&descriptor_lock);
}
void mtl_reg_tex(int id,const tex_t *t,const view_t *v) {pthread_mutex_lock(&descriptor_lock);if(id<0||id>=texture_capacity)die("Vulkan texture descriptor index %d",id);texture_descriptors[id]=(texture_descriptor){.valid=1,.texture=*t};if(v){texture_descriptors[id].view=*v;static unsigned view_diagnostics;if(v->set_format&&v->format!=t->d.format&&view_diagnostics++<12)LOG("[vulkan] texture view format texture=%#x view=%#x serial=%llu",t->d.format,v->format,(unsigned long long)t->serial);}pthread_mutex_unlock(&descriptor_lock);}
void mtl_reg_samp(int id,const samp_par *p) {
    if(id<0||id>1048576)die("Vulkan sampler descriptor index %d",id);pthread_mutex_lock(&descriptor_lock);
    if(id>=sampler_capacity){int n=(id+64)&~63;sampler_descriptor *next=realloc(sampler_descriptors,n*sizeof *next);if(!next)die("Vulkan sampler allocation");memset(next+sampler_capacity,0,(n-sampler_capacity)*sizeof *next);sampler_descriptors=next;sampler_capacity=n;}
    sampler_descriptors[id].valid=1;sampler_descriptors[id].parameters=*p;sampler_descriptors[id].sampler=VK_NULL_HANDLE;pthread_mutex_unlock(&descriptor_lock);
}
void mtl_tex_release(tex_t *t) {
    if(!device)return;mtl_finish();pthread_mutex_lock(&queue_lock);image_entry **link=&image_list;while(*link&&(*link)->serial!=t->serial)link=&(*link)->next;
    if(*link){image_entry *e=*link;graphics_cache_forget_image(e);*link=e->next;image_entry **hash=&image_index[e->serial&4095];while(*hash&&*hash!=e)hash=&(*hash)->hash_next;if(!*hash)die("Vulkan image index corruption");*hash=e->hash_next;while(e->views){cached_image_view *v=e->views;e->views=v->next;vkDestroyImageView(device,v->view,NULL);free(v);}vkDestroyImageView(device,e->view,NULL);vkDestroyImage(device,e->image,NULL);vkFreeMemory(device,e->memory,NULL);free(e);}pthread_mutex_unlock(&queue_lock);
}
void mtl_read_texels(const tex_t *t,unsigned level,unsigned layer,const NVNcopyRegion *r,uint8_t *dst,size_t row,size_t img) {
    if(r->width<=0||r->height<=0||r->depth<=0)return;mtl_sub_begin();dependencies_prepare_texture(t);image_entry *e=image_get(t);image_upload(e);
    if(e->aspect!=VK_IMAGE_ASPECT_COLOR_BIT)die("Vulkan depth readback pending");int bw,bh,bpb=nvn_fmt_bpb(t->d.format,&bw,&bh);
    uint32_t pitch=(r->width+bw-1)/bw*bpb,rows=(r->height+bh-1)/bh;uint64_t bytes=(uint64_t)pitch*rows*r->depth;vk_buffer readback=buffer_create(bytes);image_access(e,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT);
    VkBufferImageCopy copy={.imageSubresource={e->aspect,level,layer,1},.imageOffset={r->xoffset,r->yoffset,r->zoffset},.imageExtent={r->width,r->height,r->depth}};vkCmdCopyImageToBuffer(command,e->image,VK_IMAGE_LAYOUT_GENERAL,readback.buffer,1,&copy);
    VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);uint64_t ticket=mtl_sub_commit();mtl_wait(ticket,UINT64_MAX);
    // 真正 GPU 等待已结束后才持宿主写 guard，不能把 shared 锁带进 queue 等待。
    // Acquire the host-write guard only after the real GPU wait; never carry its shared lock into a queue wait.
    if(!row)row=pitch;if(!img)img=row*rows;int guarded=dm_guest_host_write_begin&&dm_guest_host_write_end;
    if(guarded)dm_guest_host_write_begin(dst,img*r->depth);else dm_guest_write_prepare(dst,img*r->depth);
    for(unsigned z=0;z<(unsigned)r->depth;z++)for(unsigned y=0;y<rows;y++)memcpy(dst+z*img+y*row,(uint8_t*)readback.mapped+(z*rows+y)*pitch,pitch);
    if(guarded)dm_guest_host_write_end();buffer_destroy(&readback);
}
