// 无游戏互斥／条件集成测试：真实 pthread、客体原子快路径及主线程登记。
// Game-free mutex/condition integration tests: real pthreads, guest atomic fast paths and main-thread registration.
#include "../runtime/internal.h"
#include "../runtime/patch.h"
#include "../image_map.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#define CHECK(c)do{if(!(c)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);abort();}}while(0)
#define UNIT (2u*1024u*1024u)
#define MASK 0x40000000u
#define TIMEOUT 0xea01u
static dm_gate_table gates={0};
#if defined(__aarch64__)
extern void dm_gate(void);
static dm_image_mapping program;
static dm_gate_site sites[4];
static void put(uint8_t*p,uint32_t word){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(word>>(8*i));}
static void prepare_gates(void){
    size_t page=(size_t)sysconf(_SC_PAGESIZE);dm_nso_image image={.header.image_size=page};
    image.header.segments[0].size=256;image.header.segments[1].memory_offset=(uint32_t)page;image.header.segments[2].memory_offset=(uint32_t)page;
    image.bytes=calloc(1,page);CHECK(image.bytes);
    for(unsigned i=0;i<4;i++){
        unsigned off=i*32;uint32_t svc=0xd4000001u|((0x1au+i)<<5);
        put(image.bytes+off,0xa9bf7bfd);put(image.bytes+off+8,0xa8c17bfd);put(image.bytes+off+12,0xd65f03c0);
        dm_patch_request request={.instruction=svc,.original_offset=off+4,.stub_offset=128+i*32,.host_gate=(uintptr_t)dm_gate};
        dm_patch_output patch;CHECK(!dm_patch_system(&request,&patch));put(image.bytes+off+4,patch.replacement);
        memcpy(image.bytes+request.stub_offset,patch.stub,sizeof(patch.stub));sites[i]=patch.site;
    }
    CHECK(!dm_image_map(&image,&program));dm_nso_free(&image);
    for(unsigned i=0;i<4;i++)sites[i].return_address+=(uintptr_t)program.base;
    gates=(dm_gate_table){sites,4};
}
#endif
static uint32_t load(uint32_t*p){return __atomic_load_n(p,__ATOMIC_SEQ_CST);}
static void store(uint32_t*p,uint32_t value){__atomic_store_n(p,value,__ATOMIC_SEQ_CST);}
static uint64_t mono(void){struct timespec t;CHECK(!clock_gettime(CLOCK_MONOTONIC,&t));return(uint64_t)t.tv_sec*1000000000+t.tv_nsec;}
static void pause_thread(void){struct timespec t={.tv_nsec=1000000};nanosleep(&t,NULL);}
static void wait_value(uint32_t*p,uint32_t value){uint64_t end=mono()+3000000000ull;while(load(p)!=value){CHECK(mono()<end);pause_thread();}}
static void wait_count(dm_host*h,size_t value){uint64_t end=mono()+3000000000ull;for(;;){
    pthread_mutex_lock(&h->mutex);size_t n=dm_host_mutex_waiters(h);pthread_mutex_unlock(&h->mutex);if(n==value)return;CHECK(mono()<end);pause_thread();}}
