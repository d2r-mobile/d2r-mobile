// 只读 SDK 文件导入测试；依据 romfs.h，以合成目录验证读取、句柄代际、并发与路径隔离，可选只读检查本地资源。
// Read-only SDK file-import tests; based on romfs.h, synthetic directories check reads, generations, concurrency and path confinement, with optional local-resource inspection.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "../runtime/romfs.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>
#define CHECK(c)do{if(!(c)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);abort();}}while(0)
typedef uint32_t(*open_fn)(uint64_t*,const char*,int);
typedef uint32_t(*read_fn)(size_t*,uint64_t,int64_t,void*,size_t);
typedef uint32_t(*size_fn)(int64_t*,uint64_t);
typedef void(*close_fn)(uint64_t);
typedef uint32_t(*query_fn)(size_t*);
typedef uint32_t(*mount_fn)(const char*,void*,size_t);
static open_fn open_file;static read_fn read_file;static size_fn file_size;static close_fn close_file;
static struct{pthread_mutex_t mutex;pthread_cond_t condition;void*buffer;int entered,release,error;}read_barrier={
    .mutex=PTHREAD_MUTEX_INITIALIZER,.condition=PTHREAD_COND_INITIALIZER};
// 测试进程在真正 preadv 前阻塞 pread，确定性验证读取期间的映射租约；产品无测试钩子。
// Interpose pread only in this test process, blocking before real preadv to verify mapping leases without production hooks.
ssize_t pread(int fd,void*buffer,size_t size,off_t offset){
    CHECK(!pthread_mutex_lock(&read_barrier.mutex));int error=0;
    if(buffer==read_barrier.buffer){
        read_barrier.entered=1;CHECK(!pthread_cond_broadcast(&read_barrier.condition));
        while(!read_barrier.release)CHECK(!pthread_cond_wait(&read_barrier.condition,&read_barrier.mutex));
        error=read_barrier.error;
    }
    CHECK(!pthread_mutex_unlock(&read_barrier.mutex));if(error){errno=error;return -1;}
    struct iovec vector={buffer,size};return preadv(fd,&vector,1,offset);
}
static uintptr_t need(const char*name){size_t n;const dm_symbol_override*imports=dm_romfs_imports(&n);
    for(size_t i=0;i<n;i++)if(!strcmp(imports[i].name,name))return(uintptr_t)imports[i].address;CHECK(0);return 0;}
