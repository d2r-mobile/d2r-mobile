// Vulkan 资源分配之前解决客体内存读取依赖；不在绑定途中提交或提前公布 GPU 结果。
// Resolve guest-memory read dependencies before allocating Vulkan resources; never submit during binding or publish GPU results early.
static uint64_t dependency_flushes;
enum {
    DEP_NONE,DEP_UBO,DEP_SSBO,DEP_COMPUTE_UBO_POINTER,DEP_TEXEL,DEP_SHADER_TEXTURE,
    DEP_INDEX,DEP_VERTEX,DEP_COPY_BUFFER_SOURCE,DEP_TEXTURE,DEP_COPY_TEXTURE_SOURCE,DEP_COPY_TEXTURE_DESTINATION,DEP_COUNT
};
typedef struct dependency_timing {uint64_t calls,commit_ns,wait_ns,begin_ns,clock_invalid;} dependency_timing;
static dependency_timing dependency_times[DEP_COUNT];
typedef struct dependency_detail {
    int valid,forward_result,producer_source,host_kind;
    char shader[17];unsigned stage,binding,flags,producer_stage,host_value;
    uintptr_t address,producer_address,host_address;
    uint64_t bytes,alignment,producer_bytes,producer_backing_bytes,producer_order,host_order;
} dependency_detail;
static dependency_detail dependency_details[DEP_COUNT];
static uint64_t dependencies_clock(void) {
    int saved_errno=errno;uint64_t now=dm_android_clock_ns(CLOCK_MONOTONIC);errno=saved_errno;return now;
}
static void dependencies_report(void) {
    static uint64_t frame;static dependency_timing previous[DEP_COUNT];
    static const char *const names[DEP_COUNT]={"none","ubo","ssbo","compute_ubo_pointer","texel","shader_texture","index","vertex","copy_buffer_source","texture","copy_texture_source","copy_texture_destination"};
    if(!presents||presents%60||presents==frame)return;uint64_t span=presents-frame;dependency_timing total={0};
    // wait 是完整客体完成等待，包含 GPU、发布 worker 与锁调度；不是 GPU 忙时。
    // Wait is guest completion latency including GPU, publication worker and lock scheduling, not GPU busy time.
    for(unsigned i=1;i<DEP_COUNT;i++){
        dependency_timing *s=&dependency_times[i],*p=&previous[i];uint64_t calls=s->calls-p->calls;
        total.calls+=calls;total.commit_ns+=s->commit_ns-p->commit_ns;total.wait_ns+=s->wait_ns-p->wait_ns;
        total.begin_ns+=s->begin_ns-p->begin_ns;total.clock_invalid+=s->clock_invalid-p->clock_invalid;
        if(calls)LOG("[vulkan-dependency-time] frame=%llu span=%llu reason=%s calls=%.3f/frame commit=%.4f ms/frame wait=%.4f ms/frame begin=%.4f ms/frame clock_invalid=%llu wait_scope=guest_completion",
            (unsigned long long)presents,(unsigned long long)span,names[i],calls/(double)span,
            (s->commit_ns-p->commit_ns)/(span*1e6),(s->wait_ns-p->wait_ns)/(span*1e6),(s->begin_ns-p->begin_ns)/(span*1e6),
            (unsigned long long)(s->clock_invalid-p->clock_invalid));
        dependency_detail *d=&dependency_details[i];
        if(d->valid){
            static const char *const results[]={"none","full","partial","unrecorded","alignment","bounds","host_effect"};
            const char *result=d->forward_result<0?"cpu_odd_size":d->forward_result<=FORWARD_HOST_EFFECT?results[d->forward_result]:"invalid";
            LOG("[vulkan-dependency-detail] frame=%llu reason=%s shader=%s stage=%u binding=%u flags=%#x addr=%#llx bytes=%llu align=%llu forward=%s producer_addr=%#llx producer_bytes=%llu producer_backing_bytes=%llu producer_stage=%#x producer_source=%d producer_order=%llu host_kind=%d host_value=%#x host_addr=%#llx host_order=%llu sample=first_in_window",
                (unsigned long long)presents,names[i],d->shader,d->stage,d->binding,d->flags,(unsigned long long)d->address,
                (unsigned long long)d->bytes,(unsigned long long)d->alignment,result,(unsigned long long)d->producer_address,
                (unsigned long long)d->producer_bytes,(unsigned long long)d->producer_backing_bytes,d->producer_stage,d->producer_source,
                (unsigned long long)d->producer_order,d->host_kind,d->host_value,(unsigned long long)d->host_address,(unsigned long long)d->host_order);
            d->valid=0;
        }
        *p=*s;
    }
    LOG("[vulkan-dependency-time] frame=%llu span=%llu reason=total calls=%.3f/frame commit=%.4f ms/frame wait=%.4f ms/frame begin=%.4f ms/frame clock_invalid=%llu wait_scope=guest_completion",
        (unsigned long long)presents,(unsigned long long)span,total.calls/(double)span,total.commit_ns/(span*1e6),
        total.wait_ns/(span*1e6),total.begin_ns/(span*1e6),(unsigned long long)total.clock_invalid);
    frame=presents;
}

