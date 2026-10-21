// 有界本地命名端口与同步 IPC 传输；句柄共用 sync.c 的 128 槽代际命名空间，响应仅在完整构建后原子发布。
// Bounded local named ports and synchronous IPC transport; handles share sync.c's 128-slot generation namespace; responses publish atomically only after full construction.
#include "ipc.h"
#include "internal.h"
#include "lm.h"
#include "lm_wire.h"
#include "sm.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define PORTS 8u
#define SESSIONS 32u
#define NAME_BYTES (DM_IPC_PORT_NAME_MAX + 1u)
#define HIPC_WORDS 2u            // HipcHeader 为两个 u32 位域字。 / HipcHeader is two bit-field u32 words.
#define HIPC_TYPE_REQUEST 4u     // CmifCommandType_Request
#define CMIF_IN_MAGIC 0x49434653u  // "SFCI"
#define CMIF_OUT_MAGIC 0x4F434653u // "SFCO"
#define CMIF_IN_HEADER 16u
#define CMIF_OUT_HEADER 16u
// 数据字从 HipcHeader 之后 (base+8) 开始，CMIF 头按 16 字节对齐落在 base+16，负载在 base+32。
// Data words start after the HipcHeader (base+8); the 16-byte-aligned CMIF header lands at base+16 and the payload at base+32.
#define DATA_WORDS_BIAS 8u
#define CMIF_DATA_OFFSET 16u
#define CMIF_PAYLOAD_OFFSET 32u
#define MIN_DATA_WORDS ((CMIF_PAYLOAD_OFFSET - DATA_WORDS_BIAS) / 4u)
#define MAX_DATA_WORDS ((DM_IPC_BUFFER_SIZE - DATA_WORDS_BIAS) / 4u)

typedef struct {
    char name[NAME_BYTES];
    dm_ipc_handler handler;
    dm_ipc_closer closer;
    void *user;
    unsigned connections;
    int domain_capable;
    int lm_backend;
} ipc_port;
typedef struct { unsigned port; uint32_t client; int active, busy, closing; uint32_t domain_id; uint32_t next_object_id; uint32_t children[8]; int domain_retired; } ipc_session;
struct dm_ipc { ipc_port ports[PORTS]; ipc_session sessions[SESSIONS]; };

typedef struct { uint32_t command, token, flags; size_t payload_offset, payload_size; int control; int domain; uint32_t object_id; } request_info;
// 已证明的类型6域请求：OpenLogger(0x14数据字) 与域关闭(8数据字)。仅用于显式支持域端口的会话。
// Proven type6 domain requests: Log/OpenLogger/domain close. Only for explicitly domain-capable ports.
static int parse_domain_request(const uint8_t *request, request_info *info) {
    uint32_t head, meta;
    memcpy(&head, request, 4); memcpy(&meta, request + 4, 4);
    if ((head & 0xffffu) != 6u) return 0;
    int special = (int)((meta >> 31) & 1u);
    if ((meta & 0x3ffu) == 12u && !special) {
        dm_lm_wire_log_request log;
        if (dm_lm_wire_decode_log_request(request, DM_IPC_BUFFER_SIZE, &log) != DM_LM_WIRE_OK)
            return 0;
        info->object_id = log.object_id;
        info->token = log.token;
        info->command = 0;
        info->flags = 0;
        info->domain = 3;
        info->payload_offset = 0;
        info->payload_size = 0;
        return 3;
    }
    if ((head >> 16) != 0) return 0;
    if ((meta & 0x3ffu) == 14u && special) {
        uint32_t sph; memcpy(&sph, request + 8, 4);
        if ((sph & 1u) == 0 || (sph >> 1) != 0) return 0;  // send_pid only
        size_t data_start = 20, domain = 32;
        if (request[domain] != 1u || request[domain + 1] != 0u) return 0;
        uint16_t dsize; memcpy(&dsize, request + domain + 2, 2);
        if (dsize != 24u) return 0;
        if (domain + 16 + dsize > data_start + 14u * 4u) return 0;
        uint32_t hdr[4]; memcpy(hdr, request + domain + 16, sizeof hdr);
        if (hdr[0] != CMIF_IN_MAGIC || hdr[1] != 1u || hdr[2] != 0u) return 0;
        memcpy(&info->object_id, request + domain + 4, 4);
        memcpy(&info->token, request + domain + 12, 4);
        info->command = 0; info->flags = DM_IPC_FLAG_SEND_PID; info->domain = 1;
        info->payload_offset = domain + 32; info->payload_size = 8;
        return 1;
    }
    if ((meta & 0x3ffu) == 8u && !special) {
        size_t domain = 16;                                  // 8 字节头对齐到 16。 / 8-byte header aligns to 16.
        if (request[domain] != 2u) return 0;
        memcpy(&info->object_id, request + domain + 4, 4);
        memcpy(&info->token, request + domain + 12, 4);
        if (domain + 16 > 8u + 8u * 4u) return 0;
        info->command = 2; info->domain = 2; info->payload_size = 0;
        return 2;
    }
    return 0;
}
static ipc_port *find_port(struct dm_ipc *ipc, const char *name) {
    for (unsigned i = 0; i < PORTS; i++)
        if (ipc->ports[i].handler && !strcmp(ipc->ports[i].name, name)) return &ipc->ports[i];
    return NULL;
}
static ipc_port *port_at(struct dm_ipc *ipc, size_t index) { return index < PORTS ? &ipc->ports[index] : NULL; }
size_t dm_host_ipc_open_sessions(dm_host *host) {
    if (!host) return 0;
    pthread_mutex_lock(&host->mutex);
    size_t count = 0;
    if (host->ipc) for (unsigned i = 0; i < SESSIONS; i++) if (host->ipc->sessions[i].active) count++;
    pthread_mutex_unlock(&host->mutex);
    return count;
}
int dm_host_ipc_busy(dm_host *host) {
    if (!host || !host->ipc) return 0;
    for (unsigned i = 0; i < SESSIONS; i++) if (host->ipc->sessions[i].active) return 1;
    return 0;
}
void dm_host_ipc_close(dm_host *host) { free(host->ipc); host->ipc = NULL; dm_sm_close(host); }

