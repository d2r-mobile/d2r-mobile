// 仅 GPU 消费的快照谱系；已编码消费者按同队列屏障原位更新，待编码消费者保留旧版本。
// GPU-consumer snapshot lineage; same-queue barriers permit patches after encoded consumers, while pending consumers retain old versions.
typedef struct gpu_snapshot_counters {
    uint64_t calls,hits,full_calls,full_guest_bytes,delta_calls,patch_guest_bytes,clone_bytes,patch_runs;
    uint64_t inplace_calls,cow_calls,inplace_patch_bytes,cow_patch_bytes;
    uint64_t raw_calls,raw_bytes,api,host_busy,pending,plan,epoch,validate,protection,budget,align;
} gpu_snapshot_counters;
static gpu_snapshot_counters gpu_snapshot_stats;
static uint64_t gpu_snapshot_encoded;
// 只在外层真实 draw/dispatch 编码后推进；内部辅助 dispatch 不能清除待编码绑定。
// Advance only after an outer real draw/dispatch is recorded; internal helper dispatches cannot clear pending bindings.
static void gpu_snapshot_consumer_encoded(void) {if(++gpu_snapshot_encoded==UINT64_MAX)die("Vulkan GPU consumer serial overflow");}
static int gpu_snapshot_api(void) {
    return dm_guest_snapshot_try_begin&&dm_guest_snapshot_end&&dm_guest_host_write_begin&&dm_guest_host_write_end&&
        nvn_dirty_snapshot_begin&&nvn_dirty_snapshot_end&&nvn_dirty_snapshot_abort;
}
static void gpu_snapshot_report(void) {
    static uint64_t frame;static gpu_snapshot_counters previous;
    if(!presents||presents%60||presents==frame)return;
    int saved_errno=errno;uint64_t span=presents-frame;const gpu_snapshot_counters *s=&gpu_snapshot_stats;
    LOG("[vulkan-gpu-snapshot] frame=%llu span=%llu api=%u calls=%.2f/frame hits=%.2f/frame trusted_full=%.2f/frame full_guest=%.4f MiB/frame delta=%.2f/frame patch_guest=%.4f MiB/frame GPU_clone=%.3f MiB/frame inplace=%.2f/frame COW_clone=%.2f/frame inplace_patch=%.4f MiB/frame COW_patch=%.4f MiB/frame patch_runs=%.2f/frame raw=%.2f/frame raw_bytes=%.3f MiB/frame fallback_api/hostbusy/pending/plan/epoch/revalidate/protection/budget/align=%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu (guest reads and GPU clone are distinct)",
        (unsigned long long)presents,(unsigned long long)span,(unsigned)gpu_snapshot_api(),(s->calls-previous.calls)/(double)span,(s->hits-previous.hits)/(double)span,
        (s->full_calls-previous.full_calls)/(double)span,(s->full_guest_bytes-previous.full_guest_bytes)/(span*1048576.),(s->delta_calls-previous.delta_calls)/(double)span,
        (s->patch_guest_bytes-previous.patch_guest_bytes)/(span*1048576.),(s->clone_bytes-previous.clone_bytes)/(span*1048576.),
        (s->inplace_calls-previous.inplace_calls)/(double)span,(s->cow_calls-previous.cow_calls)/(double)span,
        (s->inplace_patch_bytes-previous.inplace_patch_bytes)/(span*1048576.),(s->cow_patch_bytes-previous.cow_patch_bytes)/(span*1048576.),(s->patch_runs-previous.patch_runs)/(double)span,
        (s->raw_calls-previous.raw_calls)/(double)span,(s->raw_bytes-previous.raw_bytes)/(span*1048576.),
        (unsigned long long)(s->api-previous.api),(unsigned long long)(s->host_busy-previous.host_busy),(unsigned long long)(s->pending-previous.pending),(unsigned long long)(s->plan-previous.plan),
        (unsigned long long)(s->epoch-previous.epoch),(unsigned long long)(s->validate-previous.validate),(unsigned long long)(s->protection-previous.protection),(unsigned long long)(s->budget-previous.budget),(unsigned long long)(s->align-previous.align));
    frame=presents;previous=*s;errno=saved_errno;
}
static void gpu_snapshot_account(snapshot_diag_row *row,const owned_snapshot_counters *before) {
    if(!row)return;
    row->heap_calls+=owned_snapshot_stats.heap_calls-before->heap_calls;row->heap_bytes+=owned_snapshot_stats.heap_bytes-before->heap_bytes;
    row->heap_wall_ns+=owned_snapshot_stats.heap_valid_wall_ns-before->heap_valid_wall_ns;row->heap_unavailable+=owned_snapshot_stats.heap_clock_unavailable-before->heap_clock_unavailable;
    row->known_calls+=owned_snapshot_stats.known_calls-before->known_calls;row->safe_calls+=owned_snapshot_stats.safe_calls-before->safe_calls;row->safe_bytes+=owned_snapshot_stats.safe_requested_bytes-before->safe_requested_bytes;
}
static void gpu_snapshot_hold(vk_buffer buffer) {forward_slice held={.backing=buffer};forward_hold(&held);}
static void gpu_snapshot_acquire(read_snapshot *entry) {entry->last_acquired=gpu_snapshot_encoded+1;gpu_snapshot_hold(entry->buffer);}
static vk_buffer gpu_snapshot_buffer(uintptr_t address,uint64_t size) {
    gpu_snapshot_stats.calls++;
    unsigned bucket=read_bucket(address,size)^4096;read_snapshot *old=NULL;
    for(read_snapshot *entry=read_index[bucket];entry;entry=entry->hash_next)if(entry->gpu_only&&entry->address==address&&entry->buffer.size==size){old=entry;break;}
    snapshot_diag_row *row=snapshot_diag_current;unsigned reason=SNAP_DIAG_NO_ENTRY;
    vk_buffer output={0},staging[NVN_DIRTY_SNAPSHOT_MAX_RUNS]={{0}};unsigned staged=0;
    nvn_dirty_snapshot_plan plan={0};int host_held=0,transaction=0,full_read=0,inplace=0;
    uint64_t patch_bytes=0;owned_snapshot_counters before=owned_snapshot_stats;
    if(size<=65536||size>(64u<<20)||size>UINTPTR_MAX-address){gpu_snapshot_stats.budget++;reason=SNAP_DIAG_LIMIT;goto raw;}
    if(!mtl_addr(address,size,NULL,NULL)){gpu_snapshot_stats.plan++;reason=SNAP_DIAG_POOL;goto raw;}
    if(!gpu_snapshot_api()){gpu_snapshot_stats.api++;goto raw;}
    // 连当前命令的未编码输出也保守拒绝；不得让 clone 覆盖原有 FIFO overlay。
    // Conservatively reject even this command's unrecorded outputs; never overwrite original FIFO overlays with a clone.
    if(snapshot_pending(address,size)||async_unknown(address,size)){gpu_snapshot_stats.pending++;reason=SNAP_DIAG_PENDING;goto raw;}
    if(!dm_guest_snapshot_try_begin()){gpu_snapshot_stats.host_busy++;goto raw;}host_held=1;
    if(!nvn_dirty_snapshot_begin((void*)address,(size_t)size,old?old->stamp:0,old?old->incarnation:0,&plan)){
        gpu_snapshot_stats.plan++;
        if(plan.reason==NVN_DIRTY_SNAPSHOT_EPOCH)gpu_snapshot_stats.epoch++;
        if(plan.reason==NVN_DIRTY_SNAPSHOT_PROTECTION||plan.reason==NVN_DIRTY_SNAPSHOT_DISABLED)gpu_snapshot_stats.protection++;
        reason=plan.reason==NVN_DIRTY_SNAPSHOT_UNTRACKED?SNAP_DIAG_UNTRACKED:SNAP_DIAG_DIRTY;goto raw;
    }transaction=1;
    if(old&&row){row->old_entries++;row->old_stamp=old->stamp;}
    if(!plan.full&&!plan.run_count){
        transaction=0;int valid=nvn_dirty_snapshot_end(&plan);dm_guest_snapshot_end();host_held=0;
        if(!valid){gpu_snapshot_stats.validate++;goto raw;}
        old->stamp=plan.stamp;old->used=++read_cache_clock;read_lru_front(old);read_cache_hits++;gpu_snapshot_stats.hits++;
        if(row){row->reasons[SNAP_DIAG_HIT]++;row->new_stamp=plan.stamp;}gpu_snapshot_acquire(old);return old->buffer;
    }
    row=snapshot_diag_miss_row(address,size);reason=old?SNAP_DIAG_DIRTY:SNAP_DIAG_NO_ENTRY;
    if(row&&old&&!row->old_entries){row->old_entries++;row->old_stamp=old->stamp;}
    for(unsigned i=0;i<plan.run_count;i++){
        const nvn_dirty_snapshot_run *run=&plan.runs[i];
        if(!run->size||run->offset>size||run->size>size-run->offset){gpu_snapshot_stats.plan++;goto raw;}
        patch_bytes+=run->size;
    }
    full_read=plan.full;
    // 稠密候选不值得 GPU clone 加 patch；仍在同一认证事务中建立完整新基线。
    // Dense candidates do not justify a GPU clone plus patches; form a full new baseline inside the same certified transaction.
    if(!full_read&&patch_bytes>=size/2){full_read=1;gpu_snapshot_stats.budget++;}
    if(!full_read){
        if(!old||(size&3)){gpu_snapshot_stats.align++;goto raw;}
        for(unsigned i=0;i<plan.run_count;i++)if((plan.runs[i].offset|plan.runs[i].size)&3){gpu_snapshot_stats.align++;goto raw;}
    }
    inplace=!full_read&&old->last_acquired<=gpu_snapshot_encoded;
    if(!inplace)output=buffer_create(size);
    if(full_read){
        if(snapshot_read_owned(address,&output,size))die("Vulkan trusted full snapshot read");gpu_snapshot_stats.full_guest_bytes+=size;
    }else for(unsigned i=0;i<plan.run_count;i++){
        staging[i]=buffer_create(plan.runs[i].size);staged++;
        if(snapshot_read_owned(address+plan.runs[i].offset,&staging[i],plan.runs[i].size))die("Vulkan delta staging read");
        gpu_snapshot_stats.patch_guest_bytes+=plan.runs[i].size;
    }
    transaction=0;int valid=nvn_dirty_snapshot_end(&plan);dm_guest_snapshot_end();host_held=0;
    if(!valid){gpu_snapshot_stats.validate++;goto raw;}
    // 读取/重验全成功后才编码；CPU staging 不会先 overlay 再被整个 GPU clone 盖掉。
    // Record only after all reads/revalidation succeed; no CPU staging overlay can be overwritten by the whole GPU clone.
    if(!full_read){
        graphics_pass_end();gpu_snapshot_hold(old->buffer);
        VkBufferMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.buffer=old->buffer.buffer,.size=size};
        if(inplace){
            // 直到认证成功才借用旧目标；失败 raw 只能 CPU 写全新独占 buffer。
            // Borrow the old target only after certification; failed raw reads can CPU-write only a fresh exclusive buffer.
            output=old->buffer;barrier.srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,1,&barrier,0,NULL);
        }else{
            barrier.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,1,&barrier,0,NULL);
            VkBufferCopy clone={.size=size};vkCmdCopyBuffer(command,old->buffer.buffer,output.buffer,1,&clone);
            barrier.buffer=output.buffer;barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,1,&barrier,0,NULL);
            gpu_snapshot_stats.cow_calls++;gpu_snapshot_stats.cow_patch_bytes+=patch_bytes;gpu_snapshot_stats.clone_bytes+=size;
        }
        for(unsigned i=0;i<plan.run_count;i++){
            VkBufferCopy patch={.dstOffset=plan.runs[i].offset,.size=plan.runs[i].size};vkCmdCopyBuffer(command,staging[i].buffer,output.buffer,1,&patch);post_new(3)->source=staging[i];memset(&staging[i],0,sizeof staging[i]);
        }staged=0;
        barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,NULL,1,&barrier,0,NULL);
        gpu_snapshot_stats.delta_calls++;gpu_snapshot_stats.patch_runs+=plan.run_count;
        if(row){row->gpu_delta_calls++;row->gpu_delta_bytes+=patch_bytes;if(!inplace)row->gpu_clone_bytes+=size;}
        if(inplace){
            gpu_snapshot_stats.inplace_calls++;gpu_snapshot_stats.inplace_patch_bytes+=patch_bytes;
            old->stamp=plan.stamp;old->incarnation=plan.incarnation;old->used=++read_cache_clock;read_lru_front(old);read_cache_misses++;
            if(row){row->reasons[reason]++;row->new_stamp=plan.stamp;}gpu_snapshot_account(row,&before);gpu_snapshot_acquire(old);return old->buffer;
        }
    }else{gpu_snapshot_stats.full_calls++;if(row)row->gpu_trusted_full++;}
    if(old){snapshot_diag_retired(old,reason);read_cache_retire(old);}
    while(read_cache_bytes+output.capacity>(256u<<20)){snapshot_diag_retired(read_snapshot_tail,SNAP_DIAG_LRU);read_cache_retire(read_snapshot_tail);}
    read_snapshot *entry=calloc(1,sizeof *entry);if(!entry)die("Vulkan GPU snapshot cache allocation");
    entry->address=address;entry->stamp=plan.stamp;entry->incarnation=plan.incarnation;entry->gpu_only=1;entry->used=++read_cache_clock;entry->buffer=output;
    entry->hash_next=read_index[bucket];read_index[bucket]=entry;read_lru_front(entry);read_cache_bytes+=output.capacity;read_cache_misses++;
    if(row){row->reasons[reason]++;row->new_stamp=plan.stamp;}gpu_snapshot_account(row,&before);gpu_snapshot_acquire(entry);return output;
raw:
    if(transaction)nvn_dirty_snapshot_abort(&plan);
    if(host_held)dm_guest_snapshot_end();
    for(unsigned i=0;i<staged;i++)buffer_destroy(&staging[i]);
    if(old){snapshot_diag_retired(old,reason);read_cache_retire(old);}
    // 此出口绝不调用旧 snapshot_buffer/protect；不认证缓存，保留原安全读取与顺序 overlay。
    // Never call old snapshot_buffer/protect here; certify no cache and retain original safe reading plus ordered overlays.
    if(!output.buffer)output=buffer_create(size);
    row=snapshot_diag_miss_row(address,size);if(row)row->reasons[reason]++;
    if(snapshot_read_owned(address,&output,size))die("Vulkan raw GPU snapshot read");
    gpu_snapshot_stats.raw_calls++;gpu_snapshot_stats.raw_bytes+=size;read_cache_misses++;gpu_snapshot_account(row,&before);post_new(3)->source=output;return output;
}
