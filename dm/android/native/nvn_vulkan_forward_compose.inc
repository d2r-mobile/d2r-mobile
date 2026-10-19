// 多个真实 GPU/CPU 输出按 FIFO 覆盖一个新基线；客体内存仍只在最终完成时发布。
// Overlay genuine GPU/CPU outputs over a fresh baseline in FIFO order; guest memory still publishes only on final completion.
static uint64_t forward_compose_count,forward_compose_bytes,forward_compose_overlays;
static uint64_t forward_compose_report_count,forward_compose_report_bytes,forward_compose_report_overlays;
static int forward_compose_post(const post_op *post,uintptr_t address,uint64_t size,forward_slice *slice,VkBufferCopy *region) {
    uintptr_t start;uint64_t bytes;const vk_buffer *buffer;
    if(post->kind==1){
        if(forward_overlap(address,size,(uintptr_t)post->event,4))return -1;
        return 0;
    }
    if(post->kind!=0&&post->kind!=2)return 0;
    start=(uintptr_t)post->destination;bytes=post->kind==2?16:post->size;
    if(!start||!bytes||!forward_overlap(address,size,start,bytes))return 0;
    if(post->kind==2){if(!post->counter_gpu_encoded)return -1;buffer=&post->output;}
    else if(post->source.buffer)buffer=&post->source;
    else{if(!post->gpu_stage)return -1;buffer=&post->output;}
    if(!buffer->buffer||!buffer->owner||buffer->size<bytes)return -1;
    uintptr_t low=address>start?address:start,high=address+size<start+bytes?address+size:start+bytes;
    uint64_t source=low-start,destination=low-address,count=high-low;
    if((source|destination|count)&3)return -1;
    if(slice)*slice=(forward_slice){.buffer=buffer->buffer,.offset=source,.size=count,.backing=*buffer,.producer=(post_op*)post,.host_written=post->kind==0&&!!post->source.buffer};
    if(region)*region=(VkBufferCopy){.srcOffset=source,.dstOffset=destination,.size=count};
    return 1;
}
static int forward_compose_chain_possible(const post_op *posts,const post_op *stop,uintptr_t address,uint64_t size,int *overlays) {
    for(const post_op *p=posts;p&&p!=stop;p=p->next){int result=forward_compose_post(p,address,size,NULL,NULL);
        if(result<0)return 0;if(result)*overlays=1;}
    return 1;
}
static int forward_compose_possible(uintptr_t address,uint64_t size,post_op *const *scope_marker) {
    if(!address||!size||size>(1ull<<32)||size>UINTPTR_MAX-address||(size&3))return 0;
    int overlays=0;
    for(const async_batch *batch=async_first;batch;batch=batch->next)
        if(!forward_compose_chain_possible(batch->posts,NULL,address,size,&overlays))return 0;
    const post_op *stop=scope_marker?*scope_marker:NULL;
    return forward_compose_chain_possible(post_first,stop,address,size,&overlays)&&overlays;
}
static void forward_compose_chain(post_op *posts,const post_op *stop,uintptr_t address,uint64_t size,vk_buffer output) {
    for(post_op *p=posts;p&&p!=stop;p=p->next){forward_slice slice;VkBufferCopy region;int result=forward_compose_post(p,address,size,&slice,&region);
        if(result<0)die("Vulkan compose changed after dependency preflight");if(!result)continue;
        forward_read_barrier(&slice,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT);forward_hold(&slice);
        // 各覆盖可能重叠；即使源不同也必须保持目标写入的原命令次序。
        // Overlays can overlap; preserve destination write order even when their sources differ.
        VkBufferMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,
            .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.buffer=output.buffer,.size=size};
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,1,&barrier,0,NULL);
        vkCmdCopyBuffer(command,slice.buffer,output.buffer,1,&region);forward_compose_overlays++;
    }
}
static void forward_compose_into(vk_buffer output,uintptr_t address,uint64_t bytes,VkPipelineStageFlags stage,VkAccessFlags access,post_op *const *scope_marker) {
    if(!forward_compose_possible(address,bytes,scope_marker)||!output.owner||atomic_load(&output.owner->references)!=1||output.size<bytes)die("Vulkan compose without owned destination/complete plan");
    // 不使用 snapshot_read：它会先套用 CPU copy，随后旧 GPU 覆盖会打乱 FIFO。
    // Do not use snapshot_read: its early CPU-copy overlays would be overwritten out of FIFO order by older GPU writes.
    if(dm_android_read_memory(address,output.mapped,bytes))die("Vulkan compose baseline unreadable");snapshot_read_account(bytes);
    graphics_pass_end();const post_op *stop=scope_marker?*scope_marker:NULL;
    for(async_batch *batch=async_first;batch;batch=batch->next)forward_compose_chain(batch->posts,NULL,address,bytes,output);
    forward_compose_chain(post_first,stop,address,bytes,output);
    VkBufferMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=access,
        .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.buffer=output.buffer,.size=bytes};
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,stage,0,0,NULL,1,&barrier,0,NULL);
    forward_compose_count++;forward_compose_bytes+=bytes;
}
static VkDescriptorBufferInfo forward_compose_buffer(uintptr_t address,uint64_t bytes,VkPipelineStageFlags stage,VkAccessFlags access,post_op *const *scope_marker) {
    post_op *held=post_new(3);held->source=buffer_create(bytes);
    forward_compose_into(held->source,address,bytes,stage,access,scope_marker);
    return (VkDescriptorBufferInfo){held->source.buffer,0,bytes};
}