static int dependencies_overlap(uintptr_t a,uint64_t an,uintptr_t b,uint64_t bn) {
    if(!an||!bn)return 0;
    if(an>UINTPTR_MAX-a||bn>UINTPTR_MAX-b)die("Vulkan dependency range overflow");
    return a<b+bn&&b<a+an;
}

// 每类每窗口只读一次已有 post 元数据；不重新调用 forward_find 或改依赖计数。
// Read existing post metadata once per class/window; never call forward_find again or change dependency counters.
static void dependencies_detail_posts(dependency_detail *d,const post_op *posts) {
    for(const post_op *p=posts;p;p=p->next){
        if(p->kind==0&&p->destination&&dependencies_overlap(d->address,d->bytes,(uintptr_t)p->destination,p->size)){
            d->producer_address=(uintptr_t)p->destination;d->producer_bytes=p->size;d->producer_stage=p->gpu_stage;
            d->producer_source=!!p->source.buffer;d->producer_backing_bytes=p->source.buffer?p->source.size:p->output.size;d->producer_order=p->order;
        }else if(!d->host_kind&&(p->kind==1||p->kind==2)){
            uintptr_t address=p->kind==1?(uintptr_t)p->event:(uintptr_t)p->destination;uint64_t bytes=p->kind==1?4:16;
            if(dependencies_overlap(d->address,d->bytes,address,bytes)){d->host_kind=p->kind;d->host_value=p->value;d->host_address=address;d->host_order=p->order;}
        }
    }
}
static void dependencies_detail_capture(unsigned reason,const shader_entry *shader,const shader_binding *binding,uintptr_t address,uint64_t bytes,uint64_t alignment,int result) {
    dependency_detail *d=&dependency_details[reason];if(d->valid)return;
    *d=(dependency_detail){.valid=1,.forward_result=result,.stage=shader->stage,.binding=binding->binding,.flags=binding->flags,.address=address,.bytes=bytes,.alignment=alignment};
    memcpy(d->shader,shader->key,16);d->shader[16]=0;
    for(const async_batch *b=async_first;b;b=b->next)dependencies_detail_posts(d,b->posts);
    dependencies_detail_posts(d,post_first);
}

static int dependencies_have_unknown(void) {return async_have_unknown();}
static int dependencies_unknown(uintptr_t address,uint64_t size) {return async_unknown(address,size);}

static void dependencies_flush(unsigned reason) {
    if(!reason||reason>=DEP_COUNT)die("Vulkan dependency timing reason %u",reason);
    dependency_flushes++;
    uint64_t started=dependencies_clock(),ticket=mtl_sub_commit(),committed=dependencies_clock();
    mtl_wait(ticket,UINT64_MAX);uint64_t waited=dependencies_clock();mtl_sub_begin();uint64_t begun=dependencies_clock();
    dependency_timing *s=&dependency_times[reason];s->calls++;
    if(started&&committed>=started&&waited>=committed&&begun>=waited){s->commit_ns+=committed-started;s->wait_ns+=waited-committed;s->begin_ns+=begun-waited;}
    else s->clock_invalid++;
}

static void dependencies_prepare_range(uintptr_t address,uint64_t size) {
    if(dependencies_unknown(address,size))dependencies_flush(DEP_COPY_BUFFER_SOURCE);
}

// GPU 生成图像直接采样；CPU 上传图像的待公布写入会在完成时才更新脏页代数。
// Sample GPU-generated images directly; pending writes to CPU-uploaded images update dirty generations only on completion.
static int dependencies_texture_unknown(const tex_t *texture) {
    image_entry *image=image_index[texture->serial&4095];
    while(image&&image->serial!=texture->serial)image=image->hash_next;
    const tex_t *t=image?&image->texture:texture;
    if(!t->mem||!dependencies_unknown((uintptr_t)t->mem,t->L.size))return 0;
    if(!image||!image->populated)return 1;
    if(snapshot_pending((uintptr_t)t->mem,t->L.size))return 1;
    return t->has_layout||image->stamp!=0;
}

static void dependencies_prepare_texture(const tex_t *texture) {
    if(dependencies_texture_unknown(texture))dependencies_flush(DEP_TEXTURE);
}

