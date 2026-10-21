// Horizon 32 位地址仲裁；依据 libnx SVC ABI 与公开地址仲裁语义，用共享锁保证谓词检查／入队／唤醒原子性。
// Horizon 32-bit address arbitration; based on libnx SVC ABI / public arbiter semantics, serialize predicate checks, enrollment and signaling.
#include "internal.h"
#include <errno.h>
#include <stdlib.h>
#include <time.h>
#define TIMEOUT 0xea01u
#define INVALID_ENUM 0xf001u
#define OUT_OF_RESOURCE 0xce01u
typedef struct address_waiter{struct address_waiter*next;uintptr_t address;int notified;}address_waiter;
struct dm_arbiter{pthread_cond_t condition;address_waiter*head;size_t waiters;};
size_t dm_host_arbiter_waiters(dm_host*h){return h->arbiter?h->arbiter->waiters:0;}
void dm_host_arbiter_close(dm_host*h){if(h->arbiter){pthread_cond_destroy(&h->arbiter->condition);free(h->arbiter);h->arbiter=NULL;}}
static int ensure(dm_host*h){
    if(h->arbiter)return 0;struct dm_arbiter*a=calloc(1,sizeof(*a));if(!a)return ENOMEM;
    pthread_condattr_t attr;int rc=pthread_condattr_init(&attr);if(rc){free(a);return rc;}
#if !defined(__APPLE__)
    rc=pthread_condattr_setclock(&attr,CLOCK_MONOTONIC);
#endif
    if(!rc)rc=pthread_cond_init(&a->condition,&attr);pthread_condattr_destroy(&attr);
    if(rc){free(a);return rc;}h->arbiter=a;return 0;
}
static int wait_condition(dm_host*h,const struct timespec*deadline){
#if defined(__APPLE__)
    struct timespec now;if(clock_gettime(CLOCK_MONOTONIC,&now))return errno;
    struct timespec left={.tv_sec=deadline->tv_sec-now.tv_sec,.tv_nsec=deadline->tv_nsec-now.tv_nsec};
    if(left.tv_nsec<0){left.tv_nsec+=1000000000;left.tv_sec--;}if(left.tv_sec<0)return ETIMEDOUT;
    return pthread_cond_timedwait_relative_np(&h->arbiter->condition,&h->mutex,&left);
#else
    return pthread_cond_timedwait(&h->arbiter->condition,&h->mutex,deadline);
#endif
}
static uint64_t wait_address(dm_host*h,uint32_t*address,unsigned type,uint32_t value,int64_t ns,const struct timespec*deadline){
    uint32_t observed=__atomic_load_n(address,__ATOMIC_SEQ_CST);
    if(type==2?observed!=value:(int32_t)observed>=(int32_t)value)return DM_SVC_BAD_STATE;
    // 非零等待先预留队列资源，避免资源不足时留下无对应等待者的减计数；零超时仍须执行减法。
    // Reserve queue resources before a nonzero wait so exhaustion cannot leave an unmatched decrement; zero timeout still performs the decrement.
    if(ns&&(ensure(h)||h->arbiter->waiters==128))return OUT_OF_RESOURCE;
    if(type==1){
        for(;;){
            if((int32_t)observed>=(int32_t)value)return DM_SVC_BAD_STATE;
            if(__atomic_compare_exchange_n(address,&observed,observed-1,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST))break;
        }
    }
    if(!ns)return TIMEOUT;
    struct dm_arbiter*a=h->arbiter;address_waiter waiter={.address=(uintptr_t)address};
    // shortcut: 目前同一地址按 FIFO 唤醒；接入客体线程优先级后改为优先级／FIFO 排序。
    // shortcut: currently wake same-address waiters FIFO; order by priority/FIFO once guest thread priorities are available.
    address_waiter**tail=&a->head;while(*tail)tail=&(*tail)->next;*tail=&waiter;a->waiters++;
    int rc=0;while(!waiter.notified&&!rc)rc=ns<0?pthread_cond_wait(&a->condition,&h->mutex):wait_condition(h,deadline);
    address_waiter**link=&a->head;while(*link!=&waiter)link=&(*link)->next;*link=waiter.next;a->waiters--;
    return waiter.notified?DM_SVC_OK:rc==ETIMEDOUT?TIMEOUT:DM_SVC_BAD_STATE;
}
static uint64_t signal_address(dm_host*h,uint32_t*address,unsigned type,uint32_t value,int32_t count){
    struct dm_arbiter*a=h->arbiter;size_t waiting=0;
    for(address_waiter*w=a?a->head:NULL;w;w=w->next)if(w->address==(uintptr_t)address&&!w->notified)waiting++;
    if(type){
        uint32_t next=value+1;
        if(type==2&&waiting)next=(count<=0||waiting<=(uint32_t)count)?value-1:value;
        uint32_t expected=value;
        if(next==value){if(__atomic_load_n(address,__ATOMIC_SEQ_CST)!=value)return DM_SVC_BAD_STATE;}
        else if(!__atomic_compare_exchange_n(address,&expected,next,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST))return DM_SVC_BAD_STATE;
    }
    size_t signaled=0;
    for(address_waiter*w=a?a->head:NULL;w&&(count<=0||signaled<(uint32_t)count);w=w->next){
        if(w->address!=(uintptr_t)address||w->notified)continue;w->notified=1;signaled++;
    }
    if(signaled)pthread_cond_broadcast(&a->condition);return DM_SVC_OK;
}
uint64_t dm_host_arbiter_svc(dm_host*h,unsigned number,uint64_t*r){
    if(number!=0x34&&number!=0x35)return DM_SVC_NOT_IMPLEMENTED;
    unsigned type=(uint32_t)r[1];uintptr_t address=(uintptr_t)r[0];
    if(!address||address%4||address>UINTPTR_MAX-4)return DM_SVC_INVALID_ADDRESS;
    if(type>2)return INVALID_ENUM;
    struct timespec deadline={0};int64_t ns=(int64_t)r[3];
    if(number==0x34&&ns>0){if(clock_gettime(CLOCK_MONOTONIC,&deadline))return DM_SVC_BAD_STATE;
        deadline.tv_sec+=ns/1000000000;deadline.tv_nsec+=ns%1000000000;
        if(deadline.tv_nsec>=1000000000){deadline.tv_nsec-=1000000000;deadline.tv_sec++;}}
    pthread_mutex_lock(&h->mutex);uint64_t result=DM_SVC_INVALID_ADDRESS;
    if(dm_host_writable(h,address,4))result=number==0x34?
        wait_address(h,(uint32_t*)address,type,(uint32_t)r[2],ns,&deadline):
        signal_address(h,(uint32_t*)address,type,(uint32_t)r[2],(int32_t)r[3]);
    pthread_mutex_unlock(&h->mutex);return result;
}
