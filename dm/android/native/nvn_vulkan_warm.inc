// PSO 预热：记录本机真实创建的图形管线描述，下次启动在后台重建，把驱动编译从游戏内首次遇到挪到启动/读取画面。
// PSO warm-up: record genuinely created graphics pipeline descriptions, rebuild them in the background on the next
// launch, moving driver compilation away from first in-game encounters to start-up / the loading screen.
// 与 Metal 后端同一套思路（nvn_mtl.m warm_worker）：用户清单在本机积累，种子清单随构建分发；清单只描述管线状态。
// Same design as the Metal backend (nvn_mtl.m warm_worker): a user manifest accumulates locally, a seed manifest
// ships with the build; manifests describe pipeline state only.
// 用户清单 $DM_VK_PIPELINE_CACHE_DIR/pso_vk_user.tsv（DM_PSO_USER=off 关闭记录）；种子清单 $DM_VK_SHADERS/pso_vk_manifest.tsv；
// DM_PSO_MANIFEST=off 关闭预热，DM_PSO_MANIFEST=<path> 只加载该清单。
// 行格式：'R\t<vs16hex>\t<fs16hex|->\t<pass_hex>\t<key_hex>'，头行同时校验版本与两个结构尺寸。
// Line format: 'R\t<vs16hex>\t<fs16hex|->\t<pass_hex>\t<key_hex>'; the header verifies the version and both struct sizes.
#define PSO_MANIFEST_LIMIT (8u<<20)
#define PSO_RECORD_MAX 4096
#define PSO_USER_NAME "pso_vk_user.tsv"
#define PSO_SEED_NAME "pso_vk_manifest.tsv"
typedef struct { char kind;char vs[17],fs[17];graphics_pass_key pass;pipeline_key key; } pso_record;
static pthread_mutex_t pso_manifest_mutex=PTHREAD_MUTEX_INITIALIZER;
static _Atomic int pso_warm_running;
static pso_record *pso_records;
static size_t pso_record_count;
static uint64_t *pso_seen;
static size_t pso_seen_capacity,pso_seen_count;
static uint64_t pso_user_bytes;
static int pso_user_fd=-1,pso_user_checked;
static int pso_hex_digit(char c) {return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;}
static void pso_hex_encode(const void *data,size_t size,char *text) {
    static const char digits[]="0123456789abcdef";const uint8_t *bytes=data;
    for(size_t i=0;i<size;i++){text[2*i]=digits[bytes[i]>>4];text[2*i+1]=digits[bytes[i]&15];}
}
static int pso_hex_decode(const char *text,size_t length,void *data,size_t size) {
    if(length!=2*size)return 0;uint8_t *bytes=data;
    for(size_t i=0;i<size;i++){int high=pso_hex_digit(text[2*i]),low=pso_hex_digit(text[2*i+1]);if(high<0||low<0)return 0;bytes[i]=(uint8_t)((high<<4)|low);}
    return 1;
}
static uint64_t pso_line_hash(const char *line,size_t size) {uint64_t hash=graphics_hash(line,size);return hash?hash:1;}
static void pso_seen_reserve(size_t capacity) {
    if(capacity<=pso_seen_capacity)return;
    uint64_t *table=calloc(capacity,sizeof *table);if(!table)die("Vulkan PSO dedup allocation");
    size_t previous=pso_seen_capacity;uint64_t *old=pso_seen;
    pso_seen=table;pso_seen_capacity=capacity;
    for(size_t i=0;i<previous;i++){uint64_t value=old[i];if(!value)continue;size_t mask=capacity-1,at=(size_t)(value^(value>>32))&mask;while(table[at])at=(at+1)&mask;table[at]=value;}
    free(old);
}
static int pso_seen_add(uint64_t hash) {
    if((pso_seen_count+1)*2>pso_seen_capacity)pso_seen_reserve(pso_seen_capacity?pso_seen_capacity*2:4096);
    size_t mask=pso_seen_capacity-1,at=(size_t)(hash^(hash>>32))&mask;
    while(pso_seen[at]){if(pso_seen[at]==hash)return 0;at=(at+1)&mask;}
    pso_seen[at]=hash;pso_seen_count++;return 1;
}
// 记录时归零结构体填充字节；清单行只由字段值决定，跨会话去重才稳定。
// Zero struct padding when recording so manifest lines depend on field values only and dedup stays stable across sessions.
static void pso_key_normalize(pipeline_key *out,const pipeline_key *in) {
    memset(out,0,sizeof *out);
    memcpy(out->formats,in->formats,sizeof in->formats);
    out->colors=in->colors;out->primitive=in->primitive;out->attachments=in->attachments;out->depth_attachment=in->depth_attachment;
    memcpy(out->color_indices,in->color_indices,sizeof in->color_indices);
    out->required_subgroup_size=in->required_subgroup_size;
    memcpy(out->attributes,in->attributes,sizeof in->attributes);
    memcpy(out->streams,in->streams,sizeof in->streams);
    out->color=in->color;out->mask=in->mask;
    memcpy(out->blend,in->blend,sizeof in->blend);
    out->polygon=in->polygon;out->depth=in->depth;out->multisample=in->multisample;
}
static int pso_user_file(void) {
    if(pso_user_checked)return pso_user_fd;
    pso_user_checked=1;
    const char *disable=getenv("DM_PSO_USER");if(disable&&!strcmp(disable,"off"))return -1;
    if(pipeline_disk_dir<0)return -1;
    int fd=openat(pipeline_disk_dir,PSO_USER_NAME,O_RDWR|O_APPEND|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);
    if(fd<0){LOG("[vulkan-pso] user_manifest=disabled errno=%d",errno);return -1;}
    struct stat st;
    if(fstat(fd,&st)){close(fd);return -1;}
    pso_user_bytes=(uint64_t)st.st_size;
    if(!st.st_size){
        char header[80];int n=snprintf(header,sizeof header,"# DMVKRPSO1 pass=%zu key=%zu\n",sizeof(graphics_pass_key),sizeof(pipeline_key));
        if(n<=0||(size_t)n>=sizeof header||!pipeline_disk_transfer(fd,header,(size_t)n,1)){close(fd);return -1;}
        pso_user_bytes=(uint64_t)n;
    }
    pso_user_fd=fd;return fd;
}
static void pso_record_graphics(const shader_entry *vs,const shader_entry *fs,const graphics_pass_key *pass,const pipeline_key *key) {
    int fd=pso_user_file();if(fd<0||pso_user_bytes>=PSO_MANIFEST_LIMIT)return;
    pipeline_key normalized;pso_key_normalize(&normalized,key);
    const char *fragment=fs->module?fs->key:"-";
    char line[2048];
    int n=snprintf(line,sizeof line,"R\t%s\t%s\t",vs->key,fragment);
    if(n<=0||(size_t)n+2*sizeof *pass+1+2*sizeof normalized+1>=sizeof line)return;
    pso_hex_encode(pass,sizeof *pass,line+n);n+=2*(int)sizeof *pass;line[n++]='\t';
    pso_hex_encode(&normalized,sizeof normalized,line+n);n+=2*(int)sizeof normalized;line[n++]='\n';
    uint64_t hash=pso_line_hash(line,(size_t)n-1);
    pthread_mutex_lock(&pso_manifest_mutex);
    if(pso_seen_add(hash)&&pso_user_bytes+(uint64_t)n<=PSO_MANIFEST_LIMIT){
        if(pipeline_disk_transfer(fd,line,(size_t)n,1)){pso_user_bytes+=(uint64_t)n;LOG("[vulkan-pso] record=R vs=%s fs=%s records=%zu",vs->key,fragment,pso_seen_count);}
        else LOG("[vulkan-pso] record_write_failed errno=%d",errno);
    }
    pthread_mutex_unlock(&pso_manifest_mutex);
}
static void pso_decode_r(char *line,size_t length) {
    if(pso_record_count>=PSO_RECORD_MAX)return;
    line[length]=0;
    char *vs=line+2;char *t1=strchr(vs,'\t');if(!t1)return;*t1=0;
    char *fs=t1+1;char *t2=strchr(fs,'\t');if(!t2)return;*t2=0;
    char *pass_hex=t2+1;char *t3=strchr(pass_hex,'\t');if(!t3)return;*t3=0;
    char *key_hex=t3+1;
    size_t vs_length=strlen(vs),fs_length=strlen(fs),pass_length=strlen(pass_hex),key_length=strlen(key_hex);
    if(vs_length!=16||!(fs_length==16||(fs_length==1&&fs[0]=='-')))return;
    if(pass_length!=2*sizeof(graphics_pass_key)||key_length!=2*sizeof(pipeline_key))return;
    pso_record *record=&pso_records[pso_record_count];
    memset(record,0,sizeof *record);record->kind='R';
    memcpy(record->vs,vs,16);memcpy(record->fs,fs,fs_length);
    if(!pso_hex_decode(pass_hex,pass_length,&record->pass,sizeof record->pass))return;
    if(!pso_hex_decode(key_hex,key_length,&record->key,sizeof record->key))return;
    record->key.vs=NULL;record->key.fs=NULL;
    pso_record_count++;
}
static void pso_manifest_parse_fd(int fd,const char *name,int collect) {
    struct stat st;
    if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<=0)return;
    if(st.st_size>(off_t)PSO_MANIFEST_LIMIT){LOG("[vulkan-pso] manifest_oversize name=%s bytes=%lld",name,(long long)st.st_size);return;}
    size_t size=(size_t)st.st_size;char *data=malloc(size+1);
    if(!data)return;
    if(!pipeline_disk_transfer(fd,data,size,0)){free(data);return;}
    data[size]=0;
    char *cursor=data,*end=data+size;size_t before=pso_record_count;
    if(cursor<end&&*cursor=='#'){
        char *newline=memchr(cursor,'\n',(size_t)(end-cursor));
        size_t pass_size=0,key_size=0;
        if(sscanf(cursor,"# DMVKRPSO1 pass=%zu key=%zu",&pass_size,&key_size)!=2||pass_size!=sizeof(graphics_pass_key)||key_size!=sizeof(pipeline_key)){
            LOG("[vulkan-pso] manifest_skipped name=%s reason=header",name);free(data);return;}
        cursor=newline?newline+1:end;
    }
    while(cursor<end){
        char *newline=memchr(cursor,'\n',(size_t)(end-cursor));
        char *line=cursor;size_t length=newline?(size_t)(newline-cursor):(size_t)(end-cursor);
        cursor=newline?newline+1:end;
        while(length&&line[length-1]=='\r')length--;
        if(!length||line[0]=='#')continue;
        uint64_t hash=pso_line_hash(line,length);
        if(!pso_seen_add(hash))continue;
        if(collect&&length>2&&line[0]=='R'&&line[1]=='\t')pso_decode_r(line,length);
    }
    LOG("[vulkan-pso] manifest name=%s records=%zu total=%zu",name,pso_record_count-before,pso_record_count);
    free(data);
}
static void pso_manifest_parse_path(const char *path,int collect) {
    int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
    if(fd<0)return;
    pso_manifest_parse_fd(fd,path,collect);
    close(fd);
}
static int pso_warm_one(const pso_record *record) {
    // 清单来自本机或无信任边界之外（种子），预热路径必须无副作用地跳过一切可疑记录。
    // Manifests may come from this device or from a shipped seed; the warm path must skip anything suspicious without side effects.
    const graphics_pass_key *pass_key=&record->pass;
    if(pass_key->attachments<1||pass_key->attachments>9||pass_key->colors>8)return 0;
    if(pass_key->depth!=VK_ATTACHMENT_UNUSED&&pass_key->depth>=pass_key->attachments)return 0;
    for(unsigned i=0;i<pass_key->attachments;i++)if(pass_key->load[i]>VK_ATTACHMENT_LOAD_OP_DONT_CARE||pass_key->stencil_load[i]>VK_ATTACHMENT_LOAD_OP_DONT_CARE)return 0;
    for(unsigned i=0;i<pass_key->colors;i++)if(pass_key->color_indices[i]!=VK_ATTACHMENT_UNUSED&&pass_key->color_indices[i]>=pass_key->attachments)return 0;
    shader_entry *vs=shader_load_by_key(record->vs,0,0);if(!vs)return 0;
    shader_entry *fs;
    if(record->fs[0]=='-'&&!record->fs[1])fs=graphics_empty_shader();
    else{fs=shader_load_by_key(record->fs,0,0);if(!fs)return 0;}
    // 与运行期同一套 pass/layout 缓存：预热对象直接成为游戏线程首次命中的对象。
    // The same pass/layout caches as the runtime: warmed objects are exactly what the game thread hits first.
    VkAttachmentDescription attachments[9]={0};VkAttachmentReference colors[8]={0},depth={VK_ATTACHMENT_UNUSED,VK_IMAGE_LAYOUT_GENERAL};
    for(unsigned i=0;i<pass_key->attachments;i++)attachments[i]=(VkAttachmentDescription){.format=pass_key->formats[i],.samples=VK_SAMPLE_COUNT_1_BIT,.loadOp=pass_key->load[i],.storeOp=VK_ATTACHMENT_STORE_OP_STORE,.stencilLoadOp=pass_key->stencil_load[i],.stencilStoreOp=i==pass_key->depth?VK_ATTACHMENT_STORE_OP_STORE:VK_ATTACHMENT_STORE_OP_DONT_CARE,.initialLayout=VK_IMAGE_LAYOUT_GENERAL,.finalLayout=VK_IMAGE_LAYOUT_GENERAL};
    for(unsigned i=0;i<pass_key->colors;i++)colors[i]=(VkAttachmentReference){pass_key->color_indices[i],VK_IMAGE_LAYOUT_GENERAL};
    if(pass_key->depth!=VK_ATTACHMENT_UNUSED)depth.attachment=pass_key->depth;
    graphics_pass_entry *pass=graphics_pass_get(*pass_key,attachments,colors,depth,0);
    // 驱动拒绝这个通道（多半是记录里的附件格式在这个设备/驱动上不合法）：跳过这条记录，不打断整个预热。
    // The driver rejected this pass (usually an attachment format in the record that is illegal on this device/driver):
    // skip that record without interrupting the whole warm-up.
    if(!pass)return 0;
    VkPipelineLayout layout=graphics_layout_get(vs,fs);
    VkPipeline pipeline=VK_NULL_HANDLE;
    uint64_t begin=dm_android_clock_ns(CLOCK_MONOTONIC);
    VkResult result=graphics_pipeline_create(&record->key,vs,fs,pass,layout,&pipeline,0);
    uint64_t wall=dm_android_clock_ns(CLOCK_MONOTONIC)-begin;
    if(result!=VK_SUCCESS)return 0;
    if(wall>50000000)LOG("[vulkan-pso-warm] slow vs=%s fs=%s wall_ms=%.1f",record->vs,record->fs,wall/1e6);
    // 建后即毁：编译结果留在驱动管线缓存里，游戏线程首次用到时是缓存命中而不是现编。
    // Create then destroy: the compilation stays in the driver pipeline cache, so the game's first use is a cache hit, not a compile.
    vkDestroyPipeline(device,pipeline,NULL);
    return 1;
}
static void *pso_warm_main(void *unused) {
    (void)unused;pthread_setname_np(pthread_self(),"D2R PSO warm");
    resource_create_quiet=1;   // 预热创建全部不记账（长帧账目与慢创建明细只管游戏线程）| warm creations never account (long-frame totals and slow-create details cover the game thread only)
    uint64_t begin=dm_android_clock_ns(CLOCK_MONOTONIC),built=0,skipped=0;
    for(size_t i=0;i<pso_record_count;i++){
        if(pso_warm_one(&pso_records[i]))built++;else skipped++;
        if((i&15)==15)LOG("[vulkan-pso-warm] progress=%zu/%zu built=%llu skipped=%llu",i+1,pso_record_count,(unsigned long long)built,(unsigned long long)skipped);
    }
    LOG("[vulkan-pso-warm] done records=%zu built=%llu skipped=%llu wall_ms=%.1f",pso_record_count,(unsigned long long)built,(unsigned long long)skipped,(dm_android_clock_ns(CLOCK_MONOTONIC)-begin)/1e6);
    free(pso_records);pso_records=NULL;pso_record_count=0;
    atomic_store(&pso_warm_running,0);
    return NULL;
}
// initialize() 末尾调用：读清单、建去重集合、后台开预热线程；本地缓存目录或显式清单缺失时不启动。
// Called at the end of initialize(): read manifests, build the dedup set, start the background warm thread; idle when the local cache directory and explicit manifest are both absent.
static void pso_warm_start(void) {
    const char *setting=getenv("DM_PSO_MANIFEST");
    int explicit_path=setting&&*setting&&strcmp(setting,"off");
    if(pipeline_disk_dir<0&&!explicit_path)return;
    int collect=!(setting&&!strcmp(setting,"off"));
    if(collect){pso_records=calloc(PSO_RECORD_MAX,sizeof *pso_records);if(!pso_records){LOG("[vulkan-pso-warm] allocation_failed records=%d",PSO_RECORD_MAX);collect=0;}}
    const char *shaders=getenv("DM_VK_SHADERS");
    char seed[1024];seed[0]=0;
    if(shaders&&*shaders)snprintf(seed,sizeof seed,"%s/%s",shaders,PSO_SEED_NAME);
    pthread_mutex_lock(&pso_manifest_mutex);
    if(explicit_path)pso_manifest_parse_path(setting,collect);
    else{
        // 先本机积累（更贴近实际使用顺序），再随包种子；同一行只算一次。
        // Local accumulation first (closer to actual use order), then the shipped seed; a line counts once.
        pso_user_file();
        if(pipeline_disk_dir>=0){int fd=openat(pipeline_disk_dir,PSO_USER_NAME,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd>=0){pso_manifest_parse_fd(fd,PSO_USER_NAME,collect);close(fd);}}
        if(seed[0])pso_manifest_parse_path(seed,collect);
    }
    pthread_mutex_unlock(&pso_manifest_mutex);
    if(!collect||!pso_record_count){free(pso_records);pso_records=NULL;return;}
    LOG("[vulkan-pso-warm] start records=%zu seed=%s user_dir=%s",pso_record_count,seed[0]?seed:"(none)",getenv("DM_VK_PIPELINE_CACHE_DIR")?getenv("DM_VK_PIPELINE_CACHE_DIR"):"(none)");
    atomic_store(&pso_warm_running,1);
    pthread_t thread;
    int error=pthread_create(&thread,NULL,pso_warm_main,NULL);
    if(error){atomic_store(&pso_warm_running,0);free(pso_records);pso_records=NULL;pso_record_count=0;LOG("[vulkan-pso-warm] worker_create_failed=%d",error);return;}
    pthread_detach(thread);
}