static void dependencies_prepare_copy_texture(const p_copy_b2t *copy) {
    if(copy->r.width<=0||copy->r.height<=0||copy->r.depth<=0||!dependencies_have_unknown())return;
    if(dependencies_texture_unknown(copy->tex)){dependencies_flush(DEP_COPY_TEXTURE_DESTINATION);return;}
    int bw,bh;unsigned bpb=nvn_fmt_bpb(copy->tex->d.format,&bw,&bh);
    if(!bpb||bw<=0||bh<=0)die("Vulkan dependency texture copy format %#x",copy->tex->d.format);
    uint64_t pitch=((uint64_t)copy->r.width+bw-1)/bw*bpb,rows=((uint64_t)copy->r.height+bh-1)/bh;
    uint64_t stride=state.copy_stride>0?(uint64_t)state.copy_stride:pitch,count=rows*(uint64_t)copy->r.depth;
    if(stride<pitch)die("Vulkan dependency texture copy stride");
    if(!count||count-1>(UINT64_MAX-pitch)/stride)die("Vulkan dependency texture copy range overflow");
    if((count-1)*stride+pitch>UINTPTR_MAX-copy->src)die("Vulkan dependency texture source range overflow");
    // 逐行检查真实读取，行间的 padding 不会无故触发同步。
    // Check actual row reads; padding between rows does not unnecessarily force synchronization.
    for(uint64_t i=0;i<count;i++)if(dependencies_unknown(copy->src+i*stride,pitch)){dependencies_flush(DEP_COPY_TEXTURE_SOURCE);return;}
}

static int dependencies_gpu_unknown(uintptr_t address,uint64_t size,uint64_t alignment);
static int dependencies_gpu_unknown_reason(uintptr_t address,uint64_t size,uint64_t alignment,int *result);
static int dependencies_shader_unknown(const shader_entry *shader) {
    if(!shader)return 0;
    for(unsigned i=0;i<shader->count;i++){
        const shader_binding *binding=&shader->bindings[i];
        const shader_binding_plan *plan=shader->binding_plan?&shader->binding_plan[i]:NULL;
        VkDescriptorType type=plan?plan->type:descriptor_type(binding);
        if(descriptor_type_uniform(type)){
            if(binding->kind==0||binding->slot<3||binding->slot>=19)continue;
            const p_bind_buf *u=&state.ubo[shader->stage][binding->slot-3];
            if(u->addr&&dependencies_unknown(u->addr,u->size<65536?u->size:65536))return DEP_UBO;
        }else if(type==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER){
            int uniform_pointer,slot;
            if(plan){uniform_pointer=plan->uniform_pointer;slot=plan->storage_slot;}
            else{
                unsigned base=shader->stage==0?0x110:shader->stage==5?0x310:0x510;int off=binding->cb_offset*4;
                uniform_pointer=shader->stage==5&&off>=0x210&&off<0x310;
                slot=(off-(uniform_pointer?0x210:(int)base))/16;
                if(binding->cb_slot!=0||slot<0||slot>=16)die("Vulkan dependency storage mapping: shader=%s cb=%u offset=%#x",shader->key,binding->cb_slot,off);
            }
            const p_bind_buf *u=uniform_pointer?&state.ubo[shader->stage][slot]:&state.ssbo[shader->stage][slot];
            // 可写 buffer 也读取初值；反射只有 Write 位，不能把它当作纯覆盖输出。
            // Writable buffers also read their initial contents; reflection has only a Write bit, not a write-only guarantee.
            uint64_t bytes=u->size?u->size:65536,alignment=(binding->flags&1)?4:gpu_limits.minStorageBufferOffsetAlignment;
            int result=-1;
            if(u->addr&&((binding->flags&1)&&(bytes&3)?dependencies_unknown(u->addr,bytes):dependencies_gpu_unknown_reason(u->addr,bytes,alignment,&result))){
                unsigned reason=uniform_pointer?DEP_COMPUTE_UBO_POINTER:DEP_SSBO;
                dependencies_detail_capture(reason,shader,binding,u->addr,bytes,alignment,result);return reason;
            }
        }else if(type==VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER||type==VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER){
            for(unsigned j=0;j<binding->count;j++){
                unsigned id=shader_handle(binding,shader->stage,j)&0xfffff;
                texture_descriptor descriptor=descriptor_texture(id);
                if(descriptor.valid){uintptr_t address=(uintptr_t)descriptor.texture.mem;uint64_t bytes=descriptor.texture.L.size;
                    if(type==VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER?dependencies_gpu_unknown(address,bytes,texel_buffer_alignment(&descriptor)):dependencies_unknown(address,bytes))return DEP_TEXEL;}
            }
        }else if(type!=VK_DESCRIPTOR_TYPE_SAMPLER){
            // Packaged storage 图像也读 CPU 初值；普通 GPU storage 图像保持直接消费。
            // Packaged storage images also read CPU initial contents; ordinary GPU storage images remain direct consumers.
            for(unsigned j=0;j<binding->count;j++){
                uint32_t handle=shader_handle(binding,shader->stage,j);
                texture_descriptor descriptor=descriptor_texture(handle&0xfffff);
                if(descriptor.valid&&(binding->kind!=4||(descriptor.texture.has_layout&&descriptor.texture.mem))&&dependencies_texture_unknown(&descriptor.texture))return DEP_SHADER_TEXTURE;
            }
        }
    }
    return 0;
}

