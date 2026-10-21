// 持久图形对象及同附件通道复用；附件读取、复制和写回边界由执行层关闭通道。
// Persistent graphics objects and same-attachment passes; attachment reads, copies and writeback boundaries close the pass in the executor.
static uint64_t graphics_hash(const void *data,size_t size) {
    // 仅选择进程内桶；安全加载所有字节，调用方仍完整比较 key 来排除碰撞。
    // Select only process-local buckets; load every byte safely and retain callers' full key comparisons to reject collisions.
    const uint8_t *bytes=data;const uint64_t multiplier=0xc6a4a7935bd1e995ull;
    uint64_t hash=14695981039346656037ull^((uint64_t)size*multiplier);
    while(size>=8){uint64_t word;memcpy(&word,bytes,sizeof word);
        word*=multiplier;word^=word>>47;word*=multiplier;
        hash^=word;hash*=multiplier;bytes+=8;size-=8;
    }
    if(size){uint64_t tail=0;memcpy(&tail,bytes,size);hash^=tail;hash*=multiplier;}
    hash^=hash>>47;hash*=multiplier;hash^=hash>>47;
    return hash;
}
typedef struct graphics_layout_entry {
    shader_entry *vs,*fs;VkPipelineLayout layout;struct graphics_layout_entry *next;
} graphics_layout_entry;
static graphics_layout_entry *graphics_layouts[1024];
static uint64_t graphics_layout_creations,graphics_pass_creations,graphics_frame_creations;
static VkPipelineLayout graphics_layout_get(shader_entry *vs,shader_entry *fs) {
    uintptr_t key[2]={(uintptr_t)vs,(uintptr_t)fs};unsigned bucket=graphics_hash(key,sizeof key)&1023;
    pthread_mutex_lock(&graphics_object_lock);
    for(graphics_layout_entry *entry=graphics_layouts[bucket];entry;entry=entry->next)if(entry->vs==vs&&entry->fs==fs){VkPipelineLayout layout=entry->layout;pthread_mutex_unlock(&graphics_object_lock);return layout;}
    graphics_layout_entry *entry=calloc(1,sizeof *entry);if(!entry)die("Vulkan graphics layout cache allocation");entry->vs=vs;entry->fs=fs;
    VkDescriptorSetLayout layouts[2]={vs->layout,fs->layout};VkPipelineLayoutCreateInfo info={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=2,.pSetLayouts=layouts};
    VK_CHECK(vkCreatePipelineLayout(device,&info,NULL,&entry->layout));entry->next=graphics_layouts[bucket];graphics_layouts[bucket]=entry;graphics_layout_creations++;VkPipelineLayout layout=entry->layout;pthread_mutex_unlock(&graphics_object_lock);return layout;
}
typedef struct {
    uint32_t attachments,colors,depth,color_indices[8];VkFormat formats[9];uint32_t load[9],stencil_load[9];
} graphics_pass_key;
typedef struct graphics_pass_entry { graphics_pass_key key;VkRenderPass pass;struct graphics_pass_entry *next; } graphics_pass_entry;
static graphics_pass_entry *graphics_passes[256];
typedef struct {
    graphics_pass_entry *pass;VkImageView views[9];uint32_t width,height;
} graphics_frame_key;
typedef struct graphics_frame_entry {
    graphics_frame_key key;image_entry *images[9];VkFramebuffer frame;struct graphics_frame_entry *next;
} graphics_frame_entry;
static graphics_frame_entry *graphics_frames[1024],*graphics_active_frame;
static uint32_t graphics_active_query;
static graphics_pass_key graphics_pass_key_make(unsigned count,unsigned ncolors,const VkAttachmentDescription *attachments,const VkAttachmentReference *colors,VkAttachmentReference depth) {
    graphics_pass_key key={.attachments=count,.colors=ncolors,.depth=depth.attachment};
    for(unsigned i=0;i<count;i++){key.formats[i]=attachments[i].format;key.load[i]=attachments[i].loadOp;key.stencil_load[i]=attachments[i].stencilLoadOp;}
    for(unsigned i=0;i<ncolors;i++)key.color_indices[i]=colors[i].attachment;return key;
}
static graphics_pass_entry *graphics_pass_get(graphics_pass_key key,const VkAttachmentDescription *attachments,const VkAttachmentReference *colors,VkAttachmentReference depth,int fatal) {
    unsigned bucket=graphics_hash(&key,sizeof key)&255;
    pthread_mutex_lock(&graphics_object_lock);
    for(graphics_pass_entry *entry=graphics_passes[bucket];entry;entry=entry->next)if(!memcmp(&entry->key,&key,sizeof key)){pthread_mutex_unlock(&graphics_object_lock);return entry;}
    graphics_pass_entry *entry=calloc(1,sizeof *entry);if(!entry)die("Vulkan render pass cache allocation");entry->key=key;
    VkSubpassDescription sub={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,.colorAttachmentCount=key.colors,.pColorAttachments=colors,.pDepthStencilAttachment=depth.attachment==VK_ATTACHMENT_UNUSED?NULL:&depth};
    VkRenderPassCreateInfo info={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.attachmentCount=key.attachments,.pAttachments=attachments,.subpassCount=1,.pSubpasses=&sub};
    // 驱动可能因为格式或组合非法而拒绝这个通道。运行期（fatal=1）的通道来自真实贴图，拒绝就是真 bug，照旧大声退出；
    // 预热路径（fatal=0）的通道来自清单 —— 记录可能损坏、也可能来自别人的机器，拒绝只说明"这条记录不该预热"，
    // 丢弃它即可。与 graphics_pipeline_create 同一约定，不另造一套错误处理。
    // The driver may reject this pass for an illegal format or combination. At runtime (fatal=1) the pass comes from
    // real textures, so a rejection is a genuine bug and still exits loudly. On the warm path (fatal=0) it comes from
    // a manifest whose records may be damaged or written on another machine, and a rejection only means "this record
    // must not be warmed" -- drop it. Same convention as graphics_pipeline_create, not a second one.
    VkResult created=vkCreateRenderPass(device,&info,NULL,&entry->pass);
    if(created!=VK_SUCCESS){free(entry);pthread_mutex_unlock(&graphics_object_lock);if(!fatal)return NULL;die("Vulkan render pass create failed: %d",(int)created);}
    entry->next=graphics_passes[bucket];graphics_passes[bucket]=entry;graphics_pass_creations++;pthread_mutex_unlock(&graphics_object_lock);return entry;
}
static graphics_frame_entry *graphics_frame_get(graphics_pass_entry *pass,const VkImageView *views,image_entry *const *images,uint32_t width,uint32_t height) {
    graphics_frame_key key={.pass=pass,.width=width,.height=height};memcpy(key.views,views,pass->key.attachments*sizeof *views);unsigned bucket=graphics_hash(&key,sizeof key)&1023;
    for(graphics_frame_entry *entry=graphics_frames[bucket];entry;entry=entry->next)if(!memcmp(&entry->key,&key,sizeof key))return entry;
    graphics_frame_entry *entry=calloc(1,sizeof *entry);if(!entry)die("Vulkan framebuffer cache allocation");entry->key=key;memcpy(entry->images,images,pass->key.attachments*sizeof *images);
    VkFramebufferCreateInfo info={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=pass->pass,.attachmentCount=pass->key.attachments,.pAttachments=views,.width=width,.height=height,.layers=1};
    VK_CHECK(vkCreateFramebuffer(device,&info,NULL,&entry->frame));entry->next=graphics_frames[bucket];graphics_frames[bucket]=entry;graphics_frame_creations++;return entry;
}
static int graphics_image_is_attachment(const image_entry *image) {
    if(!graphics_active_frame)return 0;
    for(unsigned i=0;i<graphics_active_frame->key.pass->key.attachments;i++)if(graphics_active_frame->images[i]==image)return 1;
    return 0;
}
static int graphics_pass_matches(const graphics_pass_key *key,const VkImageView *views,uint32_t width,uint32_t height) {
    if(!graphics_active_frame)return 0;const graphics_frame_key *frame=&graphics_active_frame->key;const graphics_pass_key *active=&frame->pass->key;
    // LOAD 只在通道开始时发生；已活动的通道可保留首次 DONT_CARE 后的后续绘制。
    // LOAD applies only at pass begin; an active pass may retain draws following its first DONT_CARE load.
    return frame->width==width&&frame->height==height&&active->attachments==key->attachments&&active->colors==key->colors&&active->depth==key->depth
        &&!memcmp(active->color_indices,key->color_indices,sizeof key->color_indices)&&!memcmp(active->formats,key->formats,sizeof key->formats)
        &&!memcmp(frame->views,views,key->attachments*sizeof *views);
}
static void graphics_pass_end(void) {
    if(!graphics_active_frame)return;
    // 连续绘制共享遮挡查询；重置/报告边界先关闭通道，保证计数次序不变。
    // Consecutive draws share an occlusion query; reset/report boundaries close the pass first to preserve counter order.
    vkCmdEndQuery(command,draw_queries,graphics_active_query);post_new(5)->value=graphics_active_query;
    vkCmdEndRenderPass(command);graphics_active_frame=NULL;
}
static void graphics_pass_begin(graphics_frame_entry *frame) {
    if(graphics_active_frame==frame)return;graphics_pass_end();
    VkRenderPassBeginInfo info={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=frame->key.pass->pass,.framebuffer=frame->frame,.renderArea={{0,0},{frame->key.width,frame->key.height}}};
    if(used_queries>=1024)die("Vulkan occlusion batch capacity");graphics_active_query=used_queries++;
    vkCmdBeginRenderPass(command,&info,VK_SUBPASS_CONTENTS_INLINE);graphics_active_frame=frame;render_passes++;
    vkCmdBeginQuery(command,draw_queries,graphics_active_query,gpu_features.occlusionQueryPrecise?VK_QUERY_CONTROL_PRECISE_BIT:0);
}
static void graphics_cache_forget_image(image_entry *image) {
    // 执行层已等待该图像的 GPU 使用结束；先销毁 framebuffer，再销毁它引用的 views。
    // The executor has waited for GPU image use; destroy framebuffers before their referenced views.
    if(graphics_image_is_attachment(image))graphics_pass_end();
    for(unsigned bucket=0;bucket<1024;bucket++){
        graphics_frame_entry **link=&graphics_frames[bucket];
        while(*link){graphics_frame_entry *entry=*link;int contains=0;
            for(unsigned i=0;i<entry->key.pass->key.attachments;i++)contains|=entry->images[i]==image;
            if(contains){*link=entry->next;vkDestroyFramebuffer(device,entry->frame,NULL);free(entry);}else link=&entry->next;
        }
    }
}
static int graphics_shader_writes(const shader_entry *shader) {
    for(unsigned i=0;i<shader->count;i++){const shader_binding *binding=&shader->bindings[i];if((binding->kind==2&&(binding->flags&1))||(binding->kind==4&&(binding->flags&4)))return 1;}
    return 0;
}
