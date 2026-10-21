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
void dm_lm_close(dm_host *host);
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
// 测试／宿主检查用：复制并取出最早保留的完整包；不伪造成功。
// Test/host inspection: copy out one retained packet; never fabricates success.
int dm_lm_take_packet(dm_host *host, dm_lm_packet *out);