static int dependencies_gpu_unknown_reason(uintptr_t address,uint64_t size,uint64_t alignment,int *result) {
    if(!dependencies_unknown(address,size))return 0;
    forward_slice slice;forward_result found=forward_find(address,size,alignment,NULL,&slice);if(result)*result=found;
    return found!=FORWARD_FULL&&(found==FORWARD_NONE||!forward_compose_possible(address,size,NULL));
}
static int dependencies_gpu_unknown(uintptr_t address,uint64_t size,uint64_t alignment) {
    return dependencies_gpu_unknown_reason(address,size,alignment,NULL);
}
static void dependencies_prepare_shader(const shader_entry *shader) {
    if(dependencies_have_unknown()){unsigned reason=dependencies_shader_unknown(shader);if(reason)dependencies_flush(reason);}
}

// 先等待索引本身，再用不可变快照的最大值检查顶点前缀；flush 之后 Draw 重新取得快照。
// Wait for indices before checking the vertex prefix using immutable-snapshot maxima; after a flush the draw acquires its snapshot again.
static int64_t dependencies_highest_vertex(const p_draw *draw,const p_draw_idx *indexed) {
    if(draw)return (int64_t)draw->first+draw->count-1;
    unsigned bytes=indexed->itype==0?1:indexed->itype==1?2:4;
    uint64_t size=(uint64_t)indexed->count*bytes;
    if(size>UINTPTR_MAX-indexed->ibuf)die("Vulkan dependency index range overflow");
    vk_buffer indices=snapshot_buffer(indexed->ibuf,size);uint32_t maximum=index_buffer_maximum(indices,bytes);
    return (int64_t)maximum+indexed->base_vertex;
}

static void dependencies_prepare_draw(const shader_entry *vertex,const shader_entry *fragment,const p_draw *draw,const p_draw_idx *indexed) {
    if((draw&&draw->count<=0)||(indexed&&(indexed->count<=0||!indexed->instances))||!dependencies_have_unknown())return;
    // 第一阶段不能读取未知索引，更不能分配会被 flush 清理的 descriptor/arena。
    // Stage one must not read unknown indices or allocate descriptors/arena data that flushing would reclaim.
    unsigned index_bytes=indexed?(indexed->itype==0?1:indexed->itype==1?2:4):0;
    // 保留原来的短路次序；分类来自真正首先触发的分支，不重复查询依赖。
    // Preserve short-circuit order; classify the first actual trigger without querying dependencies twice.
    unsigned reason=dependencies_shader_unknown(vertex);
    if(!reason)reason=dependencies_shader_unknown(fragment);
    if(!reason&&indexed&&dependencies_unknown(indexed->ibuf,(uint64_t)indexed->count*index_bytes))reason=DEP_INDEX;
    if(reason){dependencies_flush(reason);return;}
    int64_t highest=dependencies_highest_vertex(draw,indexed);
    if(highest<0)die("Vulkan dependency negative vertex fetch");
    uint32_t attribute_end[16]={0};
    for(unsigned i=0;i<16;i++){
        va_st attribute=state.attributes[i];if(!attribute.fmt||!vertex->attributes[i])continue;
        if(attribute.stream>=16)die("Vulkan dependency vertex stream bounds");
        int bw,bh;uint32_t end=attribute.offset+nvn_fmt_bpb(attribute.fmt,&bw,&bh);
        if(end>attribute_end[attribute.stream])attribute_end[attribute.stream]=end;
    }
    // 第二阶段按真实属性与实例偏移限制范围；等待后正常 draw 会重新读取索引和绑定。
    // Stage two bounds reads by actual attributes and instance offsets; after waiting, normal drawing re-reads indices and bindings.
    for(unsigned i=0;i<16;i++)if(attribute_end[i]){
        const vs_st *stream=&state.streams[i];const NVNbufferRange *buffer=&state.vertices[i];
        if(stream->divisor>1)die("Vulkan dependency vertex divisor %u pending",stream->divisor);
        uint64_t element=stream->divisor?(indexed?(uint64_t)indexed->base_instance+indexed->instances-1:0):(uint64_t)highest;
        uint64_t needed=stream->stride&&element>(UINT64_MAX-attribute_end[i])/stream->stride?buffer->size:element*stream->stride+attribute_end[i];
        if(needed>buffer->size)needed=buffer->size;
        if(buffer->address&&dependencies_gpu_unknown(buffer->address,needed,1)){dependencies_flush(DEP_VERTEX);return;}
    }
}
