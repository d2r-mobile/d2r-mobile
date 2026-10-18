// 真实合成模块与 TLS 生命周期的预算验证，不使用游戏内容或 RSS。
// Budget verification with real synthetic modules and TLS lifecycles, never game content or RSS.
#include "../runtime/accounting.h"
#include "../runtime/internal.h"
#include "../runtime/patch.h"
#include <errno.h>
#include <dlfcn.h>
#include <signal.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_vm.h>
#endif
#define CHECK(c) do {if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);abort();}}while(0)
#define UNIT (2u*1024u*1024u)
static const dm_gate_table empty={0};
static uintptr_t protect_low,protect_high;static unsigned protect_calls,protect_fail;
int mprotect(void*address,size_t size,int mode){
    static int(*real_protect)(void*,size_t,int);
    if(!real_protect){*(void**)(&real_protect)=dlsym(RTLD_NEXT,"mprotect");CHECK(real_protect);}
    uintptr_t base=(uintptr_t)address;
    if(protect_low&&base>=protect_low&&base<=protect_high&&size<=protect_high-base&&++protect_calls==protect_fail){errno=EACCES;return -1;}
    return real_protect(address,size,mode);
}
static uintptr_t denied_address;
static void deny_signal(int sig,siginfo_t*info,void*context){(void)context;_exit((sig==SIGSEGV||sig==SIGBUS)&&(uintptr_t)info->si_addr==denied_address?71:72);}
__attribute__((no_sanitize("address","thread","undefined")))
static void read_denied(uintptr_t address){volatile unsigned char value=*(volatile unsigned char*)address;(void)value;}
static void denied(uintptr_t address){
    pid_t child=fork();CHECK(child>=0);if(!child){denied_address=address;struct sigaction action={0};action.sa_sigaction=deny_signal;action.sa_flags=SA_SIGINFO;sigemptyset(&action.sa_mask);
        if(sigaction(SIGSEGV,&action,NULL)||sigaction(SIGBUS,&action,NULL))_exit(73);read_denied(address);_exit(74);}
    int result;CHECK(waitpid(child,&result,0)==child&&WIFEXITED(result)&&WEXITSTATUS(result)==71);
}
static void put32(uint8_t *p, uint32_t v) { for (unsigned b = 0; b < 4; b++) p[b] = (uint8_t)(v >> (b * 8)); }
static void put64(uint8_t *p, uint64_t v) { for (unsigned b = 0; b < 8; b++) p[b] = (uint8_t)(v >> (b * 8)); }
static uint64_t get64(const uint8_t *p) {
    uint64_t v = 0; for (unsigned b = 0; b < 8; b++) v |= (uint64_t)p[b] << (b * 8); return v;
}
static dm_nso_image fixture(int defined) {
    dm_nso_image im = {.header = {.image_size = 0x10000, .bss_size = 0x4000}};
    im.header.segments[0] = (dm_nso_segment){.size = 0x4000};
    im.header.segments[1] = (dm_nso_segment){.memory_offset = 0x4000, .size = 0x4000};
    im.header.segments[2] = (dm_nso_segment){.memory_offset = 0x8000, .size = 0x4000};
    im.bytes = calloc(1, im.header.image_size); CHECK(im.bytes);
    put32(im.bytes + 4, 8); memcpy(im.bytes + 8, "MOD0", 4); put32(im.bytes + 12, 0x8000 - 8);
    const uint64_t tags[][2] = {{4,0x4000},{5,0x4200},{6,0x4100},{10,16},{11,24},
        {7,0x4300},{8,72},{9,24},{23,0x4380},{2,24},{20,7},{12,0x30},{25,0x8100},{27,8},{0,0}};
    for (size_t i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
        put64(im.bytes + 0x8000 + i * 16, tags[i][0]); put64(im.bytes + 0x8008 + i * 16, tags[i][1]);
    }
    put32(im.bytes + 0x4000, 1); put32(im.bytes + 0x4004, 3);
    put32(im.bytes + 0x4008, 1); put32(im.bytes + 0x4010, 2);
    memcpy(im.bytes + 0x4200, "\0shared\0weak\0", 13);
    uint8_t *s = im.bytes + 0x4118;
    put32(s, 1); s[4] = 0x11; s[6] = (uint8_t)defined; put64(s + 8, defined ? 0x8200 : 0); put64(s + 16, defined ? 8 : 0);
    s += 24; put32(s, 8); s[4] = 0x21;
    const uint64_t relas[][3] = {{0x8300,1027,0x30},{0x8308,((uint64_t)1<<32)|257,5},
        {0x8310,((uint64_t)2<<32)|1025,0}};
    for (unsigned i = 0; i < 3; i++) for (unsigned j = 0; j < 3; j++)
        put64(im.bytes + 0x4300 + i * 24 + j * 8, relas[i][j]);
    put64(im.bytes + 0x4380, 0x8318); put64(im.bytes + 0x4388, ((uint64_t)1<<32)|1026); put64(im.bytes + 0x4390, 7);
    return im;
}
static uint64_t info(dm_host*h,unsigned id){uint64_t r[31]={[1]=id,[2]=0xffff8001u};CHECK(!dm_host_svc(h,0x29,r));return r[1];}
static uint64_t resize(dm_host*h,size_t size){uint64_t r[31]={[1]=size};return dm_host_svc(h,1,r);}
static dm_memory_info query(dm_host*h,uintptr_t address){dm_memory_info*out=(void*)(h->heap+256);uint64_t r[31]={(uintptr_t)out,0,address};CHECK(!dm_host_svc(h,6,r));return *out;}
static void unmapped(uintptr_t address,size_t length,size_t page){
#if defined(__APPLE__)
    (void)page;mach_vm_address_t base=address;mach_vm_size_t size=0;vm_region_basic_info_data_64_t data;
    mach_msg_type_number_t count=VM_REGION_BASIC_INFO_COUNT_64;mach_port_t object=MACH_PORT_NULL;
    kern_return_t r=mach_vm_region(mach_task_self(),&base,&size,VM_REGION_BASIC_INFO_64,(vm_region_info_t)&data,&count,&object);
    if(object!=MACH_PORT_NULL)CHECK(mach_port_deallocate(mach_task_self(),object)==KERN_SUCCESS);
    CHECK(r==KERN_INVALID_ADDRESS||(r==KERN_SUCCESS&&base>=address+length));
#else
    for(size_t offset=0;offset<length;offset+=page){unsigned char value;errno=0;CHECK(mincore((void*)(address+offset),page,(void*)&value)==-1&&errno==ENOMEM);}
#endif
}