typedef struct{dm_host*host;const dm_gate_table*gates;uint64_t handle;}worker;
static void*read_worker(void*arg){worker*w=arg;CHECK(!dm_guest_attach(w->host,w->gates));
    for(unsigned i=0;i<1000;i++){unsigned off=i%6;size_t got=0;char bytes[2];CHECK(!read_file(&got,w->handle,off,bytes,2));
        CHECK(got==(off==5?1:2)&&!memcmp(bytes,&"abcdef"[off],got));}
    CHECK(!dm_guest_detach());return NULL;
}
static void write_fixture(const char*path,const char*text){FILE*f=fopen(path,"wb");CHECK(f);CHECK(fwrite(text,1,strlen(text),f)==strlen(text));CHECK(!fclose(f));}
typedef struct{dm_host*host;uint64_t handle;size_t*out;char*buffer;uint32_t result;}blocked_read;
static void*blocked_reader(void*arg){
    blocked_read*w=arg;dm_gate_table gates={0};CHECK(!dm_guest_attach(w->host,&gates));
    w->result=read_file(w->out,w->handle,0,w->buffer,6);CHECK(!dm_guest_detach());return NULL;
}
static uint64_t resize_heap(dm_host*h,size_t size,uintptr_t*out){
    uint64_t r[31]={[1]=size};uint64_t result=dm_host_svc(h,1,r);if(!result&&out)*out=r[1];return result;
}
static uint64_t map_stack(dm_host*h,unsigned svc,uintptr_t dst,uintptr_t src,size_t size){
    uint64_t r[31]={dst,src,size};return dm_host_svc(h,svc,r);
}
static void memory_leases(const char*root){
    const size_t unit=2u*1024u*1024u;long native_page=sysconf(_SC_PAGESIZE);CHECK(native_page>0);size_t page=(size_t)native_page;
    dm_host*h=dm_host_create_layout(2*unit,unit);CHECK(h);CHECK(!dm_host_mount_romfs(h,root));
    char*readonly=mmap(NULL,page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);CHECK(readonly!=MAP_FAILED);
    strcpy(readonly,"rom:/data/Name.bin");strcpy(readonly+64,"rom");CHECK(!mprotect(readonly,page,PROT_READ|PROT_EXEC));
    CHECK(!dm_host_register_executable(h,readonly,page));
    dm_gate_table gates={0};CHECK(!dm_guest_attach(h,&gates));uintptr_t heap=0;CHECK(!resize_heap(h,2*unit,&heap));
    uint64_t info[31]={[1]=14,[2]=0xffff8001u};CHECK(!dm_host_svc(h,0x29,info));uintptr_t alias=info[1];
    // 分别只租用堆缓冲区、堆输出、别名缓冲区和别名输出，避免一个租约掩盖另一个遗漏。
    // Isolate heap-buffer, heap-output, alias-buffer and alias-output leases so one pin cannot hide a missing second pin.
    for(unsigned mode=0;mode<5;mode++){
        char local[6]={0};size_t got=123;uintptr_t source=heap+unit+(mode==3?page:0);
        if(mode==2||mode==3)CHECK(!map_stack(h,4,alias,source,page));
        char*buffer=mode==0||mode==4?(char*)(heap+unit):mode==2?(char*)alias:local;
        size_t*out=mode==1?(size_t*)(heap+unit+page):mode==3?(size_t*)alias:&got;
        memset(buffer,0,6);*out=123;uint64_t handle;CHECK(!open_file(&handle,"rom:/data/Name.bin",1));
        CHECK(!pthread_mutex_lock(&read_barrier.mutex));read_barrier.buffer=buffer;read_barrier.entered=0;
        read_barrier.release=0;read_barrier.error=mode==4?EIO:0;CHECK(!pthread_mutex_unlock(&read_barrier.mutex));
        blocked_read work={h,handle,out,buffer,0};pthread_t thread;CHECK(!pthread_create(&thread,NULL,blocked_reader,&work));
        CHECK(!pthread_mutex_lock(&read_barrier.mutex));
        while(!read_barrier.entered)CHECK(!pthread_cond_wait(&read_barrier.condition,&read_barrier.mutex));
        CHECK(!pthread_mutex_unlock(&read_barrier.mutex));
        CHECK(resize_heap(h,unit,NULL)==DM_SVC_BAD_STATE);
        if(mode==2||mode==3)CHECK(map_stack(h,5,alias,source,page)==DM_SVC_BAD_STATE);
        else CHECK(map_stack(h,4,alias,heap+unit+(mode==1?page:0),page)==DM_SVC_BAD_STATE);
        close_file(handle);
        CHECK(!pthread_mutex_lock(&read_barrier.mutex));read_barrier.release=1;
        CHECK(!pthread_cond_broadcast(&read_barrier.condition));CHECK(!pthread_mutex_unlock(&read_barrier.mutex));
        CHECK(!pthread_join(thread,NULL));
        if(mode==4)CHECK(work.result==0x2a2u&&*out==123&&!memcmp(buffer,"\0\0\0\0\0\0",6));
        else CHECK(!work.result&&*out==6&&!memcmp(buffer,"abcdef",6));
        CHECK(!pthread_mutex_lock(&read_barrier.mutex));read_barrier.buffer=NULL;CHECK(!pthread_mutex_unlock(&read_barrier.mutex));
        if(mode==2||mode==3)CHECK(!map_stack(h,5,alias,source,page));
        else{CHECK(!map_stack(h,4,alias,heap+unit+(mode==1?page:0),page));CHECK(!map_stack(h,5,alias,heap+unit+(mode==1?page:0),page));}
        CHECK(!resize_heap(h,unit,NULL));CHECK(!resize_heap(h,2*unit,NULL));
        const char*cases[]={"heap buffer","heap output","alias buffer","alias output","pread error"};
        printf("PASS: blocked ReadFile %s lease survives close and releases before mapping reuse\n",cases[mode]);
    }
    uint64_t handle;CHECK(!open_file(&handle,"rom:/data/Name.bin",1));char bytes[6]={0};size_t got=123;
    char*trusted=malloc(6);size_t*trusted_count=malloc(sizeof(*trusted_count));CHECK(trusted&&trusted_count);
    CHECK(!read_file(trusted_count,handle,0,trusted,6)&&*trusted_count==6&&!memcmp(trusted,"abcdef",6));free(trusted_count);free(trusted);
    CHECK(!resize_heap(h,unit,NULL));
    CHECK(read_file(&got,handle,0,(void*)(heap+unit),6)==0x2a2u&&got==123);
    CHECK(read_file((size_t*)(heap+unit),handle,0,bytes,6)==0x2a2u&&!memcmp(bytes,"\0\0\0\0\0\0",6));
    CHECK(read_file(&got,handle,0,(void*)(heap+unit-3),6)==0x2a2u&&got==123);
    CHECK(read_file((size_t*)(heap+unit-4),handle,0,bytes,6)==0x2a2u&&!memcmp(bytes,"\0\0\0\0\0\0",6));
    CHECK(read_file(&got,handle,0,(void*)(alias+unit-3),6)==0x2a2u&&got==123);
    query_fn query=(query_fn)need("_ZN2nn2fs22QueryMountRomCacheSizeEPm");
    mount_fn mount=(mount_fn)need("_ZN2nn2fs8MountRomEPKcPvm");uint64_t unchanged=123;
    uint64_t readonly_handle;CHECK(!open_file(&readonly_handle,readonly,1));close_file(readonly_handle);
    CHECK(!mount(readonly+64,NULL,0));CHECK(query((size_t*)readonly)==0x2a2u);
    CHECK(query((size_t*)(heap+unit))==0x2a2u);
    CHECK(open_file((uint64_t*)(heap+unit),"rom:/data/Name.bin",1)==0x2a2u);
    CHECK(file_size((int64_t*)(heap+unit),handle)==0x2a2u);
    CHECK(open_file(&unchanged,(const char*)(heap+unit),1)==0x2a2u&&unchanged==123);
    CHECK(mount((const char*)(heap+unit),NULL,0)==0x2a2u);
    const char name[]="rom:/data/Name.bin";char*edge=(char*)(heap+unit-sizeof(name));memcpy(edge,name,sizeof(name));
    uint64_t edge_handle;CHECK(!open_file(&edge_handle,edge,1));close_file(edge_handle);
    memcpy((void*)(heap+unit-2),"ro",2);CHECK(open_file(&unchanged,(char*)(heap+unit-2),1)==0x2a2u&&unchanged==123);
    CHECK(!map_stack(h,4,alias,heap,page));CHECK(read_file(&got,handle,0,(void*)heap,6)==0x2a2u&&got==123);
    CHECK(open_file(&unchanged,(const char*)heap,1)==0x2a2u&&unchanged==123);
    CHECK(query((size_t*)heap)==0x2a2u&&file_size((int64_t*)heap,handle)==0x2a2u);
    CHECK(!map_stack(h,5,alias,heap,page));CHECK(!resize_heap(h,2*unit,NULL));
    size_t*out=(size_t*)(heap+unit+page);*out=123;
    CHECK(read_file(out,0,0,(void*)(heap+unit),6)==0x2a2u&&*out==123);
    struct rlimit descriptors;CHECK(!getrlimit(RLIMIT_NOFILE,&descriptors));struct rlimit exhausted=descriptors;exhausted.rlim_cur=0;
    CHECK(!setrlimit(RLIMIT_NOFILE,&exhausted));uint32_t duplicate_failure=read_file(out,handle,0,(void*)(heap+unit),6);
    CHECK(!setrlimit(RLIMIT_NOFILE,&descriptors));CHECK(duplicate_failure==0x2a2u&&*out==123);
    CHECK(!resize_heap(h,unit,NULL));close_file(handle);CHECK(!dm_guest_detach());CHECK(!dm_host_destroy(h));CHECK(!munmap(readonly,page));
    puts("PASS: blocked ReadFile pins heap/alias buffer and output, rejects map/unmap/shrink, releases after success/error/close, denies inaccessible ranges without mutation");
}
int main(int argc,char**argv){
    open_file=(open_fn)need("_ZN2nn2fs8OpenFileEPNS0_10FileHandleEPKci");
    read_file=(read_fn)need("_ZN2nn2fs8ReadFileEPmNS0_10FileHandleElPvm");
    file_size=(size_fn)need("_ZN2nn2fs11GetFileSizeEPlNS0_10FileHandleE");close_file=(close_fn)need("_ZN2nn2fs9CloseFileENS0_10FileHandleE");
    size_t native_cache=0;CHECK(!dm_romfs_query_cache_size(&native_cache)&&native_cache==4096);
    dm_host*host=dm_host_create(4u*1024u*1024u);CHECK(host);dm_gate_table gates={0};
    if(argc==4&&!strcmp(argv[1],"--inspect")){
        CHECK(!dm_host_mount_romfs(host,argv[2]));CHECK(!dm_guest_attach(host,&gates));uint64_t handle=0;
        CHECK(!open_file(&handle,argv[3],1));int64_t size;CHECK(!file_size(&size,handle));size_t got=0;char bytes[512];
        CHECK(!read_file(&got,handle,0,bytes,sizeof(bytes)));printf("RomFS SDK import read: file_size=%lld prefix_bytes=%zu\n",(long long)size,got);
        close_file(handle);CHECK(!dm_guest_detach());CHECK(!dm_host_destroy(host));return 0;
    }
    CHECK(argc==1);const char*tmp=getenv("TMPDIR");CHECK(tmp);char root[4096],path[4096],link[4096];
    CHECK(snprintf(root,sizeof(root),"%s/dm-romfs.XXXXXX",tmp)<(int)sizeof(root));CHECK(mkdtemp(root));
    CHECK(snprintf(path,sizeof(path),"%s/data",root)<(int)sizeof(path));CHECK(!mkdir(path,0700));
    CHECK(snprintf(path,sizeof(path),"%s/data/Name.bin",root)<(int)sizeof(path));write_fixture(path,"abcdef");
    CHECK(snprintf(link,sizeof(link),"%s/data/link",root)<(int)sizeof(link));CHECK(!symlink("Name.bin",link));
    CHECK(!dm_host_mount_romfs(host,root));CHECK(dm_host_mount_romfs(host,root)==EALREADY);
    CHECK(!dm_guest_attach(host,&gates));CHECK(dm_host_mount_romfs(host,root)==EBUSY);
    size_t cache=0;CHECK(!((uint32_t(*)(size_t*))need("_ZN2nn2fs22QueryMountRomCacheSizeEPm"))(&cache)&&cache==4096);
    CHECK(!((uint32_t(*)(const char*,void*,size_t))need("_ZN2nn2fs8MountRomEPKcPvm"))("rom",NULL,0));
    uint64_t handle=0;CHECK(!open_file(&handle,"rom:/data/Name.bin",1));int64_t size=0;CHECK(!file_size(&size,handle)&&size==6);
    char bytes[16]={0};size_t got=0;CHECK(!read_file(&got,handle,2,bytes,10)&&got==4&&!memcmp(bytes,"cdef",4));
    got=123;CHECK(read_file(&got,handle,-1,bytes,2)&&got==123);CHECK(read_file(&got,handle,0,NULL,2));
    CHECK(!read_file(&got,handle,0,NULL,0)&&got==0);CHECK(!read_file(&got,handle,6,bytes,10)&&got==0);
    const char*bad[]={"rom:/data/name.bin","rom:/data/link","rom:/data/../data/Name.bin","rom:/data/./Name.bin","rom:/data","/etc/passwd","save:/data/Name.bin"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++){uint64_t unchanged=123;CHECK(open_file(&unchanged,bad[i],1)&&unchanged==123);}
    uint64_t unchanged=123;CHECK(open_file(&unchanged,"rom:/data/Name.bin",2)&&unchanged==123);
    pthread_t threads[4];worker w={host,&gates,handle};for(unsigned i=0;i<4;i++)CHECK(!pthread_create(&threads[i],NULL,read_worker,&w));
    for(unsigned i=0;i<4;i++)CHECK(!pthread_join(threads[i],NULL));
    uint64_t old=handle;close_file(handle);CHECK(!open_file(&handle,"rom:/data/Name.bin",1)&&handle!=old);
    CHECK(read_file(&got,old,0,bytes,2));close_file(old);CHECK(!read_file(&got,handle,0,bytes,2));close_file(handle);
    uint64_t many[128];for(unsigned i=0;i<128;i++)CHECK(!open_file(&many[i],"rom:/data/Name.bin",1));
    unchanged=123;CHECK(open_file(&unchanged,"rom:/data/Name.bin",1)&&unchanged==123);for(unsigned i=0;i<128;i++)close_file(many[i]);
    CHECK(!dm_guest_detach());CHECK(!dm_host_destroy(host));memory_leases(root);CHECK(!unlink(link));CHECK(!unlink(path));
    CHECK(snprintf(path,sizeof(path),"%s/data",root)<(int)sizeof(path));CHECK(!rmdir(path));CHECK(!rmdir(root));
    puts("PASS: nn::fs readonly mounts/imports, exact case/no traversal/no symlinks, pread/EOF, stale/full handles and 4000 concurrent reads");
    return 0;
}
