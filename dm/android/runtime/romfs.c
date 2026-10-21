// nn::fs 只读导入与代际句柄；依据 romfs.h / POSIX openat、pread，不复用 Mac 专用的 APFS 存档事务。
// Read-only nn::fs imports and generation handles; based on romfs.h / POSIX openat/pread, without Mac-specific APFS save transactions.
#include "romfs.h"
#include "internal.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define MAX_FILES 128u
#define FS_FAIL 0x2a2u
#define FS_MISSING 0x202u
struct dm_romfs{int root;struct{int fd;uint32_t generation;}files[MAX_FILES];};
void dm_host_romfs_close(dm_host*host){
    struct dm_romfs*fs=host->romfs;if(!fs)return;
    for(unsigned i=0;i<MAX_FILES;i++)if(fs->files[i].fd>=0)close(fs->files[i].fd);
    close(fs->root);free(fs);host->romfs=NULL;
}
int dm_host_mount_romfs(dm_host*host,const char*root){
    if(!host||!root)return EINVAL;size_t length=strnlen(root,4096);if(!length||length>=4096)return EINVAL;
    char normalized[4096];memcpy(normalized,root,length+1);while(length>1&&normalized[length-1]=='/')normalized[--length]=0;
    int fd=open(normalized,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return errno;
    struct dm_romfs*fs=calloc(1,sizeof(*fs));if(!fs){close(fd);return ENOMEM;}fs->root=fd;
    for(unsigned i=0;i<MAX_FILES;i++){fs->files[i].fd=-1;fs->files[i].generation=1;}
    pthread_mutex_lock(&host->mutex);int rc=host->attached?EBUSY:host->romfs?EALREADY:0;
    if(!rc)host->romfs=fs;pthread_mutex_unlock(&host->mutex);if(rc){close(fd);free(fs);}return rc;
}
static int exact_name(int directory,const char*name){
    int fd=openat(directory,".",O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(fd<0)return 0;
    DIR*d=fdopendir(fd);if(!d){close(fd);return 0;}int found=0;struct dirent*entry;
    while((entry=readdir(d)))if(!strcmp(entry->d_name,name)){found=1;break;}
    closedir(d);if(!found)errno=ENOENT;return found;
}
static int open_path(dm_host*h,const char*path){
    char copy[4096];size_t length=0;
    for(;length<sizeof(copy);length++){
        dm_memory_pin pin={0};uintptr_t address=(uintptr_t)path;
        if(length>UINTPTR_MAX-address||dm_host_memory_read_pin_locked(h,&pin,address+length,1)){errno=EINVAL;return -1;}
        copy[length]=path[length];dm_host_memory_unpin_locked(h,&pin);if(!copy[length])break;
    }
    if(length==sizeof(copy)||strncmp(copy,"rom:/",5)){errno=EINVAL;return -1;}
    char*state=NULL,*part=strtok_r(copy+5,"/",&state);struct dm_romfs*fs=h->romfs;
    if(!part){errno=ENOENT;return -1;}int directory=openat(fs->root,".",O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(directory<0)return -1;
    for(;;){
        char*next=strtok_r(NULL,"/",&state);int fd=-1;
        if(!strcmp(part,".")||!strcmp(part,"..")||strchr(part,':')){errno=EINVAL;goto fail;}
        if(!exact_name(directory,part))goto fail;
        fd=openat(directory,part,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|(next?O_DIRECTORY:0));
        if(fd<0)goto fail;close(directory);directory=fd;
        if(!next){struct stat st;if(fstat(fd,&st)||!S_ISREG(st.st_mode)){errno=ENOENT;goto fail;}return fd;}
        part=next;continue;
fail: {int error=errno;close(directory);errno=error;return -1;}
    }
}
static dm_host*current(void){dm_guest_context*c=dm_guest_current();return c?c->host:NULL;}
static uint32_t result_errno(void){return errno==ENOENT||errno==ENOTDIR?FS_MISSING:FS_FAIL;}
static int file_index(struct dm_romfs*fs,uint64_t handle){
    unsigned slot=(uint32_t)handle;if(!fs||!slot||slot>MAX_FILES)return -1;slot--;
    return fs->files[slot].fd>=0&&fs->files[slot].generation==(uint32_t)(handle>>32)?(int)slot:-1;
}
uint32_t dm_romfs_query_cache_size(size_t*out){
    if(!out)return FS_FAIL;dm_host*h=current();if(!h){*out=0x1000;return 0;}
    dm_memory_pin pin={0};pthread_mutex_lock(&h->mutex);uint32_t result=FS_FAIL;
    if(!dm_host_memory_pin_locked(h,&pin,(uintptr_t)out,sizeof(*out))){*out=0x1000;result=0;}
    dm_host_memory_unpin_locked(h,&pin);pthread_mutex_unlock(&h->mutex);return result;
}
static uint32_t mount_rom(const char*name,void*cache,size_t size){
    (void)cache;(void)size;dm_host*h=current();if(!h||!name)return FS_FAIL;
    dm_memory_pin pin={0};pthread_mutex_lock(&h->mutex);uint32_t result=FS_FAIL;
    if(!dm_host_memory_read_pin_locked(h,&pin,(uintptr_t)name,4)&&!strcmp(name,"rom"))result=h->romfs?0:FS_FAIL;
    dm_host_memory_unpin_locked(h,&pin);pthread_mutex_unlock(&h->mutex);return result;
}
static uint32_t open_file(uint64_t*out,const char*path,int mode){
    dm_host*h=current();if(!h||!out||!path||mode!=1)return FS_FAIL;
    dm_memory_pin pin={0};pthread_mutex_lock(&h->mutex);uint32_t result=FS_FAIL;struct dm_romfs*fs=h->romfs;
    if(!fs||dm_host_memory_pin_locked(h,&pin,(uintptr_t)out,sizeof(*out)))goto done;
    unsigned slot=0;while(slot<MAX_FILES&&(fs->files[slot].fd>=0||!fs->files[slot].generation))slot++;if(slot==MAX_FILES)goto done;
    int fd=open_path(h,path);if(fd<0){result=result_errno();goto done;}
    fs->files[slot].fd=fd;*out=((uint64_t)fs->files[slot].generation<<32)|(slot+1);result=0;
done:dm_host_memory_unpin_locked(h,&pin);pthread_mutex_unlock(&h->mutex);return result;
}
static uint32_t read_file(size_t*out,uint64_t handle,int64_t offset,void*buffer,size_t size){
    dm_host*h=current();if(!h||!out||(size&&!buffer)||size>UINTPTR_MAX-(uintptr_t)buffer||offset<0||size>(uint64_t)INT64_MAX-(uint64_t)offset)return FS_FAIL;
    dm_memory_pin output_pin={0},buffer_pin={0};uint32_t result=FS_FAIL;
    pthread_mutex_lock(&h->mutex);
    if(dm_host_memory_pin_locked(h,&output_pin,(uintptr_t)out,sizeof(*out))||
       (size&&dm_host_memory_pin_locked(h,&buffer_pin,(uintptr_t)buffer,size)))goto release;
    int i=file_index(h->romfs,handle),fd=i<0?-1:fcntl(h->romfs->files[i].fd,F_DUPFD_CLOEXEC,0);
    if(fd<0)goto release;pthread_mutex_unlock(&h->mutex);
    // fd 引用保护并发关闭；内存租约保护读取缓冲区及输出，阻塞读取不占共享锁。
    // The fd reference survives close; memory leases protect the buffer and output while blocking reads release the shared lock.
    size_t got=0;while(got<size){size_t remaining=size-got;if(remaining>SSIZE_MAX)remaining=SSIZE_MAX;
        ssize_t n=pread(fd,(uint8_t*)buffer+got,remaining,offset+(int64_t)got);
        if(n<0){if(errno==EINTR)continue;goto done;}if(!n)break;got+=(size_t)n;}
    *out=got;result=0;
done:close(fd);pthread_mutex_lock(&h->mutex);
release:dm_host_memory_unpin_locked(h,&buffer_pin);dm_host_memory_unpin_locked(h,&output_pin);
    pthread_mutex_unlock(&h->mutex);return result;
}
static uint32_t file_size(int64_t*out,uint64_t handle){
    dm_host*h=current();if(!h||!out)return FS_FAIL;pthread_mutex_lock(&h->mutex);uint32_t result=FS_FAIL;
    dm_memory_pin pin={0};int i=file_index(h->romfs,handle);struct stat st;
    if(i>=0&&!dm_host_memory_pin_locked(h,&pin,(uintptr_t)out,sizeof(*out))&&!fstat(h->romfs->files[i].fd,&st)){*out=st.st_size;result=0;}
    dm_host_memory_unpin_locked(h,&pin);
    pthread_mutex_unlock(&h->mutex);return result;
}
static void close_file(uint64_t handle){
    dm_host*h=current();if(!h)return;pthread_mutex_lock(&h->mutex);int i=file_index(h->romfs,handle);
    if(i>=0){close(h->romfs->files[i].fd);h->romfs->files[i].fd=-1;
        h->romfs->files[i].generation++;}
    pthread_mutex_unlock(&h->mutex);
}
const dm_symbol_override*dm_romfs_imports(size_t*count){
#if defined(__aarch64__)
    extern void dm_bridge_romfs_query(void),dm_bridge_romfs_mount(void),dm_bridge_romfs_open(void);
    extern void dm_bridge_romfs_read(void),dm_bridge_romfs_size(void),dm_bridge_romfs_close(void);
#define IMPORT(name,native) ((uintptr_t)dm_bridge_romfs_##name)
#else
#define IMPORT(name,native) ((uintptr_t)(native))
#endif
    static const dm_symbol_override imports[]={
        {"_ZN2nn2fs22QueryMountRomCacheSizeEPm",IMPORT(query,dm_romfs_query_cache_size)},
        {"_ZN2nn2fs8MountRomEPKcPvm",IMPORT(mount,mount_rom)},
        {"_ZN2nn2fs8OpenFileEPNS0_10FileHandleEPKci",IMPORT(open,open_file)},
        {"_ZN2nn2fs8ReadFileEPmNS0_10FileHandleElPvm",IMPORT(read,read_file)},
        {"_ZN2nn2fs11GetFileSizeEPlNS0_10FileHandleE",IMPORT(size,file_size)},
        {"_ZN2nn2fs9CloseFileENS0_10FileHandleE",IMPORT(close,close_file)}};
#undef IMPORT
    if(count)*count=sizeof(imports)/sizeof(*imports);return imports;
}
uint64_t dm_bridge_import_dispatch(unsigned id,const uint64_t args[9]){
    switch(id){
    case 1:return dm_romfs_query_cache_size((size_t*)(uintptr_t)args[0]);
    case 2:return mount_rom((const char*)(uintptr_t)args[0],(void*)(uintptr_t)args[1],(size_t)args[2]);
    case 3:return open_file((uint64_t*)(uintptr_t)args[0],(const char*)(uintptr_t)args[1],(int)(uint32_t)args[2]);
    case 4:return read_file((size_t*)(uintptr_t)args[0],args[1],(int64_t)args[2],(void*)(uintptr_t)args[3],(size_t)args[4]);
    case 5:return file_size((int64_t*)(uintptr_t)args[0],args[1]);
    case 6:close_file(args[0]);return 0;
    default:abort();
    }
}
