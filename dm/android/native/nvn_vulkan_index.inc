// 不可变索引快照的最大值缓存；activation 区分同一 VkBuffer backing 的每次重新使用。
// Cache maxima of immutable index snapshots; activation distinguishes every reuse of one VkBuffer backing.
typedef struct index_maximum_entry {
    uint64_t activation,size,used;
    uint32_t maximum,width;
} index_maximum_entry;
static index_maximum_entry index_maxima[4096][4];
static uint64_t index_maximum_clock,index_maximum_hits,index_maximum_misses,index_maximum_scan_bytes;
static uint32_t index_buffer_maximum(vk_buffer buffer,unsigned width) {
    if(!buffer.owner||!buffer.owner->activation||!buffer.mapped||!buffer.size||(width!=1&&width!=2&&width!=4)||buffer.size%width)
        die("Vulkan index snapshot contract");
    // 只存整数身份/结果，不持 owner 或 VkBuffer 指针，旧 backing 销毁、缓存复用后无需失效回调。
    // Retain only integer identities/results, never owner or VkBuffer pointers; destruction/cache reuse needs no invalidation callback.
    uint64_t activation=buffer.owner->activation,key=activation^(buffer.size*0x9e3779b97f4a7c15ull)^((uint64_t)width<<59);
    key^=key>>33;key*=0xff51afd7ed558ccdull;key^=key>>33;unsigned bucket=key&4095;
    index_maximum_entry *victim=&index_maxima[bucket][0];
    for(unsigned i=0;i<4;i++){
        index_maximum_entry *entry=&index_maxima[bucket][i];
        if(entry->activation==activation&&entry->size==buffer.size&&entry->width==width){entry->used=++index_maximum_clock;index_maximum_hits++;return entry->maximum;}
        if(entry->used<victim->used)victim=entry;
    }
    uint32_t maximum=0;uint64_t count=buffer.size/width;
    if(width==1){const uint8_t *values=buffer.mapped;for(uint64_t i=0;i<count;i++)if(values[i]>maximum)maximum=values[i];}
    else if(width==2){const uint16_t *values=buffer.mapped;for(uint64_t i=0;i<count;i++)if(values[i]>maximum)maximum=values[i];}
    else{const uint32_t *values=buffer.mapped;for(uint64_t i=0;i<count;i++)if(values[i]>maximum)maximum=values[i];}
    *victim=(index_maximum_entry){.activation=activation,.size=buffer.size,.used=++index_maximum_clock,.maximum=maximum,.width=width};
    index_maximum_misses++;index_maximum_scan_bytes+=buffer.size;return maximum;
}
