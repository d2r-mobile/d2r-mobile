// Horizon 事件／线程句柄与 WaitSynchronization；依据上游 hle_thread.c ABI，通过公共 pthread 与独立客体栈执行。
// Horizon event/thread handles and WaitSynchronization; based on upstream hle_thread.c ABI, use public pthreads and separate guest stacks.
#include "internal.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "bridge.h"
#define HANDLES 128u
#define EVENTS 64u
#define OBJECTS 128u
#define BAD_HANDLE 0xe401u
#define TIMEOUT 0xea01u
#define OUT_OF_RESOURCE 0xce01u
typedef struct sync_waiter sync_waiter;
typedef struct{
    dm_host*host;const dm_gate_table*gates;uint64_t entry,arg,id;uint32_t handle;
    unsigned object,stack;int started,startup_ready,startup_error,external,priority,automatic_stack;
    uintptr_t return_stack,initial_sp;
    dm_bridge_record *bridge;
    int wait_cancelled;
    sync_waiter *wait;
}guest_thread;
typedef struct{size_t refs;int signaled;guest_thread*thread;}sync_object;
struct sync_waiter{sync_waiter*next;guest_thread*thread;const unsigned*objects;size_t count,index;uint64_t result;int done;};
struct dm_sync{
    pthread_cond_t condition;size_t waiters,stack_pins,running,starting;
    sync_object objects[OBJECTS];
    struct{uint32_t generation;unsigned event,kind;}handles[HANDLES];
    sync_waiter *wait_list;
};
size_t dm_host_threads_pinned(dm_host*h){return h->sync?h->sync->stack_pins:0;}
size_t dm_host_sync_waiters(dm_host*h){return h->sync?h->sync->waiters:0;}
int dm_host_sync_busy(dm_host*h){return h->sync&&(h->sync->waiters||h->sync->stack_pins||h->sync->running||h->sync->starting);}
void dm_host_sync_close(dm_host*h){if(h->sync){
    for(unsigned i=EVENTS;i<OBJECTS;i++)free(h->sync->objects[i].thread);
    pthread_cond_destroy(&h->sync->condition);free(h->sync);h->sync=NULL;
}}
static int ensure(dm_host*h){
    if(h->sync)return 0;struct dm_sync*s=calloc(1,sizeof(*s));if(!s)return ENOMEM;
    pthread_condattr_t attr;int rc=pthread_condattr_init(&attr);if(rc){free(s);return rc;}
#if !defined(__APPLE__)
    rc=pthread_condattr_setclock(&attr,CLOCK_MONOTONIC);
#endif
    if(!rc)rc=pthread_cond_init(&s->condition,&attr);pthread_condattr_destroy(&attr);
    if(rc){free(s);return rc;}for(unsigned i=0;i<HANDLES;i++)s->handles[i].generation=1;
    h->sync=s;return 0;
}
static int lookup(struct dm_sync*s,uint32_t handle,unsigned kind){
    unsigned slot=handle&255u;if(!s||!slot||slot>HANDLES)return -1;slot--;
    return s->handles[slot].kind&&(!kind||s->handles[slot].kind==kind)&&s->handles[slot].generation==(handle>>8)?(int)slot:-1;
}
static uint32_t new_handle(struct dm_sync*s,unsigned slot,unsigned event,unsigned kind){
    s->handles[slot].event=event;s->handles[slot].kind=kind;s->objects[event].refs++;
    return(s->handles[slot].generation<<8)|(slot+1);
}
// IPC 会话句柄与事件／线程共用同一表；对象存储由 ipc.c 拥有，这里只发布代际句柄。
// IPC session handles share the table with events/threads; object storage is owned by ipc.c, this only publishes a generation handle.
int dm_host_handle_alloc_locked(dm_host*h,unsigned kind,unsigned object,uint32_t*out){
    if(!out||!kind)return EINVAL;int rc=ensure(h);if(rc)return rc;
    struct dm_sync*s=h->sync;unsigned slot=0;
    while(slot<HANDLES&&(s->handles[slot].kind||!s->handles[slot].generation))slot++;
    if(slot==HANDLES)return ENOSPC;
    s->handles[slot].kind=kind;s->handles[slot].event=object;
    *out=(s->handles[slot].generation<<8)|(slot+1);return 0;
}
int dm_host_handle_lookup_locked(dm_host*h,uint32_t handle,unsigned kind,unsigned*slot,unsigned*object){
    if(!h->sync)return ESRCH;int found=lookup(h->sync,handle,kind);if(found<0)return ESRCH;
    if(slot)*slot=(unsigned)found;if(object)*object=h->sync->handles[found].event;return 0;
}
void dm_host_handle_clear_locked(dm_host*h,unsigned slot,uint32_t handle){
    struct dm_sync*s=h->sync;if(!s||slot>=HANDLES)return;
    if(((s->handles[slot].generation<<8)|(slot+1))!=(handle&0xffffffffu))return;
    s->handles[slot].kind=0;
    if(++s->handles[slot].generation==(1u<<22))s->handles[slot].generation=0;
}
// 在宿主锁内固定首次完成结果；恢复前的复位、后续信号或取消不能覆盖它。
// Latch the first completion under the host mutex; reset, later signals and cancellation cannot overwrite it before resumption.
static void complete_wait(struct dm_sync*s,sync_waiter*w,uint64_t result,size_t index){
    if(w->done)return;w->done=1;w->result=result;w->index=index;
    if(w->thread)w->thread->wait=NULL;
    pthread_cond_broadcast(&s->condition);
}
static void signal_object(struct dm_sync*s,unsigned object){
    s->objects[object].signaled=1;
    for(sync_waiter*w=s->wait_list;w;w=w->next)if(!w->done)
        for(size_t i=0;i<w->count;i++)if(w->objects[i]==object){complete_wait(s,w,0,i);break;}
}
static void release_stack(dm_host*h,guest_thread*t){
    h->thread_stacks[t->stack].in_use=0;h->sync->stack_pins--;
    if(t->automatic_stack)memset(&h->thread_stacks[t->stack],0,sizeof(h->thread_stacks[t->stack]));
}
static void release_object(dm_host*h,unsigned index){
    sync_object*o=&h->sync->objects[index];o->refs--;
    if(!o->refs&&o->thread){guest_thread*t=o->thread;
        if(!t->started&&!t->external)release_stack(h,t);
        free(t);o->thread=NULL;
    }
}
void dm_host_thread_ref(dm_host*h,unsigned object){h->sync->objects[object].refs++;}
void dm_host_thread_unref(dm_host*h,unsigned object){release_object(h,object);}
int dm_host_thread_priority(dm_host*h,unsigned object){return h->sync->objects[object].thread->priority;}
int dm_host_thread_retain(dm_host*h,uint32_t handle,unsigned*object){
    int slot=lookup(h->sync,handle,3);if(slot<0)return 0;
    unsigned index=h->sync->handles[slot].event;
    guest_thread*t=h->sync->objects[index].thread;
    if(!t->started||h->sync->objects[index].signaled)return 0;
    dm_host_thread_ref(h,index);*object=index;return 1;
}
int dm_host_thread_current(dm_host*h,unsigned*object,uint32_t*handle,int*priority){
    dm_guest_context*c=dm_guest_current();
    if(!c||c->host!=h||!c->thread_state)return 0;
    guest_thread*t=c->thread_state;*object=t->object;*handle=t->handle;*priority=t->priority;return 1;
}
int dm_guest_register_current_thread(int priority,uint32_t*out){
    dm_guest_context*c=dm_guest_current();if(!out||priority<0||priority>63)return EINVAL;
    if(!c)return ENOENT;if(c->thread_state)return EALREADY;
    dm_host*h=c->host;pthread_mutex_lock(&h->mutex);int rc=ENOMEM;
    if(ensure(h))goto done;struct dm_sync*s=h->sync;unsigned object=EVENTS,slot=0;
    while(object<OBJECTS&&s->objects[object].refs)object++;
    while(slot<HANDLES&&(s->handles[slot].kind||!s->handles[slot].generation))slot++;
    if(object==OBJECTS||slot==HANDLES){rc=ENOSPC;goto done;}
    guest_thread*t=calloc(1,sizeof(*t));if(!t)goto done;
    *t=(guest_thread){.host=h,.gates=c->gates,.id=c->thread_id,.object=object,.external=1,.started=1,.priority=priority};
    s->objects[object]=(sync_object){.thread=t,.refs=1};t->handle=new_handle(s,slot,object,3);s->running++;
    dm_guest_bind_thread(t->handle,t->id,t);*out=t->handle;rc=0;
done:pthread_mutex_unlock(&h->mutex);return rc;
}
void dm_host_thread_context_detach(dm_guest_context*c){
    guest_thread*t=c->thread_state;if(!t)return;
    dm_host*h=c->host;dm_host_mutex_owner_ended(h,t->object);
    if(!t->external)return;
    signal_object(h->sync,t->object);h->sync->running--;
    release_object(h,t->object);pthread_cond_broadcast(&h->sync->condition);
}
#if defined(__aarch64__)
extern void dm_thread_stack_run(void(*entry)(void*),void*arg,void*top,uintptr_t*saved);
extern void dm_thread_stack_leave(uintptr_t saved);
#if DM_BRIDGE_SANITIZED
extern void dm_thread_stack_enter(uintptr_t entry,uint64_t arg,uintptr_t top);
extern void dm_thread_returned(void);
__attribute__((disable_sanitizer_instrumentation,no_sanitize("address","thread","shadow-call-stack"))) static void guest_boot(void*arg){
    guest_thread*t=arg;
    dm_bridge_finish_guest(t->bridge);
    dm_thread_stack_enter((uintptr_t)t->entry,t->arg,t->initial_sp);dm_thread_returned();
}
#endif
static void*thread_main(void*arg){
    guest_thread*t=arg;dm_host*h=t->host;int rc=dm_guest_attach_reserved(h,t->gates,t->id);
    if(!rc){t->bridge=dm_guest_current()->bridge;
        rc=dm_bridge_prepare(t->bridge,&t->return_stack,h->thread_stacks[t->stack].base,h->thread_stacks[t->stack].size);
        if(rc){if(dm_guest_detach())abort();t->bridge=NULL;}
        else dm_guest_bind_thread(t->handle,t->id,t);
    }
    // StartThread 等待上下文初始化结果；失败不发布完成信号，保留未启动句柄以便重试／关闭。
    // StartThread waits for context initialization; failure publishes no completion and retains an unstarted handle for retry/close.
    pthread_mutex_lock(&h->mutex);t->startup_ready=1;t->startup_error=rc;
    pthread_cond_broadcast(&h->sync->condition);
    if(rc){h->sync->running--;release_object(h,t->object);pthread_mutex_unlock(&h->mutex);return NULL;}
    pthread_mutex_unlock(&h->mutex);
    dm_bridge_start_guest(t->bridge);
#if DM_BRIDGE_SANITIZED
    dm_thread_stack_run(guest_boot,t,(void*)t->initial_sp,&t->return_stack);
#else
    // 生产入口直接由 RUN 汇编尾跳；切到小客体栈后不执行宿主 C 引导函数。
    // Production RUN tail-enters directly; no host C bootstrap executes after selecting a small guest stack.
    dm_thread_stack_run((void(*)(void*))(uintptr_t)t->entry,(void*)(uintptr_t)t->arg,(void*)t->initial_sp,&t->return_stack);
#endif
    dm_bridge_finish_native(t->bridge);
    if(dm_guest_detach())abort();
    // 先回到宿主栈并释放 TLS，再发布完成；等待者此后可以安全释放／缩回客体栈。
    // Return to the native stack and release TLS before publishing completion; waiters may then release/shrink the guest stack safely.
    pthread_mutex_lock(&h->mutex);struct dm_sync*s=h->sync;
    signal_object(s,t->object);release_stack(h,t);s->running--;
    release_object(h,t->object);pthread_cond_broadcast(&s->condition);pthread_mutex_unlock(&h->mutex);return NULL;
}
#endif
#if defined(__aarch64__)
static int executable(dm_host*h,uintptr_t address){
    if(!address||address%4)return 0;
    for(size_t i=0;i<h->executable_count;i++){dm_host_region region=h->executable[i];
        if(address>=region.base&&address-region.base<=region.size&&4<=region.size-(address-region.base))return 1;}
    return 0;
}
// SDK 栈元数据必须来自显式 ABI 和可信可写映射；租约覆盖完整分配，SP 单独保留顶部空间。
// SDK stack metadata requires an explicit ABI and trusted writable mappings; lease the full allocation and reserve top space separately from SP.
static int sdk_stack(dm_host*h,uintptr_t entry,uintptr_t tt,uintptr_t top,unsigned*slot){
    dm_sdk_thread_abi*a=&h->sdk_thread_abi;
    if(entry!=a->entry||!a->entry||!tt||tt%8||!dm_host_writable(h,tt,a->thread_type_size))return 0;
    uint64_t base,size,callback;
    memcpy(&base,(void*)(tt+a->stack_base_offset),8);memcpy(&size,(void*)(tt+a->stack_size_offset),8);
    if(!base||base%16||size<dm_host_thread_stack_minimum()||size%16||size>UINTPTR_MAX-base||size<=a->top_reserve||size-a->top_reserve<dm_host_thread_stack_entry_headroom()||
       top!=base+size-a->top_reserve||!dm_host_stack_writable(h,(uintptr_t)base,(size_t)size))return 0;
    memcpy(&callback,(void*)top,8);if(!executable(h,(uintptr_t)callback))return 0;
    unsigned free_slot=64;
    for(unsigned i=0;i<64;i++){
        if(!h->thread_stacks[i].size){if(free_slot==64)free_slot=i;continue;}
        if(base<h->thread_stacks[i].base+h->thread_stacks[i].size&&h->thread_stacks[i].base<base+size)return 0;
    }
    if(free_slot==64)return 0;
    h->thread_stacks[free_slot].base=(uintptr_t)base;h->thread_stacks[free_slot].size=(size_t)size;*slot=free_slot;return 1;
}
#endif
static uint64_t create_thread(dm_host*h,uint64_t*r){
#if !defined(__aarch64__)
    (void)h;(void)r;return DM_SVC_NOT_IMPLEMENTED;
#else
    dm_guest_context*c=dm_guest_current();if(!c||c->host!=h)return DM_SVC_BAD_STATE;
    // 虚拟优先级用于互斥队列；宿主调度和物理核心亲和性尚未映射。
    // Virtual priorities order mutex queues; native scheduling and physical affinity are not mapped yet.
    if((uint32_t)r[4]>63||((int32_t)r[5]!=-2&&(uint32_t)r[5]>3))return 0xf001;
    uintptr_t entry=(uintptr_t)r[1],top=(uintptr_t)r[3];unsigned stack=64;
    if(!entry||entry%4||!top||top%16)return DM_SVC_INVALID_ADDRESS;
    if(!executable(h,entry))return DM_SVC_INVALID_ADDRESS;
    for(unsigned i=0;i<64;i++)if(h->thread_stacks[i].size&&h->thread_stacks[i].base+h->thread_stacks[i].size==top){stack=i;break;}
    if(ensure(h)||h->next_thread_id==UINT64_MAX)return OUT_OF_RESOURCE;
    struct dm_sync*s=h->sync;unsigned object=EVENTS,slot=0;
    while(object<OBJECTS&&s->objects[object].refs)object++;
    while(slot<HANDLES&&(s->handles[slot].kind||!s->handles[slot].generation))slot++;
    if(object==OBJECTS||slot==HANDLES)return OUT_OF_RESOURCE;
    guest_thread*t=calloc(1,sizeof(*t));if(!t)return OUT_OF_RESOURCE;
    int automatic=0;
    if(stack==64){automatic=sdk_stack(h,entry,(uintptr_t)r[2],top,&stack);if(!automatic){free(t);return DM_SVC_INVALID_ADDRESS;}}
    if(!dm_host_stack_writable(h,h->thread_stacks[stack].base,h->thread_stacks[stack].size)||h->thread_stacks[stack].in_use){
        if(automatic)memset(&h->thread_stacks[stack],0,sizeof(h->thread_stacks[stack]));
        free(t);return h->thread_stacks[stack].in_use?DM_SVC_BAD_STATE:DM_SVC_INVALID_ADDRESS;
    }
    *t=(guest_thread){.host=h,.gates=c->gates,.entry=entry,.arg=r[2],.id=++h->next_thread_id,.object=object,.stack=stack,
        .initial_sp=top,.priority=(int)r[4],.automatic_stack=automatic};
    s->objects[object]=(sync_object){.thread=t};t->handle=new_handle(s,slot,object,3);
    h->thread_stacks[stack].in_use=1;s->stack_pins++;r[1]=t->handle;return 0;
#endif
}
static uint64_t start_thread(dm_host*h,uint32_t handle){
#if !defined(__aarch64__)
    (void)h;(void)handle;return DM_SVC_NOT_IMPLEMENTED;
#else
    struct dm_sync*s=h->sync;int slot=lookup(s,handle,3);if(slot<0)return BAD_HANDLE;
    guest_thread*t=s->objects[s->handles[slot].event].thread;if(t->started)return DM_SVC_BAD_STATE;
    pthread_attr_t attr;if(pthread_attr_init(&attr))return OUT_OF_RESOURCE;
    int rc=pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);pthread_t thread;
    // 启动握手引用与运行引用分离，承受等待期间关闭句柄或客体提前完成。
    // Separate startup-operation and running references to survive handle closure or early guest completion during the handshake.
    if(!rc){t->started=1;t->startup_ready=0;t->startup_error=0;s->objects[t->object].refs+=2;s->running++;s->starting++;
        rc=pthread_create(&thread,&attr,thread_main,t);
        if(rc){t->started=0;s->running--;release_object(h,t->object);}
        else{while(!t->startup_ready)if(pthread_cond_wait(&s->condition,&h->mutex))abort();
            rc=t->startup_error;if(rc)t->started=0;}
        s->starting--;release_object(h,t->object);
    }
    pthread_attr_destroy(&attr);return rc?OUT_OF_RESOURCE:0;
