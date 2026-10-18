// 自有别名内存回归：真实权限故障、共享字节、查询、回滚及 SDK 式线程租约；不含游戏代码。
// Owned alias-memory regression: real permission faults, shared bytes, queries, rollback and SDK-like thread leases; no game code.
#include "../runtime/internal.h"
#include "../runtime/patch.h"
#include "../nso.h"
#include "../image_map.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include "generated_entry.h"
#define CHECK(c)do{if(!(c)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);abort();}}while(0)
#define UNIT (2u*1024u*1024u)
#define STACK 65536u
#if defined(__aarch64__)
static const dm_gate_table gates={0};
#endif
typedef struct{uint64_t base,size;uint32_t state,attr,perm,ipc,device,pad;}info;
static uint64_t heap_size(dm_host*h,size_t size){uint64_t r[31]={[1]=size};return dm_host_svc(h,1,r);}
static uint64_t alias(dm_host*h,unsigned n,void*dst,void*src,size_t size){
    uint64_t r[31]={(uintptr_t)dst,(uintptr_t)src,size};return dm_host_svc(h,n,r);
}
static uint64_t get(dm_host*h,unsigned id){uint64_t r[31]={[1]=id,[2]=0xffff8001};CHECK(!dm_host_svc(h,0x29,r));return r[1];}
static info query(dm_host*h,void*address){info result;uint8_t*out=h->heap+h->committed-64;memset(out-1,0xa5,sizeof(result)+2);
    uint64_t r[31]={(uintptr_t)out,0xfeed,(uintptr_t)address};CHECK(!dm_host_svc(h,6,r));memcpy(&result,out,sizeof(result));
    CHECK(out[-1]==0xa5&&out[sizeof(result)]==0xa5);CHECK(!r[1]&&!result.ipc&&!result.device&&!result.pad);return result;
}
static dm_host*layout(void){dm_host*h=dm_host_create_layout(2*UNIT,2*UNIT);CHECK(h);CHECK(!heap_size(h,2*UNIT));return h;}
static void fault_handler(int signal){_exit(signal==SIGSEGV||signal==SIGBUS?73:74);}
static void denied(void*address,int write){
    pid_t pid=fork();CHECK(pid>=0);
    if(!pid){struct sigaction action={0};action.sa_handler=fault_handler;sigemptyset(&action.sa_mask);
        if(sigaction(SIGSEGV,&action,NULL)||sigaction(SIGBUS,&action,NULL))_exit(75);
        volatile unsigned char*p=address;if(write)*p=17;else{volatile unsigned char value=*p;(void)value;}_exit(76);}
    int status;CHECK(waitpid(pid,&status,0)==pid);CHECK(WIFEXITED(status)&&WEXITSTATUS(status)==73);
}
static void transfer(void){
    dm_host*h=layout();size_t page=h->page_size;uint8_t*src=h->heap+page,*dst=(void*)(uintptr_t)(get(h,14)+page);
    CHECK(get(h,4)==(uintptr_t)h->heap&&get(h,5)==2*UNIT&&get(h,12)==(uintptr_t)h->heap&&get(h,13)==4*UNIT&&get(h,15)==2*UNIT);
    memset(src,0x6a,page);src[-1]=0x37;src[page]=0x48;
    info m=query(h,src);CHECK(m.base==(uintptr_t)h->heap&&m.size==2*UNIT&&m.state==5&&m.perm==3&&!m.attr);
    denied(dst,0);CHECK(!alias(h,4,dst,src,page));CHECK(dm_host_destroy(h)==EBUSY);
    CHECK(dst[0]==0x6a&&dst[page-1]==0x6a);denied(src,0);denied(src,1);
    m=query(h,src);CHECK(m.base==(uintptr_t)src&&m.size==page&&m.state==5&&m.attr==1&&!m.perm);
    m=query(h,dst);CHECK(m.base==(uintptr_t)dst&&m.size==page&&m.state==0xb&&m.perm==3&&!m.attr);
    m=query(h,dst-1);CHECK(!m.state&&!m.perm&&m.base+m.size==(uintptr_t)dst);
    m=query(h,dst+page);CHECK(!m.state&&!m.perm&&m.base==(uintptr_t)dst+page);
    dst[0]=0x9d;dst[page-1]=0x81;unsigned char byte=0;
    CHECK(pread(h->backing_fd,&byte,1,(off_t)page)==1&&byte==0x9d);
    uint64_t r[31]={(uintptr_t)src,1};CHECK(dm_host_svc(h,0x27,r)==DM_SVC_INVALID_ADDRESS);
    r[0]=(uintptr_t)src;r[1]=0xfeed;r[2]=(uintptr_t)dst;CHECK(dm_host_svc(h,6,r)==DM_SVC_INVALID_ADDRESS&&r[1]==0xfeed);
    r[0]=1;r[1]=0xfeed;CHECK(dm_host_svc(h,6,r)==DM_SVC_INVALID_ADDRESS&&r[1]==0xfeed);
    r[0]=0;r[1]=(uintptr_t)src;r[2]=1;r[3]=0;CHECK(dm_host_svc(h,0x18,r)==DM_SVC_INVALID_ADDRESS);
    CHECK(heap_size(h,UNIT)==DM_SVC_BAD_STATE);
    CHECK(!alias(h,5,dst,src,page));CHECK(src[0]==0x9d&&src[page-1]==0x81&&src[-1]==0x37&&src[page]==0x48);
    denied(dst,0);denied(dst,1);m=query(h,src);CHECK(m.state==5&&m.perm==3&&!m.attr);
    uint64_t entropy[4];for(unsigned i=0;i<4;i++){uint64_t v[31]={[1]=11,[3]=i};CHECK(!dm_host_svc(h,0x29,v));entropy[i]=v[1];
        v[1]=11;CHECK(!dm_host_svc(h,0x29,v)&&v[1]==entropy[i]);}
    unsigned ids[]={4,5,12,13,14,15};for(size_t i=0;i<sizeof(ids)/sizeof(ids[0]);i++){
        r[1]=ids[i];r[2]=0;r[3]=0;CHECK(dm_host_svc(h,0x29,r)==DM_SVC_INVALID_HANDLE&&r[1]==ids[i]);
        r[1]=ids[i];r[2]=0;r[3]=1;CHECK(dm_host_svc(h,0x29,r)==DM_SVC_INVALID_COMBINATION&&r[1]==ids[i]);
    }
    r[1]=11;r[2]=1;r[3]=4;CHECK(dm_host_svc(h,0x29,r)==DM_SVC_INVALID_HANDLE&&r[1]==11);
    r[1]=11;r[2]=0;r[3]=4;CHECK(dm_host_svc(h,0x29,r)==DM_SVC_INVALID_COMBINATION&&r[1]==11);
    r[2]=0xffff8001;
    r[1]=2;r[3]=0;CHECK(dm_host_svc(h,0x29,r)==DM_SVC_NOT_IMPLEMENTED&&r[1]==2);
    CHECK(!dm_host_destroy(h));puts("PASS transfer: shared backing, denied source/stale destination, query extents, safe outputs and GetInfo");
}
static void invalid_and_capacity(void){
    dm_host*h=layout();size_t page=h->page_size;uint8_t*src=h->heap,*dst=(void*)(uintptr_t)get(h,14);
    uint8_t*outside=mmap(NULL,page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);CHECK(outside!=MAP_FAILED);outside[0]=0x52;
    CHECK(alias(h,4,outside,src,page)==DM_SVC_INVALID_MEMORY_REGION&&outside[0]==0x52);
    CHECK(alias(h,4,dst,src,0)==DM_SVC_INVALID_SIZE);CHECK(alias(h,4,dst+1,src,page)==DM_SVC_INVALID_ADDRESS);
    CHECK(alias(h,4,dst,src+1,page)==DM_SVC_INVALID_ADDRESS);CHECK(alias(h,4,dst,src,page-1)==DM_SVC_INVALID_SIZE);
    CHECK(alias(h,4,dst,(void*)(UINTPTR_MAX-page+1),2*page)==DM_SVC_INVALID_MEMORY_STATE);
    if(page>4096){CHECK(alias(h,4,dst+4096,src,page)==DM_SVC_INVALID_ADDRESS);CHECK(alias(h,4,dst,src,4096)==DM_SVC_INVALID_SIZE);}
    for(size_t i=0;i<65;i++)src[i*page]=(uint8_t)i;
    for(size_t i=0;i<64;i++)CHECK(!alias(h,4,dst+i*page,src+i*page,page));
    CHECK(alias(h,4,dst+64*page,src+64*page,page)==DM_SVC_OUT_OF_MEMORY&&src[64*page]==64);
    CHECK(alias(h,4,dst+64*page,src,page)==DM_SVC_INVALID_MEMORY_STATE);
    CHECK(alias(h,4,dst,src+64*page,page)==DM_SVC_INVALID_MEMORY_STATE);
    CHECK(alias(h,5,dst,src+page,page)==DM_SVC_INVALID_MEMORY_STATE);
    CHECK(alias(h,5,dst,src,2*page)==DM_SVC_INVALID_MEMORY_STATE);
    for(size_t i=0;i<64;i++){CHECK(dst[i*page]==(uint8_t)i);CHECK(!alias(h,5,dst+i*page,src+i*page,page));CHECK(src[i*page]==(uint8_t)i);}
    CHECK(alias(h,5,dst,src,page)==DM_SVC_INVALID_MEMORY_STATE);
    CHECK(!munmap(outside,page));CHECK(!dm_host_destroy(h));
    CHECK(!dm_host_create_layout(UNIT,0)&&errno==EINVAL);CHECK(!dm_host_create_layout(UNIT,page+1)&&errno==EINVAL);
    h=dm_host_create(UNIT);CHECK(h);CHECK(alias(h,4,NULL,NULL,page)==DM_SVC_NOT_IMPLEMENTED);CHECK(!dm_host_destroy(h));
    puts("PASS invalid/capacity: bounds, alignment, exact tuples, 64 entries, untouched outside mapping and page geometry");
}
static void rollback_and_zero(void){
    dm_host*h=layout();size_t page=h->page_size;uint8_t*src=h->heap,*dst=(void*)(uintptr_t)get(h,14);src[0]=61;src[UNIT]=83;
    int fd=h->backing_fd;h->backing_fd=-1;
    CHECK(alias(h,4,dst,src,page)==DM_SVC_OUT_OF_MEMORY&&src[0]==61&&!h->alias_count);denied(dst,0);
    CHECK(heap_size(h,UNIT)==DM_SVC_OUT_OF_MEMORY&&h->committed==2*UNIT&&src[UNIT]==83);
    h->backing_fd=fd;CHECK(!heap_size(h,UNIT));CHECK(src[0]==61);denied(src+UNIT,0);
    h->backing_fd=-1;CHECK(heap_size(h,2*UNIT)==DM_SVC_OUT_OF_MEMORY&&h->committed==UNIT);denied(src+UNIT,0);
    h->backing_fd=fd;CHECK(!heap_size(h,2*UNIT));CHECK(src[0]==61&&src[UNIT]==0&&src[2*UNIT-1]==0);
    CHECK(!alias(h,4,dst,src,page));dst[0]=75;CHECK(!alias(h,5,dst,src,page));CHECK(src[0]==75);
    CHECK(!heap_size(h,0));CHECK(!heap_size(h,2*UNIT));CHECK(src[0]==0);CHECK(!dm_host_destroy(h));
    puts("PASS rollback/zero: real bad-fd map and truncate failures restore state, shrink/recommit zeroes shared backing");
}
static void pins(void){
    dm_host*h=layout();size_t page=h->page_size;uint8_t*src=h->heap,*dst=(void*)(uintptr_t)get(h,14);dm_memory_pin pin={0};
    pthread_mutex_lock(&h->mutex);CHECK(!dm_host_memory_pin_locked(h,&pin,(uintptr_t)src,page));pthread_mutex_unlock(&h->mutex);
    CHECK(alias(h,4,dst,src,page)==DM_SVC_BAD_STATE);CHECK(heap_size(h,0)==DM_SVC_BAD_STATE);CHECK(dm_host_destroy(h)==EBUSY);
    pthread_mutex_lock(&h->mutex);dm_host_memory_unpin_locked(h,&pin);pthread_mutex_unlock(&h->mutex);
    CHECK(!alias(h,4,dst,src,page));
    pthread_mutex_lock(&h->mutex);CHECK(dm_host_memory_pin_locked(h,&pin,(uintptr_t)src,1)==EINVAL);
    CHECK(dm_host_memory_read_pin_locked(h,&pin,(uintptr_t)src,1)==EINVAL);
    CHECK(dm_host_memory_pin_locked(h,&pin,(uintptr_t)dst-1,2)==EINVAL);
    CHECK(!dm_host_memory_pin_locked(h,&pin,(uintptr_t)dst,page));pthread_mutex_unlock(&h->mutex);
    CHECK(alias(h,5,dst,src,page)==DM_SVC_BAD_STATE);
    pthread_mutex_lock(&h->mutex);dm_host_memory_unpin_locked(h,&pin);pthread_mutex_unlock(&h->mutex);
    CHECK(!alias(h,5,dst,src,page));CHECK(!dm_host_destroy(h));
    h=dm_host_create(UNIT);CHECK(h);CHECK(!heap_size(h,UNIT));
    pthread_mutex_lock(&h->mutex);CHECK(!dm_host_memory_pin_locked(h,&pin,(uintptr_t)h->heap,4));pthread_mutex_unlock(&h->mutex);
    CHECK(heap_size(h,0)==DM_SVC_BAD_STATE);
    pthread_mutex_lock(&h->mutex);dm_host_memory_unpin_locked(h,&pin);uint32_t external=0;
    CHECK(!dm_host_memory_pin_locked(h,&pin,(uintptr_t)&external,4)&&!pin.active);pthread_mutex_unlock(&h->mutex);
    CHECK(!heap_size(h,0));CHECK(!dm_host_destroy(h));puts("PASS pins: map/unmap/shrink/destruction guards, partial overlap, ordinary heap and trusted external memory");
}
typedef struct{dm_host*h;void*destination,*source;size_t size;uint64_t result;}race;
static void*map_worker(void*argument){race*r=argument;r->result=alias(r->h,4,r->destination,r->source,r->size);return NULL;}
static void collisions(void){
    dm_host*h=layout();void*dst=(void*)(uintptr_t)get(h,14);race a={h,dst,h->heap,h->page_size,1},b=a;pthread_t x,y;
    CHECK(!pthread_create(&x,NULL,map_worker,&a)&&!pthread_create(&y,NULL,map_worker,&b));CHECK(!pthread_join(x,NULL)&&!pthread_join(y,NULL));
    CHECK((a.result==0&&b.result==DM_SVC_INVALID_MEMORY_STATE)||(b.result==0&&a.result==DM_SVC_INVALID_MEMORY_STATE));
    CHECK(!alias(h,5,dst,h->heap,h->page_size));CHECK(!dm_host_destroy(h));puts("PASS concurrent collision: exactly one mapper succeeds");
}
#if defined(__aarch64__)
extern void dm_gate(void);
typedef struct{uint64_t result,value;}gate_result;
typedef gate_result(*gate_fn)(uint64_t,uint64_t,uint64_t,uint64_t);
static void put(uint8_t*p,uint32_t word){memcpy(p,&word,4);}
static void DM_NO_UBSAN_FUNCTION native_gates(void){
    size_t page=(size_t)sysconf(_SC_PAGESIZE);dm_nso_image image={.header.image_size=page};dm_image_mapping program;
    image.header.segments[0].size=256;image.header.segments[1].memory_offset=(uint32_t)page;image.header.segments[2].memory_offset=(uint32_t)page;
    image.bytes=calloc(1,page);CHECK(image.bytes);unsigned numbers[]={4,5,6,0x29};dm_gate_site sites[4];
    for(unsigned i=0;i<4;i++){
        unsigned off=i*32;put(image.bytes+off,0xa9bf7bfd);put(image.bytes+off+8,0xa8c17bfd);put(image.bytes+off+12,0xd65f03c0);
        dm_patch_request request={.instruction=0xd4000001u|(numbers[i]<<5),.original_offset=off+4,.stub_offset=128+i*32,.host_gate=(uintptr_t)dm_gate};
        dm_patch_output patch;CHECK(!dm_patch_system(&request,&patch));put(image.bytes+off+4,patch.replacement);
        memcpy(image.bytes+request.stub_offset,patch.stub,sizeof(patch.stub));sites[i]=patch.site;
    }
    CHECK(!dm_image_map(&image,&program));dm_nso_free(&image);
    for(unsigned i=0;i<4;i++)sites[i].return_address+=(uintptr_t)program.base;
    dm_gate_table table={sites,4};dm_host*h=layout();CHECK(!dm_guest_attach(h,&table));
    gate_fn map=(gate_fn)(void*)program.base,unmap=(gate_fn)(void*)(program.base+32);
    gate_fn query_gate=(gate_fn)(void*)(program.base+64),get_gate=(gate_fn)(void*)(program.base+96);
    gate_result r=get_gate(0,14,0xffff8001,0);CHECK(!r.result&&r.value==(uintptr_t)h->heap+h->capacity);
    uintptr_t dst=r.value,src=(uintptr_t)h->heap;h->heap[0]=0x74;r=map(dst,src,page,0);CHECK(!r.result&&r.value==src);
    CHECK(*(uint8_t*)dst==0x74);*(uint8_t*)dst=0x93;
    info*out=(void*)(h->heap+page);r=query_gate((uintptr_t)out,0xfeed,dst,0);
    CHECK(!r.result&&!r.value&&out->base==dst&&out->size==page&&out->state==0xb&&out->perm==3);
    r=query_gate(1,0xfeed,dst,0);CHECK(r.result==DM_SVC_INVALID_ADDRESS&&r.value==0xfeed);
    r=unmap(dst,src,page,0);CHECK(!r.result&&r.value==src&&h->heap[0]==0x93);
    CHECK(!dm_guest_detach());CHECK(!dm_host_destroy(h));CHECK(!dm_image_unmap(&program));
    puts("PASS native gates: actual rewritten ARM64 SVC04/05/06/29, x0 result/x1 value ABI and invalid output rejection");
}
extern void dm_test_sdk_entry(void*);
typedef struct{dm_host*h;uintptr_t top;unsigned runs;}payload;
static void callback(void*tt){uintptr_t sp;payload*p;memcpy(&sp,(uint8_t*)tt+0x28,8);memcpy(&p,(uint8_t*)tt+0x30,8);
    CHECK(sp==p->top);p->runs++;uint64_t r[31]={0};dm_host_svc(p->h,0x0a,r);abort();}