typedef struct {dm_host*h;pthread_mutex_t mutex;pthread_cond_t cond;unsigned ready;int finish;uintptr_t tls;} group;
static void*attached_worker(void*arg){group*g=arg;CHECK(!dm_guest_attach(g->h,&empty));
    pthread_mutex_lock(&g->mutex);g->ready++;pthread_cond_broadcast(&g->cond);while(!g->finish)pthread_cond_wait(&g->cond,&g->mutex);pthread_mutex_unlock(&g->mutex);
    CHECK(!dm_guest_detach());return NULL;
}
static void*pin_worker(void*arg){group*g=arg;dm_memory_pin pin={0};
    pthread_mutex_lock(&g->h->mutex);CHECK(!dm_host_memory_pin_locked(g->h,&pin,g->tls,16));pthread_mutex_unlock(&g->h->mutex);
    pthread_mutex_lock(&g->mutex);g->ready=1;pthread_cond_broadcast(&g->cond);while(!g->finish)pthread_cond_wait(&g->cond,&g->mutex);pthread_mutex_unlock(&g->mutex);
    *(uint64_t*)g->tls=0x1234;CHECK(*(uint64_t*)g->tls==0x1234);
    pthread_mutex_lock(&g->h->mutex);dm_host_memory_unpin_locked(g->h,&pin);pthread_mutex_unlock(&g->h->mutex);return NULL;
}
#if defined(__aarch64__)
extern void dm_gate(void);
typedef struct {uint64_t result,value;} pair;
typedef pair(*info_fn)(uint64_t,uint64_t,uint64_t,uint64_t);
#endif
int main(void){
    size_t page=(size_t)sysconf(_SC_PAGESIZE),local=(8192+0x600+page-1)/page*page;
    dm_nso_image images[2]={fixture(0),fixture(1)};
    uint8_t stub[32]={0};dm_image_code codes[2]={{0x14000,sizeof(stub),stub},{0}};
    dm_gate_site site={0};
#if defined(__aarch64__)
    put32(images[0].bytes+0x100,0xa9bf7bfdu);put32(images[0].bytes+0x108,0xa8c17bfdu);put32(images[0].bytes+0x10c,0xd65f03c0u);
    dm_patch_request request={.instruction=0xd4000001u|(0x29u<<5),.original_offset=0x104,.stub_offset=0x14000,.host_gate=(uintptr_t)dm_gate};
    dm_patch_output patch;CHECK(!dm_patch_system(&request,&patch));put32(images[0].bytes+0x104,patch.replacement);memcpy(stub,patch.stub,sizeof(stub));site=patch.site;
#endif
    size_t module_bytes=0x14000+page+0x10000,budget=module_bytes+UNIT+4*local;
    errno=0;CHECK(!dm_host_create_accounted_process(UNIT,page,page,0)&&errno==EINVAL);
    dm_host*h=dm_host_create_accounted_process(2*UNIT,page,page,budget);CHECK(h&&info(h,21)==budget&&!info(h,22));
    uintptr_t aslr=info(h,12);size_t aslr_size=info(h,13);CHECK(aslr==(uintptr_t)h->heap&&aslr_size==2*UNIT+2*page+budget);
    uint8_t external[64]={0};CHECK(dm_host_register_writable(h,external,sizeof(external))==EINVAL);
    CHECK(dm_host_register_executable(h,external,sizeof(external))==EINVAL);
    dm_memory_pin pin={0};pthread_mutex_lock(&h->mutex);CHECK(dm_host_memory_pin_locked(h,&pin,(uintptr_t)external,1)==EINVAL);pthread_mutex_unlock(&h->mutex);
    const dm_module_mappings*maps=(void*)1;
    CHECK(dm_host_map_modules(h,images,codes,1,NULL,0,&maps)==EINVAL&&maps==(void*)1&&!info(h,22));
    CHECK(!dm_host_map_modules(h,images,codes,2,NULL,0,&maps)&&maps->count==2&&info(h,22)==module_bytes);
    for(size_t i=0;i<maps->count;i++)CHECK((uintptr_t)maps->mappings[i].base>=aslr&&maps->mappings[i].size<=aslr+aslr_size-(uintptr_t)maps->mappings[i].base);
    dm_module_mappings forbidden=*maps;CHECK(dm_modules_unmap(&forbidden)==-1&&forbidden.mappings==maps->mappings);
    CHECK(get64(maps->mappings[0].base+0x8308)==(uintptr_t)maps->mappings[1].base+0x8205);
    CHECK(dm_host_map_modules(h,images,codes,2,NULL,0,&maps)==EBUSY&&info(h,22)==module_bytes);
    CHECK(!resize(h,UNIT)&&info(h,22)==module_bytes+UNIT);
    dm_image_mapping malformed={h->heap,page-1};h->heap[0]=0x69;errno=0;
    CHECK(dm_image_reset_reserved(&malformed)==-1&&errno==EINVAL&&h->heap[0]==0x69);
    CHECK(resize(h,2*UNIT)==DM_SVC_OUT_OF_MEMORY&&info(h,22)==module_bytes+UNIT);
    uintptr_t ro=(uintptr_t)maps->mappings[0].base+0x4000,hole=(uintptr_t)maps->mappings[0].base+0x10000;
    dm_memory_info q=query(h,ro);CHECK(q.permission==1&&q.base==ro&&q.size==0x4000);
    q=query(h,hole);CHECK(!q.permission&&!q.state&&q.base==hole&&q.size==0x4000);
    pthread_mutex_lock(&h->mutex);CHECK(!dm_host_memory_read_pin_locked(h,&pin,ro,4));dm_host_memory_unpin_locked(h,&pin);
    CHECK(dm_host_memory_pin_locked(h,&pin,ro,4)==EINVAL);CHECK(dm_host_memory_read_pin_locked(h,&pin,hole-1,2)==EINVAL);pthread_mutex_unlock(&h->mutex);
    CHECK(dm_host_register_thread_stack(h,maps->mappings[0].base+0x8000,dm_host_thread_stack_minimum())==EINVAL);
    uint64_t r[31]={(uintptr_t)h->heap+h->capacity,(uintptr_t)h->heap,page};CHECK(!dm_host_svc(h,4,r)&&info(h,22)==module_bytes+UNIT);
    r[0]=(uintptr_t)h->heap+h->capacity;r[1]=(uintptr_t)h->heap;r[2]=page;CHECK(!dm_host_svc(h,5,r));
#if defined(__aarch64__)
    site.return_address+=(uintptr_t)maps->mappings[0].base;dm_gate_table gates={&site,1};CHECK(!dm_guest_attach(h,&gates));
    info_fn gate=(info_fn)(void*)(maps->mappings[0].base+0x100);pair returned=gate(0,21,0xffff8001u,0);CHECK(!returned.result&&returned.value==budget);
    returned=gate(0,22,0xffff8001u,0);CHECK(!returned.result&&returned.value==module_bytes+UNIT+local);
#else
    (void)site;CHECK(!dm_guest_attach(h,&empty));
#endif
    CHECK(info(h,22)==module_bytes+UNIT+local);uintptr_t tls=dm_guest_current()->slots[DM_SLOT_TLS];
    CHECK(tls>=aslr&&local<=aslr+aslr_size-tls);CHECK(dm_guest_current()->slots[DM_SLOT_TLR]==tls+8192);q=query(h,tls);CHECK(q.base==tls&&q.size==local&&q.permission==3&&q.state==0xc);
    CHECK(dm_host_register_thread_stack(h,(void*)tls,dm_host_thread_stack_minimum())==EINVAL);
    pthread_mutex_lock(&h->mutex);CHECK(!dm_host_writable(h,tls,4));pthread_mutex_unlock(&h->mutex);
    group held={.h=h,.mutex=PTHREAD_MUTEX_INITIALIZER,.cond=PTHREAD_COND_INITIALIZER,.tls=tls};pthread_t io;
    CHECK(!pthread_create(&io,NULL,pin_worker,&held));pthread_mutex_lock(&held.mutex);while(!held.ready)pthread_cond_wait(&held.cond,&held.mutex);pthread_mutex_unlock(&held.mutex);
    CHECK(!dm_guest_detach()&&info(h,22)==module_bytes+UNIT+local);
    CHECK(!dm_guest_attach(h,&empty)&&dm_guest_current()->slots[DM_SLOT_TLS]!=tls);CHECK(info(h,22)==module_bytes+UNIT+2*local);CHECK(!dm_guest_detach());
    dm_memory_pin stale={0};pthread_mutex_lock(&h->mutex);CHECK(dm_host_memory_pin_locked(h,&stale,tls,1)==EINVAL);pthread_mutex_unlock(&h->mutex);CHECK(dm_host_destroy(h)==EBUSY);
    pthread_mutex_lock(&held.mutex);held.finish=1;pthread_cond_broadcast(&held.cond);pthread_mutex_unlock(&held.mutex);CHECK(!pthread_join(io,NULL));
    CHECK(!pthread_cond_destroy(&held.cond)&&!pthread_mutex_destroy(&held.mutex));
    CHECK(info(h,22)==module_bytes+UNIT);q=query(h,tls);CHECK(!q.permission&&!q.state);denied(tls);
    CHECK(!dm_guest_attach(h,&empty)&&dm_guest_current()->slots[DM_SLOT_TLS]==tls&&*(uint64_t*)tls==0);CHECK(!dm_guest_detach());
    group g={.h=h,.mutex=PTHREAD_MUTEX_INITIALIZER,.cond=PTHREAD_COND_INITIALIZER};pthread_t threads[4];
    for(unsigned i=0;i<4;i++)CHECK(!pthread_create(&threads[i],NULL,attached_worker,&g));
    pthread_mutex_lock(&g.mutex);while(g.ready<4)pthread_cond_wait(&g.cond,&g.mutex);pthread_mutex_unlock(&g.mutex);
    CHECK(info(h,22)==budget&&dm_guest_attach(h,&empty)==ENOMEM&&!dm_guest_current());
    pthread_mutex_lock(&g.mutex);g.finish=1;pthread_cond_broadcast(&g.cond);pthread_mutex_unlock(&g.mutex);
    for(unsigned i=0;i<4;i++)CHECK(!pthread_join(threads[i],NULL));CHECK(info(h,22)==module_bytes+UNIT);
    CHECK(!pthread_cond_destroy(&g.cond)&&!pthread_mutex_destroy(&g.mutex));
    uintptr_t module0=(uintptr_t)maps->mappings[0].base,module1=(uintptr_t)maps->mappings[1].base;
    CHECK(!resize(h,0)&&info(h,22)==module_bytes);CHECK(!dm_host_destroy(h));unmapped(aslr,aslr_size,page);(void)module0;(void)module1;
    h=dm_host_create_accounted_process(UNIT,page,page,module_bytes-page);CHECK(h);maps=(void*)1;
    CHECK(dm_host_map_modules(h,images,codes,2,NULL,0,&maps)==ENOMEM&&maps==(void*)1&&!info(h,22));CHECK(!dm_host_destroy(h));
    for(unsigned failure=1;failure<=11;failure++){
        h=dm_host_create_accounted_process(UNIT,page,page,budget);CHECK(h);CHECK(!resize(h,UNIT));
        protect_low=(uintptr_t)h->heap+UNIT+2*page;protect_high=protect_low+budget;protect_calls=0;protect_fail=failure;maps=(void*)1;
        CHECK(dm_host_map_modules(h,images,codes,2,NULL,0,&maps)==EINVAL&&maps==(void*)1&&info(h,22)==UNIT);
        protect_low=protect_high=0;protect_fail=0;
        q=query(h,(uintptr_t)h->heap+UNIT+2*page);CHECK(!q.state&&!q.permission&&q.size==budget);denied(q.base);denied(q.base+module_bytes-page);
        CHECK(!dm_host_map_modules(h,images,codes,2,NULL,0,&maps)&&info(h,22)==UNIT+module_bytes);
        CHECK(!dm_host_destroy(h));
    }
    for(unsigned i=0;i<2;i++)dm_nso_free(&images[i]);
    puts("PASS accounting: real linked owned images, exact backing budget, RO/gap pins, alias dedup, heap quota, real SVC21/22, TLS deferred lifetime and concurrent quota rollback; unified address containment, protected reset/reuse and11 partial-commit failures/retries");return 0;
}
