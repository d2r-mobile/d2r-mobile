// Horizon 互斥／条件服务；共享宿主锁串行化入队和移交，客体锁字仍使用原子访问。
// Horizon mutex/condition services; the host lock serializes enrollment/handoff, while guest lock words remain atomic.
#include "internal.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <time.h>
#define WAIT_MASK 0x40000000u
#define TIMEOUT 0xea01u
#define BAD_HANDLE 0xe401u
#define OUT_OF_RESOURCE 0xce01u
#define NO_OWNER UINT_MAX
enum{LOCK=1,CONDITION=2};
typedef struct mutex_waiter{
    struct mutex_waiter*next;uint32_t *address,*key,tag;unsigned self,owner;int kind,notified;
    uint64_t result;
}mutex_waiter;
struct dm_mutex_state{pthread_cond_t condition;mutex_waiter*head;size_t waiters;};
size_t dm_host_mutex_waiters(dm_host*h){return h->mutex_state?h->mutex_state->waiters:0;}
void dm_host_mutex_close(dm_host*h){if(h->mutex_state){pthread_cond_destroy(&h->mutex_state->condition);free(h->mutex_state);h->mutex_state=NULL;}}
static uint32_t load(uint32_t*p){return __atomic_load_n(p,__ATOMIC_SEQ_CST);}
static void store(uint32_t*p,uint32_t value){__atomic_store_n(p,value,__ATOMIC_SEQ_CST);}
static int ensure(dm_host*h){
    if(h->mutex_state)return 0;struct dm_mutex_state*s=calloc(1,sizeof(*s));if(!s)return ENOMEM;
    pthread_condattr_t attr;int rc=pthread_condattr_init(&attr);if(rc){free(s);return rc;}
#if !defined(__APPLE__)
    rc=pthread_condattr_setclock(&attr,CLOCK_MONOTONIC);
#endif
    if(!rc)rc=pthread_cond_init(&s->condition,&attr);pthread_condattr_destroy(&attr);
    if(rc){free(s);return rc;}h->mutex_state=s;return 0;
}
static int timed_wait(dm_host*h,const struct timespec*deadline){
#if defined(__APPLE__)
    struct timespec now;if(clock_gettime(CLOCK_MONOTONIC,&now))return errno;
    struct timespec left={.tv_sec=deadline->tv_sec-now.tv_sec,.tv_nsec=deadline->tv_nsec-now.tv_nsec};
    if(left.tv_nsec<0){left.tv_nsec+=1000000000;left.tv_sec--;}if(left.tv_sec<0)return ETIMEDOUT;
    return pthread_cond_timedwait_relative_np(&h->mutex_state->condition,&h->mutex,&left);
#else
    return pthread_cond_timedwait(&h->mutex_state->condition,&h->mutex,deadline);
#endif
}
static void unpin(dm_host*h,mutex_waiter*w){if(w->owner!=NO_OWNER){dm_host_thread_unref(h,w->owner);w->owner=NO_OWNER;}}
static void wake(dm_host*h,mutex_waiter*w,uint64_t result){unpin(h,w);w->result=result;w->notified=1;}
static mutex_waiter*pick(dm_host*h,int kind,uint32_t*key,unsigned owner){
    mutex_waiter*best=NULL;
    for(mutex_waiter*w=h->mutex_state?h->mutex_state->head:NULL;w;w=w->next){
        if(w->notified||w->kind!=kind||(kind==LOCK?(w->address!=key||w->owner!=owner):w->key!=key))continue;
        if(!best||dm_host_thread_priority(h,w->self)<dm_host_thread_priority(h,best->self))best=w;
    }return best;
}
static void handoff(dm_host*h,uint32_t*address,unsigned owner){
    mutex_waiter*next=pick(h,LOCK,address,owner);uint32_t value=0;
    if(next){
        value=next->tag;
        for(mutex_waiter*w=h->mutex_state->head;w;w=w->next){
            if(w==next||w->notified||w->kind!=LOCK||w->address!=address||w->owner!=owner)continue;
            dm_host_thread_ref(h,next->self);unpin(h,w);w->owner=next->self;value|=WAIT_MASK;
        }
    }
    store(address,value);if(next)wake(h,next,0);
    if(h->mutex_state)pthread_cond_broadcast(&h->mutex_state->condition);
}
void dm_host_mutex_owner_ended(dm_host*h,unsigned object){
    if(!h->mutex_state)return;
    for(mutex_waiter*w=h->mutex_state->head;w;w=w->next)
        if(!w->notified&&w->kind==LOCK&&w->owner==object)wake(h,w,DM_SVC_BAD_STATE);
    pthread_cond_broadcast(&h->mutex_state->condition);
}
static void append(dm_host*h,mutex_waiter*w){mutex_waiter**link=&h->mutex_state->head;while(*link)link=&(*link)->next;*link=w;h->mutex_state->waiters++;}
static uint64_t block(dm_host*h,mutex_waiter*w,int64_t ns,const struct timespec*deadline){
    int rc=0;while(!w->notified&&!rc)rc=ns<0?pthread_cond_wait(&h->mutex_state->condition,&h->mutex):timed_wait(h,deadline);
    mutex_waiter**link=&h->mutex_state->head;while(*link!=w)link=&(*link)->next;*link=w->next;h->mutex_state->waiters--;
    // 超时不重新取锁；SDK 在返回后自行重试，等待标志可由持有者清除。
    // Timeout does not reacquire the lock; the SDK retries after return, and the owner can clear the waiter flag.
    unpin(h,w);return w->notified?w->result:rc==ETIMEDOUT?TIMEOUT:DM_SVC_BAD_STATE;
}
static uint64_t lock_wait(dm_host*h,uint32_t*address,uint32_t owner,uint32_t tag,unsigned self){
    if(load(address)!=(owner|WAIT_MASK))return 0;
    if(ensure(h)||h->mutex_state->waiters==128)return OUT_OF_RESOURCE;
    unsigned object;if(!dm_host_thread_retain(h,owner,&object))return BAD_HANDLE;
    if(object==self){dm_host_thread_unref(h,object);return DM_SVC_BAD_STATE;}
    mutex_waiter w={.address=address,.tag=tag,.self=self,.owner=object,.kind=LOCK};append(h,&w);
    return block(h,&w,-1,NULL);
}
static uint64_t condition_wait(dm_host*h,uint32_t*address,uint32_t*key,uint32_t tag,unsigned self,int64_t ns,const struct timespec*deadline){
    if((load(address)&~WAIT_MASK)!=tag)return DM_SVC_BAD_STATE;
    if(ns&&(ensure(h)||h->mutex_state->waiters==128))return OUT_OF_RESOURCE;
    mutex_waiter w={.address=address,.key=key,.tag=tag,.self=self,.owner=NO_OWNER,.kind=CONDITION};
    // SDK 在用户态检查条件键，因此必须先置键，再释放互斥锁，避免丢失唤醒。
    // The SDK checks the condition key in user mode: publish it before releasing the mutex to prevent lost wakeups.
    store(key,1);if(ns)append(h,&w);handoff(h,address,self);
    return ns?block(h,&w,ns,deadline):TIMEOUT;
}
static uint64_t condition_signal(dm_host*h,uint32_t*key,int32_t count){
    for(int64_t n=0;count<=0||n<count;n++){
        mutex_waiter*w=pick(h,CONDITION,key,NO_OWNER);if(!w)break;
        // 转入互斥队列时排到同优先级现有等待者之后。
        // Join the mutex queue after existing waiters of the same priority.
        mutex_waiter**link=&h->mutex_state->head;while(*link!=w)link=&(*link)->next;*link=w->next;
        while(*link)link=&(*link)->next;w->next=NULL;*link=w;
        w->kind=LOCK;uint32_t observed=load(w->address);
        for(;;){uint32_t next=observed?observed|WAIT_MASK:w->tag;
            if(__atomic_compare_exchange_n(w->address,&observed,next,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST))break;}
        if(!observed)wake(h,w,0);
        else if(!dm_host_thread_retain(h,observed&~WAIT_MASK,&w->owner))wake(h,w,DM_SVC_BAD_STATE);
        else if(w->owner==w->self)wake(h,w,DM_SVC_BAD_STATE);
    }
    if(!pick(h,CONDITION,key,NO_OWNER))store(key,0);
    if(h->mutex_state)pthread_cond_broadcast(&h->mutex_state->condition);return 0;
}
uint64_t dm_host_mutex_svc(dm_host*h,unsigned number,uint64_t*r){
    if(number<0x1a||number>0x1d)return DM_SVC_NOT_IMPLEMENTED;
    uintptr_t address=(uintptr_t)r[number==0x1a?1:0],key=(uintptr_t)r[1];
    if(!address||address%4||address>UINTPTR_MAX-4)return DM_SVC_INVALID_ADDRESS;
    if(number==0x1c&&(!key||key%4||key>UINTPTR_MAX-4||key==address))return DM_SVC_INVALID_ADDRESS;
    int64_t ns=(int64_t)r[3];struct timespec deadline={0};
    if(number==0x1c&&ns>0){if(clock_gettime(CLOCK_MONOTONIC,&deadline))return DM_SVC_BAD_STATE;
        deadline.tv_sec+=ns/1000000000;deadline.tv_nsec+=ns%1000000000;
        if(deadline.tv_nsec>=1000000000){deadline.tv_nsec-=1000000000;deadline.tv_sec++;}}
    pthread_mutex_lock(&h->mutex);uint64_t result=DM_SVC_INVALID_ADDRESS;unsigned self;uint32_t tag;int priority;
    if(!dm_host_writable(h,address,4)||(number==0x1c&&!dm_host_writable(h,key,4)))goto done;
    result=DM_SVC_BAD_STATE;if(!dm_host_thread_current(h,&self,&tag,&priority))goto done;
    if((number==0x1a||number==0x1c)&&(uint32_t)r[2]!=tag)goto done;
    if(number==0x1a)result=lock_wait(h,(uint32_t*)address,(uint32_t)r[0],tag,self);
    else if(number==0x1b){if((load((uint32_t*)address)&~WAIT_MASK)!=tag)goto done;handoff(h,(uint32_t*)address,self);result=0;}
    else if(number==0x1c)result=condition_wait(h,(uint32_t*)address,(uint32_t*)key,tag,self,ns,&deadline);
    else result=condition_signal(h,(uint32_t*)address,(int32_t)r[1]);
done:pthread_mutex_unlock(&h->mutex);return result;
}
