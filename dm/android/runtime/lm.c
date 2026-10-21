// 有界 lm 服务：真实 OpenLogger 获取/域关闭与有界 Log 包保留；无 sink/SetDestination。
// Bounded lm backend: real OpenLogger acquisition, domain close and bounded Log
// packet retention; no sink/SetDestination.
#include "lm.h"
#include "internal.h"
#include "lm_wire.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

enum { LM_FREE = 0, LM_STAGED = 1, LM_READY = 2 };
typedef struct {
    int state;
    uint32_t client, object_id, length;
    uint64_t seq;
    uint8_t data[DM_LM_PACKET_MAX];
} lm_slot;
struct dm_lm { lm_slot slots[DM_LM_QUEUE_PACKETS]; uint64_t next_seq; int registered; pthread_mutex_t install_lock; };

static int lm_handler(void *user, uint64_t client, unsigned flags, uint32_t command,
                      const uint8_t *request, size_t size,
                      uint8_t *reply, size_t capacity, size_t *reply_size, uint32_t *out_handle) {
    (void)flags; (void)request;
    if (out_handle) *out_handle = 0;
    if (command != 0 || size != 8 || capacity < 4) return -1;
    uint32_t id;
    if (dm_host_ipc_object_alloc((dm_host *)user, client, &id)) return -2;
    memcpy(reply, &id, 4);
    *reply_size = 4;
    return 0;
}

static void build_log_reply(uint8_t *reply, uint32_t token) {
    uint32_t header[2] = {0, 10};                    // type0/counts0, 10 data words
    uint32_t domain_out[4] = {0, 0, 0, 0};           // count0 + reserved padding
    uint32_t cmif[4] = {0x4f434653u, 1u, 0u, token}; // SFCO v1/Result0/token convention
    memcpy(reply, header, sizeof header);
    memcpy(reply + 16, domain_out, sizeof domain_out);
    memcpy(reply + 32, cmif, sizeof cmif);
}

// 会话关闭回调在宿主锁内运行；清掉该会话尚未取走的包/暂存，避免陈旧数据。
// Session-close callback runs under the host lock; drop that session's queued/staged packets.
static void lm_closer(void *user, uint64_t client) {
    dm_host *host = user;
    if (!host || !host->lm) return;
    for (unsigned i = 0; i < DM_LM_QUEUE_PACKETS; ++i)
        if (host->lm->slots[i].state != LM_FREE && host->lm->slots[i].client == (uint32_t)client)
            memset(&host->lm->slots[i], 0, sizeof host->lm->slots[i]);
}

int dm_lm_handle_log(dm_host *host, uint64_t client, const uint8_t *request, size_t size,
                     uint8_t *reply, size_t *reply_size) {
    dm_lm_wire_log_request log;
    if (!host || !reply || !reply_size ||
        dm_lm_wire_decode_log_request(request, size, &log) != DM_LM_WIRE_OK)
        return -1;

    pthread_mutex_lock(&host->mutex);
    struct dm_lm *lm = host->lm;
    if (!lm || !dm_host_ipc_child_live_locked(host, client, log.object_id)) {
        pthread_mutex_unlock(&host->mutex);
        return -1;
    }

    // 全范围严格 owned READ pin；未知 range 在 unaccounted 宿主上也不放行。
    // Strict owned full-range READ pin; unknown ranges are refused even on unaccounted hosts.
    dm_memory_pin pin = {0};
    if (dm_host_memory_read_pin_owned_locked(host, &pin, (uintptr_t)log.pointer, log.length)) {
        pthread_mutex_unlock(&host->mutex);
        return -3;
    }
    lm_slot *slot = NULL;
    for (unsigned i = 0; i < DM_LM_QUEUE_PACKETS; ++i)
        if (lm->slots[i].state == LM_FREE) { slot = &lm->slots[i]; break; }
    if (!slot) {
        dm_host_memory_unpin_locked(host, &pin);
        pthread_mutex_unlock(&host->mutex);
        return -2;
    }
    // 全范围读 pin 下复制；pin 前不解引用源指针。
    // Copy under the full-range read pin; never dereference the source before it.
    slot->state = LM_STAGED;
    slot->client = (uint32_t)client;
    slot->object_id = log.object_id;
    slot->length = log.length;
    memcpy(slot->data, (const void *)log.pointer, log.length);

    dm_host_memory_unpin_locked(host, &pin);
    pthread_mutex_unlock(&host->mutex);

    build_log_reply(reply, log.token);
    *reply_size = 48;
    return 0;
}

static lm_slot *find_staged(struct dm_lm *lm, uint64_t client, uint32_t object_id) {
    for (unsigned i = 0; i < DM_LM_QUEUE_PACKETS; ++i)
        if (lm->slots[i].state == LM_STAGED && lm->slots[i].client == (uint32_t)client &&
            lm->slots[i].object_id == object_id)
            return &lm->slots[i];
    return NULL;
}

