// 自有进程内存验证：真实页保护、查询边界、资源池状态及改写调用门；不含游戏代码。
// Owned process-memory verification: real page protection, query boundaries, resource-pool state and rewritten gates; no game code.
#include "../runtime/internal.h"
#include "../runtime/bridge.h"
#include "../runtime/patch.h"
#include "../image_map.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_vm.h>
#endif
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); abort(); } } while (0)
#define UNIT (2u*1024u*1024u)
#define PROCESS 0xffff8001u
#define HIGH 0xa1b2c3d400000000ull
typedef struct { uint64_t base,size; uint32_t state,attributes,permission,ipc,device,padding; } memory_info;
typedef struct { uint64_t result,value; } returned;
static const dm_gate_table empty_gates={0};
static int fault_pipe;
static uintptr_t fault_address;
static unsigned char signal_stack[65536];
static void fault(int sig,siginfo_t *info,void *context) {
    (void)context;
    char marker=(sig==SIGSEGV||sig==SIGBUS)&&(uintptr_t)info->si_addr==fault_address?'P':'X';
    if(write(fault_pipe,&marker,1)!=1)_exit(74);
    _exit(marker=='P'?73:75);
}
__attribute__((no_sanitize("address","thread","undefined")))
static void touch(void *address,int writing) {
    volatile unsigned char *p=address;
    if(writing)*p=17;else{volatile unsigned char value=*p;(void)value;}
}
static void denied(uintptr_t address,int writing) {
    int descriptors[2];CHECK(!pipe(descriptors));CHECK(!fflush(NULL));pid_t child=fork();CHECK(child>=0);
    if(!child) {
        close(descriptors[0]);fault_pipe=descriptors[1];fault_address=address;
        stack_t alternate={.ss_sp=signal_stack,.ss_size=sizeof(signal_stack)};
        struct sigaction action={0};action.sa_sigaction=fault;action.sa_flags=SA_SIGINFO|SA_ONSTACK;sigemptyset(&action.sa_mask);
        if(sigaltstack(&alternate,NULL)||sigaction(SIGSEGV,&action,NULL)||sigaction(SIGBUS,&action,NULL))_exit(76);
        touch((void*)address,writing);_exit(77);
    }
    CHECK(!close(descriptors[1]));int status;CHECK(waitpid(child,&status,0)==child);
    char marker=0;CHECK(read(descriptors[0],&marker,1)==1&&marker=='P');CHECK(!close(descriptors[0]));
    CHECK(WIFEXITED(status)&&WEXITSTATUS(status)==73);
}
static uint64_t resize(dm_host *h,size_t size) {
    uint64_t r[31]={[1]=size};uint64_t result=dm_host_svc(h,1,r);
    CHECK(r[0]==result);CHECK(r[1]==(result?size:(uintptr_t)h->heap));return result;
}
static uint64_t map(dm_host *h,unsigned svc,uintptr_t destination,uintptr_t source,size_t size) {
    uint64_t r[31]={destination,source,size};uint64_t result=dm_host_svc(h,svc,r);
    CHECK(r[0]==result&&r[1]==source&&r[2]==size);return result;
}
static uint64_t get(dm_host *h,uint64_t id,uint64_t handle,uint64_t subtype,uint64_t expected) {
    uint64_t r[31];for(unsigned i=0;i<31;i++)r[i]=HIGH+i;r[1]=id;r[2]=handle;r[3]=subtype;
    CHECK(dm_host_svc(h,0x29,r)==expected&&r[0]==expected);
    if(expected)CHECK(r[1]==id);
    CHECK(r[2]==handle&&r[3]==subtype);for(unsigned i=4;i<31;i++)CHECK(r[i]==HIGH+i);
    return r[1];
}
static uint64_t value(dm_host *h,unsigned id) { return get(h,id,PROCESS,0,0); }
static memory_info query(dm_host *h,uintptr_t address) {
    unsigned char *out=h->heap+h->committed-64;memory_info result;
    memset(out-1,0xa5,sizeof(result)+2);uint64_t r[31]={(uintptr_t)out,HIGH,address};
    CHECK(!dm_host_svc(h,6,r)&&!r[0]&&!r[1]);memcpy(&result,out,sizeof(result));
    CHECK(out[-1]==0xa5&&out[sizeof(result)]==0xa5&&!result.ipc&&!result.device&&!result.padding);return result;
}
static void extent(dm_host *h,uintptr_t address,uintptr_t base,size_t size,unsigned state,unsigned permission) {
    memory_info result=query(h,address);
    CHECK(result.base==base&&result.size==size&&result.state==state&&result.permission==permission&&!result.attributes);
}
static int mapped_page(uintptr_t address,size_t page) {
#if defined(__APPLE__)
    (void)page;mach_vm_address_t base=address;mach_vm_size_t size=0;
    vm_region_basic_info_data_64_t info;mach_msg_type_number_t count=VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object=MACH_PORT_NULL;
    kern_return_t result=mach_vm_region(mach_task_self(),&base,&size,VM_REGION_BASIC_INFO_64,(vm_region_info_t)&info,&count,&object);
    if(object!=MACH_PORT_NULL)CHECK(mach_port_deallocate(mach_task_self(),object)==KERN_SUCCESS);
    CHECK(result==KERN_SUCCESS||result==KERN_INVALID_ADDRESS);
    return result==KERN_SUCCESS&&base<=address&&address-base<size;
#else
    unsigned char resident;errno=0;int result=mincore((void*)address,page,(void*)&resident);
    CHECK(!result||(result==-1&&errno==ENOMEM));return result==0;
#endif
}
static void unmapped(uintptr_t base,size_t size,size_t page) {
#if defined(__APPLE__)
    (void)page;mach_vm_address_t next=base;mach_vm_size_t length=0;
    vm_region_basic_info_data_64_t info;mach_msg_type_number_t count=VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object=MACH_PORT_NULL;
    kern_return_t result=mach_vm_region(mach_task_self(),&next,&length,VM_REGION_BASIC_INFO_64,(vm_region_info_t)&info,&count,&object);
    if(object!=MACH_PORT_NULL)CHECK(mach_port_deallocate(mach_task_self(),object)==KERN_SUCCESS);
    CHECK(result==KERN_INVALID_ADDRESS||(result==KERN_SUCCESS&&next>=base+size));
#else
    for(size_t offset=0;offset<size;offset+=page) {
        CHECK(!mapped_page(base+offset,page));
    }
#endif
}
static void rejected_constructor(size_t heap,size_t stack,size_t alias) {
    errno=0;CHECK(!dm_host_create_process_layout(heap,stack,alias)&&errno==EINVAL);
}
static void legacy_and_invalid(size_t page) {
    rejected_constructor(0,page,page);rejected_constructor(UNIT-1,page,page);
    rejected_constructor(UNIT,0,page);rejected_constructor(UNIT,page,0);
    rejected_constructor(UNIT,page+1,page);rejected_constructor(UNIT,page,page+1);
    size_t maximum=SIZE_MAX-(SIZE_MAX%page);
    rejected_constructor(UNIT,maximum,page);rejected_constructor(UNIT,page,maximum);
    rejected_constructor(SIZE_MAX-(SIZE_MAX%UNIT),page,page);
    for(unsigned mode=0;mode<2;mode++) {
        dm_host *h=mode?dm_host_create_layout(2*UNIT,page):dm_host_create(UNIT);CHECK(h);
        const unsigned unsupported[]={2,3,16,21,22};
        for(size_t i=0;i<sizeof(unsupported)/sizeof(*unsupported);i++)
            get(h,unsupported[i],PROCESS,0,DM_SVC_NOT_IMPLEMENTED);
        if(mode) {
            CHECK(value(h,13)==2*UNIT+page&&value(h,14)==(uintptr_t)h->heap+2*UNIT);
            CHECK(!resize(h,UNIT));extent(h,(uintptr_t)h->heap+UNIT,(uintptr_t)h->heap+UNIT,UNIT+page,0,0);
        }
        CHECK(!dm_host_destroy(h));
    }
    puts("PASS constructors: malformed/overflow rejected; legacy constructors retain unsupported process-layout IDs");
}
static void layout_boundaries(size_t page) {
    const size_t heap_capacity=2*UNIT,stack_capacity=65536+3*page,alias_capacity=65536+7*page;
    dm_host *h=dm_host_create_process_layout(heap_capacity,stack_capacity,alias_capacity);CHECK(h);
    uintptr_t heap=(uintptr_t)h->heap,stack=heap+heap_capacity,alias=stack+stack_capacity,end=alias+alias_capacity;
    CHECK(value(h,2)==alias&&value(h,3)==alias_capacity&&value(h,4)==heap&&value(h,5)==heap_capacity);
    CHECK(value(h,12)==heap&&value(h,13)==end-heap&&value(h,14)==stack&&value(h,15)==stack_capacity&&value(h,16)==0);
    const unsigned unsupported[]={6,7,17,21,22};
    for(size_t i=0;i<sizeof(unsupported)/sizeof(*unsupported);i++)get(h,unsupported[i],PROCESS,0,DM_SVC_NOT_IMPLEMENTED);
    CHECK(!resize(h,UNIT));
    extent(h,heap,heap,UNIT,5,3);extent(h,heap+UNIT-1,heap,UNIT,5,3);
    extent(h,heap+UNIT,heap+UNIT,UNIT,0,0);extent(h,stack-1,heap+UNIT,UNIT,0,0);
    extent(h,stack,stack,stack_capacity,0,0);extent(h,alias-1,stack,stack_capacity,0,0);
    extent(h,alias,alias,alias_capacity,0,0);extent(h,end-1,alias,alias_capacity,0,0);
    CHECK(mapped_page(end-page,page));
    denied(alias,0);denied(alias,1);denied(end-1,0);denied(end-1,1);
    CHECK(dm_host_register_writable(h,(void*)alias,page)==EINVAL);
    CHECK(dm_host_register_executable(h,(void*)alias,page)==EINVAL);
    CHECK(dm_host_register_writable(h,(void*)(end-page),2*page)==EINVAL);
    CHECK(dm_host_register_executable(h,(void*)(end-page),2*page)==EINVAL);
    CHECK(dm_host_register_thread_stack(h,(void*)alias,65536)==EINVAL);
    dm_memory_pin pin={0};pthread_mutex_lock(&h->mutex);
    CHECK(!dm_host_writable(h,alias,1));
    CHECK(dm_host_memory_pin_locked(h,&pin,alias,1)==EINVAL&&!pin.active);
    CHECK(dm_host_memory_read_pin_locked(h,&pin,end-1,1)==EINVAL&&!pin.active);
    CHECK(dm_host_memory_pin_locked(h,&pin,end-1,2)==EINVAL&&!pin.active);
    pthread_mutex_unlock(&h->mutex);
    unsigned char *external=mmap(NULL,page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);CHECK(external!=MAP_FAILED);external[0]=0x6a;
    pthread_mutex_lock(&h->mutex);CHECK(!dm_host_memory_pin_locked(h,&pin,(uintptr_t)external,page)&&!pin.active);pthread_mutex_unlock(&h->mutex);
    CHECK(!dm_host_register_writable(h,external,page));
    extent(h,(uintptr_t)external,(uintptr_t)external,page,4,3);
    CHECK(!dm_guest_attach(h,&empty_gates));CHECK(dm_host_register_writable(h,external+page/2,1)==EBUSY);CHECK(dm_host_destroy(h)==EBUSY);CHECK(!dm_guest_detach());
    unsigned char *out=h->heap+page;memset(out,0xa5,sizeof(memory_info));
    uint64_t r[31]={(uintptr_t)out,HIGH,end};CHECK(dm_host_svc(h,6,r)==DM_SVC_INVALID_ADDRESS&&r[1]==HIGH);
    for(size_t i=0;i<sizeof(memory_info);i++)CHECK(out[i]==0xa5);
    r[0]=(uintptr_t)out;r[1]=HIGH;r[2]=UINTPTR_MAX;CHECK(dm_host_svc(h,6,r)==DM_SVC_INVALID_ADDRESS&&r[1]==HIGH);
    for(size_t i=0;i<sizeof(memory_info);i++)CHECK(out[i]==0xa5);
    r[0]=alias;r[1]=HIGH;r[2]=heap;CHECK(dm_host_svc(h,6,r)==DM_SVC_INVALID_ADDRESS&&r[1]==HIGH);
    CHECK(map(h,4,alias,heap,page)==DM_SVC_INVALID_MEMORY_REGION);
    CHECK(map(h,5,alias,heap,page)==DM_SVC_INVALID_MEMORY_REGION);
    CHECK(map(h,4,end-page,heap,2*page)==DM_SVC_INVALID_MEMORY_REGION);
    CHECK(map(h,4,stack,alias,page)==DM_SVC_INVALID_MEMORY_STATE);
    h->heap[0]=0x71;CHECK(!map(h,4,stack,heap,page));CHECK(*(unsigned char*)stack==0x71);
    CHECK(value(h,2)==alias&&value(h,3)==alias_capacity&&value(h,13)==end-heap&&value(h,16)==0);
    get(h,21,PROCESS,0,DM_SVC_NOT_IMPLEMENTED);get(h,22,PROCESS,0,DM_SVC_NOT_IMPLEMENTED);
    CHECK(resize(h,0)==DM_SVC_BAD_STATE);CHECK(dm_host_destroy(h)==EBUSY);CHECK(!map(h,5,stack,heap,page));
    int backing=h->backing_fd;h->backing_fd=-1;
    CHECK(resize(h,2*UNIT)==DM_SVC_OUT_OF_MEMORY);extent(h,heap+UNIT,heap+UNIT,UNIT,0,0);
    h->backing_fd=backing;CHECK(!resize(h,2*UNIT));h->heap[UNIT]=0x59;
    h->backing_fd=-1;CHECK(resize(h,UNIT)==DM_SVC_OUT_OF_MEMORY&&h->heap[UNIT]==0x59);
    extent(h,heap+UNIT,heap,heap_capacity,5,3);h->backing_fd=backing;
    CHECK(!resize(h,UNIT));extent(h,heap+UNIT,heap+UNIT,UNIT,0,0);denied(heap+UNIT,0);
    CHECK(!resize(h,2*UNIT)&&h->heap[UNIT]==0);extent(h,alias,alias,alias_capacity,0,0);
    CHECK(!resize(h,0));CHECK(!dm_host_destroy(h));unmapped(heap,end-heap,page);
    CHECK(external[0]==0x6a);CHECK(!munmap(external,page));
    puts("PASS layout: exact heap/stack/alias cuts, real PROT_NONE faults, denied pins/registration/map destination, rollback and whole-range teardown");
}
#if defined(__aarch64__)
extern void dm_gate(void);
typedef returned (*gate_function)(uint64_t,uint64_t,uint64_t,uint64_t);
static void word(unsigned char *p,uint32_t instruction) { memcpy(p,&instruction,4); }
static void native_gates(size_t page) {
    dm_nso_image image={.header.image_size=page};dm_image_mapping program;dm_gate_site sites[2];
    image.header.segments[0].size=192;image.header.segments[1].memory_offset=(uint32_t)page;image.header.segments[2].memory_offset=(uint32_t)page;
    image.bytes=calloc(1,page);CHECK(image.bytes);
    for(unsigned i=0;i<2;i++) {
        unsigned offset=32*i;word(image.bytes+offset,0xa9bf7bfdu);word(image.bytes+offset+8,0xa8c17bfdu);word(image.bytes+offset+12,0xd65f03c0u);
        dm_patch_request request={.instruction=0xd4000001u|((i?6u:0x29u)<<5),.original_offset=offset+4,.stub_offset=128+32*i,.host_gate=(uintptr_t)dm_gate};
        dm_patch_output patch;CHECK(!dm_patch_system(&request,&patch));word(image.bytes+offset+4,patch.replacement);
        memcpy(image.bytes+request.stub_offset,patch.stub,sizeof(patch.stub));sites[i]=patch.site;
    }
    CHECK(!dm_image_map(&image,&program));dm_nso_free(&image);
    for(unsigned i=0;i<2;i++)sites[i].return_address+=(uintptr_t)program.base;
    dm_gate_table gates={sites,2};dm_host *h=dm_host_create_process_layout(UNIT,2*page,3*page);CHECK(h);
    CHECK(!resize(h,UNIT));CHECK(!dm_host_register_executable(h,program.base,192));CHECK(!dm_guest_attach(h,&gates));
    gate_function info=(gate_function)(void*)program.base,query_gate=(gate_function)(void*)(program.base+32);
    const unsigned ids[]={2,3,4,5,12,13,14,15,16};
    for(size_t i=0;i<sizeof(ids)/sizeof(*ids);i++) {
        uint64_t expected=value(h,ids[i]);CHECK(get(h,HIGH|ids[i],HIGH|PROCESS,0,0)==expected);
        returned r=info(0,HIGH|ids[i],HIGH|PROCESS,0);CHECK(!r.result&&r.value==expected);
    }
    struct {uint64_t id,handle,subtype,result;} bad[]={
        {2,0,0,DM_SVC_INVALID_HANDLE},{2,0xffff8000u,0,DM_SVC_INVALID_HANDLE},
        {2,PROCESS,1,DM_SVC_INVALID_COMBINATION},{3,PROCESS,HIGH,DM_SVC_INVALID_COMBINATION},
        {HIGH|2,0,HIGH,DM_SVC_INVALID_COMBINATION},
        {21,PROCESS,0,DM_SVC_NOT_IMPLEMENTED},{22,PROCESS,0,DM_SVC_NOT_IMPLEMENTED},
        {UINT64_MAX,PROCESS,0,DM_SVC_NOT_IMPLEMENTED}
    };
    for(size_t i=0;i<sizeof(bad)/sizeof(*bad);i++) {
        get(h,bad[i].id,bad[i].handle,bad[i].subtype,bad[i].result);
        returned r=info(0,bad[i].id,bad[i].handle,bad[i].subtype);CHECK(r.result==bad[i].result&&r.value==bad[i].id);
    }
    memory_info *out=(void*)(h->heap+page);uintptr_t alias=(uintptr_t)h->heap+UNIT+2*page;
    returned r=query_gate((uintptr_t)out,HIGH,alias,0);
    CHECK(!r.result&&!r.value&&out->base==alias&&out->size==3*page&&!out->state&&!out->permission);
    r=query_gate(alias,HIGH,alias,0);CHECK(r.result==DM_SVC_INVALID_ADDRESS&&r.value==HIGH);
    dm_guest_context *context=dm_guest_current();CHECK(context&&context->bridge->calls==19&&dm_bridge_on_native_stack());
    CHECK(context->bridge->last_native_sp>=context->bridge->native_low&&context->bridge->last_native_sp<context->bridge->native_high);
    CHECK(!dm_guest_detach());CHECK(!dm_host_destroy(h));CHECK(!dm_image_unmap(&program));
    puts("PASS native gates: real SVC29/06, w1/w2 truncation, full x3, preserved error outputs and native service stack");
}
#endif
int main(void) {
    long native_page=sysconf(_SC_PAGESIZE);CHECK(native_page>0);size_t page=(size_t)native_page;
    legacy_and_invalid(page);layout_boundaries(page);
#if defined(__aarch64__)
    native_gates(page);
#endif
    puts("PASS scope: reserved virtual address geometry and disabled dedicated pool; physical/non-system accounting stays unsupported");
    return 0;
}
