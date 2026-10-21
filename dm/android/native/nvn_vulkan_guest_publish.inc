// 帧边界只等真实 guest 发布及 acquire token 消费；更晚的纯 GPU 工作可继续在途。
// At frame boundaries wait only for real guest publication and acquire-token consumption; later pure GPU work may remain in flight.
typedef struct guest_publish_result {
    uint64_t queued_ticket,required_ticket,gap,wait_ns;
} guest_publish_result;
static guest_publish_result guest_publish_last;
static uint64_t guest_publish_calls,guest_publish_gaps,guest_publish_max_gap,guest_publish_wait_ns;
#ifdef DM_VK_REFERENCE_GATES
// 仅回归构建用于固定 host signal 的时序；游戏没有 callback 或 timeline 依赖。
// Regression-only hook fixes the host-signal ordering; games have no callback or timeline dependency.
static void (*guest_publish_reference_before_wait)(const guest_publish_result *result);
#endif
static guest_publish_result guest_publish_wait(uint64_t acquire_consumer_ticket) {
    // recording 调用者已经持 queue_lock；真实 commit 会提交命令并解锁，然后才能查 pending FIFO。
    // A recording caller already holds queue_lock; a real commit submits/unlocks before inspecting the pending FIFO.
    if(recording)mtl_sub_commit();
    pthread_mutex_lock(&queue_lock);guest_publish_result result={.queued_ticket=atomic_load(&submitted),.required_ticket=atomic_load(&completed)};
    if(acquire_consumer_ticket>result.queued_ticket)die("Vulkan guest publication acquire ticket exceeds queued work");
    if(acquire_consumer_ticket>result.required_ticket)result.required_ticket=acquire_consumer_ticket;
    for(const async_batch *batch=async_first;batch;batch=batch->next)
        if((batch->copies||batch->host_sync)&&batch->ticket>result.required_ticket)result.required_ticket=batch->ticket;
    // copies 包含所有 post0（已知 CPU-copy 也会写 guest）；host_sync 包含 post1/event 和 post2/counter。
    // copies covers every post0 (including known CPU copies that write guest); host_sync covers post1/events and post2/counters.
    // 后面的 post3/view退役、post4/5内部查询、image-only draws/blits 不会改变 guest0/1/2。
    // Later post3/view retirement, post4/5 internal queries and image-only draws/blits cannot change guest0/1/2.
    result.gap=result.queued_ticket-result.required_ticket;guest_publish_calls++;guest_publish_gaps+=result.gap;if(result.gap>guest_publish_max_gap)guest_publish_max_gap=result.gap;guest_publish_last=result;
    pthread_mutex_unlock(&queue_lock);
#ifdef DM_VK_REFERENCE_GATES
    if(guest_publish_reference_before_wait)guest_publish_reference_before_wait(&result);
#endif
    uint64_t before=dm_android_clock_ns(CLOCK_MONOTONIC);if(!mtl_wait(result.required_ticket,UINT64_MAX))die("Vulkan guest publication wait failed");result.wait_ns=dm_android_clock_ns(CLOCK_MONOTONIC)-before;
    pthread_mutex_lock(&queue_lock);guest_publish_wait_ns+=result.wait_ns;guest_publish_last=result;pthread_mutex_unlock(&queue_lock);return result;
}
