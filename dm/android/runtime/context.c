// 客体 TLS／TLR、线程 ID 与 TCB 句柄；退出时只释放原始分配，依据上游 thread.c／hle_thread.c 的 8192/0x200/0x400 布局。
// Guest TLS/TLR, thread IDs and TCB handles; free only original allocations at exit, based on upstream thread.c/hle_thread.c's 8192/0x200/0x400 layout.
#include "internal.h"
#include "bridge.h"
#include <errno.h>
#include <stdalign.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    dm_guest_context context;
    dm_bridge_sanitizers sanitizers;
    dm_accounted_local *local;
    uint8_t *tls,*tlr;
    alignas(16) uint8_t storage[];
} owned_context;
static pthread_key_t key;
static pthread_once_t once = PTHREAD_ONCE_INIT;
static int key_error;

static void release(void *value) {
    owned_context *owned = value;
    dm_host *host = owned->context.host;
    dm_bridge_detach(&owned->context);
    pthread_mutex_lock(&host->mutex);
    dm_host_thread_context_detach(&owned->context);
    dm_host_accounting_local_release_locked(host,owned->local);
    host->attached--; pthread_mutex_unlock(&host->mutex);
    free(owned);
}
static void make_key(void) { key_error = pthread_key_create(&key, release); }

dm_guest_context *dm_guest_current(void) {
    if (pthread_once(&once, make_key) || key_error) return NULL;
    owned_context *owned = pthread_getspecific(key);
    return owned ? &owned->context : NULL;
}

int dm_gate_table_validate(const dm_gate_table *gates) {
    if (!gates || (gates->count && !gates->sites)) return EINVAL;
    for (size_t i = 0; i < gates->count; i++) {
        const dm_gate_site *s = &gates->sites[i];
        if (!s->return_address || (i && s->return_address <= gates->sites[i - 1].return_address)) return EINVAL;
        switch (s->kind) {
        case DM_GATE_SVC: if (s->argument > 0xffff) return EINVAL; break;
        case DM_GATE_TICKS: case DM_GATE_TLS_READ: case DM_GATE_TLS_WRITE: case DM_GATE_TLR_READ:
            if (s->argument > 31) return EINVAL; break;
        case DM_GATE_X18_ADDRESS: if (s->argument > 15) return EINVAL; break;
        default: return EINVAL;
        }
    }
    return 0;
}

static int attach_context(dm_host *host, const dm_gate_table *gates,uint64_t id) {
    if (!host || dm_gate_table_validate(gates)) return EINVAL;
    int result = pthread_once(&once, make_key);
    if (result || key_error) return result ? result : key_error;
    if (pthread_getspecific(key)) return EALREADY;
    owned_context *owned = calloc(1, sizeof *owned+(host->accounting?0:8192+0x600));
    if (!owned) return ENOMEM;
    if(host->accounting){
        result=dm_host_accounting_local_create(host,&owned->local,8192+0x600);
        if(result){free(owned);return result;}
        owned->tls=(uint8_t*)owned->local->base;
    }else owned->tls=owned->storage;
    owned->tlr=owned->tls+8192;
    owned->context.host = host; owned->context.gates = gates;
    owned->context.slots[DM_SLOT_TLS] = (uintptr_t)owned->tls;
    owned->context.slots[DM_SLOT_TLR] = (uintptr_t)owned->tlr;
    void *thread_object = owned->tlr + 0x200;
    memcpy(owned->tlr + 0x1f8, &thread_object, sizeof thread_object);
#if defined(__aarch64__)
    extern void dm_gate(void);
    owned->context.slots[DM_SLOT_GATE] = (uintptr_t)dm_gate;
#endif
    result=dm_bridge_attach(&owned->context,&owned->sanitizers);
    if(result){
        pthread_mutex_lock(&host->mutex);dm_host_accounting_local_release_locked(host,owned->local);pthread_mutex_unlock(&host->mutex);
        free(owned);return result;
    }
    pthread_mutex_lock(&host->mutex);
    if((!id&&host->next_thread_id==UINT64_MAX)||(id&&id>host->next_thread_id)){
        dm_host_accounting_local_release_locked(host,owned->local);
        pthread_mutex_unlock(&host->mutex);dm_bridge_detach(&owned->context);free(owned);return EOVERFLOW;
    }
    owned->context.thread_id=id?id:++host->next_thread_id;host->attached++;pthread_mutex_unlock(&host->mutex);
    result = pthread_setspecific(key, owned);
    if (result) release(owned);
    return result;
}
int dm_guest_attach(dm_host*host,const dm_gate_table*gates){return attach_context(host,gates,0);}
int dm_guest_attach_reserved(dm_host*host,const dm_gate_table*gates,uint64_t id){return id?attach_context(host,gates,id):EINVAL;}

void dm_guest_bind_thread(uint32_t handle,uint64_t id,void*state){
    owned_context*owned=(owned_context*)dm_guest_current();
    owned->context.thread_handle=handle;owned->context.thread_id=id;owned->context.thread_state=state;
    memcpy(owned->tlr+0x200+0x1b0,&handle,sizeof(handle));
    memcpy(owned->tlr+0x200+0x1b8,&id,sizeof(id));
}

int dm_guest_bootstrap_main(dm_host*h,const dm_gate_table*gates,uint32_t*handle){
    if(!handle)return EINVAL;
    int rc=dm_guest_attach(h,gates);if(rc)return rc;
    rc=dm_guest_register_current_thread(44,handle);
    if(rc&&dm_guest_detach())abort();return rc;
}

int dm_guest_detach(void) {
    dm_guest_context *context = dm_guest_current();
    if (!context) return ENOENT;
    int result = pthread_setspecific(key, NULL);
    if (!result) release((owned_context *)context);
    return result;
}
