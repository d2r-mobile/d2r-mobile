#pragma once
#include "host.h"
#include <stdint.h>

#define DM_LM_PACKET_MAX 1024u
#define DM_LM_QUEUE_PACKETS 4u

typedef struct {
    uint32_t client;
    uint32_t object_id;
    uint32_t length;
    uint8_t data[DM_LM_PACKET_MAX];
} dm_lm_packet;

int dm_lm_install(dm_host *host);
// 销毁 lm 状态；活动 consume 返回 EBUSY，确保宿主/回调生命周期不被并发关闭释放。
// Destroy lm state; an active consume yields EBUSY so concurrent close cannot free the callback's host/lm lifetime.
int dm_lm_close(dm_host *host);
// 以下两个必须在持有 host->mutex 时调用。 | The following two require host->mutex held.
int dm_lm_close_locked(dm_host *host);
int dm_lm_busy_locked(dm_host *host);
// 处理器内的窄入口：校验活动子对象、读 pin 源范围、预留不可见槽位并构造48字节回复。
// Narrow handler entry: validate a live child, read-pin the source, reserve an
// invisible slot and build the 48-byte reply. No output on failure.
int dm_lm_handle_log(dm_host *host, uint64_t client, const uint8_t *request, size_t size,
                     uint8_t *reply, size_t *reply_size);
// 宿主锁内调用：发布已预留槽位或幂等丢弃。 / Called under host lock.
int dm_lm_has_staged_locked(dm_host *host, uint64_t client, uint32_t object_id);
void dm_lm_commit_log(dm_host *host, uint64_t client, uint32_t object_id);
void dm_lm_abort_log(dm_host *host, uint64_t client, uint32_t object_id);
size_t dm_lm_packet_count(dm_host *host);
// 显式宿主 sink：在宿主锁外接收已复制的最早 READY 包；返回非零表示投递失败，包保持待投递。
// sink 生命周期归调用方：回调期间不得销毁宿主、关闭宿主或调用宿主 SVC；宿主必须比 consume 活得久。
// Explicit host sink: receives the oldest READY packet copied outside the host lock; nonzero means the delivery
// failed and the packet stays pending. The sink is caller-owned: it must not destroy/close the host or call host SVCs
// during the callback, and the host must outlive the consume call.
typedef int (*dm_lm_sink)(void *user, const dm_lm_packet *packet);
// 配置/停止 sink：NULL 只暂停消费，不清空队列；回调期间调用返回 EBUSY。
// Configure/stop the sink: NULL only pauses consumption, never drops queued packets; returns EBUSY while a
// delivery callback is in flight.
int dm_lm_set_sink(dm_host *host, dm_lm_sink sink, void *user);
// 有界连续消费最多 budget 个包，按 seq 顺序；未配置 sink 返回 -EAGAIN，空队列 -ENOENT，sink 失败 -EIO，
// 重入/并发消费者 -EBUSY，非法 host/budget -EINVAL。成功返回实际投递数；整个调用持有 lm 生命周期 guard，
// 期间 dm_lm_close/dm_host_destroy 返回 EBUSY，take_packet/set_sink 亦拒绝。
// Bounded ordered consumption of up to budget packets; no sink -> -EAGAIN, empty -> -ENOENT, sink failure -> -EIO,
// reentrant/concurrent consumer -> -EBUSY, invalid host/budget -> -EINVAL. Returns the delivered count; the whole call
// holds the lm lifetime guard, so dm_lm_close/dm_host_destroy return EBUSY and take_packet/set_sink are refused.
int dm_lm_consume(dm_host *host, unsigned budget);
// 测试／宿主检查用：复制并取出最早保留的完整包；不伪造成功。
// Test/host inspection: copy out one retained packet; never fabricates success.
int dm_lm_take_packet(dm_host *host, dm_lm_packet *out);