int dm_host_ipc_register_port_full(dm_host *host, const char *name, dm_ipc_handler handler, dm_ipc_closer closer, void *user) {
    if (!host || !name || !handler) return EINVAL;
    size_t length = strlen(name);
    if (!length || length > DM_IPC_PORT_NAME_MAX) return EINVAL;
    for (size_t i = 0; i < length; i++) if ((unsigned char)name[i] < 0x20 || (unsigned char)name[i] > 0x7e) return EINVAL;
    pthread_mutex_lock(&host->mutex);
    if (!host->ipc) { host->ipc = calloc(1, sizeof *host->ipc); if (!host->ipc) { pthread_mutex_unlock(&host->mutex); return ENOMEM; } }
    int result = ENOSPC;
    if (find_port(host->ipc, name)) result = EEXIST;
    else for (unsigned i = 0; i < PORTS; i++) if (!host->ipc->ports[i].handler) {
        strcpy(host->ipc->ports[i].name, name); host->ipc->ports[i].handler = handler;
        host->ipc->ports[i].closer = closer; host->ipc->ports[i].user = user;
        host->ipc->ports[i].connections = 0; result = 0; break;
    }
    pthread_mutex_unlock(&host->mutex);
    return result;
}
int dm_host_ipc_register_port(dm_host *host, const char *name, dm_ipc_handler handler, void *user) {
    return dm_host_ipc_register_port_full(host, name, handler, NULL, user);
}
int dm_host_ipc_unregister_port(dm_host *host, const char *name) {
    if (!host || !name) return EINVAL;
    pthread_mutex_lock(&host->mutex);
    int result = ENOENT;
    ipc_port *port = host->ipc ? find_port(host->ipc, name) : NULL;
    if (port) {
        if (port->connections) result = EBUSY;
        else { memset(port, 0, sizeof *port); result = 0; }
    }
    pthread_mutex_unlock(&host->mutex);
    return result;
}

