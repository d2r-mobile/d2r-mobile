// 预留稳定堆地址并按 2 MiB 调整提交大小；依据 libnx SetHeapSize 与 POSIX 页保护，绝不创建 RWX 页。
// Reserve a stable heap and resize commitment in 2 MiB units; based on libnx SetHeapSize and POSIX page protection, never create RWX pages.
#include "internal.h"
#include "lm.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/random.h>
#include <sys/syscall.h>
#endif

#define HEAP_UNIT (2u * 1024u * 1024u)
static int backing_file(void) {
#if defined(__linux__)
    return (int)syscall(SYS_memfd_create,"dm-heap",1u /* MFD_CLOEXEC */);
#else
    const char *directory=getenv("TMPDIR");if(!directory||!directory[0]){errno=EINVAL;return -1;}
    char path[PATH_MAX];int length=snprintf(path,sizeof(path),"%s/dm-heap-XXXXXX",directory);
    if(length<0||(size_t)length>=sizeof(path)){errno=ENAMETOOLONG;return -1;}
    int fd=mkstemp(path);if(fd<0)return -1;
    int error=0;if(unlink(path))error=errno;
    if(!error&&fcntl(fd,F_SETFD,FD_CLOEXEC))error=errno;
    if(error){close(fd);errno=error;return -1;}return fd;
#endif
}
static int seed_entropy(uint64_t entropy[4]) {
#if defined(__linux__)
    size_t done=0;
    while(done<32){ssize_t n=getrandom((uint8_t*)entropy+done,32-done,0);
        if(n<0){if(errno==EINTR)continue;return -1;}if(!n){errno=EIO;return -1;}done+=(size_t)n;}
#else
    arc4random_buf(entropy,32);
#endif
    return 0;
}
static dm_host *create_host(size_t heap_capacity,size_t stack_capacity,size_t alias_capacity,size_t arena_capacity) {
    long page = sysconf(_SC_PAGESIZE);
    if (!heap_capacity || heap_capacity % HEAP_UNIT || page <= 0 || HEAP_UNIT % (size_t)page ||
        stack_capacity%(size_t)page||alias_capacity%(size_t)page||arena_capacity%(size_t)page||
        stack_capacity>SIZE_MAX-heap_capacity||alias_capacity>SIZE_MAX-heap_capacity-stack_capacity||
        arena_capacity>SIZE_MAX-heap_capacity-stack_capacity-alias_capacity||heap_capacity>INT64_MAX) {
        errno = EINVAL; return NULL;
    }
    size_t reservation_size=heap_capacity+stack_capacity+alias_capacity+arena_capacity;
    dm_host *host = calloc(1, sizeof *host);
    if (!host) return NULL;
    host->backing_fd=-1;host->page_size=(size_t)page;
    int error = pthread_mutex_init(&host->mutex, NULL);
    if (error) { free(host); errno = error; return NULL; }
    host->heap = mmap(NULL, reservation_size, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (host->heap == MAP_FAILED) {
        error = errno; pthread_mutex_destroy(&host->mutex); free(host); errno = error; return NULL;
    }
    if(reservation_size>UINTPTR_MAX-(uintptr_t)host->heap){
        munmap(host->heap,reservation_size);pthread_mutex_destroy(&host->mutex);free(host);errno=ENOMEM;return NULL;
    }
    host->capacity = heap_capacity;host->stack_capacity=stack_capacity;
    host->alias_capacity=alias_capacity;host->arena_capacity=arena_capacity;host->reservation_size=reservation_size;
    if(stack_capacity){
        host->backing_fd=backing_file();
        if(host->backing_fd<0||seed_entropy(host->entropy)||
           mmap(host->heap,heap_capacity,PROT_NONE,MAP_SHARED|MAP_FIXED,host->backing_fd,0)==MAP_FAILED){
            error=errno;if(host->backing_fd>=0)close(host->backing_fd);
            munmap(host->heap,reservation_size);pthread_mutex_destroy(&host->mutex);free(host);errno=error;return NULL;
        }
    }
    return host;
}
dm_host *dm_host_create(size_t heap_capacity){return create_host(heap_capacity,0,0,0);}
dm_host *dm_host_create_layout(size_t heap_capacity,size_t stack_capacity){
    if(!stack_capacity){errno=EINVAL;return NULL;}return create_host(heap_capacity,stack_capacity,0,0);
}

dm_host *dm_host_create_process_layout(size_t heap_capacity,size_t stack_capacity,size_t alias_capacity){
    if(!stack_capacity||!alias_capacity||stack_capacity%4096||alias_capacity%4096){errno=EINVAL;return NULL;}
    return create_host(heap_capacity,stack_capacity,alias_capacity,0);
}

dm_host *dm_host_create_arena(size_t heap,size_t stack,size_t alias,size_t arena){
    if(!stack||!alias||!arena||stack%4096||alias%4096||arena%4096){errno=EINVAL;return NULL;}
    return create_host(heap,stack,alias,arena);
}

int dm_host_configure_virtual_cores(dm_host *host, uint64_t mask, uint32_t preferred_core) {
    if (!host || !mask || preferred_core >= 64 || !(mask & (UINT64_C(1) << preferred_core))) return EINVAL;
    pthread_mutex_lock(&host->mutex);
    int busy = host->attached || dm_host_sync_busy(host);
    for (unsigned i = 0; !busy && i < 64; i++) if (host->thread_stacks[i].in_use) busy = 1;
    if (!busy) {
        host->virtual_core_mask = mask;
        host->virtual_core_preferred = preferred_core;
        host->virtual_cores_configured = 1;
    }
    pthread_mutex_unlock(&host->mutex);
    return busy ? EBUSY : 0;
}

int dm_host_destroy(dm_host *host) {
    if (!host) return EINVAL;
    pthread_mutex_lock(&host->mutex);
    if (host->attached||host->alias_count||host->memory_pins||dm_host_sync_busy(host)||dm_host_ipc_busy(host)||dm_host_arbiter_waiters(host)||dm_host_mutex_waiters(host)||dm_host_accounting_busy(host)) { pthread_mutex_unlock(&host->mutex); return EBUSY; }
    if (munmap(host->heap, host->reservation_size)) {
        int error = errno; pthread_mutex_unlock(&host->mutex); return error;
    }
    dm_host_accounting_close(host);
    dm_host_romfs_close(host);
    dm_host_mutex_close(host);
    dm_host_ipc_close(host);
    dm_lm_close(host);
    dm_host_sync_close(host);
    dm_host_arbiter_close(host);
    if(host->backing_fd>=0)close(host->backing_fd);
    pthread_mutex_unlock(&host->mutex);
    pthread_mutex_destroy(&host->mutex); free(host);
    return 0;
}

uint64_t dm_host_set_heap(dm_host *host, uint64_t registers[31]) {
    uint64_t wanted = registers[1];
    if (wanted % HEAP_UNIT) return DM_SVC_INVALID_SIZE;
    pthread_mutex_lock(&host->mutex);
    uint64_t result = DM_SVC_OK;
    if (wanted > host->capacity||(wanted>host->committed&&!dm_host_accounting_fits(host,(size_t)wanted-host->committed))) result = DM_SVC_OUT_OF_MEMORY;
    else if(wanted<host->committed&&(host->alias_count||dm_host_arbiter_waiters(host)||dm_host_threads_pinned(host)||dm_host_mutex_waiters(host)||
        dm_host_memory_pinned(host,(uintptr_t)host->heap+(size_t)wanted,host->committed-(size_t)wanted)))result=DM_SVC_BAD_STATE;
    else if (wanted != host->committed) {
        size_t first = wanted < host->committed ? (size_t)wanted : host->committed;
        size_t last = wanted > host->committed ? (size_t)wanted : host->committed;
        int mode = wanted > host->committed ? PROT_READ | PROT_WRITE : PROT_NONE;
        // 只重映射自己已预留的收缩区，保证重新提交时为零；不依赖平台各异的 madvise 提示语义。
        // Remap only our own reserved shrink range so recommit is zeroed; avoid platform-dependent madvise hints.
        int failed;
        if(host->stack_capacity){
            if(wanted>host->committed){
                failed=ftruncate(host->backing_fd,(off_t)wanted);
                if(!failed&&mprotect(host->heap+first,last-first,mode)){
                    failed=1;if(ftruncate(host->backing_fd,(off_t)host->committed))abort();
                }
            }else{
                failed=mprotect(host->heap+first,last-first,mode);
                if(!failed&&ftruncate(host->backing_fd,(off_t)wanted)){
                    failed=1;if(mprotect(host->heap+first,last-first,PROT_READ|PROT_WRITE))abort();
                }
            }
        }else failed = wanted < host->committed
            ? mmap(host->heap + first, last - first, PROT_NONE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0) == MAP_FAILED
            : mprotect(host->heap + first, last - first, mode);
        if (failed) result = DM_SVC_OUT_OF_MEMORY;
        else host->committed = (size_t)wanted;
    }
    if (!result) registers[1] = (uintptr_t)host->heap;
    pthread_mutex_unlock(&host->mutex);
    return result;
}

int dm_host_writable(dm_host*h,uintptr_t address,size_t size){
    if(size>UINTPTR_MAX-address)return 0;
    for(size_t i=0;i<h->alias_count;i++){
        dm_memory_alias a=h->aliases[i];
        if(address<a.source+a.size&&a.source<address+size)return 0;
        if(address>=a.destination&&address-a.destination<=a.size&&size<=a.size-(address-a.destination))return 1;
    }
    uintptr_t base=(uintptr_t)h->heap;
    if(address>=base&&address-base<=h->committed&&size<=h->committed-(address-base))return 1;
    for(size_t i=0;i<h->writable_count;i++){
        base=h->writable[i].base;
        if(address>=base&&address-base<=h->writable[i].size&&size<=h->writable[i].size-(address-base))return 1;
    }
    return 0;
}
// 严格模式的客体栈只来自已计费堆及其栈别名，不允许模块或 TLS 充当栈。
// Strict guest stacks come only from charged heap or its stack aliases, never modules or TLS.
int dm_host_stack_writable(dm_host*h,uintptr_t address,size_t size){
    if(!dm_host_writable(h,address,size))return 0;
    if(!h->accounting)return 1;
    uintptr_t base=(uintptr_t)h->heap;
    return address>=base&&address-base<=h->capacity+h->stack_capacity&&size<=h->capacity+h->stack_capacity-(address-base);
}
static int register_region(dm_host*h,void*memory,size_t size,int code){
    uintptr_t base=(uintptr_t)memory;
    if(!h||!base||!size||size>UINTPTR_MAX-base)return EINVAL;
    pthread_mutex_lock(&h->mutex);int rc=0;
    uintptr_t heap=(uintptr_t)h->heap;
    dm_host_region*regions=code?h->executable:h->writable;size_t*count=code?&h->executable_count:&h->writable_count;
    dm_host_region*others=code?h->writable:h->executable;size_t other_count=code?h->writable_count:h->executable_count;
    if(h->accounting)rc=EINVAL;
    else if(h->attached||dm_host_arbiter_waiters(h)||dm_host_sync_busy(h)||dm_host_mutex_waiters(h))rc=EBUSY;
    else if(base<heap+h->reservation_size&&heap<base+size)rc=EINVAL;
    else{
        for(size_t i=0;i<*count;i++)if(base<regions[i].base+regions[i].size&&regions[i].base<base+size){rc=EINVAL;break;}
        for(size_t i=0;i<other_count;i++)if(base<others[i].base+others[i].size&&others[i].base<base+size){rc=EINVAL;break;}
        if(!rc){if(*count==16)rc=ENOSPC;else regions[(*count)++]=(dm_host_region){base,size};}
    }
    pthread_mutex_unlock(&h->mutex);return rc;
}
int dm_host_register_writable(dm_host*h,void*base,size_t size){return register_region(h,base,size,0);}
int dm_host_register_executable(dm_host*h,void*base,size_t size){return register_region(h,base,size,1);}
int dm_host_register_thread_stack(dm_host*h,void*memory,size_t size){
    uintptr_t base=(uintptr_t)memory;if(!h||!base||base%16||size<dm_host_thread_stack_minimum()||size%16||size>UINTPTR_MAX-base)return EINVAL;
    pthread_mutex_lock(&h->mutex);int rc=0;size_t slot=64;
    if(!dm_host_stack_writable(h,base,size))rc=EINVAL;
    else{
        for(size_t i=0;i<64;i++){
            if(!h->thread_stacks[i].size){if(slot==64)slot=i;continue;}
            if(base<h->thread_stacks[i].base+h->thread_stacks[i].size&&h->thread_stacks[i].base<base+size){rc=EINVAL;break;}
        }
        if(!rc){if(slot==64)rc=ENOSPC;else{h->thread_stacks[slot].base=base;h->thread_stacks[slot].size=size;}}
    }
    pthread_mutex_unlock(&h->mutex);return rc;
}
int dm_host_unregister_thread_stack(dm_host*h,void*top){
    if(!h||!top)return EINVAL;pthread_mutex_lock(&h->mutex);int rc=ENOENT;
    for(size_t i=0;i<64;i++)if(h->thread_stacks[i].size&&h->thread_stacks[i].base+h->thread_stacks[i].size==(uintptr_t)top){
        if(h->thread_stacks[i].in_use)rc=EBUSY;else{memset(&h->thread_stacks[i],0,sizeof(h->thread_stacks[i]));rc=0;}break;
    }
    pthread_mutex_unlock(&h->mutex);return rc;
}
int dm_host_set_sdk_thread_abi(dm_host*h,const dm_sdk_thread_abi*a){
    if(!h||!a||!a->entry||a->entry%4||a->thread_type_size<8||a->thread_type_size>1048576||
       a->stack_base_offset%8||a->stack_size_offset%8||a->stack_base_offset==a->stack_size_offset||
       a->stack_base_offset>a->thread_type_size-8||a->stack_size_offset>a->thread_type_size-8||
       a->top_reserve<16||a->top_reserve%16||a->top_reserve>=65536)return EINVAL;
    pthread_mutex_lock(&h->mutex);int rc=EINVAL;
    if(h->attached||dm_host_sync_busy(h))rc=EBUSY;
    else for(size_t i=0;i<h->executable_count;i++){
        dm_host_region region=h->executable[i];
        if(a->entry>=region.base&&a->entry-region.base<=region.size&&4<=region.size-(a->entry-region.base)){
            h->sdk_thread_abi=*a;rc=0;break;
        }
    }
    pthread_mutex_unlock(&h->mutex);return rc;
}
