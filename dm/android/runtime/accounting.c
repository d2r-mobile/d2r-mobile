// 自有客体后备的预算与生命周期；不使用 RSS，也不把借用权限登记当作物理用量。
// Budget and lifecycle for owned guest backing; never use RSS or treat borrowed permission registration as physical usage.
#include "internal.h"
#include "accounting.h"
#include "bridge.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

typedef struct dm_accounting {
    size_t budget,module_bytes,local_bytes;
    dm_module_mappings modules;
    uint8_t *permissions[16];
    dm_accounted_local *locals;
} dm_accounting;
static uintptr_t arena_base(dm_host*h){return (uintptr_t)h->heap+h->capacity+h->stack_capacity+h->alias_capacity;}
static int contains(uintptr_t base,size_t size,uintptr_t address,size_t length){
    return address>=base&&address-base<=size&&length<=size-(address-base);
}
static int overlap(uintptr_t base,size_t size,uintptr_t address,size_t length){
    return size&&length&&base<address+length&&address<base+size;
}
int dm_host_accounting_busy(dm_host*h){return h&&h->accounting&&(h->accounting->locals||h->accounting->local_bytes);}
size_t dm_host_accounting_used(dm_host*h){return h->committed+h->accounting->module_bytes+h->accounting->local_bytes;}
size_t dm_host_accounting_budget(dm_host*h){return h->accounting->budget;}
int dm_host_accounting_fits(dm_host*h,size_t additional){
    return !h->accounting||additional<=h->accounting->budget-dm_host_accounting_used(h);
}
dm_host *dm_host_create_accounted_process(size_t heap,size_t stack,size_t alias,size_t budget){
    dm_host*h=dm_host_create_arena(heap,stack,alias,budget);if(!h)return NULL;
    if(!budget||budget%h->page_size||budget%4096){dm_host_destroy(h);errno=EINVAL;return NULL;}
    h->accounting=calloc(1,sizeof(*h->accounting));
    if(!h->accounting){dm_host_destroy(h);errno=ENOMEM;return NULL;}
    h->accounting->budget=budget;return h;
}
int dm_host_accounting_access(dm_host*h,uintptr_t address,size_t size,int permission){
    if(!h->accounting||size>UINTPTR_MAX-address)return 0;
    dm_accounting*a=h->accounting;
    for(size_t i=0;i<a->modules.count;i++){
        dm_image_mapping m=a->modules.mappings[i];uintptr_t base=(uintptr_t)m.base;
        if(!permission&&overlap(base,m.size,address,size))return 1;
        if(permission&&size&&contains(base,m.size,address,size)){
            for(size_t p=(address-base)/h->page_size;p<=(address-base+size-1)/h->page_size;p++)
                if((a->permissions[i][p]&permission)!=permission)return 0;
            return 1;
        }
    }
    for(dm_accounted_local*l=a->locals;l;l=l->next){
        if(!permission&&overlap(l->base,l->size,address,size))return 1;
        if(permission&&!l->retired&&!(permission&PROT_EXEC)&&contains(l->base,l->size,address,size))return 1;
    }
    return 0;
}
int dm_host_accounting_query(dm_host*h,uintptr_t address,dm_memory_info*info){
    if(!h->accounting)return 0;dm_accounting*a=h->accounting;
    for(size_t i=0;i<a->modules.count;i++){
        dm_image_mapping m=a->modules.mappings[i];uintptr_t base=(uintptr_t)m.base;
        if(!contains(base,m.size,address,1))continue;
        size_t first=(address-base)/h->page_size,last=first+1;uint8_t mode=a->permissions[i][first];
        while(first&&a->permissions[i][first-1]==mode)first--;
        while(last<m.size/h->page_size&&a->permissions[i][last]==mode)last++;
        info->base=base+first*h->page_size;info->size=(last-first)*h->page_size;
        info->permission=(mode&PROT_READ?1u:0u)|(mode&PROT_WRITE?2u:0u)|(mode&PROT_EXEC?4u:0u);
        info->state=mode?(mode&PROT_WRITE?4:3):0;return 1;
    }
    for(dm_accounted_local*l=a->locals;l;l=l->next)if(contains(l->base,l->size,address,1)){
        info->base=l->base;info->size=l->size;info->state=0xc;info->permission=3;return 1;
    }
    uintptr_t base=arena_base(h);
    if(contains(base,h->arena_capacity,address,1)){
        uintptr_t lo=base,hi=base+h->arena_capacity;
        for(size_t i=0;i<a->modules.count;i++){
            uintptr_t end=(uintptr_t)a->modules.mappings[i].base+a->modules.mappings[i].size;
            if(end<=address&&end>lo)lo=end;
        }
        for(dm_accounted_local*l=a->locals;l;l=l->next){
            if(l->base>address&&l->base<hi)hi=l->base;
            if(l->base+l->size<=address&&l->base+l->size>lo)lo=l->base+l->size;
        }
        info->base=lo;info->size=hi-lo;return 1;
    }
    return 0;
}
int dm_host_map_modules(dm_host*h,const dm_nso_image*images,const dm_image_code*codes,size_t count,
    const dm_symbol_override*imports,size_t import_count,const dm_module_mappings**out){
    if(!h||!h->accounting||!images||!out||!count||count>16)return EINVAL;
    uint8_t*permissions[16]={0};size_t sizes[16]={0},total=0;int result=0;
    for(size_t i=0;i<count;i++){
        const dm_image_code*c=codes&&(codes[i].offset||codes[i].size||codes[i].bytes)?&codes[i]:NULL;
        size_t page;dm_image_result r=dm_image_layout(&images[i],c,&sizes[i],&page,&permissions[i]);
        if(r!=DM_IMAGE_OK||page!=h->page_size||sizes[i]>SIZE_MAX-total){result=r==DM_IMAGE_MEMORY?ENOMEM:EINVAL;goto done;}
        total+=sizes[i];
    }
    pthread_mutex_lock(&h->mutex);dm_accounting*a=h->accounting;
    if(h->attached||h->next_thread_id||a->locals||a->modules.count||dm_host_sync_busy(h))result=EBUSY;
    else if(!dm_host_accounting_fits(h,total))result=ENOMEM;
    dm_module_mappings mapped={0};dm_host_region writable[16],executable[16];size_t nw=0,nx=0;
    dm_image_mapping reserved[16];size_t offset=0;
    if(!result)for(size_t i=0;i<count;i++){reserved[i]=(dm_image_mapping){(uint8_t*)arena_base(h)+offset,sizes[i]};offset+=sizes[i];}
    if(!result&&dm_modules_commit_imports(images,codes,count,imports,import_count,reserved,&mapped)!=DM_MODULE_OK)result=EINVAL;
    for(size_t i=0;i<count&&!result;i++){
        dm_image_mapping m=mapped.mappings[i];uintptr_t base=(uintptr_t)m.base;
        if(m.size!=sizes[i]||m.size>UINTPTR_MAX-base||!contains(arena_base(h),h->arena_capacity,base,m.size)){result=EINVAL;break;}
        for(size_t j=0;j<i;j++)if(overlap(base,m.size,(uintptr_t)mapped.mappings[j].base,mapped.mappings[j].size))result=EINVAL;
        for(size_t first=0;first<m.size/h->page_size&&!result;){
            size_t last=first+1;uint8_t mode=permissions[i][first];
            while(last<m.size/h->page_size&&permissions[i][last]==mode)last++;
            dm_host_region region={base+first*h->page_size,(last-first)*h->page_size};
            if(mode&PROT_WRITE){if(nw==16)result=ENOSPC;else writable[nw++]=region;}
            if(mode&PROT_EXEC){if(nx==16)result=ENOSPC;else executable[nx++]=region;}
            first=last;
        }
    }
    if(!result){
        a->modules=mapped;a->module_bytes=total;
        for(size_t i=0;i<count;i++){a->permissions[i]=permissions[i];permissions[i]=NULL;}
        memcpy(h->writable,writable,nw*sizeof(*writable));h->writable_count=nw;
        memcpy(h->executable,executable,nx*sizeof(*executable));h->executable_count=nx;
        *out=&a->modules;
    }else if(mapped.mappings){
        for(size_t i=0;i<mapped.count;i++)if(dm_image_reset_reserved(&mapped.mappings[i]))abort();
        free(mapped.mappings);
    }
    pthread_mutex_unlock(&h->mutex);
done:
    for(size_t i=0;i<count;i++)free(permissions[i]);return result;
}
int dm_host_accounting_local_create(dm_host*h,dm_accounted_local**out,size_t bytes){
    if(!h->accounting||!bytes)return EINVAL;
    if(bytes>SIZE_MAX-(h->page_size-1))return ENOMEM;
    size_t size=(bytes+h->page_size-1)/h->page_size*h->page_size;
    dm_accounted_local*l=calloc(1,sizeof(*l));if(!l)return ENOMEM;
    pthread_mutex_lock(&h->mutex);int result=0;
    if(!dm_host_accounting_fits(h,size))result=ENOMEM;
    else{
        size_t slots=(h->arena_capacity-h->accounting->module_bytes)/size;
        if(slots>DM_BRIDGE_SLOTS)slots=DM_BRIDGE_SLOTS;
        uintptr_t base=arena_base(h)+h->accounting->module_bytes;size_t slot=0;
        for(;slot<slots;slot++,base+=size){
            int occupied=0;for(dm_accounted_local*other=h->accounting->locals;other;other=other->next)if(overlap(base,size,other->base,other->size)){occupied=1;break;}
            if(!occupied)break;
        }
        if(slot==slots||mprotect((void*)base,size,PROT_READ|PROT_WRITE))result=ENOMEM;
        else{*l=(dm_accounted_local){.base=base,.size=size,.next=h->accounting->locals};h->accounting->locals=l;h->accounting->local_bytes+=size;*out=l;}
    }
    pthread_mutex_unlock(&h->mutex);if(result)free(l);return result;
}
void dm_host_accounting_local_release_locked(dm_host*h,dm_accounted_local*l){
    if(!l)return;l->retired=1;dm_host_accounting_reap_locked(h);
}
void dm_host_accounting_reap_locked(dm_host*h){
    if(!h->accounting)return;dm_accounted_local**p=&h->accounting->locals;
    while(*p){dm_accounted_local*l=*p;
        if(!l->retired||dm_host_memory_pinned(h,l->base,l->size)){p=&l->next;continue;}
        dm_image_mapping span={(uint8_t*)l->base,l->size};if(dm_image_reset_reserved(&span))abort();
        *p=l->next;h->accounting->local_bytes-=l->size;free(l);
    }
}

void dm_host_accounting_close(dm_host*h){
    dm_accounting*a=h->accounting;if(!a)return;
    if(a->locals||a->local_bytes)abort();
    free(a->modules.mappings);
    for(size_t i=0;i<16;i++)free(a->permissions[i]);free(a);h->accounting=NULL;
}
