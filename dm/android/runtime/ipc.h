// 本地命名端口与同步 IPC 传输；依据 Horizon SVC 0x1f/0x21 与 CMIF 原始数据子集。
// Local named-port and synchronous IPC transport; based on Horizon SVC 0x1f/0x21 and the CMIF raw-data subset.
#pragma once
#include "host.h"
#include <stddef.h>
#include <stdint.h>

#define DM_IPC_BUFFER_SIZE 0x100u
#define DM_IPC_PORT_NAME_MAX 31u
#define DM_IPC_PAYLOAD_MAX (DM_IPC_BUFFER_SIZE - 32u - 16u)

// 处理器在宿主锁之外运行，只能看到有界副本；返回 0 才发布回复，非零结果不写回任何客体内存。
// Handlers run outside the host lock and see only bounded copies; a zero return publishes the reply, nonzero writes no guest memory.
// client 为发起请求的代际会话句柄；flags 记录由运行时校验过的 HIPC 特殊头语义（客体 PID 字节永不可信）。
// client is the originating generation session handle; flags carry HIPC special-header semantics validated by the runtime (guest PID bytes are never trusted).
#define DM_IPC_FLAG_SEND_PID 1u
typedef int (*dm_ipc_handler)(void *user, uint64_t client, unsigned flags, uint32_t command,
                              const uint8_t *request, size_t request_size,
                              uint8_t *reply, size_t reply_capacity, size_t *reply_size,
                              uint32_t *out_handle);

// 端口由宿主／测试显式登记；未登记名称的连接明确失败，不伪造服务成功。
// Ports are registered explicitly by the host/tests; connecting to an unregistered name fails explicitly, never fakes success.
int dm_host_ipc_register_port(dm_host *host, const char *name, dm_ipc_handler handler, void *user);
// 会话关闭回调（在宿主锁内、会话槽释放前）用于注销该客户端的真实状态；不得回调宿主 SVC。
// Session-close callback (under the host lock, before the slot is freed) retires the client's real state; it must not call host SVCs.
typedef void (*dm_ipc_closer)(void *user, uint64_t client);
int dm_host_ipc_register_port_full(dm_host *host, const char *name, dm_ipc_handler handler, dm_ipc_closer closer, void *user);
int dm_host_ipc_register_domain_port(dm_host *host, const char *name, dm_ipc_handler handler, void *user);
int dm_host_ipc_register_domain_port_full(dm_host *host, const char *name, dm_ipc_handler handler, dm_ipc_closer closer, void *user);
int dm_host_ipc_object_alloc(dm_host *host, uint64_t client, uint32_t *out_id);
// 调用方必须持有 host->mutex；仅活动会话的 children[] 命中，根/已退休 ID 不算。
// Caller must hold host->mutex; only a live session's children[] match; root/retired ids do not.
int dm_host_ipc_child_live_locked(dm_host *host, uint64_t client, uint32_t id);
int dm_host_ipc_port_matches_locked(dm_host *host, const char *name, dm_ipc_handler handler,
                                    dm_ipc_closer closer, void *user);
int dm_host_ipc_mark_lm_port(dm_host *host, dm_ipc_handler handler, dm_ipc_closer closer, void *user);
// 仍有会话时拒绝注销；名称可复用仅在全部会话关闭后。
// Refuses unregistration while sessions remain; the name becomes reusable only after every session closes.
int dm_host_ipc_unregister_port(dm_host *host, const char *name);
size_t dm_host_ipc_open_sessions(dm_host *host);
int dm_host_ipc_busy(dm_host *host);
void dm_host_ipc_close(dm_host *host);
