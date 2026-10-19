// 采样器由实际有效参数共享缓存持有；槽号只保存引用，重登记不销毁在途对象。
// Shared sampler objects are owned by effective parameter keys; slots retain references, and re-registration never destroys in-flight objects.
typedef struct {
    uint32_t mag,min,mip,u,v,w,compare,compare_op,border;
    float bias,min_lod,max_lod;
} sampler_key;
typedef struct sampler_cache_entry { sampler_key key;VkSampler sampler;struct sampler_cache_entry *next; } sampler_cache_entry;
static sampler_cache_entry *sampler_cache[256];
static uint64_t sampler_creations,sampler_reuses;
static VkBorderColor sampler_border(const samp_par *p) {
    // 和原 Metal 一样只支持三种标准边界色；任意 NVN 自定义边界色仍有限制。
    // Match the original Metal backend's three standard border colors; arbitrary NVN custom colors remain limited.
    return p->border[3]>.5f?(p->border[0]>.5f?VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE:VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK):VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
}
static sampler_key sampler_key_make(const samp_par *p) {
    sampler_key key={.mag=p->magf?VK_FILTER_LINEAR:VK_FILTER_NEAREST,.min=(p->minf&1)?VK_FILTER_LINEAR:VK_FILTER_NEAREST,
        .mip=p->minf>=4?VK_SAMPLER_MIPMAP_MODE_LINEAR:VK_SAMPLER_MIPMAP_MODE_NEAREST,.u=wrap_mode(p->ws),.v=wrap_mode(p->wt),.w=wrap_mode(p->wr),
        .compare=!!p->cmp_mode,.compare_op=p->cmp_mode&&p->cmp_func?p->cmp_func-1:VK_COMPARE_OP_ALWAYS,.border=sampler_border(p),.bias=p->lod_bias};
    if(p->minf>=2){key.min_lod=p->lod_min>0?p->lod_min:0;key.max_lod=p->lod_max>=key.min_lod?p->lod_max:key.min_lod;}
    if(key.u!=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER&&key.v!=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER&&key.w!=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER)key.border=VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    if(key.bias==0)key.bias=0;
    // 镜像钳制/各向异性仍沿用现有能力限制；不能使用未启用的 Vulkan 特性。
    // Mirror-clamp/anisotropy retain the existing capability limits; never use Vulkan features that were not enabled.
    return key;
}
static unsigned sampler_key_bucket(const sampler_key *key) {
    const uint8_t *bytes=(const uint8_t*)key;uint64_t hash=14695981039346656037ull;
    for(unsigned i=0;i<sizeof *key;i++)hash=(hash^bytes[i])*1099511628211ull;return hash&255;
}
static void sampler_compare_note_locked(int id,uint32_t type,const sampler_descriptor *descriptor) {
    if(!(type&255)||!descriptor)return;
    static unsigned notes;const samp_par *parameters=&descriptor->parameters;
    if(notes<12&&!!(type&1024)!=!!parameters->cmp_mode){notes++;LOG("[vulkan-sampler] compare mismatch id=%d reflected_shadow=%u registered_compare=%u func=%u",id,!!(type&1024),!!parameters->cmp_mode,(unsigned)parameters->cmp_func);}
}
static VkSampler sampler_get_for_type(int id,uint32_t type) {
    pthread_mutex_lock(&descriptor_lock);sampler_descriptor *descriptor=id>=0&&id<sampler_capacity&&sampler_descriptors[id].valid?&sampler_descriptors[id]:NULL;
    sampler_compare_note_locked(id,type,descriptor);
    if(descriptor&&descriptor->sampler){VkSampler sampler=descriptor->sampler;sampler_reuses++;pthread_mutex_unlock(&descriptor_lock);return sampler;}
    samp_par parameters=descriptor?descriptor->parameters:(samp_par){.lod_max=1000};sampler_key key=sampler_key_make(&parameters);unsigned bucket=sampler_key_bucket(&key);
    for(sampler_cache_entry *entry=sampler_cache[bucket];entry;entry=entry->next)if(!memcmp(&entry->key,&key,sizeof key)){
        if(descriptor)descriptor->sampler=entry->sampler;sampler_reuses++;pthread_mutex_unlock(&descriptor_lock);return entry->sampler;
    }
    sampler_cache_entry *entry=calloc(1,sizeof *entry);if(!entry)die("Vulkan sampler parameter cache allocation");entry->key=key;
    VkSamplerCreateInfo info={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,.magFilter=key.mag,.minFilter=key.min,.mipmapMode=key.mip,
        .addressModeU=key.u,.addressModeV=key.v,.addressModeW=key.w,.mipLodBias=key.bias,.compareEnable=key.compare,.compareOp=key.compare_op,
        .minLod=key.min_lod,.maxLod=key.max_lod,.borderColor=key.border};
    VK_CHECK(vkCreateSampler(device,&info,NULL,&entry->sampler));entry->next=sampler_cache[bucket];sampler_cache[bucket]=entry;sampler_creations++;
    if(descriptor)descriptor->sampler=entry->sampler;
    static unsigned border_notes;if(border_notes<12&&(key.u==VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER||key.v==VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER||key.w==VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER)){
        border_notes++;LOG("[vulkan-sampler] id=%d wrap=%u,%u,%u border=%g,%g,%g,%g canonical=%u compare=%u/%u",id,(unsigned)parameters.ws,(unsigned)parameters.wt,(unsigned)parameters.wr,parameters.border[0],parameters.border[1],parameters.border[2],parameters.border[3],key.border,key.compare,key.compare_op);
    }
    pthread_mutex_unlock(&descriptor_lock);return entry->sampler;
}
static VkSampler sampler_get(int id) {return sampler_get_for_type(id,0);}
