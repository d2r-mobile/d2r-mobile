// 本地 sm: 端点：RegisterClient 真实登记，GetServiceHandle 返回已登记端口的真实会话句柄。
// Local sm: endpoint: real RegisterClient registration; GetServiceHandle returns a real session handle for a registered port.
#include "sm.h"
#include "internal.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define SM_CLIENTS 32u
#define SM_CMD_REGISTER_CLIENT 0u
#define SM_CMD_GET_SERVICE_HANDLE 1u
#define SM_NAME_BYTES 8u

typedef struct { uint64_t client; int active; } sm_client;
struct dm_sm_state {
    dm_host *host;
    pthread_mutex_t lock;
    sm_client clients[SM_CLIENTS];
};

static void sm_closer(void *user, uint64_t client) {
    struct dm_sm_state *state = user;
    pthread_mutex_lock(&state->lock);
    for (unsigned i = 0; i < SM_CLIENTS; i++) if (state->clients[i].active && state->clients[i].client == client) state->clients[i].active = 0;
    pthread_mutex_unlock(&state->lock);
}

// 命令 0：真实登记；命令 1：把 8 字节服务名解析为已登记端口的真实会话句柄（由传输层事务发布）。
// Command 0 registers; command 1 maps an 8-byte service name to a real session handle of a registered port (published transactionally by the transport).
static int sm_handler(void *user, uint64_t client, unsigned flags, uint32_t command,
                      const uint8_t *request, size_t size, uint8_t *reply, size_t capacity,
                      size_t *reply_size, uint32_t *out_handle) {
    struct dm_sm_state *state = user;
    (void)reply; (void)capacity;
    if (!client) return -1;
    if (command == SM_CMD_REGISTER_CLIENT) {
        if (!(flags & DM_IPC_FLAG_SEND_PID) || size < 8) return -1;
        int result = -1;
        pthread_mutex_lock(&state->lock);
        sm_client *slot = NULL;
        int duplicate = 0;
        for (unsigned i = 0; i < SM_CLIENTS; i++) {
            if (state->clients[i].active && state->clients[i].client == client) { duplicate = 1; break; }
            if (!state->clients[i].active && !slot) slot = &state->clients[i];
        }
        if (!duplicate && slot) { slot->client = client; slot->active = 1; *reply_size = 0; result = 0; }
        pthread_mutex_unlock(&state->lock);
        return result;
    }
    if (command == SM_CMD_GET_SERVICE_HANDLE) {
        if (size < SM_NAME_BYTES) return -1;
        // 只有已通过 RegisterClient 登记的存活会话才能获取服务；先查登记，释放状态锁后再调用宿主 API。
        // Only a live session registered through RegisterClient may acquire services; check registration first and release the state lock before host APIs.
        int registered = 0;
        pthread_mutex_lock(&state->lock);
        for (unsigned i = 0; i < SM_CLIENTS; i++)
            if (state->clients[i].active && state->clients[i].client == client) { registered = 1; break; }
        pthread_mutex_unlock(&state->lock);
        if (!registered) return -1;
        char name[SM_NAME_BYTES + 1];
        memcpy(name, request, SM_NAME_BYTES);
        name[SM_NAME_BYTES] = 0;
        size_t length = strnlen(name, SM_NAME_BYTES);
        if (!length) return -1;
        for (size_t i = 0; i < length; i++) if ((unsigned char)name[i] < 0x20 || (unsigned char)name[i] > 0x7e) return -1;
        uint32_t handle = 0;
        int rc = dm_host_ipc_create_session(state->host, name, &handle);
        if (rc == ENOSPC) return -2;
        if (rc || !handle) return -1;
        *out_handle = handle;
        *reply_size = 0;
        return 0;
    }
    return -1;
}

int dm_sm_install(dm_host *host) {
    if (!host) return EINVAL;
    pthread_mutex_lock(&host->mutex);
    if (!host->sm) {
        struct dm_sm_state *state = calloc(1, sizeof *state);
        if (!state) { pthread_mutex_unlock(&host->mutex); return ENOMEM; }
        int rc = pthread_mutex_init(&state->lock, NULL);
        if (rc) { free(state); pthread_mutex_unlock(&host->mutex); return rc; }
        state->host = host;
        host->sm = state;
    }
    struct dm_sm_state *state = host->sm;
    pthread_mutex_unlock(&host->mutex);
    int rc = dm_host_ipc_register_port_full(host, "sm:", sm_handler, sm_closer, state);
    return rc == EEXIST ? 0 : rc;
}
// 仅在宿主销毁（无会话、无附着线程）时调用；调用方持有 host->mutex。
// Only called at host destruction (no sessions, no attached threads); the caller holds host->mutex.
void dm_sm_close(dm_host *host) {
    if (!host->sm) return;
    struct dm_sm_state *state = host->sm;
    host->sm = NULL;
    pthread_mutex_destroy(&state->lock);
    free(state);
}
size_t dm_sm_registered_clients(dm_host *host) {
    if (!host) return 0;
    pthread_mutex_lock(&host->mutex);
    struct dm_sm_state *state = host->sm;
    size_t count = 0;
    if (state) {
        pthread_mutex_lock(&state->lock);
        for (unsigned i = 0; i < SM_CLIENTS; i++) if (state->clients[i].active) count++;
        pthread_mutex_unlock(&state->lock);
    }
    pthread_mutex_unlock(&host->mutex);
    return count;
}
