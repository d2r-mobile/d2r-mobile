// 事件 SVC 语义测试；依据上游线程 ABI，验证端点权限、手动复位、等待索引／超时、关闭时保留对象与代际句柄。
// Event SVC semantics tests; based on upstream thread ABI, verify endpoint rights, manual reset, index/timeout, close-while-wait references and generation handles.
#include "../runtime/internal.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define CHECK(c)do{if(!(c)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);abort();}}while(0)
#define TIMEOUT 0xea01u
#define BAD_HANDLE 0xe401u
typedef struct{uint32_t write,read;}pair;
static pair create(dm_host*h){uint64_t r[31]={0};CHECK(!dm_host_svc(h,0x45,r));return(pair){(uint32_t)r[1],(uint32_t)r[2]};}
static uint64_t call(dm_host*h,unsigned n,uint32_t handle){uint64_t r[31]={handle};return dm_host_svc(h,n,r);}
static uint64_t wait_for(dm_host*h,const uint32_t*handles,size_t count,int64_t ns,size_t*index){
    uint64_t r[31]={0};r[1]=(uintptr_t)handles;r[2]=count;r[3]=(uint64_t)ns;uint64_t result=dm_host_svc(h,0x18,r);*index=(size_t)r[1];return result;
}
typedef struct{dm_host*host;uint32_t handles[2];size_t count,index;uint64_t result;}worker;
static void*wait_worker(void*arg){worker*w=arg;dm_gate_table gates={0};CHECK(!dm_guest_attach(w->host,&gates));
    w->result=wait_for(w->host,w->handles,w->count,-1,&w->index);CHECK(!dm_guest_detach());return NULL;}
static uint64_t mono(void){struct timespec t;CHECK(!clock_gettime(CLOCK_MONOTONIC,&t));return(uint64_t)t.tv_sec*1000000000u+t.tv_nsec;}
static void wait_until_registered(dm_host*h){uint64_t deadline=mono()+2000000000u;
    for(;;){pthread_mutex_lock(&h->mutex);int busy=dm_host_sync_busy(h);pthread_mutex_unlock(&h->mutex);if(busy)return;
        CHECK(mono()<deadline);struct timespec pause={.tv_nsec=1000000};nanosleep(&pause,NULL);}}
int main(void){
    dm_host*h=dm_host_create(4u*1024u*1024u);CHECK(h);size_t index=123;
    pair a=create(h),b=create(h);uint32_t handles[]={a.read,b.read};
    CHECK(wait_for(h,handles,2,0,&index)==TIMEOUT&&index==0);
    CHECK(wait_for(h,handles,0,0,&index)==DM_SVC_INVALID_SIZE);
    CHECK(wait_for(h,NULL,1,0,&index)==DM_SVC_INVALID_ADDRESS);
    CHECK(wait_for(h,&a.write,1,0,&index)==BAD_HANDLE);
    uint32_t invalid[]={a.read,0};CHECK(wait_for(h,invalid,2,0,&index)==BAD_HANDLE);
    CHECK(call(h,0x11,a.read)==BAD_HANDLE&&call(h,0x17,a.write)==BAD_HANDLE);
    CHECK(call(h,0x17,a.read)==DM_SVC_BAD_STATE);
    CHECK(!call(h,0x11,b.write));CHECK(!wait_for(h,handles,2,0,&index)&&index==1);
    CHECK(!wait_for(h,handles,2,0,&index)&&index==1);CHECK(!call(h,0x11,a.write));
    CHECK(!wait_for(h,handles,2,0,&index)&&index==0);CHECK(!call(h,0x12,a.write));CHECK(!call(h,0x17,b.read));
    uint64_t start=mono();CHECK(wait_for(h,handles,2,1000000,&index)==TIMEOUT&&mono()-start>=500000);
    worker w={.host=h,.handles={a.read,b.read},.count=2};pthread_t thread;CHECK(!pthread_create(&thread,NULL,wait_worker,&w));
    wait_until_registered(h);CHECK(dm_host_destroy(h)==EBUSY);CHECK(!call(h,0x11,b.write));CHECK(!pthread_join(thread,NULL));CHECK(!w.result&&w.index==1);
    CHECK(!call(h,0x12,b.read));w=(worker){.host=h,.handles={a.read},.count=1};CHECK(!pthread_create(&thread,NULL,wait_worker,&w));
    wait_until_registered(h);CHECK(!call(h,0x16,a.read));CHECK(!call(h,0x11,a.write));CHECK(!pthread_join(thread,NULL));CHECK(!w.result&&w.index==0);
    CHECK(call(h,0x16,a.read)==BAD_HANDLE);CHECK(!call(h,0x16,a.write));CHECK(!call(h,0x16,b.read));CHECK(!call(h,0x16,b.write));
    pair replacement=create(h);CHECK(replacement.write!=a.write&&replacement.read!=a.read);CHECK(call(h,0x11,a.write)==BAD_HANDLE);
    CHECK(!call(h,0x16,replacement.write));CHECK(!call(h,0x16,replacement.read));
    pair many[64];for(unsigned i=0;i<64;i++)many[i]=create(h);
    uint64_t r[31]={0,123,456};CHECK(dm_host_svc(h,0x45,r)==0xce01&&r[1]==123&&r[2]==456);
    for(unsigned i=0;i<64;i++){CHECK(!call(h,0x16,many[i].write));CHECK(!call(h,0x16,many[i].read));}
    CHECK(!dm_host_destroy(h));puts("PASS: Horizon event SVC rights, manual reset, wait-any indexes/timeouts, pinned close, stale handles and capacity");return 0;
}
