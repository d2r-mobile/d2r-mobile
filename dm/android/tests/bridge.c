// 自有 ARM64 改写门的正常／阻塞／终止切换与有界注册表容量测试，不加载游戏。
// Owned ARM64 rewritten-gate normal/blocking/terminal transitions and bounded registry capacity, without game loading.
#include "../runtime/internal.h"
#include "../runtime/bridge.h"
#include "../runtime/patch.h"
#include "../image_map.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <sys/wait.h>
#ifndef __has_feature
#define __has_feature(x) 0
#endif
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define SAN_BOUNDARY 1
#endif
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);abort();}}while(0)
#define UNIT (2u*1024u*1024u)
#define STACK 65536u
extern void dm_gate(void),dm_test_bridge_guest(void*);
typedef struct{uintptr_t functions[4],sp,x18;uint64_t id,tls,done,after;uint32_t event,pad;dm_bridge_record*record;uint64_t write_event,stack_override;}payload;
_Static_assert(offsetof(payload,event)==80&&offsetof(payload,record)==88,"fixture ABI");
void dm_test_bridge_fail(payload*p,uint64_t actual,uint64_t expected,uint64_t result){fprintf(stderr,"ABI failure actual=%llx expected=%llx result=%llx id=%llx tls=%llx done=%llu\n",(unsigned long long)actual,(unsigned long long)expected,(unsigned long long)result,(unsigned long long)p->id,(unsigned long long)p->tls,(unsigned long long)p->done);abort();}
static void entry(uint64_t arg){payload*p=(void*)(uintptr_t)arg;p->record=dm_guest_current()->bridge;dm_test_bridge_guest(p);abort();}
static void put(uint8_t*p,uint32_t w){memcpy(p,&w,4);}
static dm_image_mapping make_program(dm_gate_site sites[4]){
    size_t page=(size_t)sysconf(_SC_PAGESIZE);dm_nso_image im={.header.image_size=page};
    im.header.segments[0].size=512;im.header.segments[1].memory_offset=(uint32_t)page;im.header.segments[2].memory_offset=(uint32_t)page;
    im.bytes=calloc(1,page);CHECK(im.bytes);uint32_t ins[4]={0xd40004a1,0xd53bd040,0xd4000301,0xd4000141};
    for(unsigned i=0;i<4;i++){unsigned off=i*32;put(im.bytes+off,0xa9bf7bfd);put(im.bytes+off+8,0xa8c17bfd);put(im.bytes+off+12,0xd65f03c0);
        dm_patch_request req={ins[i],off+4,256+i*32,(uintptr_t)dm_gate};dm_patch_output out;CHECK(!dm_patch_system(&req,&out));
        put(im.bytes+off+4,out.replacement);memcpy(im.bytes+req.stub_offset,out.stub,sizeof(out.stub));sites[i]=out.site;}
    dm_image_mapping map={0};CHECK(!dm_image_map(&im,&map));dm_nso_free(&im);
    for(unsigned i=0;i<4;i++)sites[i].return_address+=(uintptr_t)map.base;return map;
}
static uint64_t svc(dm_host*h,unsigned n,uint64_t a){uint64_t r[31]={a};return dm_host_svc(h,n,r);}
static uint32_t create(dm_host*h,payload*p,uintptr_t top){uint64_t r[31]={0,(uintptr_t)entry,(uintptr_t)p,top,44,(uint32_t)-2};CHECK(!dm_host_svc(h,8,r));return(uint32_t)r[1];}
static void waiters(dm_host*h,size_t count){struct timespec t={.tv_nsec=1000000};for(unsigned i=0;i<5000;i++){
    pthread_mutex_lock(&h->mutex);size_t n=dm_host_sync_waiters(h);pthread_mutex_unlock(&h->mutex);if(n==count)return;nanosleep(&t,NULL);}CHECK(0);}
static void wait_thread(dm_host*h,uint32_t handle){uint64_t r[31]={0,(uintptr_t)&handle,1,3000000000};CHECK(!dm_host_svc(h,0x18,r));}
typedef struct{dm_host*h;const dm_gate_table*gates;pthread_mutex_t mutex;pthread_cond_t cond;unsigned ready,success,failed;int release;}capacity;
static void*fill(void*arg){capacity*c=arg;int rc=dm_guest_attach(c->h,c->gates);CHECK(!rc||rc==ENOSPC);
    pthread_mutex_lock(&c->mutex);c->ready++;if(rc)c->failed++;else c->success++;pthread_cond_broadcast(&c->cond);
    while(!c->release)pthread_cond_wait(&c->cond,&c->mutex);pthread_mutex_unlock(&c->mutex);if(!rc)CHECK(!dm_guest_detach());return NULL;}