static void field(void*tt,size_t offset,uint64_t value){memcpy((uint8_t*)tt+offset,&value,8);}
static uint64_t handle_call(dm_host*h,unsigned svc,uint32_t handle){uint64_t r[31]={handle};return dm_host_svc(h,svc,r);}
static void sdk_lifecycle(void){
    dm_host*h=layout();uint8_t*src=h->heap+STACK,*dst=(void*)(uintptr_t)(get(h,14)+STACK);void*tt=h->heap;
    CHECK(!dm_host_register_executable(h,(void*)(uintptr_t)dm_test_sdk_entry,16));CHECK(!dm_host_register_executable(h,(void*)(uintptr_t)callback,4));
    dm_sdk_thread_abi abi={(uintptr_t)dm_test_sdk_entry,0x80,0x10,0x18,32};CHECK(!dm_host_set_sdk_thread_abi(h,&abi));
    uint32_t main_handle;CHECK(!dm_guest_bootstrap_main(h,&gates,&main_handle));payload p={h,(uintptr_t)dst+STACK-32,0};
    field(tt,0x10,(uintptr_t)dst);field(tt,0x18,STACK);field(tt,0x30,(uintptr_t)&p);
    for(unsigned i=0;i<100;i++){
        info free=query(h,dst-16384);CHECK(!free.state&&free.base+free.size>=(uintptr_t)dst+STACK+16384);
        CHECK(!alias(h,4,dst,src,STACK));field((void*)p.top,0,(uintptr_t)callback);
        uint64_t r[31]={0,(uintptr_t)dm_test_sdk_entry,(uintptr_t)tt,p.top,44,(uint32_t)-2};CHECK(!dm_host_svc(h,8,r));uint32_t handle=(uint32_t)r[1];
        CHECK(alias(h,5,dst,src,STACK)==DM_SVC_BAD_STATE);
        if(i==0){CHECK(!handle_call(h,0x16,handle));CHECK(!alias(h,5,dst,src,STACK));continue;}
        CHECK(!handle_call(h,9,handle));uint64_t wait[31]={0,(uintptr_t)&handle,1,3000000000};CHECK(!dm_host_svc(h,0x18,wait));
        CHECK(!handle_call(h,0x16,handle));CHECK(!alias(h,5,dst,src,STACK));
    }
    CHECK(p.runs==99);CHECK(!dm_guest_detach());CHECK(!handle_call(h,0x16,main_handle));CHECK(!dm_host_destroy(h));
    puts("PASS SDK-shaped lifecycle: guard queries, 100 map/create/lease/release/unmap cycles, 99 native entries and exact SP");
}
#endif
int main(void){
    printf("host_page_size=%ld\n",sysconf(_SC_PAGESIZE));transfer();invalid_and_capacity();rollback_and_zero();pins();collisions();
#if defined(__aarch64__)
    native_gates();sdk_lifecycle();
#endif
    puts("PASS: owned alias-memory scenarios complete");return 0;
}