static uint64_t call(dm_host*h,unsigned svc,uint64_t a,uint64_t b,uint64_t c,uint64_t d){
#if defined(__aarch64__)
    // 已附着场景全部经过真正改写后的 ARM64 SVC 门，包括阻塞及跨线程移交。
    // Every attached scenario runs through rewritten ARM64 SVC gates, including blocking and cross-thread handoff.
    if(svc>=0x1a&&svc<=0x1d&&dm_guest_current()){
        typedef uint64_t(*fn)(uint64_t,uint64_t,uint64_t,uint64_t);
        return ((fn)(void*)(program.base+(svc-0x1a)*32))(a,b,c,d);
    }
#endif
    uint64_t r[31]={a,b,c,d};return dm_host_svc(h,svc,r);
}
static uint64_t unlock(dm_host*h,uint32_t*p){return call(h,0x1b,(uintptr_t)p,0,0,0);}
static uint64_t signal_key(dm_host*h,uint32_t*p,int count){return call(h,0x1d,(uintptr_t)p,(uint32_t)count,0,0);}
static uint64_t condition(dm_host*h,uint32_t*p,uint32_t*key,uint32_t me,int64_t ns){return call(h,0x1c,(uintptr_t)p,(uintptr_t)key,me,(uint64_t)ns);}
static uint64_t acquire(dm_host*h,uint32_t*p,uint32_t me){
    for(;;){uint32_t value=load(p);if(!value){if(__atomic_compare_exchange_n(p,&value,me,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST))return 0;continue;}
        uint32_t expected=value;if(!(value&MASK)&&!__atomic_compare_exchange_n(p,&expected,value|MASK,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST))continue;
        uint64_t rc=call(h,0x1a,value&~MASK,(uintptr_t)p,me,0);if(rc)return rc;if((load(p)&~MASK)==me)return 0;
    }
}
static void finish(dm_host*h,uint32_t handle){CHECK(!dm_guest_detach());CHECK(!call(h,0x16,handle,0,0,0));}
typedef struct{dm_host*h;uint32_t*p,*key;int priority,mode;uint32_t handle,ready,release;uint64_t result;unsigned*order;unsigned position;int64_t ns;}worker;
static void*run(void*arg){worker*w=arg;CHECK(!dm_guest_attach(w->h,&gates));uint32_t me;
    CHECK(!dm_guest_register_current_thread(w->priority,&me));store(&w->handle,me);
    if(w->mode==2){CHECK(!acquire(w->h,w->p,me));store(&w->ready,1);wait_value(&w->release,1);finish(w->h,me);return NULL;}
    if(w->mode==1){CHECK(!acquire(w->h,w->p,me));store(&w->ready,1);w->result=condition(w->h,w->p,w->key,me,w->ns);
        if(!w->result){if(w->order)w->position=__atomic_fetch_add(w->order,1,__ATOMIC_SEQ_CST);CHECK(!unlock(w->h,w->p));}}
    else{w->result=acquire(w->h,w->p,me);if(!w->result){if(w->order)w->position=__atomic_fetch_add(w->order,1,__ATOMIC_SEQ_CST);CHECK(!unlock(w->h,w->p));}}
    finish(w->h,me);return NULL;
}
static void lost_wake_roundtrips(dm_host*h,uint32_t*p,uint32_t*key,uint32_t me){
    // 发信线程只在用户态抢锁成功后读条件键，覆盖发布键与释放锁之间的竞争。
    // The signaling thread reads the key only after its user-mode lock succeeds, exercising key publication versus unlock.
    for(unsigned i=0;i<100;i++){
        store(p,0);store(key,0);worker w={.h=h,.p=p,.key=key,.priority=44,.mode=1,.ns=1000000000};pthread_t thread;
        CHECK(!pthread_create(&thread,NULL,run,&w));wait_value(&w.ready,1);
        CHECK(!acquire(h,p,me));CHECK(load(key)==1);CHECK(!signal_key(h,key,1));CHECK(!unlock(h,p));
        CHECK(!pthread_join(thread,NULL));CHECK(!w.result&&load(p)==0&&load(key)==0);
    }
}
int main(void){
#if defined(__aarch64__)
    prepare_gates();
#endif
    dm_host*h=dm_host_create(2*UNIT);CHECK(h);uint64_t r[31]={[1]=UNIT};CHECK(!dm_host_svc(h,1,r));uint32_t*p=(uint32_t*)(uintptr_t)r[1],*key=p+1;
    CHECK(unlock(h,p)==DM_SVC_BAD_STATE);uint32_t me;CHECK(!dm_guest_bootstrap_main(h,&gates,&me));
    CHECK(dm_guest_current()->thread_handle==me);CHECK(!call(h,0x25,0,me,0,0));
    CHECK(unlock(h,NULL)==DM_SVC_INVALID_ADDRESS);CHECK(unlock(h,(uint32_t*)((char*)p+1))==DM_SVC_INVALID_ADDRESS);
    store(p,me);CHECK(condition(h,p,p,me,0)==DM_SVC_INVALID_ADDRESS&&load(p)==me);
    CHECK(condition(h,p,key,me+1,0)==DM_SVC_BAD_STATE&&load(p)==me&&load(key)==0);
    CHECK(condition(h,p,key,me,0)==TIMEOUT&&load(p)==0&&load(key)==1);CHECK(!signal_key(h,key,0)&&load(key)==0);
    CHECK(call(h,0x1a,0x1234,(uintptr_t)p,me,0)==0);store(p,0x1234|MASK);
    CHECK(call(h,0x1a,0x1234,(uintptr_t)p,me,0)==0xe401u);store(p,me|MASK);
    CHECK(call(h,0x1a,me,(uintptr_t)p,me,0)==DM_SVC_BAD_STATE);store(p,me);
    worker ws[3];pthread_t ts[3];unsigned order=0;int priorities[3]={44,10,10};
    for(unsigned i=0;i<3;i++){ws[i]=(worker){.h=h,.p=p,.priority=priorities[i],.order=&order};CHECK(!pthread_create(&ts[i],NULL,run,&ws[i]));wait_count(h,i+1);}
    r[1]=0;CHECK(dm_host_svc(h,1,r)==DM_SVC_BAD_STATE);CHECK(dm_host_destroy(h)==EBUSY);
    // 关闭持有者句柄不能释放仍被等待者引用的线程对象。
    // Closing an owner's handle must not free a thread object retained by waiters.
    CHECK(!call(h,0x16,me,0,0,0));CHECK(!unlock(h,p));
    for(unsigned i=0;i<3;i++){CHECK(!pthread_join(ts[i],NULL));CHECK(!ws[i].result);}
    CHECK(ws[1].position==0&&ws[2].position==1&&ws[0].position==2);wait_count(h,0);
    CHECK(!dm_guest_detach());CHECK(!dm_guest_bootstrap_main(h,&gates,&me));
    worker owner={.h=h,.p=p,.priority=44,.mode=2},waiter={.h=h,.p=p,.priority=44};pthread_t a,b;
    CHECK(!pthread_create(&a,NULL,run,&owner));wait_value(&owner.ready,1);CHECK(!pthread_create(&b,NULL,run,&waiter));wait_count(h,1);
    store(&owner.release,1);CHECK(!pthread_join(a,NULL));CHECK(!pthread_join(b,NULL));CHECK(waiter.result==DM_SVC_BAD_STATE);wait_count(h,0);store(p,0);
    worker timed={.h=h,.p=p,.key=key,.priority=44,.mode=1,.ns=100000000};
    CHECK(!pthread_create(&a,NULL,run,&timed));wait_count(h,1);CHECK(!acquire(h,p,me));CHECK(!signal_key(h,key,1));
    CHECK(!pthread_join(a,NULL));CHECK(timed.result==TIMEOUT&&(load(p)&~MASK)==me&&load(key)==0);CHECK(!unlock(h,p));wait_count(h,0);
    order=0;
    for(unsigned i=0;i<3;i++){
        ws[i]=(worker){.h=h,.p=p,.key=key,.priority=priorities[i],.mode=1,.ns=3000000000ll,.order=&order};
        CHECK(!pthread_create(&ts[i],NULL,run,&ws[i]));wait_count(h,i+1);
    }
    CHECK(!acquire(h,p,me));CHECK(!signal_key(h,key,1)&&load(key)==1);CHECK(!unlock(h,p));
    CHECK(!pthread_join(ts[1],NULL));CHECK(!ws[1].result&&ws[1].position==0);wait_count(h,2);
    CHECK(!signal_key(h,key,-1));
    CHECK(!pthread_join(ts[0],NULL));CHECK(!pthread_join(ts[2],NULL));
    CHECK(!ws[0].result&&!ws[2].result&&ws[2].position==1&&ws[0].position==2&&load(key)==0);wait_count(h,0);
    order=0;
    worker transferred={.h=h,.p=p,.key=key,.priority=44,.mode=1,.ns=3000000000ll,.order=&order};
    worker queued={.h=h,.p=p,.priority=44,.order=&order};
    CHECK(!pthread_create(&a,NULL,run,&transferred));wait_count(h,1);CHECK(!acquire(h,p,me));
    CHECK(!pthread_create(&b,NULL,run,&queued));wait_count(h,2);CHECK(!signal_key(h,key,1));CHECK(!unlock(h,p));
    CHECK(!pthread_join(a,NULL));CHECK(!pthread_join(b,NULL));
    CHECK(!transferred.result&&!queued.result&&queued.position==0&&transferred.position==1);wait_count(h,0);
    lost_wake_roundtrips(h,p,key,me);
    finish(h,me);CHECK(!dm_host_destroy(h));
#if defined(__aarch64__)
    CHECK(!dm_image_unmap(&program));
#endif
    puts("PASS: ARM64 gates, registered main, priority/FIFO handoff, closed owner, owner exit, condition transfer/timeout and 100 lost-wake races");return 0;
}
