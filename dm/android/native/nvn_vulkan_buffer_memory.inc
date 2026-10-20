// Small buffers keep distinct Vulkan handles and disjoint memory until their final completed reference retires.
// 小缓冲仍保留独立句柄；互不重叠的内存槽仅在最后一个已完成引用销毁后归还。
typedef struct buffer_memory_page {
    VkDeviceMemory memory;
    void *mapped;
    uint64_t used;
    uint32_t type;
    struct buffer_memory_page *next;
} buffer_memory_page;
static buffer_memory_page *buffer_memory_pages;
static uint64_t buffer_memory_page_count,buffer_memory_page_allocations,buffer_memory_suballocations;
static int buffer_memory_enabled=1,buffer_memory_configured;
static int buffer_memory_bind(vk_buffer *buffer,const VkMemoryRequirements *requirements,uint32_t type,int dedicated) {
    if(dedicated||buffer->capacity!=4096||!requirements->size||requirements->size>4096||
       !requirements->alignment||4096%requirements->alignment)return 0;
    pthread_mutex_lock(&buffer_cache_lock);
    if(!buffer_memory_configured){
#ifdef __ANDROID__
        char value[92]={0};__system_property_get("debug.d2r.buffer_memory_pool",value);
        if(!strcmp(value,"0"))buffer_memory_enabled=0;
#endif
        buffer_memory_configured=1;
    }
    if(!buffer_memory_enabled){pthread_mutex_unlock(&buffer_cache_lock);return 0;}
    buffer_memory_page *page=buffer_memory_pages;
    while(page&&(page->type!=type||page->used==UINT64_MAX))page=page->next;
    if(!page){
        page=calloc(1,sizeof *page);if(!page)die("Vulkan buffer memory page metadata");
        VkMemoryAllocateInfo info={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=64u*4096,.memoryTypeIndex=type};
        VK_CHECK(vkAllocateMemory(device,&info,NULL,&page->memory));
        VK_CHECK(vkMapMemory(device,page->memory,0,VK_WHOLE_SIZE,0,&page->mapped));
        page->type=type;page->next=buffer_memory_pages;buffer_memory_pages=page;
        buffer_memory_page_count++;buffer_memory_page_allocations++;
    }
    unsigned slot=__builtin_ctzll(~page->used);page->used|=UINT64_C(1)<<slot;
    buffer->owner->page=page;buffer->owner->slot=slot;
    buffer->memory=page->memory;buffer->mapped=(uint8_t*)page->mapped+slot*4096;
    buffer_memory_suballocations++;
    pthread_mutex_unlock(&buffer_cache_lock);
    VK_CHECK(vkBindBufferMemory(device,buffer->buffer,buffer->memory,(VkDeviceSize)slot*4096));
    return 1;
}
static void buffer_memory_release(vk_buffer *buffer) {
    buffer_memory_page *page=buffer->owner->page;
    if(!page){vkUnmapMemory(device,buffer->memory);vkFreeMemory(device,buffer->memory,NULL);return;}
    pthread_mutex_lock(&buffer_cache_lock);
    if(buffer->owner->slot>=64||!(page->used&(UINT64_C(1)<<buffer->owner->slot)))die("Vulkan buffer memory slot ownership");
    page->used&=~(UINT64_C(1)<<buffer->owner->slot);buffer->owner->page=NULL;
    int empty=!page->used;
    if(empty){
        buffer_memory_page **link=&buffer_memory_pages;while(*link&&*link!=page)link=&(*link)->next;
        if(!*link)die("Vulkan buffer memory page ownership");*link=page->next;buffer_memory_page_count--;
    }
    pthread_mutex_unlock(&buffer_cache_lock);
    if(empty){vkUnmapMemory(device,page->memory);vkFreeMemory(device,page->memory,NULL);free(page);}
}
static void buffer_memory_report(uint64_t ticket) {
    pthread_mutex_lock(&buffer_cache_lock);
    uint64_t pages=buffer_memory_page_count,allocations=buffer_memory_page_allocations,suballocations=buffer_memory_suballocations;
    uint64_t cached=buffer_cache_bytes,evictions=buffer_cache_evictions;
    int enabled=buffer_memory_enabled;
    pthread_mutex_unlock(&buffer_cache_lock);
    LOG("[vulkan-buffer-memory] frame=%llu enabled=%d pages=%llu pooled=%.3f MiB page_allocations=%llu suballocations=%llu free_cache=%.3f MiB evictions=%llu",(unsigned long long)ticket,enabled,(unsigned long long)pages,pages/4.,(unsigned long long)allocations,(unsigned long long)suballocations,cached/1048576.,(unsigned long long)evictions);
}
