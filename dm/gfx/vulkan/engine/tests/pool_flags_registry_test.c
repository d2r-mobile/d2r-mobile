// 直接测试生产C池表：容量、owner删除、alias、未知及地址溢出；不访问伪造地址。
// Test the production C pool registry directly: capacity, owner deletion, aliases, unknowns and overflow; never dereference synthetic addresses.
#ifdef NDEBUG
#error Build this regression with assertions enabled (-UNDEBUG).
#endif
#include "../../../nvn/nvn_pool_flags_registry.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do {++checks;assert(x);} while(0)
static const void *ptr(uintptr_t value) {return (const void*)value;}
int main(void) {
    nvn_pool_flags_registry *table=calloc(1,sizeof *table);CHECK(table!=NULL);
    CHECK(nvn_pool_flags_registry_get(table,ptr(0x1000),16)==-1);
    CHECK(nvn_pool_flags_registry_add(table,ptr(1),ptr(0x1000),64,1));
    CHECK(nvn_pool_flags_registry_get(table,ptr(0x1000),64)==1);
    CHECK(nvn_pool_flags_registry_get(table,ptr(0x103f),1)==1);
    CHECK(nvn_pool_flags_registry_get(table,ptr(0x1040),1)==-1);
    CHECK(nvn_pool_flags_registry_get(table,ptr(0x103f),2)==-1);
    CHECK(nvn_pool_flags_registry_get(table,ptr(0x1000),0)==-1);
    CHECK(nvn_pool_flags_registry_get(table,ptr(UINTPTR_MAX),1)==-1);
    CHECK(nvn_pool_flags_registry_add(table,ptr(2),ptr(0x1010),16,4));
    CHECK(nvn_pool_flags_registry_get(table,ptr(0x1010),1)==-1);
    nvn_pool_flags_registry_del(table,ptr(2));CHECK(nvn_pool_flags_registry_get(table,ptr(0x1010),16)==1);
    CHECK(nvn_pool_flags_registry_add(table,ptr(2),ptr(0x1000),64,4));
    nvn_pool_flags_registry_del(table,ptr(2));CHECK(nvn_pool_flags_registry_get(table,ptr(0x1000),16)==1);
    CHECK(nvn_pool_flags_registry_add(table,ptr(1),ptr(0x2000),64,4));CHECK(table->count==1);
    CHECK(nvn_pool_flags_registry_get(table,ptr(0x1000),1)==-1);CHECK(nvn_pool_flags_registry_get(table,ptr(0x2000),1)==4);
    memset(table,0,sizeof *table);
    for(unsigned i=0;i<NVN_POOL_FLAGS_REGISTRY_MAX;++i)CHECK(nvn_pool_flags_registry_add(table,ptr(i+1),ptr(0x1000+(uintptr_t)i*0x1000),128,1));
    CHECK(table->count==NVN_POOL_FLAGS_REGISTRY_MAX && !table->incomplete);
    CHECK(nvn_pool_flags_registry_get(table,ptr(0x1000),128)==1);
    CHECK(!nvn_pool_flags_registry_add(table,ptr(99999),ptr(0x1000),128,4));
    CHECK(table->incomplete && nvn_pool_flags_registry_get(table,ptr(0x1000),128)==-1);
    nvn_pool_flags_registry_del(table,ptr(1));CHECK(nvn_pool_flags_registry_get(table,ptr(0x2000),1)==-1);
    memset(table,0,sizeof *table);
    CHECK(!nvn_pool_flags_registry_add(table,ptr(1),ptr(UINTPTR_MAX-2),8,1));CHECK(table->incomplete);
    free(table);printf("PASS pool_flags_registry production_C checks=%u; host_mutex_and_GPU=NOT_TESTED\n",checks);return 0;
}