int dm_lm_has_staged_locked(dm_host *host, uint64_t client, uint32_t object_id) {
    if (!host || !host->lm) return 0;
    return find_staged(host->lm, client, object_id) != NULL;
}

void dm_lm_commit_log(dm_host *host, uint64_t client, uint32_t object_id) {
    if (!host || !host->lm) return;
    struct dm_lm *lm = host->lm;
    lm_slot *slot = find_staged(lm, client, object_id);
    if (slot && slot->state == LM_STAGED) {
        slot->state = LM_READY;
        slot->seq = ++lm->next_seq;
    }
}

void dm_lm_abort_log(dm_host *host, uint64_t client, uint32_t object_id) {
    if (!host || !host->lm) return;
    struct dm_lm *lm = host->lm;
    lm_slot *slot = find_staged(lm, client, object_id);
    if (slot && slot->state == LM_STAGED) memset(slot, 0, sizeof *slot);
}

size_t dm_lm_packet_count(dm_host *host) {
    if (!host) return 0;
    pthread_mutex_lock(&host->mutex);
    size_t count = 0;
    if (host->lm)
        for (unsigned i = 0; i < DM_LM_QUEUE_PACKETS; ++i)
            if (host->lm->slots[i].state == LM_READY) count++;
    pthread_mutex_unlock(&host->mutex);
    return count;
}

int dm_lm_take_packet(dm_host *host, dm_lm_packet *out) {
    if (!host || !out) return EINVAL;
    pthread_mutex_lock(&host->mutex);
    lm_slot *slot = NULL;
    if (host->lm)
        for (unsigned i = 0; i < DM_LM_QUEUE_PACKETS; ++i) {
            lm_slot *candidate = &host->lm->slots[i];
            if (candidate->state == LM_READY &&
                (!slot || candidate->seq < slot->seq)) slot = candidate;
        }
    if (!slot) { pthread_mutex_unlock(&host->mutex); return ENOENT; }
    out->client = slot->client;
    out->object_id = slot->object_id;
    out->length = slot->length;
    memcpy(out->data, slot->data, slot->length);
    memset(slot, 0, sizeof *slot);
    pthread_mutex_unlock(&host->mutex);
    return 0;
}

int dm_lm_install(dm_host *host) {
    if (!host) return EINVAL;
    pthread_mutex_lock(&host->mutex);
    if (!host->lm) {
        struct dm_lm *fresh = calloc(1, sizeof *fresh);
        if (!fresh) { pthread_mutex_unlock(&host->mutex); return ENOMEM; }
        if (pthread_mutex_init(&fresh->install_lock, NULL)) {
            free(fresh); pthread_mutex_unlock(&host->mutex); return ENOMEM;
        }
        host->lm = fresh;
    }
    struct dm_lm *lm = host->lm;
    pthread_mutex_unlock(&host->mutex);

    pthread_mutex_lock(&lm->install_lock);
    int rc = 0;
    pthread_mutex_lock(&host->mutex);
    int match = dm_host_ipc_port_matches_locked(host, "lm", lm_handler, lm_closer, host);
    if (match) lm->registered = 1;
    else lm->registered = 0;
    pthread_mutex_unlock(&host->mutex);
    if (match) {
        rc = dm_host_ipc_mark_lm_port(host, lm_handler, lm_closer, host);
    } else {
        rc = dm_host_ipc_register_domain_port_full(host, "lm", lm_handler, lm_closer, host);
        if (rc == 0) {
            rc = dm_host_ipc_mark_lm_port(host, lm_handler, lm_closer, host);
            if (rc == 0) {
                pthread_mutex_lock(&host->mutex); lm->registered = 1; pthread_mutex_unlock(&host->mutex);
            }
        } else if (rc == EEXIST) {
            pthread_mutex_lock(&host->mutex);
            match = dm_host_ipc_port_matches_locked(host, "lm", lm_handler, lm_closer, host);
            pthread_mutex_unlock(&host->mutex);
            if (match) {
                rc = dm_host_ipc_mark_lm_port(host, lm_handler, lm_closer, host);
                if (rc == 0) {
                    pthread_mutex_lock(&host->mutex); lm->registered = 1; pthread_mutex_unlock(&host->mutex);
                }
            }
        }
    }
    pthread_mutex_unlock(&lm->install_lock);
    return rc;
}

void dm_lm_close(dm_host *host) {
    if (!host) return;
    if (host->lm) {
        pthread_mutex_destroy(&host->lm->install_lock);
        free(host->lm);
        host->lm = NULL;
    }
}
