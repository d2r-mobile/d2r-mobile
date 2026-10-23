// PR15宿主池标志登记的有界只读查询；调用者持互斥锁，未知/重叠/溢出均不证明CPU_NO_ACCESS。
// Bounded PR15 host pool-flags registry; callers hold a mutex, and unknown/overlapping/overflowed ranges never prove CPU_NO_ACCESS.
#pragma once
#include <stdint.h>
#include <stddef.h>
enum { NVN_POOL_FLAGS_REGISTRY_MAX=4096 };
typedef struct { uintptr_t address; uint64_t bytes; int flags; const void *owner; } nvn_pool_flags_record;
typedef struct { nvn_pool_flags_record entries[NVN_POOL_FLAGS_REGISTRY_MAX]; unsigned count; int incomplete; } nvn_pool_flags_registry;

static inline int nvn_pool_flags_range_valid(uintptr_t address,uint64_t bytes) {
    return bytes && bytes<=UINTPTR_MAX-address;
}
static inline void nvn_pool_flags_registry_del(nvn_pool_flags_registry *table,const void *owner) {
    for(unsigned i=0;i<table->count;) {
        if(table->entries[i].owner==owner)table->entries[i]=table->entries[--table->count];
        else ++i;
    }
}
static inline int nvn_pool_flags_registry_add(nvn_pool_flags_registry *table,const void *owner,const void *mem,uint64_t bytes,int flags) {
    if(!owner || !mem || !nvn_pool_flags_range_valid((uintptr_t)mem,bytes)) {table->incomplete=1;return 0;}
    nvn_pool_flags_registry_del(table,owner);
    if(table->count==NVN_POOL_FLAGS_REGISTRY_MAX) {table->incomplete=1;return 0;}
    table->entries[table->count++]=(nvn_pool_flags_record){(uintptr_t)mem,bytes,flags,owner};return 1;
}
static inline int nvn_pool_flags_registry_get(const nvn_pool_flags_registry *table,const void *mem,size_t bytes) {
    uintptr_t address=(uintptr_t)mem;
    if(table->incomplete || !nvn_pool_flags_range_valid(address,bytes))return -1;
    int match=-1;
    for(unsigned i=0;i<table->count;++i) {
        const nvn_pool_flags_record *record=&table->entries[i];
        if(address<record->address+record->bytes && record->address<address+bytes) {
            if(match>=0 || address<record->address || address-record->address>record->bytes || bytes>record->bytes-(address-record->address))return -1;
            match=(int)i;
        }
    }
    return match<0?-1:table->entries[match].flags;
}
