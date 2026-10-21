// 无游戏地址仲裁测试；依据 SVC 0x34/0x35，覆盖谓词、原子修改、FIFO／计数、超时、内存边界与堆生命周期。
// Game-free address arbiter tests; based on SVC 0x34/0x35, cover predicates, atomic updates, FIFO/count, timeouts, memory bounds and heap lifetime.
#include "../runtime/internal.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define CHECK(c)do{if(!(c)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);abort();}}while(0)
#define UNIT (2u*1024u*1024u)
#define TIMEOUT 0xea01u
static uint64_t wait_address(dm_host*h,uint32_t*p,unsigned type,uint32_t value,int64_t ns){
    uint64_t r[31]={(uintptr_t)p,type,value,(uint64_t)ns};uint64_t result=dm_host_svc(h,0x34,r);
    CHECK(r[1]==type&&r[2]==value&&r[3]==(uint64_t)ns&&r[0]==result);return result;
}
static uint64_t signal_address(dm_host*h,uint32_t*p,unsigned type,uint32_t value,int32_t count){
    uint64_t r[31]={(uintptr_t)p,type,value,(uint32_t)count};return dm_host_svc(h,0x35,r);
}
static uint32_t load(uint32_t*p){return __atomic_load_n(p,__ATOMIC_SEQ_CST);}
static void store(uint32_t*p,uint32_t value){__atomic_store_n(p,value,__ATOMIC_SEQ_CST);}
static uint64_t mono(void){struct timespec t;CHECK(!clock_gettime(CLOCK_MONOTONIC,&t));return(uint64_t)t.tv_sec*1000000000u+t.tv_nsec;}
static void wait_count(dm_host*h,size_t wanted){uint64_t deadline=mono()+2000000000u;
    for(;;){pthread_mutex_lock(&h->mutex);size_t n=dm_host_arbiter_waiters(h);pthread_mutex_unlock(&h->mutex);
        if(n==wanted)return;CHECK(mono()<deadline);struct timespec pause={.tv_nsec=1000000};nanosleep(&pause,NULL);}}
typedef struct{dm_host*host;uint32_t*address;uint64_t result;int done;}worker;
static void*wait_worker(void*arg){worker*w=arg;w->result=wait_address(w->host,w->address,2,7,3000000000ll);
    __atomic_store_n(&w->done,1,__ATOMIC_SEQ_CST);return NULL;}
static void wait_value(dm_host*h,uint32_t*p){
    while(!load(p)){uint64_t r=wait_address(h,p,2,0,2000000000ll);CHECK(!r||r==DM_SVC_BAD_STATE);}
}
typedef struct{dm_host*host;uint32_t*request,*reply;}pingpong;
static void*ping_worker(void*arg){pingpong*p=arg;for(unsigned i=0;i<500;i++){
    wait_value(p->host,p->request);store(p->request,0);CHECK(!signal_address(p->host,p->reply,1,0,1));}return NULL;}
