"""Exercise actual buffer release bodies against owned Vulkan boundary shims.

The fixture tests reference lifetime and retained-memory budgets, not GPU work.
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
static unsigned released;
static void vkUnmapMemory(int d,uintptr_t m){(void)d;(void)m;}
static void vkDestroyBuffer(int d,uintptr_t b,void *a){(void)d;(void)b;(void)a;released++;}
static void vkFreeMemory(int d,uintptr_t m,void*a){(void)d;(void)m;(void)a;}
'''
start=source.index('typedef struct { _Atomic unsigned references;')
end=source.index('// 只读探针',start)
# Keep the real production types and cache accounting declarations.
prefix+=source[start:end]
# Baseline does not declare the new diagnostic accounting variable.
if 'buffer_cache_large_bytes' not in prefix: prefix+='\nstatic uint64_t buffer_cache_large_bytes;\n'
body=r'''
static vk_buffer fresh(uint64_t capacity){
    buffer_owner *owner=calloc(1,sizeof *owner);assert(owner);atomic_init(&owner->references,1);
    active_buffer_bytes+=capacity;
    return (vk_buffer){.buffer=1,.memory=1,.capacity=capacity,.size=capacity,.owner=owner};
}
int main(void){
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
    vk_buffer oversize=fresh(64ull<<20);buffer_destroy(&oversize);assert(released==3);
    assert(active_buffer_bytes==0 && buffer_cache_bytes==(96ull<<20));
    while(buffer_cache){cached_buffer *entry=buffer_cache;buffer_cache=entry->next;free(entry->buffer.owner);free(entry);}
    puts("actual buffer release: last reference, single large slot, small budget, overflow PASS");
}
'''
path=Path(sys.argv[2]); path.write_text(prefix+extract('buffer_borrow')+'\n'+extract('buffer_destroy')+body)
subprocess.run([sys.argv[3] if len(sys.argv)>3 else 'clang','-std=c11','-pthread','-Wall','-Wextra','-Wno-unused-variable','-Werror','-fsanitize=address,undefined',str(path),'-o',str(path.with_suffix(''))],check=True)
subprocess.run([str(path.with_suffix(''))],check=True)