// 会话槽位只在 !busy 时释放；关闭中的会话由完成路径回收。
// Session slots free only when not busy; closing sessions are reclaimed by the completion path.
static void free_session(struct dm_ipc *ipc, unsigned index) {
    ipc_session *session = &ipc->sessions[index];
    if (!session->active) return;
    ipc_port *port = port_at(ipc, session->port);
    if (port && port->closer) port->closer(port->user, session->client);
    if (port && port->connections) port->connections--;
    memset(session, 0, sizeof *session);
}
void dm_host_ipc_session_closed_locked(dm_host *host, unsigned index) {
    if (!host->ipc || index >= SESSIONS) return;
    ipc_session *session = &host->ipc->sessions[index];
    if (!session->active) return;
    session->closing = 1;
    if (!session->busy) free_session(host->ipc, index);
}

// 原生 SVC 0x1f 只消费 x1（名称）并返回 w0/w1；x0 是库调用方保存的 C 出参指针，内核与运行时都不解引用它。
// Native SVC 0x1f consumes only x1 (name) and returns w0/w1; x0 is the caller stub's saved C out-pointer and is never dereferenced here.
static uint64_t connect(dm_host *host, uint64_t *r) {
    uintptr_t name_pointer = (uintptr_t)r[1];
    if (!name_pointer) return DM_SVC_INVALID_ADDRESS;
    // 真实 Horizon 进程总有 sm:；延迟安装仅注册已实现的 RegisterClient 端点，不伪造其他服务。
    // A real Horizon process always has sm:; lazy installation registers only the implemented RegisterClient endpoint, never fakes other services.
    int install = dm_sm_install(host);
    if (install) return DM_SVC_OUT_OF_MEMORY;
    install = dm_lm_install(host);
    if (install) return DM_SVC_OUT_OF_MEMORY;
    pthread_mutex_lock(&host->mutex);
    if (!host->ipc) host->ipc = calloc(1, sizeof *host->ipc);
    uint64_t result = DM_SVC_OUT_OF_MEMORY;
    if (!host->ipc) goto done;
    char name[NAME_BYTES];
    dm_memory_pin pin = {0};
    if (dm_host_memory_read_pin_locked(host, &pin, name_pointer, NAME_BYTES)) { result = DM_SVC_INVALID_ADDRESS; goto done; }
    memcpy(name, (const void *)name_pointer, NAME_BYTES);
    dm_host_memory_unpin_locked(host, &pin);
    size_t length = strnlen(name, NAME_BYTES);
    if (!length || length >= NAME_BYTES) { result = DM_SVC_INVALID_SIZE; goto done; }
    for (size_t i = 0; i < length; i++) if ((unsigned char)name[i] < 0x20 || (unsigned char)name[i] > 0x7e) {
        result = DM_SVC_INVALID_SIZE; goto done;
    }
    ipc_port *port = find_port(host->ipc, name);
    if (!port) { result = DM_SVC_INVALID_HANDLE; goto done; }
    unsigned slot = SESSIONS;
    for (unsigned i = 0; i < SESSIONS; i++) if (!host->ipc->sessions[i].active) { slot = i; break; }
    if (slot == SESSIONS) { result = DM_SVC_OUT_OF_RESOURCE; goto done; }
    uint32_t handle;
    if (dm_host_handle_alloc_locked(host, 4, slot, &handle)) { result = DM_SVC_OUT_OF_RESOURCE; goto done; }
    host->ipc->sessions[slot] = (ipc_session){.port = (unsigned)(port - host->ipc->ports), .client = handle, .active = 1};
    port->connections++;
    r[1] = handle;
    result = DM_SVC_OK;
done:
    pthread_mutex_unlock(&host->mutex);
    if (result != DM_SVC_OK) r[1] = 0;
    return result;
}

