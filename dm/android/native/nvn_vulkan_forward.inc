// FIFO 与当前批 GPU 转发：借用先前已编码且完整初始化的版本，保留客体发布和 backing 寿命。
// GPU forwarding across FIFO/current batches: borrow recorded, fully initialized versions while preserving guest publication and backing lifetime.
typedef enum forward_result {
    FORWARD_NONE,FORWARD_FULL,FORWARD_PARTIAL,FORWARD_UNRECORDED,FORWARD_ALIGNMENT,FORWARD_BOUNDS,FORWARD_HOST_EFFECT
} forward_result;
typedef struct forward_slice {
    VkBuffer buffer;
    VkDeviceSize offset,size;
    vk_buffer backing;
    post_op *producer;
    int host_written;
} forward_slice;
typedef struct forward_hold_entry { buffer_owner *owner; struct forward_hold_entry *next; } forward_hold_entry;
typedef struct forward_hold_chunk { forward_hold_entry entries[64]; unsigned used; struct forward_hold_chunk *next; } forward_hold_chunk;
static forward_hold_entry *forward_holds[1024];
static forward_hold_chunk *forward_hold_chunks,*forward_hold_current;
static uint64_t forward_hits,forward_partial_misses,forward_unrecorded_misses,forward_alignment_misses,forward_barriers,forward_references;
static void forward_reset(void) {
    // 这里只重置当前批的 retain 去重索引；producer readiness 在 post 内，跨 slot 保留直到发布完成。
    // Reset only current-batch retain deduplication; producer readiness lives in its post across slots until publication completes.
    memset(forward_holds,0,sizeof forward_holds);for(forward_hold_chunk *c=forward_hold_chunks;c;c=c->next)c->used=0;forward_hold_current=forward_hold_chunks;
}
static void forward_mark_gpu_recorded(post_op *post,VkPipelineStageFlags stage,VkAccessFlags access,int full_version) {
    // full_version 表示整个 size 已从旧版本初始化，未知 shader 部分写仍保留其余字节。
    // full_version means all size bytes were seeded from the previous version, preserving bytes outside unknown partial shader stores.
    if(!full_version)return;
    if(!post||post->kind!=0||post->source.buffer||!post->destination||!post->size||!post->output.buffer||post->size>post->output.size||!stage||!(access&(VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT)))die("Vulkan forward producer contract");
    if(post->gpu_stage)return;post->gpu_stage=stage;post->gpu_access=access;memset(post->gpu_synchronized,0,sizeof post->gpu_synchronized);
}
static int forward_overlap(uintptr_t address,uint64_t size,uintptr_t start,uint64_t bytes) {
    if(!size||!bytes)return 0;if(size>UINTPTR_MAX-address||bytes>UINTPTR_MAX-start)die("Vulkan forward range overflow");return address<start+bytes&&start<address+size;
}
static post_op *forward_latest_chain(post_op *copies,uintptr_t address,uint64_t size,const post_op *stop_exclusive,post_op *latest) {
    for(post_op *p=copies;p&&p!=stop_exclusive;p=p->copy_next)if(p->destination&&forward_overlap(address,size,(uintptr_t)p->destination,p->size))latest=p;return latest;
}
static int forward_host_effects(const async_batch *batch,uintptr_t address,uint64_t size,const post_op *stop_exclusive) {
    if(!batch)return 0;
    // 保留动态 *copy_tail 的首个未来输出边界；只捕获 marker 时的 order 会漏掉期间的 event。
    // Preserve the dynamic first-future-output boundary at *copy_tail; capturing an order at marker creation would miss intervening events.
    int result=pending_range_overlap(batch,DM_PENDING_HOST_EFFECT,address,size,stop_exclusive?stop_exclusive->order:DM_PENDING_ORDER_ALL);
#ifdef DM_VK_REFERENCE_GATES
    post_op *posts=batch==async_current?post_first:batch->posts;int reference=0;
    for(post_op *p=posts;p&&p!=stop_exclusive;p=p->next){
        pending_index_oracle_nodes++;
        if((p->kind==1&&forward_overlap(address,size,(uintptr_t)p->event,4))||(p->kind==2&&forward_overlap(address,size,(uintptr_t)p->destination,16))){reference=1;break;}
    }
    pending_index_oracle_queries++;if(reference!=result)die("Vulkan pending index/forward host-effect prefix mismatch");
#endif
    return result;
}
static forward_result forward_find(uintptr_t address,uint64_t size,uint64_t alignment,post_op *const *scope_marker,forward_slice *out) {
    if(!out)return FORWARD_BOUNDS;*out=(forward_slice){0};if(!address||!size||size>UINTPTR_MAX-address)return FORWARD_BOUNDS;
    // 捕获 bind 前的 post_copy_tail 地址，*marker 之后才是本次 NVN 命令的新输出，不要求当前有新节点。
    // Capture post_copy_tail's address before binding; *marker starts this NVN command's new outputs and may still be null.
    const post_op *stop_exclusive=scope_marker?*scope_marker:NULL;post_op *latest=NULL;
    for(async_batch *batch=async_first;batch;batch=batch->next){
        if(batch->host_sync&&forward_host_effects(batch,address,size,NULL))return FORWARD_HOST_EFFECT;
        latest=forward_latest_chain(batch->copies,address,size,NULL,latest);
    }
    if(pending_host_sync&&forward_host_effects(async_current,address,size,stop_exclusive))return FORWARD_HOST_EFFECT;
    latest=forward_latest_chain(post_copy_first,address,size,stop_exclusive,latest);if(!latest)return FORWARD_NONE;
    uintptr_t start=(uintptr_t)latest->destination;
    // 最新一次写只覆盖部分请求时不得退回更老的完整版本，否则会丢失这次部分覆盖。
    // Never fall back to an older full version when the latest write covers only part of the requested range.
    if(address<start||size>latest->size||address-start>latest->size-size){forward_partial_misses++;return FORWARD_PARTIAL;}
    const vk_buffer *buffer;
    if(latest->source.buffer)buffer=&latest->source;
    else{if(!latest->gpu_stage){forward_unrecorded_misses++;return FORWARD_UNRECORDED;}buffer=&latest->output;}
    uint64_t offset=address-start;if(!buffer->buffer||!buffer->owner||offset>buffer->size||size>buffer->size-offset)return FORWARD_BOUNDS;
    if(alignment&&offset%alignment){forward_alignment_misses++;return FORWARD_ALIGNMENT;}
    *out=(forward_slice){.buffer=buffer->buffer,.offset=offset,.size=size,.backing=*buffer,.producer=latest,.host_written=!!latest->source.buffer};forward_hits++;return FORWARD_FULL;
}
static void forward_read_barrier(const forward_slice *slice,VkPipelineStageFlags stage,VkAccessFlags access) {
    VkAccessFlags writes=VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_HOST_WRITE_BIT|VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    if(!slice||!slice->producer||!stage||!access||(access&writes))die("Vulkan forward read-only contract");
    // coherent host 输入在提交前写完，提交已提供 host-write 顺序，不另打断 pass。
    // Coherent host input is written before submission, whose host-write ordering needs no additional pass break.
    if(slice->host_written)return;post_op *producer=slice->producer;if(!producer->gpu_stage)die("Vulkan forward recorded version missing");
    int needed=0;for(unsigned bit=0;bit<32;bit++)if((stage&(1u<<bit))&&(access&~producer->gpu_synchronized[bit]))needed=1;if(!needed)return;
    // 整个 producer 范围同步后才能复用标记；只同步一个小 slice 无法覆盖后续不同 slice。
    // Synchronize the whole producer range before memoizing; a small-slice barrier cannot cover later slices.
    graphics_pass_end();VkBufferMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,.srcAccessMask=producer->gpu_access,.dstAccessMask=access,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.buffer=slice->buffer,.offset=0,.size=producer->size};
    vkCmdPipelineBarrier(command,producer->gpu_stage,stage,0,0,NULL,1,&barrier,0,NULL);forward_barriers++;
    for(unsigned bit=0;bit<32;bit++)if(stage&(1u<<bit))producer->gpu_synchronized[bit]|=access;
}
static void forward_hold(const forward_slice *slice) {
    if(!slice||!slice->backing.buffer||!slice->backing.owner)die("Vulkan forward backing missing");
    uint64_t key=(uintptr_t)slice->backing.owner>>4;key^=key>>33;key*=0xff51afd7ed558ccdull;key^=key>>33;unsigned bucket=key&1023;
    for(forward_hold_entry *entry=forward_holds[bucket];entry;entry=entry->next)if(entry->owner==slice->backing.owner)return;
    if(!forward_hold_current){forward_hold_chunks=calloc(1,sizeof *forward_hold_chunks);if(!forward_hold_chunks)die("Vulkan forward hold allocation");forward_hold_current=forward_hold_chunks;}
    while(forward_hold_current->used==64){if(!forward_hold_current->next){forward_hold_current->next=calloc(1,sizeof *forward_hold_current);if(!forward_hold_current->next)die("Vulkan forward hold allocation");}forward_hold_current=forward_hold_current->next;}
    forward_hold_entry *entry=&forward_hold_current->entries[forward_hold_current->used++];entry->owner=slice->backing.owner;entry->next=forward_holds[bucket];forward_holds[bucket]=entry;
    // post3 持独立 backing 引用；producer post 完成后可以发布/释放，但 buffer 仍活到 consumer fence。
    // post3 owns an independent backing reference; the producer post can publish/retire while the buffer stays alive until the consumer fence.
    post_new(3)->source=buffer_borrow(slice->backing);forward_references++;
}