static void rejects_full_queue_without_decrementing(dm_host*h,uint32_t*p){
    store(p,7);worker workers[128];pthread_t threads[128];
    for(unsigned i=0;i<128;i++){workers[i]=(worker){.host=h,.address=p};
        CHECK(!pthread_create(&threads[i],NULL,wait_worker,&workers[i]));wait_count(h,i+1);}
    CHECK(wait_address(h,p,1,8,1000000000)==0xce01&&load(p)==7);
    CHECK(wait_address(h,p,1,8,0)==TIMEOUT&&load(p)==6);
    CHECK(!signal_address(h,p,0,0,0));
    for(unsigned i=0;i<128;i++){CHECK(!pthread_join(threads[i],NULL));CHECK(!workers[i].result);}
    wait_count(h,0);
}
int main(void){
    dm_host*h=dm_host_create(2*UNIT);CHECK(h);
    uint64_t r[31]={[1]=UNIT};CHECK(!dm_host_svc(h,1,r));uint32_t*p=(uint32_t*)(uintptr_t)r[1];
    uint32_t external[32]={0};
    CHECK(dm_host_register_writable(NULL,external,sizeof(external))==EINVAL);
    CHECK(dm_host_register_writable(h,NULL,4)==EINVAL&&dm_host_register_writable(h,external,0)==EINVAL);
    CHECK(dm_host_register_writable(h,(void*)(UINTPTR_MAX-1),4)==EINVAL);
    CHECK(dm_host_register_writable(h,p,4)==EINVAL);
    CHECK(wait_address(h,external,2,0,0)==DM_SVC_INVALID_ADDRESS);
    CHECK(!dm_host_register_writable(h,external,16*sizeof(*external)));
    CHECK(dm_host_register_writable(h,external+1,4)==EINVAL);
    CHECK(wait_address(h,external+16,2,0,0)==DM_SVC_INVALID_ADDRESS);
    CHECK(wait_address(h,external+15,2,0,0)==TIMEOUT);
    const dm_gate_table gates={0};CHECK(!dm_guest_attach(h,&gates));
    CHECK(dm_host_register_writable(h,external+16,4)==EBUSY);CHECK(!dm_guest_detach());
    CHECK(wait_address(h,NULL,2,0,0)==DM_SVC_INVALID_ADDRESS);
    CHECK(wait_address(h,(uint32_t*)((uint8_t*)p+1),2,0,0)==DM_SVC_INVALID_ADDRESS);
    CHECK(wait_address(h,(uint32_t*)(UINTPTR_MAX-3),2,0,0)==DM_SVC_INVALID_ADDRESS);
    CHECK(wait_address(h,p+UNIT/4,2,0,0)==DM_SVC_INVALID_ADDRESS);
    CHECK(wait_address(h,p,3,0,0)==0xf001&&signal_address(h,p,3,0,1)==0xf001);
    CHECK(wait_address(h,p,2,1,0)==DM_SVC_BAD_STATE);
    CHECK(wait_address(h,p,0,0,0)==DM_SVC_BAD_STATE);
    CHECK(wait_address(h,p,1,0,0)==DM_SVC_BAD_STATE&&load(p)==0);
    CHECK(wait_address(h,p,1,1,0)==TIMEOUT&&load(p)==UINT32_MAX);
    CHECK(wait_address(h,p,0,0,0)==TIMEOUT);
    store(p,0x80000000u);CHECK(wait_address(h,p,1,0,0)==TIMEOUT&&load(p)==0x7fffffffu);
    CHECK(!signal_address(h,p,1,0x7fffffffu,1)&&load(p)==0x80000000u);
    CHECK(signal_address(h,p,1,7,1)==DM_SVC_BAD_STATE&&load(p)==0x80000000u);
    store(p,UINT32_MAX);CHECK(!signal_address(h,p,2,UINT32_MAX,0)&&load(p)==0);
    uint64_t start=mono();CHECK(wait_address(h,p,2,0,1000000)==TIMEOUT&&mono()-start>=500000);
    wait_count(h,0);
    store(p,7);worker workers[4];pthread_t threads[4];
    for(unsigned i=0;i<4;i++){workers[i]=(worker){.host=h,.address=p};CHECK(!pthread_create(&threads[i],NULL,wait_worker,&workers[i]));wait_count(h,i+1);}
    CHECK(dm_host_destroy(h)==EBUSY);
    r[1]=0;CHECK(dm_host_svc(h,1,r)==DM_SVC_BAD_STATE);
    r[1]=2*UNIT;CHECK(!dm_host_svc(h,1,r)&&r[1]==(uintptr_t)p);
    CHECK(dm_host_register_writable(h,external+16,4)==EBUSY);
    CHECK(!signal_address(h,p,2,7,2)&&load(p)==7);
    for(unsigned i=0;i<2;i++){CHECK(!pthread_join(threads[i],NULL));CHECK(!workers[i].result);}
    wait_count(h,2);CHECK(!__atomic_load_n(&workers[2].done,__ATOMIC_SEQ_CST)&&!__atomic_load_n(&workers[3].done,__ATOMIC_SEQ_CST));
    CHECK(!signal_address(h,p,2,7,2)&&load(p)==6);
    for(unsigned i=2;i<4;i++){CHECK(!pthread_join(threads[i],NULL));CHECK(!workers[i].result);}
    wait_count(h,0);CHECK(!signal_address(h,p,2,6,1)&&load(p)==7);
    for(unsigned i=0;i<2;i++){store(p+i,7);workers[i]=(worker){.host=h,.address=p+i};
        CHECK(!pthread_create(&threads[i],NULL,wait_worker,&workers[i]));wait_count(h,i+1);}
    CHECK(!signal_address(h,p,0,0,1));CHECK(!pthread_join(threads[0],NULL));wait_count(h,1);
    CHECK(!workers[0].result&&!__atomic_load_n(&workers[1].done,__ATOMIC_SEQ_CST));
    CHECK(!signal_address(h,p+1,0,0,-1));CHECK(!pthread_join(threads[1],NULL));CHECK(!workers[1].result);
    r[1]=UNIT;CHECK(!dm_host_svc(h,1,r));
    store(p,0);store(p+1,0);pingpong ping={h,p,p+1};pthread_t thread;
    CHECK(!pthread_create(&thread,NULL,ping_worker,&ping));
    for(unsigned i=0;i<500;i++){CHECK(!signal_address(h,p,1,0,1));wait_value(h,p+1);store(p+1,0);}
    CHECK(!pthread_join(thread,NULL));wait_count(h,0);
    rejects_full_queue_without_decrementing(h,p);
    for(unsigned i=16;i<31;i++)CHECK(!dm_host_register_writable(h,external+i,4));
    CHECK(dm_host_register_writable(h,external+31,4)==ENOSPC);
    CHECK(!dm_host_destroy(h));
    puts("PASS: address predicates/wraparound, registered bounds, FIFO/count/isolation, monotonic timeout, heap pinning and 500 round trips");return 0;
}
