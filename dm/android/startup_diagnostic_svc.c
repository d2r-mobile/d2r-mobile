// 专用诊断链接的 SVC 包装及有界管道协议；终止事件永不返回客体。
// Dedicated diagnostic SVC wrapper and bounded pipe protocol; terminal events never return to guest.
#include "startup_diagnostic.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdatomic.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/ucontext.h>
#include <unistd.h>
#define MAGIC 0x44494147u
extern uint64_t dm_diag_normal_svc(dm_host*,unsigned,uint64_t[31]);
extern int dm_diag_in_child(const dm_startup_diagnostic*);
extern uintptr_t dm_diag_setup_site(const dm_startup_diagnostic*);
static int trace_fd=-1;
static _Atomic unsigned sequence;
static uintptr_t intervals[4096][2];static size_t interval_count;
static _Alignas(16) unsigned char alternate[65536];
static _Thread_local _Alignas(16) unsigned char guest_alternate[65536];
static _Thread_local int setup_done;
static _Thread_local stack_t previous_stack;
static uintptr_t setup_site;static uint32_t intended_handle;
static _Atomic uintptr_t alternate_begin,alternate_end;
_Static_assert(ATOMIC_INT_LOCK_FREE==2,"signal trace requires lock-free counter");
_Static_assert(ATOMIC_POINTER_LOCK_FREE==2,"signal trace requires lock-free addresses");
#if defined(__clang__)
#define SIGNAL_SAFE __attribute__((disable_sanitizer_instrumentation,no_sanitize("address","undefined","thread")))
#else
#define SIGNAL_SAFE __attribute__((no_sanitize_address,no_sanitize_undefined,no_sanitize_thread))
#endif
static SIGNAL_SAFE void emit(dm_diag_event event,unsigned code,uintptr_t pc,uint64_t value){
    unsigned number=atomic_fetch_add_explicit(&sequence,1,memory_order_relaxed);
    if(number>=DM_DIAG_TRACE_LIMIT)_exit(84);
    if(number==DM_DIAG_TRACE_LIMIT-1)event=DM_DIAG_OVERFLOW;
    dm_diag_record record={MAGIC,number,event,code,pc,value};
    if(write(trace_fd,&record,sizeof(record))!=(ssize_t)sizeof(record))_exit(85);
    if(event==DM_DIAG_OVERFLOW)_exit(83);
}
static SIGNAL_SAFE void signal_stop(int number,siginfo_t*info,void*context){
    (void)info;uintptr_t pc=0;
#if defined(__APPLE__) && defined(__aarch64__)
    pc=(uintptr_t)((ucontext_t*)context)->uc_mcontext->__ss.__pc;
#elif defined(__aarch64__)
    pc=(uintptr_t)((ucontext_t*)context)->uc_mcontext.pc;
#else
    (void)context;
#endif
    dm_diag_event event=DM_DIAG_SIGNAL;
    if(number==SIGTRAP)for(size_t i=0;i<interval_count;i++)if(pc>=intervals[i][0]&&pc<intervals[i][1]&&!(pc%4)){event=DM_DIAG_QUARANTINE;break;}
    uintptr_t sp=0;
#if defined(__aarch64__)
    __asm__ volatile("mov %0, sp":"=r"(sp));
#endif
    int on_alternate=sp>=atomic_load_explicit(&alternate_begin,memory_order_relaxed)&&sp<atomic_load_explicit(&alternate_end,memory_order_relaxed);
    emit(event,(unsigned)number,pc,(uint64_t)on_alternate);_exit(event==DM_DIAG_QUARANTINE?80:81);
}
int dm_diag_trace_start(int fd,const dm_startup_diagnostic*s,uint32_t handle){
    if(trace_fd!=-1||fd<0||!handle||!dm_diag_in_child(s))return EINVAL;
    uintptr_t site=dm_diag_setup_site(s);if(!site)return EINVAL;
    struct stat status;if(fstat(fd,&status)||!S_ISFIFO(status.st_mode))return EINVAL;
    int flags=fcntl(fd,F_GETFL);if(flags<0||fcntl(fd,F_SETFL,flags|O_NONBLOCK))return errno;
    size_t count=0;
    for(size_t module=0;module<16;module++){
        const dm_diag_extent*extents=NULL;size_t n=dm_diag_quarantines(s,module,&extents);
        for(size_t i=0;i<n;i++){
            if(count==4096)return E2BIG;uintptr_t begin;
            if(dm_diag_address(s,module,extents[i].begin,extents[i].end-extents[i].begin,&begin))return EINVAL;
            intervals[count][0]=begin;intervals[count++][1]=begin+extents[i].end-extents[i].begin;
        }
    }
    stack_t stack={.ss_sp=alternate,.ss_size=sizeof(alternate)};if(sigaltstack(&stack,NULL))return errno;
    struct sigaction action={0};action.sa_sigaction=signal_stop;action.sa_flags=SA_SIGINFO|SA_ONSTACK;sigemptyset(&action.sa_mask);
    const int signals[]={SIGTRAP,SIGILL,SIGSEGV,SIGBUS,SIGABRT};
    for(size_t i=0;i<sizeof(signals)/sizeof(*signals);i++)if(sigaction(signals[i],&action,NULL))return errno;
    setup_site=site;intended_handle=handle;interval_count=count;atomic_store(&sequence,0);trace_fd=fd;return 0;
}
uint64_t dm_host_svc(dm_host*h,unsigned number,uint64_t r[31]){
    dm_guest_context*c=dm_guest_current();
    int guest=trace_fd>=0&&c&&c->host==h&&c->thread_handle==intended_handle;
    if(guest)emit(DM_DIAG_SERVICE,number,r[30],number==0x29?r[1]:r[0]);
    if(trace_fd>=0&&number==0xfffe){
        if(!c||c->host!=h||c->thread_handle!=intended_handle||setup_done||r[30]!=setup_site){emit(DM_DIAG_SIGNAL,number,0,0);_exit(81);}
        stack_t stack={.ss_sp=guest_alternate,.ss_size=sizeof(guest_alternate)},observed;
        if(sigaltstack(&stack,&previous_stack)||sigaltstack(NULL,&observed)||observed.ss_flags&SS_DISABLE||observed.ss_sp!=guest_alternate||observed.ss_size!=sizeof(guest_alternate)){emit(DM_DIAG_SIGNAL,number,0,0);_exit(81);}
        atomic_store_explicit(&alternate_begin,(uintptr_t)guest_alternate,memory_order_relaxed);
        atomic_store_explicit(&alternate_end,(uintptr_t)guest_alternate+sizeof(guest_alternate),memory_order_relaxed);
        setup_done=1;r[1]=intended_handle;r[0]=0;return 0;
    }
    if(guest&&(number==8||number==9)){
        emit(DM_DIAG_UNSUPPORTED,number,0,DM_SVC_NOT_IMPLEMENTED);_exit(82);
    }
    if(guest&&number==0x0a&&setup_done){
        stack_t observed,restore=previous_stack;
        /* 本机 Darwin libc 禁用路径仍要求有效大小；禁用后只比较状态。
           The observed Darwin libc disable path requires a valid size; compare only disabled state. */
        if(restore.ss_flags&SS_DISABLE){restore.ss_sp=guest_alternate;restore.ss_size=sizeof(guest_alternate);}
        if(sigaltstack(&restore,NULL)||sigaltstack(NULL,&observed)||observed.ss_flags!=previous_stack.ss_flags||
           (!(previous_stack.ss_flags&SS_DISABLE)&&(observed.ss_sp!=previous_stack.ss_sp||observed.ss_size!=previous_stack.ss_size))){emit(DM_DIAG_SIGNAL,number,0,0);_exit(81);}
        atomic_store_explicit(&alternate_begin,0,memory_order_relaxed);
        atomic_store_explicit(&alternate_end,0,memory_order_relaxed);
        setup_done=0;
    }
    uint64_t result=dm_diag_normal_svc(h,number,r);
    if(guest&&number!=0x1e&&result==DM_SVC_NOT_IMPLEMENTED){emit(DM_DIAG_UNSUPPORTED,number,0,result);_exit(82);}
    return result;
}
_Noreturn void dm_diag_trace_finish(void){if(trace_fd<0)_exit(86);emit(DM_DIAG_RETURNED,0,0,0);_exit(0);}
int dm_diag_trace_validate(const void*bytes,size_t size,dm_diag_event*out){
    if(!bytes||!out||!size||size%sizeof(dm_diag_record)||size/sizeof(dm_diag_record)>DM_DIAG_TRACE_LIMIT)return EINVAL;
    size_t count=size/sizeof(dm_diag_record);dm_diag_event terminal=0;int setup=0,exited=0;
    for(size_t i=0;i<count;i++){
        dm_diag_record r;memcpy(&r,(const unsigned char*)bytes+i*sizeof(r),sizeof(r));
        if(r.magic!=MAGIC||r.sequence!=i||r.event<DM_DIAG_SERVICE||r.event>DM_DIAG_OVERFLOW||terminal)return EINVAL;
        if(r.event==DM_DIAG_SERVICE&&r.code==0xfffe)setup++;
        if(r.event==DM_DIAG_SERVICE&&r.code==0x0a&&setup==1)exited=1;
        if(r.event==DM_DIAG_RETURNED&&(!exited||setup!=1))return EINVAL;
        if(r.event==DM_DIAG_QUARANTINE&&(setup!=1||r.value!=1))return EINVAL;
        if(r.event!=DM_DIAG_SERVICE)terminal=(dm_diag_event)r.event;
    }
    if(!terminal)return EINVAL;*out=terminal;return 0;
}
