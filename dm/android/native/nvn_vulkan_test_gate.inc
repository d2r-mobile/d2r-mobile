// 只在 GPU 回归构建启用 timeline gate；生产设备特性和提交路径不受影响。
// Enable timeline gates only in GPU regression builds; production device features and submission paths remain unchanged.
#ifdef DM_VK_REFERENCE_GATES
static int reference_timeline_supported;
static VkPhysicalDeviceTimelineSemaphoreFeatures reference_timeline_feature;
static const char *reference_device_extensions[64];
static VkSemaphore reference_pending_gate;
static uint64_t reference_pending_value;
static PFN_vkSignalSemaphoreKHR reference_signal_semaphore;
static PFN_vkGetSemaphoreCounterValueKHR reference_counter_value;
static pthread_mutex_t reference_signal_lock=PTHREAD_MUTEX_INITIALIZER;

static void reference_gate_device_setup(VkDeviceCreateInfo *create) {
    uint32_t count=0;VK_CHECK(vkEnumerateDeviceExtensionProperties(physical,NULL,&count,NULL));
    VkExtensionProperties *extensions=calloc(count?count:1,sizeof *extensions);if(!extensions)die("Vulkan test extension allocation");
    VK_CHECK(vkEnumerateDeviceExtensionProperties(physical,NULL,&count,extensions));int supported=0;
    for(unsigned i=0;i<count;i++)if(!strcmp(extensions[i].extensionName,VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME))supported=1;
    free(extensions);
    if(!supported){LOG("[vulkan-test] timeline gate extension unsupported");return;}
    VkPhysicalDeviceTimelineSemaphoreFeatures timeline={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
    VkPhysicalDeviceFeatures2 features={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,.pNext=&timeline};vkGetPhysicalDeviceFeatures2(physical,&features);
    if(!timeline.timelineSemaphore){LOG("[vulkan-test] timeline gate feature unsupported");return;}
    if(create->enabledExtensionCount>=64)die("Vulkan test extension capacity");
    unsigned used=0,present=0;
    for(unsigned i=0;i<create->enabledExtensionCount;i++){
        reference_device_extensions[used++]=create->ppEnabledExtensionNames[i];
        present|=!strcmp(create->ppEnabledExtensionNames[i],VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
    }
    if(!present)reference_device_extensions[used++]=VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME;
    create->enabledExtensionCount=used;create->ppEnabledExtensionNames=reference_device_extensions;
    reference_timeline_feature=(VkPhysicalDeviceTimelineSemaphoreFeatures){.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,.pNext=(void*)create->pNext,.timelineSemaphore=VK_TRUE};
    create->pNext=&reference_timeline_feature;reference_timeline_supported=1;
}

static int reference_gates_supported(void) {return reference_timeline_supported;}

static VkSemaphore reference_gate_create(void) {
    if(!reference_timeline_supported)die("Vulkan test timeline feature was not enabled");
    if(!reference_signal_semaphore){
        reference_signal_semaphore=(PFN_vkSignalSemaphoreKHR)vkGetDeviceProcAddr(device,"vkSignalSemaphoreKHR");
        reference_counter_value=(PFN_vkGetSemaphoreCounterValueKHR)vkGetDeviceProcAddr(device,"vkGetSemaphoreCounterValueKHR");
        if(!reference_signal_semaphore||!reference_counter_value)die("Vulkan test timeline entrypoints missing");
    }
    VkSemaphoreTypeCreateInfo type={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,.semaphoreType=VK_SEMAPHORE_TYPE_TIMELINE,.initialValue=0};
    VkSemaphoreCreateInfo create={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,.pNext=&type};VkSemaphore semaphore;VK_CHECK(vkCreateSemaphore(device,&create,NULL,&semaphore));return semaphore;
}

// 在持 queue_lock 的录制线程调用，下一次 raw commit 才消费该 gate。
// Call from the recording thread holding queue_lock; the next raw commit consumes the gate.
static void reference_gate_arm(VkSemaphore semaphore,uint64_t value) {
    if(!recording||!semaphore||!value||reference_pending_gate)die("Vulkan test timeline gate arm state");
    reference_pending_gate=semaphore;reference_pending_value=value;
}

static void reference_gate_submit(VkSubmitInfo *submit) {
    if(!reference_pending_gate)return;
    static VkSemaphore waits[8];static VkPipelineStageFlags stages[8];static uint64_t wait_values[8],signal_values[8];
    static VkTimelineSemaphoreSubmitInfo timeline;
    if(submit->waitSemaphoreCount>=8||submit->signalSemaphoreCount>8)die("Vulkan test timeline submit capacity");
    for(const VkBaseInStructure *p=submit->pNext;p;p=p->pNext)if(p->sType==VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO)die("Vulkan test duplicate timeline submit chain");
    unsigned count=submit->waitSemaphoreCount;
    for(unsigned i=0;i<count;i++){waits[i]=submit->pWaitSemaphores[i];stages[i]=submit->pWaitDstStageMask[i];wait_values[i]=0;}
    waits[count]=reference_pending_gate;stages[count]=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;wait_values[count]=reference_pending_value;
    for(unsigned i=0;i<submit->signalSemaphoreCount;i++)signal_values[i]=0;
    timeline=(VkTimelineSemaphoreSubmitInfo){.sType=VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,.pNext=submit->pNext,.waitSemaphoreValueCount=count+1,.pWaitSemaphoreValues=wait_values,.signalSemaphoreValueCount=submit->signalSemaphoreCount,.pSignalSemaphoreValues=signal_values};
    submit->pNext=&timeline;submit->waitSemaphoreCount=count+1;submit->pWaitSemaphores=waits;submit->pWaitDstStageMask=stages;
    reference_pending_gate=VK_NULL_HANDLE;reference_pending_value=0;
}

static void reference_gate_signal(VkSemaphore semaphore,uint64_t value) {
    pthread_mutex_lock(&reference_signal_lock);uint64_t reached=0;VK_CHECK(reference_counter_value(device,semaphore,&reached));
    if(reached<value){VkSemaphoreSignalInfo signal={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO,.semaphore=semaphore,.value=value};VK_CHECK(reference_signal_semaphore(device,&signal));}
    pthread_mutex_unlock(&reference_signal_lock);
}

// 先释放未来值等待，再真正 drain；绝不先销毁仍有 GPU waiter 的同步对象。
// Release a future-value wait before actually draining; never destroy a synchronization object with pending GPU waiters.
static void reference_gate_close(VkSemaphore semaphore,uint64_t value) {
    reference_gate_signal(semaphore,value);
    mtl_drain();vkDestroySemaphore(device,semaphore,NULL);
}

typedef struct reference_gate_watchdog {
    pthread_mutex_t lock;
    pthread_cond_t condition;
    pthread_t thread;
    VkSemaphore semaphore;
    uint64_t value;
    unsigned seconds;
    int canceled,fired;
} reference_gate_watchdog;

static void *reference_gate_watchdog_main(void *argument) {
    reference_gate_watchdog *watchdog=argument;struct timespec until;if(clock_gettime(CLOCK_REALTIME,&until))die("Vulkan test gate watchdog clock");until.tv_sec+=watchdog->seconds;
    pthread_mutex_lock(&watchdog->lock);
    while(!watchdog->canceled){
        int result=pthread_cond_timedwait(&watchdog->condition,&watchdog->lock,&until);
        if(result==ETIMEDOUT){
            watchdog->fired=1;pthread_mutex_unlock(&watchdog->lock);
            LOG("[vulkan-test] timeline gate watchdog fired after %u seconds; releasing real GPU wait",watchdog->seconds);
            reference_gate_signal(watchdog->semaphore,watchdog->value);return NULL;
        }
        if(result)die("Vulkan test gate watchdog condition %d",result);
    }
    pthread_mutex_unlock(&watchdog->lock);return NULL;
}

static void reference_gate_watchdog_start(reference_gate_watchdog *watchdog,VkSemaphore semaphore,uint64_t value,unsigned seconds) {
    if(!semaphore||!value||!seconds)die("Vulkan test gate watchdog argument");
    memset(watchdog,0,sizeof *watchdog);watchdog->semaphore=semaphore;watchdog->value=value;watchdog->seconds=seconds;
    int result=pthread_mutex_init(&watchdog->lock,NULL);if(result)die("Vulkan test gate watchdog mutex %d",result);
    result=pthread_cond_init(&watchdog->condition,NULL);if(result)die("Vulkan test gate watchdog condition init %d",result);
    result=pthread_create(&watchdog->thread,NULL,reference_gate_watchdog_main,watchdog);if(result)die("Vulkan test gate watchdog thread %d",result);
}

// stop 必须在 close/destroy gate 之前调用；超时只释放真实等待，测试仍应报告失败。
// Stop before closing/destroying the gate; timeout releases only a real wait, and the test must still report failure.
static int reference_gate_watchdog_stop(reference_gate_watchdog *watchdog) {
    pthread_mutex_lock(&watchdog->lock);watchdog->canceled=1;pthread_cond_signal(&watchdog->condition);pthread_mutex_unlock(&watchdog->lock);
    int result=pthread_join(watchdog->thread,NULL);if(result)die("Vulkan test gate watchdog join %d",result);
    int fired=watchdog->fired;pthread_cond_destroy(&watchdog->condition);pthread_mutex_destroy(&watchdog->lock);return fired;
}
#endif
