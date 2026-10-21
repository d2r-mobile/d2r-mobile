// 自有 ARM64 客体在独立栈上调用文件导入；阻塞真实读取以验证宿主栈、并发和内存租约。
// Owned ARM64 guests call file imports from separate stacks; block real reads to verify native stacks, concurrency and memory leases.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "../runtime/romfs.h"
#include "../runtime/bridge.h"
#include <errno.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>
#define CHECK(c)do{if(!(c)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);abort();}}while(0)
#define UNIT (2u*1024u*1024u)
#define STACK 65536u
typedef struct{
    uintptr_t imports[6];const char*path,*mount_name;uintptr_t initial_sp,platform_x18;
    size_t cache;uint64_t handle;int64_t size;size_t got;char bytes[16];uint64_t done;
}guest_payload;
_Static_assert(offsetof(guest_payload,path)==48&&offsetof(guest_payload,mount_name)==56,"guest argument ABI");
_Static_assert(offsetof(guest_payload,initial_sp)==64&&offsetof(guest_payload,platform_x18)==72,"guest state ABI");
_Static_assert(offsetof(guest_payload,cache)==80&&offsetof(guest_payload,handle)==88&&offsetof(guest_payload,size)==96,"guest output ABI");
_Static_assert(offsetof(guest_payload,got)==104&&offsetof(guest_payload,bytes)==112&&offsetof(guest_payload,done)==128,"guest result ABI");
extern void dm_test_import_guest(void),dm_test_import_exit_return(void);
extern _Noreturn void dm_test_import_finish(void);
typedef struct{
    uintptr_t imports[6],stack_low,stack_high;const char*source_path;
    char path[1024],mount[4],bytes[16];size_t cache,got;uint64_t handle;int64_t size;int done;
}c_guest_payload;
static c_guest_payload*c_guest;
// 编译后的客体读写参与 TSAN 检查，覆盖同步跨栈调用的双向内存传递。
// Compiled guest accesses are instrumented by TSAN, covering memory handoff in both directions.
static _Noreturn void import_c_guest(c_guest_payload*p){
    uintptr_t sp;__asm__ volatile("mov %0,sp":"=r"(sp));
    CHECK(sp>=p->stack_low&&sp<p->stack_high);
    for(size_t i=0;;i++){CHECK(i<sizeof(p->path));p->path[i]=p->source_path[i];if(!p->path[i])break;}
    p->mount[0]='r';p->mount[1]='o';p->mount[2]='m';p->mount[3]=0;
    p->cache=0;p->got=0;p->handle=0;p->size=0;
    for(size_t i=0;i<sizeof(p->bytes);i++)p->bytes[i]='?';
    CHECK(!((uint32_t(*)(size_t*))p->imports[0])(&p->cache));CHECK(p->cache==4096);
    CHECK(!((uint32_t(*)(const char*,void*,size_t))p->imports[1])(p->mount,NULL,0));
    CHECK(!((uint32_t(*)(uint64_t*,const char*,int))p->imports[2])(&p->handle,p->path,1));CHECK(p->handle);
    CHECK(!((uint32_t(*)(size_t*,uint64_t,int64_t,void*,size_t))p->imports[3])(&p->got,p->handle,2,p->bytes,8));
    CHECK(p->got==8);for(size_t i=0;i<8;i++)CHECK(p->bytes[i]=="cdefghij"[i]);
    CHECK(!((uint32_t(*)(int64_t*,uint64_t))p->imports[4])(&p->size,p->handle));CHECK(p->size==10);
    ((void(*)(uint64_t))p->imports[5])(p->handle);
    p->done=1;dm_test_import_finish();
}
static struct{
    pthread_mutex_t mutex;pthread_cond_t condition;guest_payload*guests[2];
    uintptr_t native_sp[2];unsigned entered;int release;
}barrier={.mutex=PTHREAD_MUTEX_INITIALIZER,.condition=PTHREAD_COND_INITIALIZER};
ssize_t pread(int fd,void*buffer,size_t size,off_t offset){
    uintptr_t local;__asm__ volatile("mov %0,sp":"=r"(local));CHECK(dm_bridge_on_native_stack());
    dm_bridge_record*record=dm_guest_current()->bridge;CHECK(record&&record->active&&record->calls==4);
    if(c_guest&&buffer==c_guest->bytes){
        CHECK(record->active->source_domain==DM_BRIDGE_GUEST&&record->active->kind==4);
        CHECK(size==8&&offset==2);struct iovec vector={buffer,size};return preadv(fd,&vector,1,offset);
    }
    CHECK(record->active->source_domain==DM_BRIDGE_GUEST&&record->active->kind==4&&record->active->args[8]==0x818);
    CHECK(size==8&&offset==2);
    CHECK(!pthread_mutex_lock(&barrier.mutex));unsigned index=0;
    while(index<2&&buffer!=barrier.guests[index]->bytes)index++;CHECK(index<2);
    CHECK(!barrier.native_sp[index]);barrier.native_sp[index]=local;barrier.entered++;
    CHECK(!pthread_cond_broadcast(&barrier.condition));
    while(!barrier.release)CHECK(!pthread_cond_wait(&barrier.condition,&barrier.mutex));
    CHECK(!pthread_mutex_unlock(&barrier.mutex));
    struct iovec vector={buffer,size};return preadv(fd,&vector,1,offset);
}
static uint64_t mapping(dm_host*h,unsigned svc,uintptr_t dst,uintptr_t src,size_t size){
    uint64_t r[31]={dst,src,size};return dm_host_svc(h,svc,r);
}
int main(void){
    const char*tmp=getenv("TMPDIR");CHECK(tmp);char root[4096],path[4096],guest_path[4096],mount[]="rom";
    CHECK(snprintf(root,sizeof(root),"%s/dm-bridge-imports.XXXXXX",tmp)<(int)sizeof(root));CHECK(mkdtemp(root));
    strcpy(path,root);size_t rootsize=strlen(root);size_t ends[8];
    for(unsigned i=0;i<8;i++){
        size_t length=strlen(path);CHECK(length+98<sizeof(path));path[length++]='/';
        memset(path+length,'a'+(int)i,96);path[length+96]=0;CHECK(!mkdir(path,0700));ends[i]=length+96;
    }
    strcat(path,"/Name.bin");FILE*file=fopen(path,"wb");CHECK(file);
    CHECK(fwrite("abcdefghij",1,10,file)==10);CHECK(!fclose(file));
    CHECK(snprintf(guest_path,sizeof(guest_path),"rom:%s",path+rootsize)<(int)sizeof(guest_path));CHECK(strlen(guest_path)>768);
    dm_host*h=dm_host_create_layout(2*UNIT,UNIT);CHECK(h);CHECK(!dm_host_mount_romfs(h,root));
    uint64_t heap_request[31]={[1]=2*UNIT};CHECK(!dm_host_svc(h,1,heap_request));uintptr_t heap=heap_request[1];
    CHECK(!dm_host_register_executable(h,(void*)(uintptr_t)dm_test_import_guest,4));
    CHECK(!dm_host_register_executable(h,(void*)(uintptr_t)import_c_guest,4));
    for(unsigned i=0;i<3;i++)CHECK(!dm_host_register_thread_stack(h,(void*)(heap+i*STACK),STACK));
    dm_gate_site site={(uintptr_t)dm_test_import_exit_return,DM_GATE_SVC,0x0a};dm_gate_table gates={&site,1};
    CHECK(!dm_guest_attach(h,&gates));size_t count;const dm_symbol_override*imports=dm_romfs_imports(&count);CHECK(count==6);
    size_t page=(size_t)sysconf(_SC_PAGESIZE);CHECK(page&&page<=STACK);
    uint32_t handles[2];
    for(unsigned i=0;i<2;i++){
        guest_payload*p=(void*)(heap+UNIT+i*page);barrier.guests[i]=p;
        for(unsigned j=0;j<6;j++)p->imports[j]=imports[j].address;
        p->path=guest_path;p->mount_name=mount;
        uint64_t r[31]={[1]=(uintptr_t)dm_test_import_guest,[2]=(uintptr_t)p,[3]=heap+(i+1)*STACK,[4]=44,[5]=(uint32_t)-2};
        CHECK(!dm_host_svc(h,8,r));handles[i]=(uint32_t)r[1];r[0]=handles[i];CHECK(!dm_host_svc(h,9,r));
    }
    CHECK(!pthread_mutex_lock(&barrier.mutex));
    while(barrier.entered<2)CHECK(!pthread_cond_wait(&barrier.condition,&barrier.mutex));
    CHECK(barrier.native_sp[0]!=barrier.native_sp[1]);
    for(unsigned i=0;i<2;i++){
        CHECK(barrier.native_sp[i]<heap||barrier.native_sp[i]>=heap+2*UNIT);
        CHECK(barrier.guests[i]->initial_sp==heap+(i+1)*STACK);
    }
    CHECK(!pthread_mutex_unlock(&barrier.mutex));
    uint64_t info[31]={[1]=14,[2]=0xffff8001u};CHECK(!dm_host_svc(h,0x29,info));uintptr_t alias=info[1];
    for(unsigned i=0;i<2;i++)CHECK(mapping(h,4,alias+i*page,heap+UNIT+i*page,page)==DM_SVC_BAD_STATE);
    heap_request[1]=UNIT;CHECK(dm_host_svc(h,1,heap_request)==DM_SVC_BAD_STATE);
    CHECK(!pthread_mutex_lock(&barrier.mutex));barrier.release=1;CHECK(!pthread_cond_broadcast(&barrier.condition));
    CHECK(!pthread_mutex_unlock(&barrier.mutex));
    for(unsigned i=0;i<2;i++){
        uint64_t r[31]={[1]=(uintptr_t)&handles[i],[2]=1,[3]=3000000000ull};CHECK(!dm_host_svc(h,0x18,r));
        guest_payload*p=barrier.guests[i];CHECK(p->done==1&&p->got==8&&p->size==10&&p->cache==4096&&!memcmp(p->bytes,"cdefghij",8));
        r[0]=handles[i];CHECK(!dm_host_svc(h,0x16,r));
        CHECK(!mapping(h,4,alias+i*page,heap+UNIT+i*page,page));CHECK(!mapping(h,5,alias+i*page,heap+UNIT+i*page,page));
    }
    c_guest=(void*)(heap+UNIT+2*page);c_guest->source_path=guest_path;
    c_guest->stack_low=heap+2*STACK;c_guest->stack_high=heap+3*STACK;
    for(unsigned i=0;i<6;i++)c_guest->imports[i]=imports[i].address;
    uint64_t cr[31]={[1]=(uintptr_t)import_c_guest,[2]=(uintptr_t)c_guest,[3]=heap+3*STACK,[4]=44,[5]=(uint32_t)-2};
    CHECK(!dm_host_svc(h,8,cr));uint32_t c_handle=(uint32_t)cr[1];cr[0]=c_handle;CHECK(!dm_host_svc(h,9,cr));
    memset(cr,0,sizeof(cr));cr[1]=(uintptr_t)&c_handle;cr[2]=1;cr[3]=3000000000ull;CHECK(!dm_host_svc(h,0x18,cr));
    CHECK(c_guest->done==1&&c_guest->cache==4096&&c_guest->got==8&&c_guest->size==10&&!memcmp(c_guest->bytes,"cdefghij",8));
    cr[0]=c_handle;CHECK(!dm_host_svc(h,0x16,cr));c_guest=NULL;
    puts("PASS: instrumented C guest writes path/buffers, reads native import outputs, calls all six imports and exits through terminal gate");
    heap_request[1]=UNIT;CHECK(!dm_host_svc(h,1,heap_request));CHECK(!dm_guest_detach());CHECK(!dm_host_destroy(h));
    CHECK(!unlink(path));for(unsigned i=8;i>0;i--){path[ends[i-1]]=0;CHECK(!rmdir(path));}CHECK(!rmdir(root));
    printf("PASS: six guest-stack RomFS veneers, long path, typed error returns, GPR/SIMD/SP/platform-x18 ABI, concurrent native frames=%#llx/%#llx, read leases and terminal SVC gate; host_page=%zu\n",
        (unsigned long long)barrier.native_sp[0],(unsigned long long)barrier.native_sp[1],page);
    return 0;
}
