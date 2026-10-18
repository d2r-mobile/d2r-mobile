// 自有小栈矩阵；宿主页保护与逻辑栈尾哨兵分开验证，不包含游戏或 SDK 代码。
// Owned small-stack matrix; distinguish physical page guards from logical tail canaries, without game or SDK code.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "../runtime/internal.h"
#include "../runtime/bridge.h"
#include "../runtime/romfs.h"
#include "../runtime/patch.h"
#include "../runtime/x18_patch.h"
#include "../image_map.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);abort();}}while(0)
#define UNIT (2u*1024u*1024u)
extern void dm_gate(void),dm_small_guest(void),dm_small_sdk_entry(void);
typedef struct{
    uintptr_t functions[4],imports[6];const char*path,*mount;
    uintptr_t initial_sp,physical_x18;size_t cache;uint64_t file;size_t got;int64_t file_size;
    char bytes[16];uint64_t done,after;uintptr_t expected_sp,reserved,tls,id;
}payload;
_Static_assert(offsetof(payload,initial_sp)==96&&offsetof(payload,bytes)==144&&offsetof(payload,id)==200,"assembly payload ABI");
typedef struct{void*mapping;uint8_t*base;size_t accessible,total,size,reserve;}guarded;
static struct{pthread_mutex_t mutex;pthread_cond_t cond;payload*p[2];uintptr_t native_sp[2];unsigned entered;int release;}barrier={.mutex=PTHREAD_MUTEX_INITIALIZER,.cond=PTHREAD_COND_INITIALIZER};
ssize_t pread(int fd,void*buffer,size_t size,off_t offset){
    CHECK(dm_bridge_on_native_stack());uintptr_t sp;__asm__ volatile("mov %0,sp":"=r"(sp));
    dm_guest_context*c=dm_guest_current();CHECK(c&&c->bridge&&c->bridge->active&&c->slots[DM_SLOT_X18]==1);
    CHECK(c->bridge->active->kind==4&&c->bridge->active->source_domain==DM_BRIDGE_GUEST&&c->bridge->calls==7);
    CHECK(size==8&&offset==2);CHECK(!pthread_mutex_lock(&barrier.mutex));unsigned i=0;
    while(i<2&&buffer!=barrier.p[i]->bytes)i++;CHECK(i<2&&!barrier.native_sp[i]);
    barrier.native_sp[i]=sp;barrier.entered++;CHECK(!pthread_cond_broadcast(&barrier.cond));
    while(!barrier.release)CHECK(!pthread_cond_wait(&barrier.cond,&barrier.mutex));
    CHECK(!pthread_mutex_unlock(&barrier.mutex));struct iovec v={buffer,size};return preadv(fd,&v,1,offset);
}
static void put(void*p,uint32_t word){memcpy(p,&word,4);}
static dm_image_mapping program(dm_gate_site sites[4]){
    size_t page=(size_t)sysconf(_SC_PAGESIZE);dm_nso_image image={.header.image_size=page};
    image.header.segments[0].size=512;image.header.segments[1].memory_offset=(uint32_t)page;image.header.segments[2].memory_offset=(uint32_t)page;
    image.bytes=calloc(1,page);CHECK(image.bytes);uint32_t ins[4]={0xd40004a1,0xd53bd040,0,0xd4000141};
    for(unsigned i=0;i<4;i++){
        put(image.bytes+i*16+4,0xd65f03c0);
        if(i==2){dm_x18_request req={.original_offset=i*16,.stub_offset=320,.translated=0x91000529,.scratch={9,10,11},.host_gate=(uintptr_t)dm_gate};
            dm_x18_patch out;CHECK(!dm_patch_x18(&req,&out));put(image.bytes+i*16,out.replacement);memcpy(image.bytes+320,out.stub,sizeof(out.stub));sites[i]=out.site;
        }else{dm_patch_request req={ins[i],i*16,128+i*32,(uintptr_t)dm_gate};dm_patch_output out;CHECK(!dm_patch_system(&req,&out));
            put(image.bytes+i*16,out.replacement);memcpy(image.bytes+req.stub_offset,out.stub,sizeof(out.stub));sites[i]=out.site;}
    }
    dm_image_mapping mapped={0};CHECK(!dm_image_map(&image,&mapped));dm_nso_free(&image);
    for(unsigned i=0;i<4;i++)sites[i].return_address+=(uintptr_t)mapped.base;
    for(unsigned i=0;i<4;i++)for(unsigned j=i+1;j<4;j++)if(sites[j].return_address<sites[i].return_address){dm_gate_site t=sites[i];sites[i]=sites[j];sites[j]=t;}
    return mapped;
}
static guarded stack_alloc(size_t size,size_t reserve){
    size_t page=(size_t)sysconf(_SC_PAGESIZE),accessible=(size+page-1)/page*page,total=accessible+2*page;
    void*m=mmap(NULL,total,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);CHECK(m!=MAP_FAILED);
    uint8_t*base=(uint8_t*)m+page;CHECK(!mprotect(base,accessible,PROT_READ|PROT_WRITE));memset(base,0xa5,accessible);
    return(guarded){m,base,accessible,total,size,reserve};
}
static size_t stack_check(guarded*s){
    for(size_t i=s->size;i<s->accessible;i++)CHECK(s->base[i]==0xa5);
    if(s->reserve)for(size_t i=s->size-s->reserve+8;i<s->size;i++)CHECK(s->base[i]==0xa5);
    size_t usable=s->size-s->reserve,lowest=usable;
    for(size_t i=0;i<usable;i++)if(s->base[i]!=0xa5){lowest=i;break;}
    if(dm_host_thread_stack_minimum()==4096){CHECK(usable-lowest<=1056);if(usable>1056)for(size_t i=0;i<usable-1056;i++)CHECK(s->base[i]==0xa5);}
    return usable-lowest;
}
static uint64_t handle_call(dm_host*h,unsigned n,uint32_t handle){uint64_t r[31]={handle};return dm_host_svc(h,n,r);}
static uint64_t create(dm_host*h,payload*p,void*tt,uintptr_t top,uint32_t*out){
    uint64_t r[31]={0,tt?(uintptr_t)dm_small_sdk_entry:(uintptr_t)dm_small_guest,(uintptr_t)(tt?tt:(void*)p),top,44,(uint32_t)-2};
    uint64_t before[31];memcpy(before,r,sizeof(r));uint64_t result=dm_host_svc(h,8,r);
    if(!result)*out=(uint32_t)r[1];else CHECK(!memcmp(r+1,before+1,sizeof(r)-8));return result;
}
static void run_case(size_t size,size_t reserve,const char*root,const char*path,dm_image_mapping*code,dm_gate_table*gates){
    guarded stacks[2]={stack_alloc(size,0),stack_alloc(size,reserve)};
    dm_host*h=dm_host_create(UNIT);CHECK(h);uint64_t r[31]={[1]=UNIT};CHECK(!dm_host_svc(h,1,r));uint8_t*heap=(void*)(uintptr_t)r[1];
    payload*p=(void*)heap;void*tt=heap+4096;uint64_t fields[5]={0,0,(uintptr_t)stacks[1].base,size,(uintptr_t)&p[1]};memcpy(tt,fields,sizeof(fields));
    for(unsigned i=0;i<2;i++)CHECK(!dm_host_register_writable(h,stacks[i].base,size));
    CHECK(!dm_host_register_executable(h,(void*)(uintptr_t)dm_small_guest,4));CHECK(!dm_host_register_executable(h,(void*)(uintptr_t)dm_small_sdk_entry,4));
    CHECK(!dm_host_register_executable(h,code->base,512));CHECK(!dm_host_mount_romfs(h,root));
    dm_sdk_thread_abi abi={(uintptr_t)dm_small_sdk_entry,64,16,24,reserve};CHECK(!dm_host_set_sdk_thread_abi(h,&abi));
    uintptr_t callback=(uintptr_t)dm_small_guest;memcpy(stacks[1].base+size-reserve,&callback,8);
    size_t n;const dm_symbol_override*imports=dm_romfs_imports(&n);CHECK(n==6);
    for(unsigned i=0;i<2;i++){for(unsigned j=0;j<4;j++)p[i].functions[j]=(uintptr_t)code->base+j*16;
        for(unsigned j=0;j<6;j++)p[i].imports[j]=imports[j].address;p[i].path=path;p[i].mount="rom";
        p[i].expected_sp=(uintptr_t)stacks[i].base+size-stacks[i].reserve;}
    CHECK(!dm_guest_attach(h,gates));uint32_t handles[2]={0x1234,0x5678};
    if(size<dm_host_thread_stack_minimum()){
        CHECK(dm_host_register_thread_stack(h,stacks[0].base,size)==EINVAL);
        CHECK(create(h,&p[0],NULL,p[0].expected_sp,&handles[0])==DM_SVC_INVALID_ADDRESS&&handles[0]==0x1234);
        CHECK(create(h,&p[1],tt,p[1].expected_sp,&handles[1])==DM_SVC_INVALID_ADDRESS&&handles[1]==0x5678);
        printf("PASS: sanitizer refusal size=%zu, explicit/SDK outputs unchanged\n",size);
    }else{
        CHECK(!dm_host_register_thread_stack(h,stacks[0].base,size));
        if(size-reserve<dm_host_thread_stack_entry_headroom()){
            CHECK(create(h,&p[1],tt,p[1].expected_sp,&handles[1])==DM_SVC_INVALID_ADDRESS&&handles[1]==0x5678);
            printf("PASS: SDK usable headroom=%zu rejected below %zu\n",size-reserve,dm_host_thread_stack_entry_headroom());
            CHECK(!dm_host_unregister_thread_stack(h,stacks[0].base+size));goto cleanup;
        }
        for(unsigned i=0;i<2;i++){CHECK(!create(h,&p[i],i?tt:NULL,p[i].expected_sp,&handles[i]));CHECK(!handle_call(h,0x16,handles[i]));}
        CHECK(!dm_host_register_thread_stack(h,stacks[1].base,size));CHECK(!dm_host_unregister_thread_stack(h,stacks[1].base+size));
        barrier.entered=0;barrier.release=0;memset(barrier.native_sp,0,sizeof(barrier.native_sp));
        for(unsigned i=0;i<2;i++){barrier.p[i]=&p[i];CHECK(!create(h,&p[i],i?tt:NULL,p[i].expected_sp,&handles[i]));}
        for(unsigned i=0;i<2;i++)CHECK(!handle_call(h,9,handles[i]));
        CHECK(!pthread_mutex_lock(&barrier.mutex));while(barrier.entered<2)CHECK(!pthread_cond_wait(&barrier.cond,&barrier.mutex));
        CHECK(barrier.native_sp[0]!=barrier.native_sp[1]&&p[0].tls!=p[1].tls&&p[0].id!=p[1].id);
        CHECK(dm_host_unregister_thread_stack(h,stacks[0].base+size)==EBUSY);CHECK(dm_host_register_thread_stack(h,stacks[1].base,size)==EINVAL);
        barrier.release=1;CHECK(!pthread_cond_broadcast(&barrier.cond));CHECK(!pthread_mutex_unlock(&barrier.mutex));
        size_t used[2];for(unsigned i=0;i<2;i++){
            uint64_t wait[31]={0,(uintptr_t)&handles[i],1,3000000000};CHECK(!dm_host_svc(h,0x18,wait));
            CHECK(p[i].done==1&&!p[i].after&&p[i].initial_sp==p[i].expected_sp&&p[i].cache==4096&&p[i].got==8&&p[i].file_size==10&&!memcmp(p[i].bytes,"cdefghij",8));
            CHECK(!handle_call(h,0x16,handles[i]));used[i]=stack_check(&stacks[i]);}
        CHECK(!dm_host_unregister_thread_stack(h,stacks[0].base+size));CHECK(!dm_host_register_thread_stack(h,stacks[1].base,size));CHECK(!dm_host_unregister_thread_stack(h,stacks[1].base+size));
        printf("PASS: actual pure-ASM size=%zu SDKreserve=%zu exactSP, SVC/TLS/plain-x18, all imports, ABI/FP/q0-q31, native frames=%#llx/%#llx, touched=%zu/%zu, guards/tail, terminal and explicit/auto lease cleanup\n",size,reserve,(unsigned long long)barrier.native_sp[0],(unsigned long long)barrier.native_sp[1],used[0],used[1]);
    }
cleanup:
    CHECK(!dm_guest_detach());CHECK(!dm_host_destroy(h));for(unsigned i=0;i<2;i++)CHECK(!munmap(stacks[i].mapping,stacks[i].total));
}
int main(void){
    const char*tmp=getenv("TMPDIR");CHECK(tmp);char root[4096],path[4096],guest_path[4096];
    CHECK(snprintf(root,sizeof(root),"%s/dm-small-stack.XXXXXX",tmp)<(int)sizeof(root));CHECK(mkdtemp(root));strcpy(path,root);size_t rootlen=strlen(root),ends[8];
    for(unsigned i=0;i<8;i++){size_t n=strlen(path);CHECK(n+98<sizeof(path));path[n++]='/';memset(path+n,'a'+(int)i,96);path[n+96]=0;CHECK(!mkdir(path,0700));ends[i]=n+96;}
    strcat(path,"/Name.bin");FILE*f=fopen(path,"wb");CHECK(f&&fwrite("abcdefghij",1,10,f)==10&&!fclose(f));
    CHECK(snprintf(guest_path,sizeof(guest_path),"rom:%s",path+rootlen)<(int)sizeof(guest_path));CHECK(strlen(guest_path)>768);
    dm_gate_site sites[4];dm_image_mapping code=program(sites);dm_gate_table gates={sites,4};
    printf("host_page=%ld minimum=%zu entry_headroom=%zu; lower physical guard, logical upper canary, upper physical guard\n",sysconf(_SC_PAGESIZE),dm_host_thread_stack_minimum(),dm_host_thread_stack_entry_headroom());
    const size_t sizes[]={4096,8192,16384,32768,65536};for(unsigned i=0;i<5;i++)run_case(sizes[i],32,root,guest_path,&code,&gates);
    size_t minimum=dm_host_thread_stack_minimum(),headroom=dm_host_thread_stack_entry_headroom();
    run_case(minimum,minimum-headroom,root,guest_path,&code,&gates);
    run_case(minimum,minimum-headroom+16,root,guest_path,&code,&gates);
    CHECK(!dm_image_unmap(&code));CHECK(!unlink(path));for(unsigned i=8;i;i--){path[ends[i-1]]=0;CHECK(!rmdir(path));}CHECK(!rmdir(root));
    puts("PASS: owned guarded small-stack matrix complete; no game content");return 0;
}
