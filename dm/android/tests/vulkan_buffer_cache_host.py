"""Exercise actual buffer release bodies against owned Vulkan boundary shims.

The fixture tests memory isolation, reference lifetime and retained-memory budgets, not GPU work.
Pass production source, a build-directory C path, and optionally the C compiler.
"""
from pathlib import Path
import re
import subprocess
import sys
source=Path(sys.argv[1]).read_text()
def extract(name):
    matches=list(re.finditer(r'^static [^\n]+\b'+name+r'\([^\n]*\) \{',source,re.M))
    if len(matches)!=1: raise RuntimeError(f'{name}: expected exactly one production body')
    match=matches[0]
    start=match.start(); pos=source.index('{',match.start()); depth=1; end=pos+1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}'); end+=1
    return source[start:end]
prefix=r'''
#include <assert.h>
#include <stdint.h>
#include <stdatomic.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define die(...) abort()
typedef uintptr_t VkBuffer;
typedef uintptr_t VkDeviceMemory;
static int device;
static unsigned released,allocations,mappings,unmappings,memory_frees;
typedef uint64_t VkDeviceSize;
typedef struct { uint64_t size,alignment; uint32_t memoryTypeBits; } VkMemoryRequirements;
typedef struct { unsigned sType; const void *pNext; uint64_t allocationSize; uint32_t memoryTypeIndex; } VkMemoryAllocateInfo;
#define VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO 1
#define VK_WHOLE_SIZE UINT64_MAX
#define VK_CHECK(call) assert((call)==0)
#define LOG(...) ((void)printf(__VA_ARGS__), (void)puts(""))
static int vkAllocateMemory(int d,const VkMemoryAllocateInfo *info,void*a,uintptr_t *memory){
    (void)d;(void)a;assert(info->allocationSize==64u*4096);assert(info->memoryTypeIndex<2);
    *memory=(uintptr_t)calloc(1,info->allocationSize);assert(*memory);allocations++;return 0;
}
static int vkMapMemory(int d,uintptr_t memory,uint64_t offset,uint64_t size,unsigned flags,void **mapped){
    (void)d;assert(!offset&&size==VK_WHOLE_SIZE&&!flags);*mapped=(void*)memory;mappings++;return 0;
}
static int vkBindBufferMemory(int d,uintptr_t buffer,uintptr_t memory,uint64_t offset){
    (void)d;assert(buffer&&memory&&offset%4096==0&&offset<64u*4096);return 0;
}
static void vkUnmapMemory(int d,uintptr_t m){(void)d;(void)m;unmappings++;}
static void vkDestroyBuffer(int d,uintptr_t b,void *a){(void)d;(void)b;(void)a;released++;}
static void vkFreeMemory(int d,uintptr_t m,void*a){(void)d;(void)a;if(m>1)free((void*)m);memory_frees++;}
'''
start=source.index('typedef struct { _Atomic unsigned references;')
end=source.index('// 只读探针',start)
# Keep the real production types and cache accounting declarations.
prefix+=source[start:end]
# Baseline does not declare the new diagnostic accounting variable.
if 'buffer_cache_large_bytes' not in prefix: prefix+='\nstatic uint64_t buffer_cache_large_bytes;\n'
body=r'''
static vk_buffer fresh(uint64_t capacity);
static void clear_cache(void){
    while(buffer_cache){cached_buffer *entry=buffer_cache;buffer_cache_unlink(entry);buffer_release_backing(&entry->buffer);free(entry);}
    assert(!buffer_cache_tail&&!buffer_cache_bytes&&!buffer_cache_large_bytes);
}
static void check_links(void){
    cached_buffer *previous=NULL;uint64_t bytes=0;
    for(cached_buffer *e=buffer_cache;e;e=e->next){assert(e->prev==previous);previous=e;bytes+=e->buffer.capacity;}
    assert(previous==buffer_cache_tail&&bytes==buffer_cache_bytes);
}
static void memory_pages_test(void){
    VkMemoryRequirements req={4096,64,3};vk_buffer buffers[70]={0};
    for(unsigned i=0;i<70;i++){buffers[i]=(vk_buffer){.buffer=i+2,.capacity=4096,.size=80,.owner=calloc(1,sizeof(buffer_owner))};assert(buffers[i].owner);
        assert(buffer_memory_bind(&buffers[i],&req,0,0));memset(buffers[i].mapped,i+1,4096);}
    assert(allocations==2&&mappings==2&&buffer_memory_page_count==2&&buffer_memory_suballocations==70);
    for(unsigned i=0;i<70;i++)for(unsigned j=0;j<4096;j++)assert(((unsigned char*)buffers[i].mapped)[j]==i+1);
    buffer_memory_page *partial=buffers[64].owner->page;void *hole=buffers[64].mapped;
    buffer_memory_release(&buffers[64]);free(buffers[64].owner);buffers[64].owner=NULL;
    vk_buffer replacement={.buffer=100,.capacity=4096,.owner=calloc(1,sizeof(buffer_owner))};assert(replacement.owner);
    assert(buffer_memory_bind(&replacement,&req,0,0)&&replacement.mapped==hole&&replacement.owner->page==partial);
    memset(replacement.mapped,99,4096);assert(((unsigned char*)buffers[65].mapped)[0]==66);
    buffer_memory_release(&replacement);free(replacement.owner);
    buffer_memory_page *first=buffers[0].owner->page;
    buffer_memory_release(&buffers[0]);free(buffers[0].owner);assert(buffer_memory_page_count==2&&!memory_frees);
    assert(!(first->used&1)&&buffers[1].owner->page==first);
    for(unsigned i=1;i<70;i++)if(buffers[i].owner){buffer_memory_release(&buffers[i]);free(buffers[i].owner);}
    assert(!buffer_memory_pages&&!buffer_memory_page_count&&memory_frees==2&&unmappings==2);
    vk_buffer a={.buffer=90,.capacity=4096,.owner=calloc(1,sizeof(buffer_owner))},b={.buffer=91,.capacity=4096,.owner=calloc(1,sizeof(buffer_owner))};
    assert(buffer_memory_bind(&a,&req,0,0)&&buffer_memory_bind(&b,&req,1,0));
    assert(a.memory!=b.memory&&buffer_memory_page_count==2);
    buffer_memory_release(&a);buffer_memory_release(&b);free(a.owner);free(b.owner);
    assert(!buffer_memory_pages&&!buffer_memory_page_count&&allocations==4&&memory_frees==4);
    unsigned before=allocations;vk_buffer reject={.capacity=4096};
    assert(!buffer_memory_bind(&reject,&req,0,1));
    req.size=8192;assert(!buffer_memory_bind(&reject,&req,0,0));req.size=4096;
    req.alignment=8192;assert(!buffer_memory_bind(&reject,&req,0,0));req.alignment=0;assert(!buffer_memory_bind(&reject,&req,0,0));req.alignment=64;
    reject.capacity=8192;assert(!buffer_memory_bind(&reject,&req,0,0));
    buffer_memory_enabled=0;reject.capacity=4096;assert(!buffer_memory_bind(&reject,&req,0,0));buffer_memory_enabled=1;
    assert(allocations==before&&!buffer_memory_pages);
    vk_buffer held={.buffer=92,.capacity=4096,.size=80,.owner=calloc(1,sizeof(buffer_owner))};assert(held.owner);
    atomic_init(&held.owner->references,1);assert(buffer_memory_bind(&held,&req,0,0));active_buffer_bytes=4096;
    vk_buffer alias=buffer_borrow(held);buffer_destroy(&held);
    assert(!held.buffer&&buffer_memory_page_count==1&&atomic_load(&alias.owner->references)==1);
    buffer_destroy(&alias);assert(!alias.buffer&&buffer_memory_page_count==1&&!active_buffer_bytes&&buffer_cache_bytes==4096);
    for(unsigned i=0;i<8;i++){vk_buffer large=fresh(8u<<20);buffer_destroy(&large);check_links();}
    assert(!buffer_memory_page_count&&released==1&&buffer_cache_bytes==(64u<<20));
    clear_cache();released=0;
    buffer_memory_report(0);puts("actual buffer memory: disjoint slots, spill, partial/final release, memory type, dedicated/alignment fallback PASS");
}
static vk_buffer fresh(uint64_t capacity){
    buffer_owner *owner=calloc(1,sizeof *owner);assert(owner);atomic_init(&owner->references,1);
    active_buffer_bytes+=capacity;
    return (vk_buffer){.buffer=1,.memory=1,.capacity=capacity,.size=capacity,.owner=owner};
}
int main(void){
    memory_pages_test();
    vk_buffer large=fresh(32ull<<20), alias=buffer_borrow(large);
    buffer_destroy(&large);
    assert(!large.buffer && buffer_cache_bytes==0 && released==0);
    buffer_destroy(&alias);
    assert(!alias.buffer && buffer_cache_bytes==(32ull<<20) && buffer_cache_large_bytes==(32ull<<20));
    assert(released==0 && active_buffer_bytes==0);
    vk_buffer second=fresh(32ull<<20);buffer_destroy(&second);
    assert(released==1 && buffer_cache_bytes==(32ull<<20));
    for(unsigned i=0;i<8;i++){vk_buffer small=fresh(8ull<<20);buffer_destroy(&small);}
    assert(buffer_cache_bytes==(96ull<<20) && buffer_cache_large_bytes==(32ull<<20));
    vk_buffer overflow=fresh(4096);buffer_destroy(&overflow);assert(released==2);
    assert(buffer_cache->buffer.capacity==4096&&buffer_cache_bytes==((88ull<<20)+4096)&&buffer_cache_large_bytes==(32ull<<20));check_links();
    vk_buffer oversize=fresh(64ull<<20);buffer_destroy(&oversize);assert(released==3);
    assert(active_buffer_bytes==0 && buffer_cache_bytes==((88ull<<20)+4096));
    cached_buffer *head=buffer_cache,*tail=buffer_cache_tail,*middle=head->next;
    buffer_cache_unlink(middle);buffer_release_backing(&middle->buffer);free(middle);check_links();
    buffer_cache_unlink(head);buffer_release_backing(&head->buffer);free(head);check_links();
    buffer_cache_unlink(tail);buffer_release_backing(&tail->buffer);free(tail);check_links();assert(!buffer_cache_large_bytes);
    clear_cache();
    for(unsigned i=1;i<=64;i++){vk_buffer cold=fresh(1u<<20);cold.size=i;buffer_destroy(&cold);}
    unsigned released_before=released;uint64_t evictions_before=buffer_cache_evictions;
    vk_buffer hot=fresh(8u<<20);buffer_destroy(&hot);check_links();
    assert(released==released_before+8&&buffer_cache_evictions==evictions_before+8);
    assert(buffer_cache_bytes==(64u<<20)&&buffer_cache->buffer.capacity==(8u<<20)&&buffer_cache_tail->buffer.size==9);
    clear_cache();
    puts("actual buffer release: final reference, independent large slot, bounded LRU, multi-eviction, unlink endpoints PASS");
}
'''
memory=Path(sys.argv[1]).with_name('nvn_vulkan_buffer_memory.inc').read_text()
path=Path(sys.argv[2]); path.write_text(prefix+memory+extract('buffer_cache_unlink')+'\n'+extract('buffer_release_backing')+'\n'+extract('buffer_borrow')+'\n'+extract('buffer_destroy')+body)
subprocess.run([sys.argv[3] if len(sys.argv)>3 else 'clang','-std=c11','-pthread','-Wall','-Wextra','-Wno-unused-variable','-Werror','-fsanitize=address,undefined',str(path),'-o',str(path.with_suffix(''))],check=True)
subprocess.run([str(path.with_suffix(''))],check=True)
