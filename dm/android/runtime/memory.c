// 自有堆页的栈别名与查询；依据公开 Horizon SVC 和 Mesosphere 页组／保护语义。
// Owned heap-page stack aliases and queries; follows public Horizon SVC and Mesosphere page-group/protection semantics.
#include "internal.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

static int overlap(uintptr_t a,size_t n,uintptr_t b,size_t m){
    return n&&m&&a<b+m&&b<a+n;
}
static int contains(uintptr_t base,size_t size,uintptr_t address,size_t length){
    return address>=base&&address-base<=size&&length<=size-(address-base);
}
int dm_host_memory_pinned(dm_host*h,uintptr_t address,size_t size){
    for(dm_memory_pin*p=h->memory_pins;p;p=p->next)if(overlap(address,size,p->address,p->size))return 1;
    return 0;
}
static int pin_memory(dm_host*h,dm_memory_pin*p,uintptr_t address,size_t size,int read,int require_owned){
    if(p->active||size>UINTPTR_MAX-address||(!address&&size))return EINVAL;
    if(!size)return 0;
    int owned=overlap(address,size,(uintptr_t)h->heap,h->reservation_size);
    for(size_t i=0;i<h->writable_count;i++)owned|=overlap(address,size,h->writable[i].base,h->writable[i].size);
    owned|=dm_host_accounting_access(h,address,size,0);
    int readable=dm_host_writable(h,address,size)||dm_host_accounting_access(h,address,size,read?PROT_READ:PROT_WRITE);
    for(size_t i=0;i<h->executable_count;i++){
        owned|=overlap(address,size,h->executable[i].base,h->executable[i].size);
        if(read&&contains(h->executable[i].base,h->executable[i].size,address,size))readable=1;
    }
    if(!owned)return (h->accounting||require_owned)?EINVAL:0;
    if(!readable)return EINVAL;
    *p=(dm_memory_pin){address,size,h->memory_pins,1};h->memory_pins=p;return 0;
}
int dm_host_memory_pin_locked(dm_host*h,dm_memory_pin*p,uintptr_t address,size_t size){return pin_memory(h,p,address,size,0,0);}
int dm_host_memory_read_pin_locked(dm_host*h,dm_memory_pin*p,uintptr_t address,size_t size){return pin_memory(h,p,address,size,1,0);}
int dm_host_memory_read_pin_owned_locked(dm_host*h,dm_memory_pin*p,uintptr_t address,size_t size){return pin_memory(h,p,address,size,1,1);}
void dm_host_memory_unpin_locked(dm_host*h,dm_memory_pin*p){
    if(!p->active)return;
    dm_memory_pin**link=&h->memory_pins;while(*link&&*link!=p)link=&(*link)->next;
    if(*link)*link=p->next;
    memset(p,0,sizeof(*p));dm_host_accounting_reap_locked(h);
}
static int busy(dm_host*h,uintptr_t address,size_t size){
    if(dm_host_arbiter_waiters(h)||dm_host_mutex_waiters(h)||dm_host_memory_pinned(h,address,size))return 1;
    for(size_t i=0;i<64;i++)if(h->thread_stacks[i].in_use&&
        overlap(address,size,h->thread_stacks[i].base,h->thread_stacks[i].size))return 1;
    return 0;
}
static uint64_t alias_memory(dm_host*h,unsigned number,uint64_t*r){
    uintptr_t destination=(uintptr_t)r[0],source=(uintptr_t)r[1];size_t size=(size_t)r[2];
    if(!h->stack_capacity)return DM_SVC_NOT_IMPLEMENTED;
    if(destination%4096||source%4096||destination%h->page_size||source%h->page_size)return DM_SVC_INVALID_ADDRESS;
    if(!size||size%4096||size%h->page_size)return DM_SVC_INVALID_SIZE;
    if(size>UINTPTR_MAX-destination||size>UINTPTR_MAX-source)return DM_SVC_INVALID_MEMORY_STATE;
    uintptr_t heap=(uintptr_t)h->heap,stack=heap+h->capacity;
    if(!contains(stack,h->stack_capacity,destination,size))return DM_SVC_INVALID_MEMORY_REGION;
    if(!contains(heap,h->committed,source,size))return DM_SVC_INVALID_MEMORY_STATE;
    if(busy(h,source,size)||busy(h,destination,size))return DM_SVC_BAD_STATE;
    if(number==0x04){
        for(size_t i=0;i<h->alias_count;i++)if(overlap(source,size,h->aliases[i].source,h->aliases[i].size)||
            overlap(destination,size,h->aliases[i].destination,h->aliases[i].size))return DM_SVC_INVALID_MEMORY_STATE;
        if(h->alias_count==64)return DM_SVC_OUT_OF_MEMORY;
        if(mprotect((void*)source,size,PROT_NONE))return DM_SVC_OUT_OF_MEMORY;
        if(mmap((void*)destination,size,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_FIXED,h->backing_fd,(off_t)(source-heap))==MAP_FAILED){
            if(mprotect((void*)source,size,PROT_READ|PROT_WRITE))abort();
            return DM_SVC_OUT_OF_MEMORY;
        }
        h->aliases[h->alias_count++]=(dm_memory_alias){destination,source,size};return 0;
    }
    for(size_t i=0;i<h->alias_count;i++){
        dm_memory_alias a=h->aliases[i];if(a.destination!=destination||a.source!=source||a.size!=size)continue;
        if(mprotect((void*)source,size,PROT_READ|PROT_WRITE))return DM_SVC_OUT_OF_MEMORY;
        if(mmap((void*)destination,size,PROT_NONE,MAP_PRIVATE|MAP_ANON|MAP_FIXED,-1,0)==MAP_FAILED){
            if(mprotect((void*)source,size,PROT_NONE))abort();
            return DM_SVC_OUT_OF_MEMORY;
        }
        h->aliases[i]=h->aliases[--h->alias_count];return 0;
    }
    return DM_SVC_INVALID_MEMORY_STATE;
}
typedef dm_memory_info memory_info;
_Static_assert(sizeof(memory_info)==40,"Horizon MemoryInfo size");
static void boundary(uintptr_t address,uintptr_t base,size_t size,uintptr_t*lo,uintptr_t*hi){
    if(!size)return;
    uintptr_t end=base+size;
    if(base<=address&&base>*lo)*lo=base;
    if(base>address&&base<*hi)*hi=base;
    if(end<=address&&end>*lo)*lo=end;
    if(end>address&&end<*hi)*hi=end;
}
static uint64_t query_memory(dm_host*h,uint64_t*r){
    uintptr_t address=(uintptr_t)r[2],lo=0,hi=0;memory_info info={0};
    if(!dm_host_writable(h,(uintptr_t)r[0],sizeof(info))&&!dm_host_accounting_access(h,(uintptr_t)r[0],sizeof(info),PROT_WRITE))return DM_SVC_INVALID_ADDRESS;
    uintptr_t heap=(uintptr_t)h->heap;
    if(dm_host_accounting_query(h,address,&info)){
        lo=info.base;hi=lo+info.size;
    }else if(contains(heap,h->reservation_size,address,1)){
        lo=heap;hi=heap+h->reservation_size;
        if(contains(heap,h->committed,address,1)){info.state=5;info.permission=3;}
        boundary(address,heap,h->committed,&lo,&hi);
        // 新布局保留各预留区边界；旧布局维持原有空闲区合并语义。
        // Preserve reservation boundaries in the new layout, retaining legacy free-region coalescing.
        if(h->alias_capacity){
            boundary(address,heap,h->capacity,&lo,&hi);
            boundary(address,heap+h->capacity,h->stack_capacity,&lo,&hi);
            boundary(address,heap+h->capacity+h->stack_capacity,h->alias_capacity,&lo,&hi);
        }
        for(size_t i=0;i<h->alias_count;i++){
            dm_memory_alias a=h->aliases[i];
            boundary(address,a.source,a.size,&lo,&hi);boundary(address,a.destination,a.size,&lo,&hi);
            if(contains(a.source,a.size,address,1)){info.state=5;info.permission=0;info.attributes=1;}
            if(contains(a.destination,a.size,address,1)){info.state=0xb;info.permission=3;}
        }
    }else{
        for(size_t i=0;i<h->writable_count;i++)if(contains(h->writable[i].base,h->writable[i].size,address,1)){
            lo=h->writable[i].base;hi=lo+h->writable[i].size;info.state=4;info.permission=3;break;
        }
        for(size_t i=0;i<h->executable_count;i++)if(contains(h->executable[i].base,h->executable[i].size,address,1)){
            lo=h->executable[i].base;hi=lo+h->executable[i].size;info.state=3;info.permission=5;break;
        }
    }
    if(!hi)return DM_SVC_INVALID_ADDRESS;
    for(size_t i=0;i<64;i++){
        boundary(address,h->thread_stacks[i].base,h->thread_stacks[i].size,&lo,&hi);
        if(info.permission==3&&contains(h->thread_stacks[i].base,h->thread_stacks[i].size,address,1))info.state=0xb;
    }
    info.base=lo;info.size=hi-lo;memcpy((void*)(uintptr_t)r[0],&info,sizeof(info));
    r[1]=0;return 0;
}
static uint64_t get_info(dm_host*h,uint64_t*r){
    uint32_t id=(uint32_t)r[1],handle=(uint32_t)r[2];uint64_t value;
    if(!h->stack_capacity)return DM_SVC_NOT_IMPLEMENTED;
    if(id==11){
        if(handle)return DM_SVC_INVALID_HANDLE;
        if(r[3]>3)return DM_SVC_INVALID_COMBINATION;
        value=h->entropy[r[3]];
    }else{
        if(r[3])return DM_SVC_INVALID_COMBINATION;
        if(handle!=0xffff8001u)return DM_SVC_INVALID_HANDLE;
        switch(id){
        case 2:
            if(!h->alias_capacity)return DM_SVC_NOT_IMPLEMENTED;
            value=(uintptr_t)h->heap+h->capacity+h->stack_capacity;break;
        case 3:
            if(!h->alias_capacity)return DM_SVC_NOT_IMPLEMENTED;
            value=h->alias_capacity;break;
        case 16:
            if(!h->alias_capacity)return DM_SVC_NOT_IMPLEMENTED;
            value=0;break;
        case 21:case 22:
            if(!h->accounting)return DM_SVC_NOT_IMPLEMENTED;
            value=id==21?dm_host_accounting_budget(h):dm_host_accounting_used(h);break;
        case 4:case 12:value=(uintptr_t)h->heap;break;
        case 5:value=h->capacity;break;
        case 13:value=h->reservation_size;break;
        case 14:value=(uintptr_t)h->heap+h->capacity;break;
        case 15:value=h->stack_capacity;break;
        default:return DM_SVC_NOT_IMPLEMENTED;
        }
    }
    r[1]=value;return 0;
}
uint64_t dm_host_memory_svc(dm_host*h,unsigned number,uint64_t*r){
    pthread_mutex_lock(&h->mutex);uint64_t result;
    switch(number){
    case 0x04:case 0x05:result=alias_memory(h,number,r);break;
    case 0x06:result=query_memory(h,r);break;
    case 0x29:result=get_info(h,r);break;
    default:result=DM_SVC_NOT_IMPLEMENTED;break;
    }
    pthread_mutex_unlock(&h->mutex);return result;
}
