// 环中每个批次独占描述符池与 uniform arena；选择/释放前调用方必须完成该 slot 的 GPU fence。
// Each ring batch owns its descriptor pools and uniform arena; the caller must complete this slot's GPU fence before selecting/releasing it.
#ifndef DM_VK_BATCH_SLOTS
#define DM_VK_BATCH_SLOTS 3
#endif
#if DM_VK_BATCH_SLOTS < 1 || DM_VK_BATCH_SLOTS > 8
#error DM_VK_BATCH_SLOTS must be between 1 and 8
#endif
#ifndef DM_VK_ARENA_RETAIN_BYTES
#define DM_VK_ARENA_RETAIN_BYTES (128ull << 20)
#endif
typedef struct batch_resources {
    descriptor_chunk *descriptors,*descriptor_current;
    uniform_chunk *uniforms,*uniform_current;
} batch_resources;
static batch_resources batch_resource_slots[DM_VK_BATCH_SLOTS];
static unsigned batch_resource_selected=UINT32_MAX;
static void batch_resources_save(void) {
    if(batch_resource_selected==UINT32_MAX)return;
    batch_resources *r=&batch_resource_slots[batch_resource_selected];
    r->descriptors=descriptor_chunks;r->descriptor_current=descriptor_current;
    r->uniforms=uniform_chunks;r->uniform_current=uniform_current;
}
static void batch_uniform_release(uniform_chunk *chunk) {
    uint64_t capacity=chunk->buffer.capacity;
    if(capacity>immutable_buffer_bytes||capacity>uniform_arena_bytes)die("Vulkan arena ownership accounting");
    immutable_buffer_bytes-=capacity;uniform_arena_bytes-=capacity;buffer_destroy(&chunk->buffer);free(chunk);
}
static void batch_resources_trim_cold(void) {
    // 仅当前已完成 slot 的冷尾页可丢弃；不覆盖正在飞行的其它批次，也不为限额丢弃本批使用过的页。
    // Drop only cold pages in the completed current slot; never overwrite other in-flight batches or discard pages this batch used to meet a budget.
    uniform_chunk **link=&uniform_chunks;
    while(*link){uniform_chunk *chunk=*link;
        if(chunk!=uniform_chunks&&!chunk->used&&uniform_arena_bytes>DM_VK_ARENA_RETAIN_BYTES){*link=chunk->next;batch_uniform_release(chunk);}
        else link=&chunk->next;
    }
    uniform_current=uniform_chunks;
}
static void batch_resources_select(unsigned slot) {
    if(slot>=DM_VK_BATCH_SLOTS)die("Vulkan batch resource slot %u",slot);
    if(batch_resource_selected==UINT32_MAX){
        // 首次切环保留此前同步模式创建的资源；后续新 slot 则各自从空池开始。
        // Preserve resources created in synchronous mode on the first ring selection; other new slots start with empty pools.
        batch_resource_slots[slot]=(batch_resources){descriptor_chunks,descriptor_current,uniform_chunks,uniform_current};
    }else batch_resources_save();
    batch_resource_selected=slot;batch_resources *r=&batch_resource_slots[slot];
    descriptor_chunks=r->descriptors;descriptor_current=r->descriptor_current;
    uniform_chunks=r->uniforms;uniform_current=r->uniform_current;
    batch_resources_trim_cold();descriptor_reset();texel_views_select(slot);batch_resources_save();
    // descriptor_reset 同时重置 CPU driver、UBO version 与 payload 索引；GPU 不读取这些 CPU 工作区。
    // descriptor_reset also clears CPU driver, UBO-version and payload indexes; the GPU never reads these CPU workspaces.
}
static uint64_t batch_resources_uniform_bytes(unsigned slot) {
    if(slot>=DM_VK_BATCH_SLOTS)die("Vulkan batch resource slot %u",slot);batch_resources_save();uint64_t bytes=0;
    for(uniform_chunk *c=batch_resource_slots[slot].uniforms;c;c=c->next)bytes+=c->buffer.capacity;return bytes;
}
static void batch_resources_release(unsigned slot) {
    if(slot>=DM_VK_BATCH_SLOTS)die("Vulkan batch resource slot %u",slot);batch_resources_save();texel_views_reset(slot);batch_resources *r=&batch_resource_slots[slot];
    while(r->descriptors){descriptor_chunk *c=r->descriptors;r->descriptors=c->next;vkDestroyDescriptorPool(device,c->pool,NULL);free(c);}
    while(r->uniforms){uniform_chunk *c=r->uniforms;r->uniforms=c->next;batch_uniform_release(c);}
    r->descriptor_current=NULL;r->uniform_current=NULL;
    if(batch_resource_selected==slot){descriptor_chunks=NULL;descriptor_current=NULL;uniform_chunks=NULL;uniform_current=NULL;uniform_reset();binding_cache_reset();}
}