// 仅接受无描述符的 CMIF 请求；其余形式明确不支持，绝不跟随客体描述符。
// Accept only descriptor-free CMIF requests; every other form stays explicitly unsupported and no guest descriptor is followed.
// request_info 已在上方定义。 / request_info is defined above.：type4/version0（无特殊头）与 type6/version1 + send-PID。其余一律明确不支持。
// Accept only the two forms proven from SDK dataflow: type4/version0 (no special header) and type6/version1 + send-PID. Everything else is explicitly unsupported.
static int parse_request(const uint8_t *request, request_info *info) {
    uint32_t head, meta;
    memcpy(&head, request, 4);
    memcpy(&meta, request + 4, 4);
    uint32_t type = head & 0xffffu;
    if (type != 4 && type != 6 && type != 7) return EINVAL;
    if ((head >> 16) != 0) return EINVAL;                 // 无静态/缓冲/交换描述符。 / no static, buffer or exchange descriptors.
    uint32_t data_words = meta & 0x3ffu;
    if (((meta >> 10) & 0xfu) || ((meta >> 14) & 0x3fu) || ((meta >> 20) & 0x7ffu)) return EINVAL; // 保留字段全零。 / reserved fields zero.
    if (type == 7 && data_words != 8u) return EINVAL;     // 控制消息固定 8 数据字。 / control messages are fixed at 8 data words.
    int special = (int)((meta >> 31) & 1u);
    if (type == 7 && special) return EINVAL;              // 控制消息无特殊头。 / control messages have no special header.
    size_t data_start = HIPC_WORDS * 4u;
    uint32_t flags = 0;
    if (special) {
        uint32_t header;
        memcpy(&header, request + data_start, 4);
        uint32_t send_pid = header & 1u, copy = (header >> 1) & 0xfu, move = (header >> 5) & 0xfu;
        if ((header >> 9) != 0 || copy || move) return EINVAL; // 句柄传输未证明，明确拒绝。 / handle transfer is unproven and rejected.
        if (!send_pid) return EINVAL;
        data_start += 4u + 8u;                            // 特殊头 + u64 PID 槽。 / special header + u64 PID slot.
        flags = DM_IPC_FLAG_SEND_PID;
    }
    size_t data_bytes = (size_t)data_words * 4u;
    if (data_start > DM_IPC_BUFFER_SIZE || data_bytes > DM_IPC_BUFFER_SIZE - data_start) return EINVAL;
    size_t cmif = (data_start + 15u) & ~(size_t)15u;
    if (cmif + CMIF_IN_HEADER > data_start + data_bytes) return EINVAL;
    uint32_t header[4];
    memcpy(header, request + cmif, sizeof header);
    uint32_t want_version = (type == 6 || type == 7) ? 1u : 0u;
    if (header[0] != CMIF_IN_MAGIC || header[1] != want_version) return EINVAL;
    info->command = header[2]; info->token = header[3]; info->flags = flags;
    info->payload_offset = cmif + CMIF_IN_HEADER;
    info->payload_size = data_start + data_bytes - info->payload_offset;
    info->control = (type == 7);
    return 0;
}
// 为已登记端口创建真实会话；SM GetServiceHandle 使用，句柄由传输层负责事务发布或回滚。
// Create a real session for a registered port; used by SM GetServiceHandle, with the transport publishing or rolling the handle back.
int dm_host_ipc_create_session(dm_host *host, const char *name, uint32_t *out_handle) {
    if (!host || !name || !out_handle) return EINVAL;
    pthread_mutex_lock(&host->mutex);
    int result = ENOENT;
    if (!host->ipc) goto done;
    ipc_port *port = find_port(host->ipc, name);
    if (!port) goto done;
    unsigned slot = SESSIONS;
    for (unsigned i = 0; i < SESSIONS; i++) if (!host->ipc->sessions[i].active) { slot = i; break; }
    if (slot == SESSIONS) { result = ENOSPC; goto done; }
    uint32_t handle;
    if (dm_host_handle_alloc_locked(host, 4, slot, &handle)) { result = ENOSPC; goto done; }
    host->ipc->sessions[slot] = (ipc_session){.port = (unsigned)(port - host->ipc->ports), .client = handle, .active = 1};
    port->connections++;
    *out_handle = handle;
    result = 0;
done:
    pthread_mutex_unlock(&host->mutex);
    return result;
}
void dm_host_ipc_discard_handle(dm_host *host, uint32_t handle) {
    if (!host || !handle) return;
    pthread_mutex_lock(&host->mutex);
    unsigned slot = 0, index = 0;
    if (dm_host_handle_lookup_locked(host, handle, 4, &slot, &index) == 0) {
        dm_host_handle_clear_locked(host, slot, handle);
        dm_host_ipc_session_closed_locked(host, index);
    }
    pthread_mutex_unlock(&host->mutex);
}
// 控制消息：QueryPointerBufferSize(3) 与 ConvertSessionToDomain(0)。仅类型7/SFCI v1/8数据字。
// Control messages: QueryPointerBufferSize(3) and ConvertSessionToDomain(0), type7/SFCI v1/8 data words only.
static int control_request(dm_host *host, struct dm_ipc *ipc, ipc_session *session, const request_info *info,
                           uint8_t *response, size_t *reply_size, uint32_t *data_words,
                           uint32_t *pending_domain, int *publish_domain) {
    (void)host; (void)ipc;
    size_t size;
    if (info->command == 3) {
        uint16_t zero = 0;                    // 本宿主没有指针缓冲分配。 / this host has no pointer-buffer allocation.
        memcpy(response + CMIF_PAYLOAD_OFFSET, &zero, sizeof zero);
        size = sizeof zero;
    } else if (info->command == 0) {
        uint32_t id;
        pthread_mutex_lock(&host->mutex);
        if (session->domain_retired) { pthread_mutex_unlock(&host->mutex); return ENOTSUP; }
        id = session->domain_id;
        if (!id) {
            id = session->next_object_id + 1u;
            if (!id) { pthread_mutex_unlock(&host->mutex); return ENOSPC; }
            session->next_object_id = id;
            *pending_domain = id;
            *publish_domain = 1;
        }
        pthread_mutex_unlock(&host->mutex);
        memcpy(response + CMIF_PAYLOAD_OFFSET, &id, sizeof id);
        size = sizeof id;
    } else {
        return ENOTSUP;
    }
    *reply_size = size;
    *data_words = (uint32_t)((CMIF_PAYLOAD_OFFSET - DATA_WORDS_BIAS + size + 3u) / 4u);
    return 0;
}
int dm_host_ipc_object_alloc(dm_host *host, uint64_t client, uint32_t *out_id) {
    if (!host || !out_id) return EINVAL;
    pthread_mutex_lock(&host->mutex);
    struct dm_ipc *ipc = host->ipc; unsigned slot = 0, index = 0;
    int rc = EINVAL;
    if (!ipc || dm_host_handle_lookup_locked(host, (uint32_t)client, 4, &slot, &index)) goto done;
    ipc_session *s = &ipc->sessions[index];
    if (!s->active || s->closing || !s->domain_id || s->domain_retired) goto done;
    unsigned free_slot = 8;
    for (unsigned i = 0; i < 8; i++) if (!s->children[i]) { free_slot = i; break; }
    if (free_slot == 8) { rc = ENOSPC; goto done; }
    uint32_t id = s->next_object_id + 1u;
    if (!id) { rc = ENOSPC; goto done; }
    s->next_object_id = id; s->children[free_slot] = id; *out_id = id; rc = 0;
done:
    pthread_mutex_unlock(&host->mutex);
    return rc;
}
int dm_host_ipc_child_live_locked(dm_host *host, uint64_t client, uint32_t id) {
    if (!host || !host->ipc || !id) return 0;
    struct dm_ipc *ipc = host->ipc;
    unsigned slot = 0, index = 0;
    if (dm_host_handle_lookup_locked(host, (uint32_t)client, 4, &slot, &index)) return 0;
    ipc_session *s = &ipc->sessions[index];
    if (!s->active || s->closing || !s->domain_id) return 0;
    for (unsigned i = 0; i < 8; ++i) if (s->children[i] == id) return 1;
    return 0;
}
int dm_host_ipc_port_matches_locked(dm_host *host, const char *name, dm_ipc_handler handler,
                                    dm_ipc_closer closer, void *user) {
    if (!host || !host->ipc || !name || !handler) return 0;
    ipc_port *p = find_port(host->ipc, name);
    return p && p->handler == handler && p->closer == closer && p->user == user;
}
int dm_host_ipc_mark_lm_port(dm_host *host, dm_ipc_handler handler, dm_ipc_closer closer, void *user) {
    if (!host) return EINVAL;
    pthread_mutex_lock(&host->mutex);
    int ok = dm_host_ipc_port_matches_locked(host, "lm", handler, closer, user);
    if (ok) find_port(host->ipc, "lm")->lm_backend = 1;
    pthread_mutex_unlock(&host->mutex);
    return ok ? 0 : EINVAL;
}
static int dm_host_ipc_object_retire(dm_host *host, uint64_t client, uint32_t id) {
    pthread_mutex_lock(&host->mutex);
    struct dm_ipc *ipc = host->ipc; unsigned slot = 0, index = 0;
    int rc = EINVAL;
    if (!ipc || dm_host_handle_lookup_locked(host, (uint32_t)client, 4, &slot, &index)) goto done;
    ipc_session *s = &ipc->sessions[index];
    if (!s->active || s->closing || !s->domain_id) goto done;
    for (unsigned i = 0; i < 8; i++) if (s->children[i] == id) { s->children[i] = 0; rc = 0; break; }
done:
    pthread_mutex_unlock(&host->mutex);
    return rc;
}
static int dm_host_ipc_root_retire(dm_host *host, uint64_t client, uint32_t id) {
    pthread_mutex_lock(&host->mutex);
    struct dm_ipc *ipc = host->ipc; unsigned slot = 0, index = 0;
    int rc = EINVAL;
    if (!ipc || dm_host_handle_lookup_locked(host, (uint32_t)client, 4, &slot, &index)) goto done;
    ipc_session *s = &ipc->sessions[index];
    if (!s->active || s->closing || !s->domain_id || s->domain_retired || s->domain_id != id) goto done;
    s->domain_retired = 1; rc = 0;
done:
    pthread_mutex_unlock(&host->mutex);
    return rc;
}
int dm_host_ipc_register_domain_port_full(dm_host *host, const char *name, dm_ipc_handler handler, dm_ipc_closer closer, void *user) {
    int rc = dm_host_ipc_register_port_full(host, name, handler, closer, user);
    if (rc) return rc;
    pthread_mutex_lock(&host->mutex);
    if (host->ipc) { ipc_port *p = find_port(host->ipc, name); if (p) p->domain_capable = 1; }
    pthread_mutex_unlock(&host->mutex);
    return 0;
}
int dm_host_ipc_register_domain_port(dm_host *host, const char *name, dm_ipc_handler handler, void *user) {
    return dm_host_ipc_register_domain_port_full(host, name, handler, NULL, user);
}
static uint64_t send_sync(dm_host *host, uint64_t *r) {
    dm_guest_context *context = dm_guest_current();
    if (!context || context->host != host) return DM_SVC_BAD_STATE;
    uintptr_t tls = (uintptr_t)context->slots[DM_SLOT_TLS];
    if (!tls || tls % 16) return DM_SVC_INVALID_ADDRESS;
    uint8_t request[DM_IPC_BUFFER_SIZE], response[DM_IPC_BUFFER_SIZE];
    uint32_t handle = (uint32_t)r[0], out_handle = 0;
    dm_memory_pin pin = {0};
    pthread_mutex_lock(&host->mutex);
    uint64_t result = DM_SVC_INVALID_HANDLE;
    unsigned slot = 0, index = 0;
    if (dm_host_handle_lookup_locked(host, handle, 4, &slot, &index)) goto done;
    struct dm_ipc *ipc = host->ipc;
    if (!ipc || index >= SESSIONS) goto done;
    ipc_session *session = &ipc->sessions[index];
    ipc_port *port = port_at(ipc, session->port);
    if (!session->active || session->closing || !port || !port->handler) goto done;
    if (session->busy) { result = DM_SVC_BAD_STATE; goto done; }
    if (dm_host_memory_read_pin_locked(host, &pin, tls, DM_IPC_BUFFER_SIZE)) { result = DM_SVC_INVALID_ADDRESS; goto done; }
    memcpy(request, (const void *)tls, sizeof request);
    dm_host_memory_unpin_locked(host, &pin);
    session->busy = 1;
    dm_ipc_handler handler = port->handler;
    void *user = port->user;
    pthread_mutex_unlock(&host->mutex);

    size_t payload_size = 0, reply_size = 0;
    uint32_t command = 0, token = 0;
    request_info info = {0};
    int dom = parse_domain_request(request, &info);
    result = dom ? DM_SVC_OK : (parse_request(request, &info) ? DM_SVC_NOT_IMPLEMENTED : DM_SVC_OK);
    uint32_t data_words = 0, pending_domain = 0;
    int publish_domain = 0, domain_reply = 0;
    size_t custom_extent = 0;
    uint32_t new_child = 0;
    if (result == DM_SVC_OK) {
        payload_size = info.payload_size; command = info.command; token = info.token;
        // 成功路径使用确定性零缓冲；处理器只写负载区，线格式填充保持 0。
        // The success path uses a deterministic zero buffer; the handler writes only the payload and wire padding stays zero.
        memset(response, 0, sizeof response);
        if (info.domain) {
            if (info.domain == 2) {
                int is_root = 0;
                pthread_mutex_lock(&host->mutex);
                is_root = session->domain_id == info.object_id;
                pthread_mutex_unlock(&host->mutex);
                int retire = is_root ? dm_host_ipc_root_retire(host, handle, info.object_id)
                                     : dm_host_ipc_object_retire(host, handle, info.object_id);
                if (retire) result = DM_SVC_NOT_IMPLEMENTED;
                else {
                    uint32_t hdr[2] = {0, 0}; memcpy(response, hdr, sizeof hdr);
                    domain_reply = 1; custom_extent = 8; data_words = 0;
                }
            } else if (info.domain == 3) {
                if (!port->lm_backend) result = DM_SVC_NOT_IMPLEMENTED;
                else {
                    int handled = dm_lm_handle_log(host, handle, request, sizeof request,
                                                   response, &reply_size);
                    if (handled == -2) result = DM_SVC_OUT_OF_RESOURCE;
                    else if (handled == -3) result = DM_SVC_INVALID_ADDRESS;
                    else if (handled || reply_size != 48) result = DM_SVC_NOT_IMPLEMENTED;
                    else {
                        domain_reply = 1; custom_extent = 48; data_words = 10;
                    }
                }
            } else {
                int valid = 0;
                pthread_mutex_lock(&host->mutex);
                valid = session->domain_id != 0 && session->domain_id == info.object_id &&
                        !session->domain_retired && port->domain_capable;
                pthread_mutex_unlock(&host->mutex);
                if (!valid) result = DM_SVC_NOT_IMPLEMENTED;
                else {
                    int handled = handler(user, handle, info.flags, info.command, request + info.payload_offset, info.payload_size,
                                           response + 48, 4, &reply_size, &out_handle);
                    if (handled == -2) result = DM_SVC_OUT_OF_RESOURCE;
                    else if (handled || reply_size != 4 || out_handle) {
                        if (!handled) memcpy(&new_child, response + 48, 4);
                        result = DM_SVC_NOT_IMPLEMENTED;
                    }
                    else {
                        uint32_t hdr[2] = {0, 11}; memcpy(response, hdr, sizeof hdr);
                        uint32_t dout[4] = {1, 0, 0, 0}; memcpy(response + 16, dout, sizeof dout);
                        uint32_t cmif[4] = {CMIF_OUT_MAGIC, 0, 0, token}; memcpy(response + 32, cmif, sizeof cmif);
                        memcpy(&new_child, response + 48, 4);
                        domain_reply = 1; custom_extent = 52; data_words = 11;
                    }
                }
            }
        } else if (info.control) {
            int control = control_request(host, ipc, session, &info, response, &reply_size, &data_words,
                                          &pending_domain, &publish_domain);
            if (control == ENOSPC || control == ENOTSUP) result = control == ENOSPC ? DM_SVC_OUT_OF_RESOURCE : DM_SVC_NOT_IMPLEMENTED;
            else if (control) result = DM_SVC_NOT_IMPLEMENTED;
            if (result == DM_SVC_OK && data_words > MAX_DATA_WORDS) result = DM_SVC_INVALID_SIZE;
        } else {
            // 已转换会话只接受域路由；在实现域请求前，普通请求必须显式失败且不得触达原处理器。
            // A converted session accepts only domain routing; until implemented, ordinary requests must fail explicitly without the original handler.
            int converted = 0;
            pthread_mutex_lock(&host->mutex);
            converted = session->domain_id != 0;
            pthread_mutex_unlock(&host->mutex);
            if (converted) {
                result = DM_SVC_NOT_IMPLEMENTED;
            } else {
                int handled = handler(user, handle, info.flags, command, request + info.payload_offset, payload_size,
                                       response + CMIF_PAYLOAD_OFFSET, DM_IPC_PAYLOAD_MAX, &reply_size, &out_handle);
                if (handled == -2) result = DM_SVC_OUT_OF_RESOURCE;
                else if (handled || reply_size > DM_IPC_PAYLOAD_MAX) result = DM_SVC_NOT_IMPLEMENTED;
                else {
                    // 带 move-handle 的响应：HipcHeader + 特殊头 + 句柄，数据字从 +16 开始。
                    // A move-handle response places the special header and handle after the HipcHeader, with data words at +16.
                    data_words = out_handle
                        ? (uint32_t)((CMIF_DATA_OFFSET + reply_size + 3) / 4)
                        : (uint32_t)((CMIF_PAYLOAD_OFFSET - DATA_WORDS_BIAS + reply_size + 3) / 4);
                    if (data_words > MAX_DATA_WORDS) result = DM_SVC_INVALID_SIZE;
                }
            }
        }
    }
    if (result == DM_SVC_OK && !domain_reply) {
        uint32_t cmif[4] = {CMIF_OUT_MAGIC, 0, 0, token};
        if (out_handle) {
            uint32_t header[2] = {0, data_words | 0x80000000u};
            uint32_t special = 1u << 5; // num_move_handles = 1
            memcpy(response, header, sizeof header);
            memcpy(response + 8, &special, 4);
            memcpy(response + 12, &out_handle, 4);
        } else {
            uint32_t header[2] = {0, data_words}; // 客户端不检查响应 type；明确置零。 / clients do not inspect response type; explicitly zero.
            memcpy(response, header, sizeof header);
        }
        memcpy(response + CMIF_DATA_OFFSET, cmif, sizeof cmif);
    }
    pthread_mutex_lock(&host->mutex);
    if (result == DM_SVC_OK && out_handle) {
        unsigned moved_slot = 0, moved_index = 0;
        if (dm_host_handle_lookup_locked(host, out_handle, 4, &moved_slot, &moved_index)) result = DM_SVC_BAD_STATE;
    }
    if (result == DM_SVC_OK) {
        if (info.domain == 3 && !dm_lm_has_staged_locked(host, handle, info.object_id))
            result = DM_SVC_BAD_STATE;
    }
    if (result == DM_SVC_OK) {
        size_t extent = domain_reply ? custom_extent : ((out_handle ? 16u : DATA_WORDS_BIAS) + data_words * 4u);
        if (!session->active || session->closing || port_at(ipc, session->port) != port) result = DM_SVC_BAD_STATE;
        else if (dm_host_memory_pin_locked(host, &pin, tls, DM_IPC_BUFFER_SIZE)) result = DM_SVC_INVALID_ADDRESS;
        else {
            memcpy((void *)tls, response, extent);
            if (result == DM_SVC_OK && info.domain == 3)
                dm_lm_commit_log(host, handle, info.object_id);
            if (publish_domain) session->domain_id = pending_domain;
            dm_host_memory_unpin_locked(host, &pin);
        }
    }
    session->busy = 0;
    if (info.domain == 3 && result != DM_SVC_OK)
        dm_lm_abort_log(host, handle, info.object_id);
    if (session->closing) free_session(ipc, index);
    // 依据 Horizon[2]：SendSyncRequest 出错会关闭会话；这里同步清除句柄并使旧值失效。
    // Per Horizon[2], a SendSyncRequest error closes the session; clear the handle and invalidate stale values.
    if (result != DM_SVC_OK) {
        if (new_child) {
            for (unsigned i = 0; i < 8; i++) if (session->children[i] == new_child) session->children[i] = 0;
        }
        if (out_handle) {
            unsigned moved_index = 0;
            if (dm_host_handle_lookup_locked(host, out_handle, 4, &slot, &moved_index) == 0) {
                dm_host_handle_clear_locked(host, slot, out_handle);
                dm_host_ipc_session_closed_locked(host, moved_index);
            }
        }
        unsigned closed_index = 0;
        if (dm_host_handle_lookup_locked(host, handle, 4, &slot, &closed_index) == 0) {
            dm_host_handle_clear_locked(host, slot, handle);
            dm_host_ipc_session_closed_locked(host, closed_index);
        }
        if (session->active && session->closing && !session->busy) free_session(ipc, index);
    }
done:
    pthread_mutex_unlock(&host->mutex);
    return result;
}

uint64_t dm_host_ipc_svc(dm_host *host, unsigned number, uint64_t *registers) {
    if (number == 0x1f) return connect(host, registers);
    if (number == 0x21) return send_sync(host, registers);
    return DM_SVC_NOT_IMPLEMENTED;
}
