// 完成后的批次复用映射 uniform 页；未完成的绘制始终保留各自的数据版本。
// Reuse mapped uniform pages only after a batch completes; pending draws keep their own data versions.
typedef struct uniform_chunk {
    vk_buffer buffer;
    uint32_t used,dirty[16];
    struct uniform_chunk *next;
} uniform_chunk;
typedef struct uniform_driver_cache {
    VkDescriptorBufferInfo info;
    uint8_t data[0x968];
    uint32_t size,state_valid;
} uniform_driver_cache;
static uniform_chunk *uniform_chunks,*uniform_current;
static uint64_t uniform_stride,uniform_arena_bytes,uniform_driver_hits;
static uniform_driver_cache uniform_drivers[6][2];
static uint64_t uniform_driver_fast_hits,uniform_driver_builds,uniform_driver_avoided_bytes,uniform_clear_avoided_bytes;
static int uniform_driver_current(unsigned stage,unsigned kind,VkDescriptorBufferInfo *info) {
    uniform_driver_cache *cache=&uniform_drivers[stage][kind];
    if(!cache->state_valid)return 0;
    *info=cache->info;uniform_driver_hits++;uniform_driver_fast_hits++;uniform_driver_avoided_bytes+=cache->size;return 1;
}
static void uniform_reset(void) {
    // descriptor_reset 的调用点已等待该批次的 fence，才允许覆盖这些内存。
    // descriptor_reset is called after this batch's fence, before any of its memory is overwritten.
    for(uniform_chunk *c=uniform_chunks;c;c=c->next)c->used=0;
    uniform_current=uniform_chunks;
    for(unsigned stage=0;stage<6;stage++)for(unsigned kind=0;kind<2;kind++){uniform_drivers[stage][kind].size=0;uniform_drivers[stage][kind].state_valid=0;}
}
static uniform_chunk *uniform_chunk_new(void) {
    if(!uniform_stride){VkPhysicalDeviceProperties properties;vkGetPhysicalDeviceProperties(physical,&properties);
        uint64_t alignment=properties.limits.minUniformBufferOffsetAlignment;
        if(!alignment)alignment=1;uniform_stride=(65536+alignment-1)/alignment*alignment;
        if(properties.limits.maxUniformBufferRange<65536)die("Vulkan uniform range below 64 KiB");}
    uniform_chunk *c=calloc(1,sizeof *c);if(!c)die("Vulkan uniform arena allocation");
    c->buffer=buffer_create(uniform_stride*16);memset(c->buffer.mapped,0,uniform_stride*16);
    uniform_arena_bytes+=c->buffer.capacity;immutable_buffer_bytes+=c->buffer.capacity;return c;
}
// 调用方必须立即完整覆盖 dirty 字节；仅旧数据尾部需要清零。
// The caller must immediately overwrite all dirty bytes; only the old tail needs clearing.
static VkDescriptorBufferInfo uniform_allocate_mode(uint32_t dirty,uint8_t **data,int overwrite) {
    if(dirty>65536)die("Vulkan uniform write range");
    if(!uniform_current){uniform_chunks=uniform_chunk_new();uniform_current=uniform_chunks;}
    while(uniform_current->used==16){if(!uniform_current->next)uniform_current->next=uniform_chunk_new();uniform_current=uniform_current->next;}
    unsigned slot=uniform_current->used++;uint64_t offset=slot*uniform_stride;
    *data=(uint8_t*)uniform_current->buffer.mapped+offset;
    // 页最初全零；新前缀由调用方完整覆盖，仅清旧前缀多出的尾部，保留全部 64 KiB 零填充语义。
    // Pages start zero; callers replace the new prefix, so clear only the excess old tail and preserve all 64 KiB zero-padding semantics.
    uint32_t old=uniform_current->dirty[slot];
    uint32_t clear_start=overwrite?dirty:0;
    if(overwrite)uniform_clear_avoided_bytes+=old<dirty?old:dirty;
    if(old>clear_start)memset(*data+clear_start,0,old-clear_start);
    uniform_current->dirty[slot]=dirty;
    return (VkDescriptorBufferInfo){uniform_current->buffer.buffer,offset,65536};
}
// 原接口保持全零内容；明确完整覆盖的调用方才可省去前缀清零。
// Preserve the original zero-filled allocation contract; only explicit full-overwrite callers skip prefix clearing.
static VkDescriptorBufferInfo uniform_allocate(uint32_t dirty,uint8_t **data) {
    return uniform_allocate_mode(dirty,data,0);
}
static VkDescriptorBufferInfo uniform_allocate_overwrite(uint32_t dirty,uint8_t **data) {
    return uniform_allocate_mode(dirty,data,1);
}
static VkDescriptorBufferInfo uniform_driver(unsigned stage,unsigned kind,const uint8_t *data,uint32_t size) {
    if(stage>=6||kind>=2||!size||size>sizeof uniform_drivers[0][0].data)die("Vulkan uniform driver range");
    uniform_driver_cache *cache=&uniform_drivers[stage][kind];
    uniform_driver_builds++;cache->state_valid=1;
    if(cache->size==size&&!memcmp(cache->data,data,size)){uniform_driver_hits++;return cache->info;}
    uint8_t *mapped;cache->info=uniform_allocate_overwrite(size,&mapped);memcpy(mapped,data,size);
    memcpy(cache->data,data,size);cache->size=size;return cache->info;
}
