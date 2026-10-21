// 宿主私有状态：堆、文件／同步句柄及附着线程计数共享锁；依据 host.h 的生命周期约定。
// Private host state: heap, file/sync handles and attached-thread count share a lock; follows host.h lifecycle rules.
#pragma once
#include "host.h"
#include "ipc.h"
#include <pthread.h>
typedef struct{uintptr_t base;size_t size;}dm_host_region;
typedef struct dm_memory_pin {
    uintptr_t address;size_t size;
    struct dm_memory_pin *next;
    int active;
} dm_memory_pin;
typedef struct { uintptr_t destination,source;size_t size; } dm_memory_alias;
typedef struct dm_accounted_local {uintptr_t base;size_t size;struct dm_accounted_local *next;int retired;} dm_accounted_local;
typedef struct {uint64_t base,size;uint32_t state,attributes,permission,ipc_refs,device_refs,padding;} dm_memory_info;
struct dm_host {
    pthread_mutex_t mutex;
    uint8_t *heap;
    size_t capacity, committed, attached;
    size_t stack_capacity,alias_capacity,arena_capacity,reservation_size,page_size;
    int backing_fd;
    dm_memory_alias aliases[64];size_t alias_count;
    dm_memory_pin *memory_pins;
    struct dm_accounting *accounting;
    uint64_t entropy[4];
    struct dm_romfs *romfs;
    struct dm_sync *sync;
    struct dm_ipc *ipc;
    struct dm_lm *lm;
    struct dm_sm_state *sm;
    struct dm_arbiter *arbiter;
    struct dm_mutex_state *mutex_state;
    dm_sdk_thread_abi sdk_thread_abi;
    dm_host_region writable[16],executable[16];
    size_t writable_count,executable_count;
    struct{uintptr_t base;size_t size;int in_use;}thread_stacks[64];
    uint64_t next_thread_id;
};
uint64_t dm_host_set_heap(dm_host *host, uint64_t registers[31]);
void dm_host_romfs_close(dm_host *host);
uint64_t dm_host_sync_svc(dm_host *host,unsigned number,uint64_t registers[31]);
// IPC 会话句柄复用同一 128 槽代际表；调用方必须持有 host->mutex。
// IPC session handles reuse the same 128-slot generation table; callers must hold host->mutex.
#define DM_SVC_OUT_OF_RESOURCE 0xce01u
int dm_host_handle_alloc_locked(dm_host *host,unsigned kind,unsigned object,uint32_t *out);
int dm_host_handle_lookup_locked(dm_host *host,uint32_t handle,unsigned kind,unsigned *slot,unsigned *object);
void dm_host_handle_clear_locked(dm_host *host,unsigned slot,uint32_t handle);
uint64_t dm_host_ipc_svc(dm_host *host,unsigned number,uint64_t registers[31]);
void dm_host_ipc_session_closed_locked(dm_host *host,unsigned index);
// 为已登记端口创建真实会话（内部 helper，锁定宿主）；返回 errno 风格错误。
// Create a real session for a registered port (internal helper, takes the host lock); returns errno-style errors.
int dm_host_ipc_create_session(dm_host *host,const char *name,uint32_t *out_handle);
void dm_host_ipc_discard_handle(dm_host *host,uint32_t handle);
int dm_host_sync_busy(dm_host *host);
size_t dm_host_threads_pinned(dm_host *host);
size_t dm_host_sync_waiters(dm_host *host);
void dm_host_sync_close(dm_host *host);
uint64_t dm_host_arbiter_svc(dm_host *host,unsigned number,uint64_t registers[31]);
size_t dm_host_arbiter_waiters(dm_host *host);
void dm_host_arbiter_close(dm_host *host);
int dm_host_writable(dm_host *host,uintptr_t address,size_t size);
int dm_host_memory_pin_locked(dm_host *host,dm_memory_pin *pin,uintptr_t address,size_t size);
int dm_host_memory_read_pin_locked(dm_host *host,dm_memory_pin *pin,uintptr_t address,size_t size);
int dm_host_memory_read_pin_owned_locked(dm_host *host,dm_memory_pin *pin,uintptr_t address,size_t size);
void dm_host_memory_unpin_locked(dm_host *host,dm_memory_pin *pin);
int dm_host_memory_pinned(dm_host *host,uintptr_t address,size_t size);
uint64_t dm_host_memory_svc(dm_host *host,unsigned number,uint64_t registers[31]);
void dm_guest_bind_thread(uint32_t handle,uint64_t id,void *state);
int dm_guest_attach_reserved(dm_host *host,const dm_gate_table *gates,uint64_t id);
void dm_host_thread_context_detach(dm_guest_context *context);
int dm_host_thread_current(dm_host *host,unsigned *object,uint32_t *handle,int *priority);
int dm_host_thread_retain(dm_host *host,uint32_t handle,unsigned *object);
int dm_host_thread_priority(dm_host *host,unsigned object);
void dm_host_thread_ref(dm_host *host,unsigned object);
void dm_host_thread_unref(dm_host *host,unsigned object);
uint64_t dm_host_mutex_svc(dm_host *host,unsigned number,uint64_t registers[31]);
size_t dm_host_mutex_waiters(dm_host *host);
void dm_host_mutex_close(dm_host *host);
void dm_host_mutex_owner_ended(dm_host *host,unsigned object);

size_t dm_host_accounting_used(dm_host *host);
int dm_host_accounting_fits(dm_host *host,size_t additional);
size_t dm_host_accounting_budget(dm_host *host);
int dm_host_accounting_access(dm_host *host,uintptr_t address,size_t size,int permission);
int dm_host_accounting_query(dm_host *host,uintptr_t address,dm_memory_info *info);
int dm_host_accounting_local_create(dm_host *host,dm_accounted_local **local,size_t bytes);
void dm_host_accounting_local_release_locked(dm_host *host,dm_accounted_local *local);
void dm_host_accounting_close(dm_host *host);
int dm_host_stack_writable(dm_host *host,uintptr_t address,size_t size);
void dm_host_accounting_reap_locked(dm_host *host);
dm_host *dm_host_create_arena(size_t heap,size_t stack,size_t alias,size_t arena);
