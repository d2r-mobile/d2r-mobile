// 自有三模块诊断样本；不加载游戏，不放宽生产扫描器。
// Owned three-module diagnostic fixtures; no game inputs or production scanner relaxation.
#include "../startup_diagnostic.h"
#include "../rewrite/prepare.h"
#include "../runtime/accounting.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
static pid_t active_child;
static _Noreturn void failed(unsigned line,const char*condition){
    fprintf(stderr,"FAIL %u: %s\n",line,condition);
    if(active_child>0){kill(active_child,SIGKILL);while(waitpid(active_child,NULL,0)<0&&errno==EINTR){}}
    _Exit(1);
}
#define CHECK(c)do{if(!(c))failed(__LINE__,#c);}while(0)
#define UNIT (2u*1024u*1024u)
#if defined(__aarch64__)
extern void dm_gate(void);
extern unsigned char dm_diag_template_begin[],dm_diag_template_end[];
static void put32(unsigned char*p,uint32_t v){memcpy(p,&v,4);}
static void put64(unsigned char*p,uint64_t v){memcpy(p,&v,8);}
static dm_nso_image fixture(unsigned module){
    dm_nso_image im={.header={.image_size=0x10000,.bss_size=0x4000}};im.header.segments[0].size=0x1000;
    im.header.segments[1]=(dm_nso_segment){.memory_offset=0x4000,.size=0x4000};im.header.segments[2]=(dm_nso_segment){.memory_offset=0x8000,.size=0x4000};
    im.bytes=calloc(1,im.header.image_size);CHECK(im.bytes);for(unsigned i=0;i<0x1000;i+=4)put32(im.bytes+i,0xd503201fu);
    put32(im.bytes,0x14000010u);put32(im.bytes+4,0x4000);memcpy(im.bytes+0x4000,"MOD0",4);put32(im.bytes+0x4004,0x4000);
    const uint64_t tags[][2]={{4,0x4040},{5,0x4200},{6,0x4100},{10,20},{11,24},{7,0x4300},{8,24},{9,24},{0,0}};
    for(size_t i=0;i<sizeof(tags)/sizeof(*tags);i++){put64(im.bytes+0x8000+16*i,tags[i][0]);put64(im.bytes+0x8008+16*i,tags[i][1]);}
    put32(im.bytes+0x4040,1);put32(im.bytes+0x4044,3);put32(im.bytes+0x4048,1);put32(im.bytes+0x4050,2);
    memcpy(im.bytes+0x4200,"\0first\0second\0third\0",20);unsigned names[]={1,7,14};unsigned char*s=im.bytes+0x4118;
    put32(s,names[module]);s[4]=0x12;s[6]=1;put64(s+8,0x80);put64(s+16,32);s+=24;put32(s,names[(module+1)%3]);s[4]=0x12;
    put64(im.bytes+0x4300,0x8300);put64(im.bytes+0x4308,(2ull<<32)|257);
    put32(im.bytes+0x80,0xd65f03c0u);
    if(module==1){const uint32_t caller[]={0xa9bf7bfd,0x90000043,0x910c0063,0xf9400070,0xd63f0200,0xa8c17bfd,0xd65f03c0};memcpy(im.bytes+0x80,caller,sizeof(caller));}
    if(module==0){
        size_t template_size=(size_t)(dm_diag_template_end-dm_diag_template_begin);CHECK(template_size<=64);memcpy(im.bytes+0x40,dm_diag_template_begin,template_size);
        const uint32_t atomic[]={0xc85f7c02,0x8b020252,0xc8037c12,0x35ffffa3,0xd503201f,0xd503201f,0xd503201f,0xc85f7c02,0xd65f03c0};memcpy(im.bytes+0x100,atomic,sizeof(atomic));
        put32(im.bytes+0x4010,0x400);put32(im.bytes+0x4014,0x414);
        memcpy(im.bytes+0x4400,"\1\33\3\73",4);put32(im.bytes+0x4404,0xfc);put32(im.bytes+0x4408,1);
        put32(im.bytes+0x440c,(uint32_t)(0x100-0x4400));put32(im.bytes+0x4410,0x120);
        put32(im.bytes+0x4500,16);put32(im.bytes+0x4504,0);const unsigned char cie[]={1,'z','R',0,1,0x78,30,1,0x1b};memcpy(im.bytes+0x4508,cie,sizeof(cie));
        put32(im.bytes+0x4520,16);put32(im.bytes+0x4524,0x24);put32(im.bytes+0x4528,(uint32_t)(0x100-0x4528));put32(im.bytes+0x452c,sizeof(atomic));
    }
    const uint32_t low[]={0xd2800009,0x9100013f,0xd421e840};memcpy(im.bytes+0x200,low,sizeof(low));
    put32(im.bytes+0x240,0xd4000001u|(0xbeefu<<5));put32(im.bytes+0x244,0xd65f03c0u);
    const uint32_t unknown_info[]={0xd2807ce1,0x52900022,0x72bfffe2,0xaa1f03e3,0xd4000521,0xd65f03c0};memcpy(im.bytes+0x280,unknown_info,sizeof(unknown_info));
    const uint32_t known_info[]={0xaa0003ea,0xd28001c1,0x52900022,0x72bfffe2,0xaa1f03e3,0xd4000521,0xaa0a03e0,0xd65f03c0};memcpy(im.bytes+0x2a0,known_info,sizeof(known_info));
    put32(im.bytes+0x2c0,0xd40003c1);put32(im.bytes+0x2c4,0x17ffffff);put32(im.bytes+0x300,0x14000000);
    const uint32_t unknown_gate[]={0xa9bf7bfd,0xf9401270,0xd63f0200,0xa8c17bfd,0xd65f03c0};memcpy(im.bytes+0x340,unknown_gate,sizeof(unknown_gate));
    put32(im.bytes+0x3a0,0x14000000u|((uint32_t)((0x100-0x3a0)/4)&0x03ffffffu));
    return im;
}
static void refusal(dm_nso_image*images){dm_startup_diagnostic*sentinel=(void*)1;CHECK(dm_diag_prepare(images,3,(uintptr_t)dm_gate,&sentinel)!=0&&sentinel==(void*)1);}
static void prepare_cases(dm_nso_image*images,dm_startup_diagnostic**out){
    dm_rewrite_report report;dm_rewrite_output production={.count=123};
    CHECK(dm_rewrite_prepare(&images[0],0x10000,(uintptr_t)dm_gate,&report,&production)==ENOTSUP&&report.unmatched==1&&report.rewritten_windows==1&&production.count==123);
    unsigned char saved[0x10000];memcpy(saved,images[0].bytes,sizeof(saved));
    CHECK(!dm_diag_prepare(images,3,(uintptr_t)dm_gate,out));CHECK(!memcmp(saved,images[0].bytes,sizeof(saved)));
    const dm_diag_extent*e;CHECK(dm_diag_quarantines(*out,0,&e)==1&&e[0].begin==0x100&&e[0].end==0x124);
    put32(images[0].bytes+0x452c,UINT32_MAX);refusal(images);memcpy(images[0].bytes,saved,sizeof(saved));
    put32(images[0].bytes+0x4408,UINT32_MAX);refusal(images);memcpy(images[0].bytes,saved,sizeof(saved));
    put32(images[0].bytes+0x440c,(uint32_t)(0x104-0x4400));refusal(images);memcpy(images[0].bytes,saved,sizeof(saved));
    images[0].bytes[0x4510]=0xff;refusal(images);memcpy(images[0].bytes,saved,sizeof(saved));
    memcpy(images[0].bytes+0x400,images[0].bytes+0x4400,20);
    put32(images[0].bytes+0x4010,(uint32_t)(0x400-0x4000));put32(images[0].bytes+0x4014,(uint32_t)(0x414-0x4000));
    put32(images[0].bytes+0x404,0x40fc);put32(images[0].bytes+0x40c,(uint32_t)(0x100-0x400));put32(images[0].bytes+0x410,0x4120);
    put32(images[0].bytes+0x452c,0x320);refusal(images);memcpy(images[0].bytes,saved,sizeof(saved));
    put32(images[0].bytes+0x380,0xffffffffu);refusal(images);memcpy(images[0].bytes,saved,sizeof(saved));
    put32(images[0].bytes+0x3c0,0x18000000u|((uint32_t)((0x100-0x3c0)/4)&0x7ffffu)<<5);refusal(images);memcpy(images[0].bytes,saved,sizeof(saved));
    memmove(images[0].bytes+0xf8,images[0].bytes+0x100,16);put32(images[0].bytes+0x108,0xd503201f);put32(images[0].bytes+0x10c,0xd503201f);
    put32(images[0].bytes+0x3a0,0xd503201f);
    CHECK(dm_rewrite_prepare(&images[0],0x10000,(uintptr_t)dm_gate,&report,NULL)==ENOTSUP&&report.unmatched==1&&report.rewritten_windows==1);refusal(images);memcpy(images[0].bytes,saved,sizeof(saved));
    dm_host*h=dm_host_create_accounted_process(UNIT,UNIT,UNIT,8*UNIT);CHECK(h);CHECK(dm_diag_map(*out,h)==EPERM);CHECK(!dm_host_destroy(h));
    CHECK(dm_rewrite_prepare(&images[0],0x10000,(uintptr_t)dm_gate,&report,NULL)==ENOTSUP);
    puts("PASS preparation: originals strict-rejected/unchanged, generic FDE quarantine, malformed metadata/literals/cross-window rejected, parent mapping forbidden");
}
static int64_t now(void){struct timespec t;CHECK(!clock_gettime(CLOCK_MONOTONIC,&t));return(int64_t)t.tv_sec*1000000000+t.tv_nsec;}
static void run(dm_startup_diagnostic*s,size_t module,uint32_t target,dm_diag_event expected,int timed){
    uint64_t*shared=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_ANON|MAP_SHARED,-1,0);CHECK(shared!=MAP_FAILED);shared[0]=shared[1]=0;
    int descriptors[2];CHECK(!pipe(descriptors));CHECK(!fflush(NULL));pid_t child=fork();CHECK(child>=0);
    if(!child){
        close(descriptors[0]);dm_host*h=dm_host_create_accounted_process(UNIT,UNIT,UNIT,8*UNIT);CHECK(h);CHECK(!dm_diag_map(s,h));CHECK(dm_host_destroy(h)==EBUSY);
        const dm_gate_table*table=dm_diag_gates(s);for(size_t i=0;i<table->count;i++)CHECK(table->sites[i].kind!=DM_GATE_X18_ADDRESS);
        for(size_t i=0;i<table->count;i++)if(table->sites[i].kind==DM_GATE_SVC){size_t at=shared[7]++;CHECK(at<200);shared[8+2*at]=table->sites[i].return_address;shared[9+2*at]=table->sites[i].argument;}
        uintptr_t entry,stage;CHECK(!dm_diag_address(s,0,0x40,44,&entry));CHECK(!dm_diag_address(s,module,target,4,&stage));
        uintptr_t invalid=123;CHECK(dm_diag_address(s,0,0x8000,4,&invalid)==EINVAL&&invalid==123);
        uint64_t r[31]={[1]=UNIT};CHECK(!dm_host_svc(h,1,r));unsigned char*heap=(void*)(uintptr_t)r[1];CHECK(!dm_host_register_thread_stack(h,heap,65536));
        uint64_t*payload=(void*)(heap+65536);payload[0]=stage;payload[1]=payload[2]=0;payload[4]=(uintptr_t)dm_gate;
        CHECK(!dm_guest_attach(h,dm_diag_gates(s)));CHECK(dm_diag_free(s)==EBUSY);
        memset(r,0,sizeof(r));r[1]=entry;r[2]=(uintptr_t)payload;r[3]=(uintptr_t)heap+65536;r[4]=44;r[5]=(uint32_t)-2;CHECK(!dm_host_svc(h,8,r));uint32_t handle=(uint32_t)r[1];
        CHECK(!dm_diag_trace_start(descriptors[1],s,handle));r[0]=handle;CHECK(!dm_host_svc(h,9,r));
        payload[3]=handle;r[1]=(uintptr_t)&payload[3];r[2]=1;r[3]=-1;CHECK(!dm_host_svc(h,0x18,r));CHECK(payload[1]==handle&&payload[2]==1);
        r[0]=handle;CHECK(!dm_host_svc(h,0x16,r));CHECK(!dm_guest_detach());CHECK(!dm_host_unregister_thread_stack(h,heap+65536));
        CHECK(!dm_diag_free(s));CHECK(!dm_host_destroy(h));shared[0]=1;shared[1]=1;dm_diag_trace_finish();
    }
    active_child=child;CHECK(!close(descriptors[1]));int flags=fcntl(descriptors[0],F_GETFL);CHECK(flags>=0&&!fcntl(descriptors[0],F_SETFL,flags|O_NONBLOCK));
    unsigned char bytes[sizeof(dm_diag_record)*(DM_DIAG_TRACE_LIMIT+1)];size_t used=0;int status=0,done=0;int64_t deadline=now()+(timed?200000000:3000000000ll);
    while(!done){ssize_t n=read(descriptors[0],bytes+used,sizeof(bytes)-used);if(n>0)used+=(size_t)n;else CHECK(n==0||errno==EAGAIN||errno==EINTR);
        pid_t rc=waitpid(child,&status,WNOHANG);CHECK(rc==0||rc==child);done=rc==child;if(done)active_child=0;
        if(!done&&now()>=deadline){CHECK(!kill(child,SIGKILL));CHECK(waitpid(child,&status,0)==child);active_child=0;done=1;CHECK(timed);}
        if(!done){struct timespec pause={0,1000000};nanosleep(&pause,NULL);}}
    for(;;){ssize_t n=read(descriptors[0],bytes+used,sizeof(bytes)-used);if(n>0){used+=(size_t)n;continue;}CHECK(n==0);break;}
    CHECK(!close(descriptors[0]));CHECK(waitpid(child,&status,WNOHANG)==-1&&errno==ECHILD);
    int saw_info=0;
    for(size_t off=0;off+sizeof(dm_diag_record)<=used;off+=sizeof(dm_diag_record)){
        dm_diag_record record;memcpy(&record,bytes+off,sizeof(record));if(record.event!=DM_DIAG_SERVICE)continue;int found=0;
        for(size_t i=0;i<shared[7];i++)if(record.pc==shared[8+2*i]&&record.code==shared[9+2*i])found=1;CHECK(found);
        if(record.code==0x29){CHECK(record.value==(target==0x2a0?14u:999u));saw_info=1;}}
    if(target==0x280||target==0x2a0)CHECK(saw_info);
    if(timed){CHECK(WIFSIGNALED(status)&&WTERMSIG(status)==SIGKILL&&!shared[0]);}
    else{dm_diag_event terminal;CHECK(!dm_diag_trace_validate(bytes,used,&terminal)&&terminal==expected);CHECK(WIFEXITED(status));
        if(expected==DM_DIAG_RETURNED)CHECK(WEXITSTATUS(status)==0&&shared[0]&&shared[1]);else CHECK(!shared[0]&&!shared[1]);
        if(target==0x280){dm_diag_record previous;CHECK(used>=2*sizeof(previous));memcpy(&previous,bytes+used-2*sizeof(previous),sizeof(previous));CHECK(previous.event==DM_DIAG_SERVICE&&previous.code==0x29&&previous.value==999);}
        if(expected==DM_DIAG_SIGNAL||expected==DM_DIAG_QUARANTINE){dm_diag_record last;memcpy(&last,bytes+used-sizeof(last),sizeof(last));CHECK(last.code==0xfffe||last.value==1);}}
    CHECK(!munmap(shared,4096));
}
static void protocol(void){
    dm_diag_record records[3]={{0x44494147,0,DM_DIAG_SERVICE,0xfffe,0,0},{0x44494147,1,DM_DIAG_SERVICE,0xa,0,0},{0x44494147,2,DM_DIAG_RETURNED,0,0,0}};
    dm_diag_event terminal=0;CHECK(!dm_diag_trace_validate(records,sizeof(records),&terminal)&&terminal==DM_DIAG_RETURNED);
    CHECK(dm_diag_trace_validate(records,sizeof(records)-1,&terminal)==EINVAL);CHECK(dm_diag_trace_validate(records+2,sizeof(*records),&terminal)==EINVAL);
    records[1]=records[2];records[1].sequence=1;CHECK(dm_diag_trace_validate(records,sizeof(records),&terminal)==EINVAL);
    puts("PASS protocol: partial/duplicate/terminal-only records rejected; terminal status alone cannot certify a returned stage");
}
int main(void){dm_nso_image images[3];for(unsigned i=0;i<3;i++)images[i]=fixture(i);dm_startup_diagnostic*s=NULL;prepare_cases(images,&s);protocol();
    run(s,1,0x80,DM_DIAG_RETURNED,0);for(uint32_t off=0x100;off<0x124;off+=4)run(s,0,off,DM_DIAG_QUARANTINE,0);
    run(s,0,0x3a0,DM_DIAG_QUARANTINE,0);
    run(s,2,0x200,DM_DIAG_SIGNAL,0);run(s,2,0x240,DM_DIAG_UNSUPPORTED,0);run(s,2,0x280,DM_DIAG_UNSUPPORTED,0);
    run(s,2,0x2a0,DM_DIAG_RETURNED,0);
    run(s,2,0x2c0,DM_DIAG_OVERFLOW,0);run(s,2,0x300,0,1);run(s,2,0x340,DM_DIAG_SIGNAL,0);run(s,0,0x44,DM_DIAG_SIGNAL,0);
    CHECK(!dm_diag_free(s));for(unsigned i=0;i<3;i++)dm_nso_free(&images[i]);
    puts("PASS execution: real linked three-module managed stage/ExitThread; every quarantine entry stops on executing-thread altstack; unsupported service/subtype, low-SP fault, trace limit and watchdog never resume");return 0;}
#else
int main(void){puts("UNSUPPORTED: native ARM64 diagnostic fixture required");return 77;}
#endif
