// 同批只读内容版本与真实描述符 payload 缓存；reset 必须在该批 GPU 完成之后。
// Cache read-only content versions and actual descriptor payloads within a batch; reset only after its GPU completes.
typedef struct uniform_version {
    uintptr_t address;
    uint32_t size;
    const uint8_t *mapped;
    VkDescriptorBufferInfo info;
    struct uniform_version *next;
} uniform_version;
typedef struct uniform_version_chunk {
    uniform_version entries[64];
    unsigned used;
    struct uniform_version_chunk *next;
} uniform_version_chunk;
typedef struct descriptor_version {
    VkDescriptorSetLayout layout;
    VkDescriptorSet set;
    uint64_t hash,*payload;
    size_t words,capacity;
    struct descriptor_version *next;
} descriptor_version;
typedef struct descriptor_version_chunk {
    descriptor_version entries[64];
    unsigned used;
    struct descriptor_version_chunk *next;
} descriptor_version_chunk;
static uniform_version *uniform_versions[2048];
static uniform_version_chunk *uniform_version_chunks,*uniform_version_current;
static uint8_t uniform_read_scratch[65536];
typedef struct uniform_read_counters {
    uint64_t heap_calls,heap_bytes,known_calls,known_bytes,safe_calls,safe_requested_bytes,safe_errors;
} uniform_read_counters;
static uniform_read_counters uniform_read_stats;
static descriptor_version *descriptor_versions[2048];
static descriptor_version_chunk *descriptor_version_chunks,*descriptor_version_current;
static uint64_t *descriptor_payload;
static size_t descriptor_payload_words,descriptor_payload_capacity;
static uint64_t descriptor_payload_hash;
static uint64_t uniform_version_hits,uniform_version_misses,uniform_version_read_bytes,uniform_version_compare_bytes,uniform_version_copy_bytes;
static uint64_t descriptor_version_hits,descriptor_version_misses;
static void binding_cache_reset(void) {
    memset(uniform_versions,0,sizeof uniform_versions);memset(descriptor_versions,0,sizeof descriptor_versions);
    for(uniform_version_chunk *c=uniform_version_chunks;c;c=c->next)c->used=0;uniform_version_current=uniform_version_chunks;
    for(descriptor_version_chunk *c=descriptor_version_chunks;c;c=c->next)c->used=0;descriptor_version_current=descriptor_version_chunks;
}
static uniform_version *uniform_version_new(void) {
    if(!uniform_version_current){uniform_version_chunks=calloc(1,sizeof *uniform_version_chunks);if(!uniform_version_chunks)die("Vulkan uniform version allocation");uniform_version_current=uniform_version_chunks;}
    while(uniform_version_current->used==64){if(!uniform_version_current->next){uniform_version_current->next=calloc(1,sizeof *uniform_version_current);if(!uniform_version_current->next)die("Vulkan uniform version allocation");}uniform_version_current=uniform_version_current->next;}
    return &uniform_version_current->entries[uniform_version_current->used++];
}
static int uniform_read_current(uintptr_t address,uint32_t size) {
    // 目的仅为 queue-owned scratch：从不交给 GPU，也不别名 guest 或 arena；完整范围已由调用方检查。
    // The destination is only queue-owned scratch, never passed to the GPU or aliased with guest/arena memory; the caller checks the full range.
    if(async_read_known_full_copy(address,uniform_read_scratch,size)){
        uniform_read_stats.known_calls++;uniform_read_stats.known_bytes+=size;
    }else{
        int copied=0;
        if(snapshot_heap_api_available()){
            copied=dm_android_guest_heap_copy_owned(address,uniform_read_scratch,size);
            if(copied!=0&&copied!=1)die("Vulkan uniform heap copy contract: result=%d",copied);
        }
        if(copied){uniform_read_stats.heap_calls++;uniform_read_stats.heap_bytes+=size;}
        else{uniform_read_stats.safe_calls++;uniform_read_stats.safe_requested_bytes+=size;
            if(dm_android_read_memory(address,uniform_read_scratch,size)){uniform_read_stats.safe_errors++;return -1;}}
        // heap 租约只替换当前 guest 基线读取，仍按原 FIFO 顺序覆盖所有已知在途复制。
        // The heap lease replaces only the guest baseline read; overlay every known pending copy in the original FIFO order.
        async_overlay_copies(address,uniform_read_scratch,size);
    }
    snapshot_read_account(size);return 0;
}
static VkDescriptorBufferInfo uniform_read_cached_read(uintptr_t address,uint32_t size);
static VkDescriptorBufferInfo uniform_read_cached(uintptr_t address,uint32_t size) {
    uint64_t phase=dm_cpu_phase_begin();VkDescriptorBufferInfo info=uniform_read_cached_read(address,size);dm_cpu_phase_end(DM_CPU_PHASE_UBO,phase);return info;
}
static VkDescriptorBufferInfo uniform_read_cached_read(uintptr_t address,uint32_t size) {
    if(!address||!size||size>65536||size>UINTPTR_MAX-address)die("Vulkan uniform version bounds");
    // queue_lock 串行复用 scratch；每次安全读取和 FIFO overlay 后才按实际字节复用，不依赖页保护或旧 stamp。
    // queue_lock serializes scratch reuse; compare fresh safe-read/FIFO-overlay bytes without relying on page protection or old stamps.
    if(uniform_read_current(address,size))die("Unreadable Vulkan cached uniform");uniform_version_read_bytes+=size;
    unsigned bucket=read_bucket(address,size)&2047;uniform_version *entry=uniform_versions[bucket];while(entry&&(entry->address!=address||entry->size!=size))entry=entry->next;
    if(entry){uniform_version_compare_bytes+=size;if(!memcmp(entry->mapped,uniform_read_scratch,size)){uniform_version_hits++;return entry->info;}}
    uniform_version_misses++;uint8_t *mapped;VkDescriptorBufferInfo info=uniform_allocate_overwrite(size,&mapped);
    // 变化时分配新 arena slot；旧 descriptor 引用的字节在该批 fence 完成前保持不变。
    // Allocate a fresh arena slot on changes; bytes referenced by older descriptors stay immutable until this batch's fence completes.
    memcpy(mapped,uniform_read_scratch,size);uniform_version_copy_bytes+=size;
    if(!entry){entry=uniform_version_new();entry->address=address;entry->size=size;entry->next=uniform_versions[bucket];uniform_versions[bucket]=entry;}
    entry->mapped=mapped;entry->info=info;
    return info;
}
static void descriptor_payload_build(const VkWriteDescriptorSet *writes,unsigned count) {
    size_t words=0;for(unsigned i=0;i<count;i++){VkDescriptorType type=writes[i].descriptorType;words+=2+writes[i].descriptorCount*((type==VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER||type==VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER)?1:3);}
    if(words>descriptor_payload_capacity){descriptor_payload=realloc(descriptor_payload,words*sizeof *descriptor_payload);if(!descriptor_payload)die("Vulkan descriptor payload allocation");descriptor_payload_capacity=words;}
    size_t cursor=0;for(unsigned i=0;i<count;i++){const VkWriteDescriptorSet *w=&writes[i];descriptor_payload[cursor++]=((uint64_t)w->descriptorType<<32)|w->dstBinding;descriptor_payload[cursor++]=w->descriptorCount;
        for(unsigned j=0;j<w->descriptorCount;j++){
            if(descriptor_type_uniform(w->descriptorType)||w->descriptorType==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER){const VkDescriptorBufferInfo *b=&w->pBufferInfo[j];descriptor_payload[cursor++]=(uintptr_t)b->buffer;descriptor_payload[cursor++]=b->offset;descriptor_payload[cursor++]=b->range;}
            else if(w->descriptorType==VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER||w->descriptorType==VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER)descriptor_payload[cursor++]=(uintptr_t)w->pTexelBufferView[j];
            else {const VkDescriptorImageInfo *image=&w->pImageInfo[j];descriptor_payload[cursor++]=(uintptr_t)image->sampler;descriptor_payload[cursor++]=(uintptr_t)image->imageView;descriptor_payload[cursor++]=image->imageLayout;}
        }
    }
    descriptor_payload_words=cursor;descriptor_payload_hash=14695981039346656037ull;
    // 显式字段编码，不把 Vulkan 结构体未定义的 padding 当作内容。
    // Encode explicit fields rather than treating unspecified Vulkan structure padding as content.
    for(size_t i=0;i<cursor;i++){descriptor_payload_hash^=descriptor_payload[i];descriptor_payload_hash*=1099511628211ull;}
    // 64 KiB arena offset 的低位相同，末尾混合高位防止所有模型版本挤进同一个桶。
    // Arena offsets share low 64 KiB bits; mix high bits at the end so model versions do not all land in one bucket.
    descriptor_payload_hash^=descriptor_payload_hash>>33;descriptor_payload_hash*=0xff51afd7ed558ccdull;descriptor_payload_hash^=descriptor_payload_hash>>33;
}
static VkDescriptorSet descriptor_cache_find_read(VkDescriptorSetLayout layout,const VkWriteDescriptorSet *writes,unsigned count);
static VkDescriptorSet descriptor_cache_find(VkDescriptorSetLayout layout,const VkWriteDescriptorSet *writes,unsigned count) {
    uint64_t phase=dm_cpu_phase_begin();VkDescriptorSet set=descriptor_cache_find_read(layout,writes,count);dm_cpu_phase_end(DM_CPU_PHASE_DESCRIPTOR,phase);return set;
}
static VkDescriptorSet descriptor_cache_find_read(VkDescriptorSetLayout layout,const VkWriteDescriptorSet *writes,unsigned count) {
    descriptor_payload_build(writes,count);unsigned bucket=(descriptor_payload_hash^(uintptr_t)layout)&2047;
    for(descriptor_version *entry=descriptor_versions[bucket];entry;entry=entry->next)
        if(entry->layout==layout&&entry->hash==descriptor_payload_hash&&entry->words==descriptor_payload_words&&(!entry->words||!memcmp(entry->payload,descriptor_payload,entry->words*sizeof *entry->payload))){descriptor_version_hits++;return entry->set;}
    descriptor_version_misses++;return VK_NULL_HANDLE;
}
static void descriptor_cache_store(VkDescriptorSetLayout layout,VkDescriptorSet set) {
    uint64_t phase=dm_cpu_phase_begin();
    if(!descriptor_version_current){descriptor_version_chunks=calloc(1,sizeof *descriptor_version_chunks);if(!descriptor_version_chunks)die("Vulkan descriptor version allocation");descriptor_version_current=descriptor_version_chunks;}
    while(descriptor_version_current->used==64){if(!descriptor_version_current->next){descriptor_version_current->next=calloc(1,sizeof *descriptor_version_current);if(!descriptor_version_current->next)die("Vulkan descriptor version allocation");}descriptor_version_current=descriptor_version_current->next;}
    descriptor_version *entry=&descriptor_version_current->entries[descriptor_version_current->used++];
    if(descriptor_payload_words>entry->capacity){entry->payload=realloc(entry->payload,descriptor_payload_words*sizeof *entry->payload);if(!entry->payload)die("Vulkan descriptor version payload allocation");entry->capacity=descriptor_payload_words;}
    entry->layout=layout;entry->set=set;entry->hash=descriptor_payload_hash;entry->words=descriptor_payload_words;
    if(entry->words)memcpy(entry->payload,descriptor_payload,entry->words*sizeof *entry->payload);
    unsigned bucket=(entry->hash^(uintptr_t)layout)&2047;entry->next=descriptor_versions[bucket];descriptor_versions[bucket]=entry;
    dm_cpu_phase_end(DM_CPU_PHASE_DESCRIPTOR,phase);
}
