// 类型化缓冲视图按真正 backing/范围/格式在本批复用；GPU fence 完成后才清掉该 slot 的索引。
// Reuse typed-buffer views by actual backing/range/format within a batch; clear its slot index only after GPU-fence completion.
typedef struct texel_view_entry {
    VkDescriptorBufferInfo buffer;VkFormat format;VkBufferView view;struct texel_view_entry *next;
} texel_view_entry;
static texel_view_entry *texel_view_cache[DM_VK_BATCH_SLOTS][1024];
static unsigned texel_view_slot;
static uint64_t texel_view_hits,texel_view_creations,texel_cpu_read_bytes,texel_forward_bytes;
static void texel_views_reset(unsigned slot) {
    if(slot>=DM_VK_BATCH_SLOTS)die("Vulkan texel view slot bounds");
    for(unsigned i=0;i<1024;i++)while(texel_view_cache[slot][i]){texel_view_entry *entry=texel_view_cache[slot][i];texel_view_cache[slot][i]=entry->next;free(entry);}
}
static void texel_views_select(unsigned slot) {texel_views_reset(slot);texel_view_slot=slot;}
static uint64_t texel_buffer_alignment(const texture_descriptor *descriptor) {
    int bw,bh;unsigned bytes=nvn_fmt_bpb(descriptor->view.set_format?descriptor->view.format:descriptor->texture.d.format,&bw,&bh);
    if(!bytes||bw!=1||bh!=1)die("Vulkan texel buffer block format");
    uint64_t alignment=gpu_limits.minTexelBufferOffsetAlignment,a=alignment,b=bytes;
    while(b){uint64_t remainder=a%b;a=b;b=remainder;}return alignment/a*bytes;
}
static VkBufferView texel_view_get(VkDescriptorBufferInfo buffer,VkFormat format) {
    if(!buffer.buffer||!buffer.range||format==VK_FORMAT_UNDEFINED)die("Vulkan texel view contract");
    uint64_t key=(uint64_t)(uintptr_t)buffer.buffer^(buffer.offset*0x9e3779b97f4a7c15ull)^(buffer.range*0xff51afd7ed558ccdull)^format;key^=key>>33;unsigned bucket=key&1023;
    for(texel_view_entry *entry=texel_view_cache[texel_view_slot][bucket];entry;entry=entry->next)
        if(entry->buffer.buffer==buffer.buffer&&entry->buffer.offset==buffer.offset&&entry->buffer.range==buffer.range&&entry->format==format){texel_view_hits++;return entry->view;}
    // 缓冲与视图均持有到本批真实完成，批内不会把仍被引用的句柄归还缓存造成身份重用。
    // Both backing and view live through this batch's actual completion, preventing reuse of an in-use handle within this cache.
    texel_view_entry *entry=calloc(1,sizeof *entry);if(!entry)die("Vulkan texel view metadata");entry->buffer=buffer;entry->format=format;
    post_op *post=post_new(3);VkBufferViewCreateInfo info={.sType=VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,.buffer=buffer.buffer,.format=format,.offset=buffer.offset,.range=buffer.range};
    VK_CHECK(vkCreateBufferView(device,&info,NULL,&post->buffer_view));entry->view=post->buffer_view;entry->next=texel_view_cache[texel_view_slot][bucket];texel_view_cache[texel_view_slot][bucket]=entry;texel_view_creations++;return entry->view;
}