static void capacity_test(dm_host*h,const dm_gate_table*g,payload*p,uintptr_t top){
    capacity c={.h=h,.gates=g,.mutex=PTHREAD_MUTEX_INITIALIZER,.cond=PTHREAD_COND_INITIALIZER};pthread_t threads[DM_BRIDGE_SLOTS];
    for(unsigned i=0;i<DM_BRIDGE_SLOTS;i++)CHECK(!pthread_create(&threads[i],NULL,fill,&c));
    pthread_mutex_lock(&c.mutex);while(c.ready!=DM_BRIDGE_SLOTS)pthread_cond_wait(&c.cond,&c.mutex);
    CHECK(c.success==DM_BRIDGE_SLOTS-1&&c.failed==1&&dm_bridge_registry_count()==DM_BRIDGE_SLOTS);
    uint32_t handle=create(h,p,top);CHECK(svc(h,9,handle)==0xce01);CHECK(!p->done);
    c.release=1;pthread_cond_broadcast(&c.cond);pthread_mutex_unlock(&c.mutex);
    for(unsigned i=0;i<DM_BRIDGE_SLOTS;i++)CHECK(!pthread_join(threads[i],NULL));CHECK(dm_bridge_registry_count()==1);
    CHECK(!svc(h,9,handle));waiters(h,1);CHECK(!svc(h,0x11,p->write_event));wait_thread(h,handle);CHECK(p->done==1&&!p->after);CHECK(!svc(h,0x16,handle));
    pthread_cond_destroy(&c.cond);pthread_mutex_destroy(&c.mutex);
}
#if SAN_BOUNDARY
static void trapped(int signal){_exit(signal==SIGTRAP?77:78);}
static void boundary_children(const char*program){const char*modes[]={"--boundary-low","--boundary-short","--boundary-exact"};
    for(unsigned i=0;i<3;i++){pid_t child=fork();CHECK(child>=0);if(!child){execl(program,program,modes[i],(char*)NULL);_exit(79);}
        int status;CHECK(waitpid(child,&status,0)==child);CHECK(WIFEXITED(status)&&WEXITSTATUS(status)==(i<2?77:0));}
    puts("PASS: SAN source boundary low/32752 rejects and exact 32768-byte headroom normal/terminal transitions");
}
#endif
int main(int argc,char**argv){
    int boundary=argc>1;
#if SAN_BOUNDARY
    void *alt_stack=NULL;stack_t previous={0};
    if(boundary){CHECK(!sigaltstack(NULL,&previous));alt_stack=malloc(65536);stack_t alt={.ss_sp=alt_stack,.ss_size=65536};CHECK(alt_stack&&!sigaltstack(&alt,NULL));
        struct sigaction action={.sa_handler=trapped,.sa_flags=SA_ONSTACK};sigemptyset(&action.sa_mask);
        CHECK(!sigaction(SIGTRAP,&action,NULL)&&!sigaction(SIGSEGV,&action,NULL)&&!sigaction(SIGBUS,&action,NULL));}
#else
    (void)argv;
#endif
    dm_gate_site sites[4];dm_image_mapping program=make_program(sites);dm_gate_table gates={sites,4};
    dm_host*h=dm_host_create(UNIT);CHECK(h);uint64_t r[31]={[1]=UNIT};CHECK(!dm_host_svc(h,1,r));uint8_t*heap=(void*)(uintptr_t)r[1];
    CHECK(!dm_host_register_executable(h,(void*)(uintptr_t)entry,4));CHECK(!dm_host_register_executable(h,program.base,512));
    CHECK(!dm_guest_attach(h,&gates));CHECK(dm_bridge_on_native_stack());CHECK(dm_bridge_registry_count()==1);
    payload p[8]={0};uint32_t handles[8];
    for(unsigned i=0;i<8;i++){for(unsigned j=0;j<4;j++)p[i].functions[j]=(uintptr_t)program.base+j*32;
        CHECK(!dm_host_register_thread_stack(h,heap+i*STACK,STACK));
#if SAN_BOUNDARY
        if(boundary){uintptr_t headroom=!strcmp(argv[1],"--boundary-low")?0:!strcmp(argv[1],"--boundary-short")?32752:32768;
            p[i].stack_override=(uintptr_t)heap+i*STACK+headroom+832;}
#endif
        uint64_t event[31]={0};CHECK(!dm_host_svc(h,0x45,event));p[i].event=(uint32_t)event[2];p[i].write_event=event[1];
        handles[i]=create(h,&p[i],(uintptr_t)heap+(i+1)*STACK);CHECK(!svc(h,9,handles[i]));}
    waiters(h,8);
    for(unsigned i=0;i<8;i++){dm_bridge_record*b=p[i].record;CHECK(b&&b->domain==DM_BRIDGE_SERVICE&&b->active&&b->calls==3);
        CHECK(b->last_native_sp>=b->native_low&&b->last_native_sp<b->native_high);
        CHECK(p[i].sp>=(uintptr_t)heap+i*STACK&&p[i].sp<=(uintptr_t)heap+(i+1)*STACK&&p[i].id&&p[i].tls);
        for(unsigned j=0;j<i;j++)CHECK(p[i].id!=p[j].id&&p[i].tls!=p[j].tls);}
    for(unsigned i=0;i<8;i++)CHECK(!svc(h,0x11,p[i].write_event));
    for(unsigned i=0;i<8;i++){wait_thread(h,handles[i]);CHECK(p[i].done==1&&!p[i].after);CHECK(!svc(h,0x16,handles[i]));CHECK(!svc(h,0x17,p[i].event));}
    CHECK(dm_bridge_registry_count()==1);p[0].done=0;if(!boundary)capacity_test(h,&gates,&p[0],(uintptr_t)heap+STACK);
    for(unsigned i=0;i<8;i++){CHECK(!svc(h,0x16,p[i].event));CHECK(!svc(h,0x16,p[i].write_event));}CHECK(!dm_guest_detach());CHECK(!dm_bridge_registry_count());
    CHECK(!dm_host_destroy(h));CHECK(!dm_image_unmap(&program));
#if SAN_BOUNDARY
    if(!boundary)boundary_children(argv[0]);
    if(alt_stack){CHECK(!sigaltstack(&previous,NULL));stack_t restored;CHECK(!sigaltstack(NULL,&restored));
        CHECK(restored.ss_flags==previous.ss_flags);
        if(!(previous.ss_flags&SS_DISABLE))CHECK(restored.ss_sp==previous.ss_sp&&restored.ss_size==previous.ss_size);
        free(alt_stack);}
#endif
    puts("PASS: eight rewritten SVC/TLS guests, native blocking service stacks, preserved ABI/FP/x18, terminal no-return, registry full/retry/reuse/cleanup");
}