#endif
}
static uint64_t exit_thread(dm_host*h){
    dm_guest_context*c=dm_guest_current();if(!c||c->host!=h||!c->thread_state)return DM_SVC_BAD_STATE;
#if defined(__aarch64__)
    guest_thread*t=c->thread_state;if(t->external)return DM_SVC_BAD_STATE;
    int disposition=dm_bridge_request_exit(c);
    if(disposition)return disposition>0?0:DM_SVC_BAD_STATE;
    dm_bridge_exit_direct(c->bridge);
    dm_thread_stack_leave(t->return_stack);abort();
#else
    return DM_SVC_NOT_IMPLEMENTED;
#endif
}
static int wait_condition(dm_host*h,const struct timespec*deadline){
#if defined(__APPLE__)
    struct timespec now;if(clock_gettime(CLOCK_MONOTONIC,&now))return errno;
    struct timespec left={.tv_sec=deadline->tv_sec-now.tv_sec,.tv_nsec=deadline->tv_nsec-now.tv_nsec};
    if(left.tv_nsec<0){left.tv_nsec+=1000000000;left.tv_sec--;}if(left.tv_sec<0)return ETIMEDOUT;
    return pthread_cond_timedwait_relative_np(&h->sync->condition,&h->mutex,&left);
#else
    return pthread_cond_timedwait(&h->sync->condition,&h->mutex,deadline);
#endif
}
static uint64_t wait_any(dm_host*h,uint64_t*r){
    size_t count=(size_t)r[2];const void*address=(const void*)(uintptr_t)r[1];int64_t ns=(int64_t)r[3];
    if(!count||count>64)return DM_SVC_INVALID_SIZE;
    if(!address||count*4>UINTPTR_MAX-(uintptr_t)address)return DM_SVC_INVALID_ADDRESS;
    uint32_t handles[64];unsigned pinned[64];size_t pins=0;
    struct timespec deadline={0};if(ns>0){if(clock_gettime(CLOCK_MONOTONIC,&deadline))return DM_SVC_BAD_STATE;
        deadline.tv_sec+=ns/1000000000;deadline.tv_nsec+=ns%1000000000;
        if(deadline.tv_nsec>=1000000000){deadline.tv_nsec-=1000000000;deadline.tv_sec++;}}
    pthread_mutex_lock(&h->mutex);struct dm_sync*s=h->sync;uint64_t result=BAD_HANDLE;r[1]=0;
    dm_memory_pin memory={0};
    if(dm_host_memory_read_pin_locked(h,&memory,(uintptr_t)address,count*4)){result=DM_SVC_INVALID_ADDRESS;goto done;}
    memcpy(handles,address,count*4);dm_host_memory_unpin_locked(h,&memory);
    for(size_t i=0;i<count;i++){
        int slot=lookup(s,handles[i],0);if(slot<0||(s->handles[slot].kind!=1&&s->handles[slot].kind!=3))goto done;
        unsigned object=s->handles[slot].event;pinned[pins++]=object;s->objects[object].refs++;
    }
    for(size_t i=0;i<count;i++)if(s->objects[pinned[i]].signaled){result=0;r[1]=i;goto done;}
    if(!ns){result=TIMEOUT;goto done;}
    dm_guest_context*c=dm_guest_current();guest_thread*t=c&&c->host==h?c->thread_state:NULL;
    if(t&&t->wait_cancelled){t->wait_cancelled=0;result=DM_SVC_CANCELLED;goto done;}
    sync_waiter waiter={.next=s->wait_list,.thread=t,.objects=pinned,.count=count,.index=UINT32_MAX};
    s->wait_list=&waiter;s->waiters++;if(t)t->wait=&waiter;
    while(!waiter.done){
        int rc=ns<0?pthread_cond_wait(&s->condition,&h->mutex):wait_condition(h,&deadline);
        if(rc)complete_wait(s,&waiter,rc==ETIMEDOUT?TIMEOUT:DM_SVC_BAD_STATE,UINT32_MAX);
    }
    result=waiter.result;r[1]=waiter.index;
    sync_waiter**link=&s->wait_list;while(*link!=&waiter)link=&(*link)->next;*link=waiter.next;s->waiters--;
done:for(size_t i=0;i<pins;i++)release_object(h,pinned[i]);pthread_mutex_unlock(&h->mutex);return result;
}
uint64_t dm_host_sync_svc(dm_host*h,unsigned number,uint64_t*r){
    if(number==0x18)return wait_any(h,r);if(number==0x0a)return exit_thread(h);
    pthread_mutex_lock(&h->mutex);uint64_t result=BAD_HANDLE;
    if(number==0x08){result=create_thread(h,r);goto done;}
    if(number==0x09){result=start_thread(h,(uint32_t)r[0]);goto done;}
    if(number==0x0c){
        guest_thread*t=NULL;uint32_t handle=(uint32_t)r[1];
        if(handle==0xffff8000u){dm_guest_context*c=dm_guest_current();if(c&&c->host==h)t=c->thread_state;}
        else{int slot=lookup(h->sync,handle,3);if(slot>=0)t=h->sync->objects[h->sync->handles[slot].event].thread;}
        if(t){r[1]=(uint32_t)t->priority;result=0;}goto done;
    }
    if(number==0x19){
        guest_thread*t=NULL;uint32_t handle=(uint32_t)r[0];
        if(handle==0xffff8000u){dm_guest_context*c=dm_guest_current();if(c&&c->host==h)t=c->thread_state;}
        else{int slot=lookup(h->sync,handle,3);if(slot>=0)t=h->sync->objects[h->sync->handles[slot].event].thread;}
        if(t){
            if(t->wait){t->wait_cancelled=0;complete_wait(h->sync,t->wait,DM_SVC_CANCELLED,UINT32_MAX);}
            else t->wait_cancelled=1;
            result=0;
        }
        goto done;
    }
    if(number==0x25){
        dm_guest_context*c=dm_guest_current();
        if((uint32_t)r[1]==0xffff8000u){if(c&&c->host==h){r[1]=c->thread_id;result=0;}}
        else{int slot=lookup(h->sync,(uint32_t)r[1],3);if(slot>=0){r[1]=h->sync->objects[h->sync->handles[slot].event].thread->id;result=0;}}
        goto done;
    }
    if(number==0x45){
        if(ensure(h)){result=OUT_OF_RESOURCE;goto done;}struct dm_sync*s=h->sync;
        unsigned event=0;while(event<EVENTS&&s->objects[event].refs)event++;
        unsigned slots[2],found=0;for(unsigned i=0;i<HANDLES&&found<2;i++)if(!s->handles[i].kind&&s->handles[i].generation)slots[found++]=i;
        if(event==EVENTS||found<2){result=OUT_OF_RESOURCE;goto done;}
        s->objects[event]=(sync_object){0};r[1]=new_handle(s,slots[0],event,2);r[2]=new_handle(s,slots[1],event,1);result=0;goto done;
    }
    struct dm_sync*s=h->sync;int slot=lookup(s,(uint32_t)r[0],0);
    if(slot>=0&&s->handles[slot].kind==4){
        if(number!=0x16){result=BAD_HANDLE;goto done;}
        // 会话只支持 CloseHandle；在任何同步对象访问之前甄别，避免按对象下标误释放。
        // Sessions only support CloseHandle; discriminate before any sync-object access so a session index is never released as an object.
        unsigned index=s->handles[slot].event;
        dm_host_handle_clear_locked(h,(unsigned)slot,(uint32_t)r[0]);
        dm_host_ipc_session_closed_locked(h,index);result=0;goto done;
    }
    slot=lookup(s,(uint32_t)r[0],number==0x11?2:number==0x17?1:0);
    if(slot<0){if(number==0x17&&lookup(s,(uint32_t)r[0],3)>=0)result=DM_SVC_BAD_STATE;goto done;}
    unsigned object=s->handles[slot].event;sync_object*e=&s->objects[object];result=0;
    if(number==0x16){s->handles[slot].kind=0;if(++s->handles[slot].generation==(1u<<22))s->handles[slot].generation=0;release_object(h,object);}
    else if(s->handles[slot].kind==3)result=BAD_HANDLE;
    else if(number==0x11)signal_object(s,object);
    else if(number==0x17&&!e->signaled)result=DM_SVC_BAD_STATE;
    else e->signaled=0;
 done:pthread_mutex_unlock(&h->mutex);return result;
}
