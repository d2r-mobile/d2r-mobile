// 只在显式请求的一帧记录管线、读回小区域；默认关闭，不修改像素颜色。
// Trace pipelines and read back small regions of one explicitly requested frame; disabled by default and never changes colors.
#ifndef DM_ANDROID_VULKAN_PROBE_INCLUDED
#define DM_ANDROID_VULKAN_PROBE_INCLUDED
#include <sys/stat.h>
#include <float.h>
typedef struct probe_image { uint64_t serial;uint32_t mip,layer;unsigned priority,roi_x,roi_y,roi_w,roi_h;struct probe_image *next; } probe_image;
typedef struct probe_read { uint64_t serial;nvn_texdesc texture;uint32_t mip,layer,x,y,w,h,d,bpb,bw,bh;vk_buffer buffer;struct probe_read *next; } probe_read;
typedef struct probe_buffer_read { vk_buffer source,readback;uint64_t offset,size;char name[160];struct probe_buffer_read *next; } probe_buffer_read;
typedef struct probe_dump_seen { uintptr_t address;uint64_t size,hash;unsigned role;char key[17],name[160];struct probe_dump_seen *next; } probe_dump_seen;
typedef struct probe_query { uint64_t ticket,pass,first,last;uint32_t index;int written;struct probe_query *next; } probe_query;
static uint64_t probe_frame,probe_draws;static int probe_checked,probe_done;static char probe_directory[1024];static FILE *probe_trace;static probe_image *probe_images;
static probe_buffer_read *probe_buffers;static probe_dump_seen *probe_dumps;static uint64_t probe_dump_bytes;static unsigned probe_dump_count;
static probe_query *probe_queries;
static struct {int selected;uint64_t serial;unsigned mip,layer,count,id;view_t view;} probe_shadow_cube;
typedef struct probe_cs_buffer {unsigned dispatch,binding,element,writable;VkDescriptorType type;VkDescriptorBufferInfo source;VkFormat format;vk_buffer readback;char phase[5],name[160];struct probe_cs_buffer *next;} probe_cs_buffer;
static probe_cs_buffer *probe_cs_buffers;
static struct {unsigned id;char key[17];probe_cs_buffer *first;} probe_cs_pending;
static unsigned probe_cs_count;static uint64_t probe_cs_bytes;static char probe_cs_key[17];
static const char *graphics_probe_role(unsigned role) {static const char *names[]={"uniform","vertex","ssbo","texel"};return role<4?names[role]:"buffer";}
static void graphics_probe_configure(void) {
    if(probe_checked)return;probe_checked=1;const char *frame=getenv("DM_VK_PROBE_FRAME"),*dir=getenv("DM_VK_PROBE_DIR"),*temp=getenv("DM_TEMP");
    if(frame)probe_frame=strtoull(frame,NULL,10);
    if(dir&&*dir)snprintf(probe_directory,sizeof probe_directory,"%s",dir);else if(temp&&*temp)snprintf(probe_directory,sizeof probe_directory,"%s/gpu-probe",temp);
    if(!frame&&temp&&*temp){char path[1024],line[1024];snprintf(path,sizeof path,"%s/gpu-probe.request",temp);FILE *request=fopen(path,"r");
        if(request){if(fgets(line,sizeof line,request))probe_frame=strtoull(line,NULL,10);if(fgets(line,sizeof line,request)){line[strcspn(line,"\r\n")]=0;if(*line)snprintf(probe_directory,sizeof probe_directory,"%s",line);}if(fgets(line,sizeof line,request)){line[strcspn(line,"\r\n")]=0;if(strlen(line)>16)die("Vulkan probe shader key length");strcpy(probe_cs_key,line);}fclose(request);}}
    const char *key=getenv("DM_VK_PROBE_SHADER");if(key){if(strlen(key)>16)die("Vulkan probe shader key length");strcpy(probe_cs_key,key);}if(*probe_cs_key){if(strlen(probe_cs_key)!=16)die("Vulkan probe shader key length");for(unsigned i=0;i<16;i++)if(!((probe_cs_key[i]>='0'&&probe_cs_key[i]<='9')||(probe_cs_key[i]>='a'&&probe_cs_key[i]<='f')))die("Vulkan probe shader key must be lowercase hex");}
    if(probe_frame&&*probe_directory){if(mkdir(probe_directory,0700)&&errno!=EEXIST)die("Vulkan probe directory %s",probe_directory);char path[1200];snprintf(path,sizeof path,"%s/pipeline.tsv",probe_directory);probe_trace=fopen(path,"w");if(!probe_trace)die("Vulkan probe trace open");LOG("[vulkan-probe] requested frame=%llu directory=%s",(unsigned long long)probe_frame,probe_directory);}
    else probe_frame=0;
}
static int graphics_probe_active(void) {graphics_probe_configure();return probe_frame&&!probe_done&&presents+1==probe_frame;}
static image_entry *graphics_probe_image(uint64_t serial) {for(image_entry *e=image_index[serial&4095];e;e=e->hash_next)if(e->serial==serial)return e;return NULL;}
static void graphics_probe_interest(uint64_t serial,unsigned mip,unsigned layer) {
    image_entry *image=graphics_probe_image(serial);if(!image||!(image->aspect&(VK_IMAGE_ASPECT_COLOR_BIT|VK_IMAGE_ASPECT_DEPTH_BIT)))return;int bw,bh;if(!nvn_fmt_bpb(image->texture.d.format,&bw,&bh)||bw!=1||bh!=1)return;
    for(probe_image *entry=probe_images;entry;entry=entry->next)if(entry->serial==serial&&entry->mip==mip&&entry->layer==layer)return;
    probe_image *entry=calloc(1,sizeof *entry);if(!entry)die("Vulkan probe interest allocation");entry->serial=serial;entry->mip=mip;entry->layer=layer;entry->next=probe_images;probe_images=entry;
}
static void graphics_probe_material(shader_entry *shader,unsigned slot,uint32_t handle) {
    if(slot<10||slot>12)return;unsigned priority=!strcmp(shader->key,"634bc9f2af5d9216")||!strcmp(shader->key,"f1ae4555a6c44f4e")||!strcmp(shader->key,"acd655d88042767c")?2:0;
    if(!priority&&!strcmp(shader->key,"5c1ca4e7c3e4657c")){static uint32_t materials[3];static unsigned count;uint32_t albedo=0;
        for(unsigned i=0;i<shader->count;i++)if(shader->bindings[i].kind==3&&shader_texture_slot(&shader->bindings[i],0)==10){albedo=shader_handle(&shader->bindings[i],shader->stage,0)&0xfffff;break;}
        unsigned i=0;while(i<count&&materials[i]!=albedo)i++;if(i==count){if(count==3)return;materials[count++]=albedo;}priority=1;}
    if(!priority)return;texture_descriptor descriptor=descriptor_texture(handle&0xfffff);if(!descriptor.valid)return;const tex_t *texture=&descriptor.texture;const view_t *view=&descriptor.view;
    if(texture->d.format<0x41||texture->d.format>0x4e||texture->d.target!=NVN_TEXTURE_TARGET_2D)return;
    unsigned mip=view->set_levels?view->base_level:0,layer=view->set_layers?view->min_layer:0;image_entry *image=graphics_probe_image(texture->serial);if(!image||image->aspect!=VK_IMAGE_ASPECT_COLOR_BIT)return;
    probe_image *entry=probe_images;while(entry&&(entry->serial!=texture->serial||entry->mip!=mip||entry->layer!=layer))entry=entry->next;
    if(!entry){entry=calloc(1,sizeof *entry);if(!entry)die("Vulkan probe material metadata");entry->serial=texture->serial;entry->mip=mip;entry->layer=layer;entry->next=probe_images;probe_images=entry;}
    if(priority>entry->priority)entry->priority=priority;
    fprintf(probe_trace,"M\t%llu\tkey=%s\tslot=%u\tid=%u\tserial=%llu\tfmt=%#x\tmip=%u\tlayer=%u\tpriority=%u\n",(unsigned long long)probe_draws,shader->key,slot,handle&0xfffff,(unsigned long long)texture->serial,texture->d.format,mip,layer,priority);
}
// GPU 转发版本保留独立 backing，到本次帧完成后复制读回；不能把未发布客体内存误作实际 GPU 输入。
// Retain forwarded GPU versions for readback after this frame; unpublished guest memory is not the actual GPU input.
static const char *graphics_probe_buffer(unsigned role,const shader_entry *shader,unsigned slot,uintptr_t address,uint64_t size) {
    if(!address||!size||size>(1u<<20)||probe_dump_count>=2048||probe_dump_bytes+size>(16u<<20))return NULL;
    // 大型光照缓冲仅导出第一次实际读取；后续引用明确标为 first_use，不重复扫描整帧。
    // Capture the first real read of large lighting buffers; subsequent references are explicitly first_use, without rescanning the whole frame.
    if(role>=2)for(probe_dump_seen *seen=probe_dumps;seen;seen=seen->next)if(seen->role==role&&seen->address==address&&seen->size==size)return seen->name;
    forward_slice slice;forward_result forwarded=forward_find(address,size,1,forward_scope_marker,&slice);int gpu=forwarded==FORWARD_FULL&&!slice.host_written;
    if(!gpu&&dependencies_unknown(address,size)){fprintf(probe_trace,"F\t%llu\t%s\tstage=%u\tkey=%s\tslot=%u\taddr=%#llx\tsize=%llu\tsource=pending-not-captured\n",(unsigned long long)probe_draws,graphics_probe_role(role),shader->stage,shader->key,slot,(unsigned long long)address,(unsigned long long)size);return NULL;}
    uint8_t *bytes=NULL;uint64_t hash=14695981039346656037ull;
    if(gpu){hash^=(uintptr_t)slice.backing.owner;hash*=1099511628211ull;hash^=slice.offset;}
    else {bytes=malloc(size);if(!bytes||snapshot_read(address,bytes,size)){free(bytes);return NULL;}for(uint64_t i=0;i<size;i++)hash=(hash^bytes[i])*1099511628211ull;}
    for(probe_dump_seen *seen=probe_dumps;seen;seen=seen->next)if(seen->role==role&&seen->address==address&&seen->size==size&&seen->hash==hash&&!strcmp(seen->key,shader->key)){free(bytes);return seen->name;}
    probe_dump_seen *seen=calloc(1,sizeof *seen);if(!seen)die("Vulkan probe buffer metadata");seen->role=role;seen->address=address;seen->size=size;seen->hash=hash;strcpy(seen->key,shader->key);
    snprintf(seen->name,sizeof seen->name,"%s-%u-d%llu-st%u-%s-s%u.bin",graphics_probe_role(role),++probe_dump_count,(unsigned long long)probe_draws,shader->stage,shader->key,slot);seen->next=probe_dumps;probe_dumps=seen;probe_dump_bytes+=size;
    if(gpu){probe_buffer_read *read=calloc(1,sizeof *read);if(!read)die("Vulkan probe forwarded input");read->source=buffer_borrow(slice.backing);read->offset=slice.offset;read->size=size;strcpy(read->name,seen->name);read->next=probe_buffers;probe_buffers=read;}
    else{char path[1200];snprintf(path,sizeof path,"%s/%s",probe_directory,seen->name);FILE *file=fopen(path,"wb");if(!file)die("Vulkan probe input open");fwrite(bytes,1,size,file);fclose(file);free(bytes);}
    fprintf(probe_trace,"F\t%llu\t%s\tstage=%u\tkey=%s\tslot=%u\taddr=%#llx\tsize=%llu\tsource=%s\tfile=%s\n",(unsigned long long)probe_draws,graphics_probe_role(role),shader->stage,shader->key,slot,(unsigned long long)address,(unsigned long long)size,gpu?"gpu-version":"cpu-snapshot",seen->name);return seen->name;
}
// 重用 NVN 计数已经生成的每通道样本结果，不增加 query、绘制或等待。
// Reuse per-pass sample results already generated for NVN counters; no additional queries, draws or waits.
static void graphics_probe_query_batch(uint64_t ticket,const uint64_t *counts,uint32_t count) {
    if(!probe_trace)return;for(probe_query *query=probe_queries;query;query=query->next)if(query->ticket==ticket&&!query->written){if(query->index>=count)die("Vulkan probe query bounds");query->written=1;
        fprintf(probe_trace,"Q\tticket=%llu\tindex=%u\tpass=%llu\tfirst_draw=%llu\tlast_draw=%llu\tsamples=%llu\n",(unsigned long long)ticket,query->index,(unsigned long long)query->pass,(unsigned long long)query->first,(unsigned long long)query->last,(unsigned long long)counts[query->index]);}
}
static void graphics_probe_texture(const char *role,unsigned stage,unsigned slot,uint64_t handle) {
    texture_descriptor descriptor=descriptor_texture(handle&0xfffff);if(!descriptor.valid){fprintf(probe_trace,"T\t%llu\t%s\t%u\t%u\tid=%u\tunbound\n",(unsigned long long)probe_draws,role,stage,slot,(unsigned)(handle&0xfffff));return;}
    const tex_t *texture=&descriptor.texture;const view_t *view=&descriptor.view;unsigned mip=view->set_levels?view->base_level:0,layer=view->set_layers?view->min_layer:0;
    fprintf(probe_trace,"T\t%llu\t%s\t%u\t%u\tid=%u\tserial=%llu\tfmt=%#x\tviewfmt=%#x\tdim=%u,%u,%u\tmip=%u+%d\tlayer=%u+%d\ttarget=%d\n",(unsigned long long)probe_draws,role,stage,slot,(unsigned)(handle&0xfffff),(unsigned long long)texture->serial,texture->d.format,view->set_format?view->format:texture->d.format,texture->d.width,texture->d.height,texture->d.depth,mip,view->set_levels?view->nlevels:0,layer,view->set_layers?view->nlayers:0,texture->d.target);
    if(!strcmp(role,"texture")){unsigned sid=(handle>>20)&0xfff;pthread_mutex_lock(&descriptor_lock);sampler_descriptor sampler=sid<(unsigned)sampler_capacity?sampler_descriptors[sid]:(sampler_descriptor){0};pthread_mutex_unlock(&descriptor_lock);const samp_par *p=&sampler.parameters;
        fprintf(probe_trace,"P\t%llu\tstage=%u\tslot=%u\thandle=%#llx\tsid=%u\tvalid=%d\tfilter=%u,%u\twrap=%u,%u,%u\tcompare=%u,%u\tlod=%g,%g,%g\tborder=%g,%g,%g,%g\n",(unsigned long long)probe_draws,stage,slot,(unsigned long long)handle,sid,sampler.valid,(unsigned)p->magf,(unsigned)p->minf,(unsigned)p->ws,(unsigned)p->wt,(unsigned)p->wr,(unsigned)p->cmp_mode,(unsigned)p->cmp_func,p->lod_min,p->lod_max,p->lod_bias,p->border[0],p->border[1],p->border[2],p->border[3]);}
    if(texture->d.format>=0x32&&texture->d.format<=0x36){uint32_t count=view->set_layers&&view->nlayers>0?(unsigned)view->nlayers:texture->L.layers-layer;if(count>6)count=6;for(unsigned i=0;i<count;i++)graphics_probe_interest(texture->serial,mip,layer+i);}
    else if(!strcmp(role,"image")||texture->d.target==NVN_TEXTURE_TARGET_3D)graphics_probe_interest(texture->serial,mip,layer);
}
// 显式诊断帧只选择首个 R16 颜色 shadow cube 的实际 view 六面；最多192 KiB，不改变采样路径。
// In the requested diagnostic frame, capture up to six actual view faces of the first R16 color shadow cube; at most 192 KiB, without changing sampling.
static void graphics_probe_shadow_cube(const shader_entry *shader,const shader_binding *binding,int slot,uint32_t handle) {
    if(shader->stage!=1||binding->kind!=3||!(binding->type&1024)||(binding->type&255)!=5)return;
    texture_descriptor descriptor=descriptor_texture(handle&0xfffff);if(!descriptor.valid||descriptor.texture.d.format!=NVN_FORMAT_R16)return;
    const tex_t *texture=&descriptor.texture;const view_t *view=&descriptor.view;
    if(texture->d.target!=NVN_TEXTURE_TARGET_CUBEMAP&&texture->d.target!=NVN_TEXTURE_TARGET_CUBEMAP_ARRAY)return;
    image_entry *image=graphics_probe_image(texture->serial);if(!image||image->aspect!=VK_IMAGE_ASPECT_COLOR_BIT)return;
    unsigned mip=view->set_levels?view->base_level:0,layer=view->set_layers?view->min_layer:0;
    if(mip>=texture->d.levels||layer>=texture->L.layers)return;
    unsigned count=view->set_layers&&view->nlayers>0?(unsigned)view->nlayers:texture->L.layers-layer;
    if(count>texture->L.layers-layer)count=texture->L.layers-layer;if(count>6)count=6;if(!count)return;
    if(probe_shadow_cube.selected&&(probe_shadow_cube.serial!=texture->serial||probe_shadow_cube.mip!=mip||probe_shadow_cube.layer!=layer))return;
    if(!probe_shadow_cube.selected){probe_shadow_cube.selected=1;probe_shadow_cube.serial=texture->serial;probe_shadow_cube.mip=mip;probe_shadow_cube.layer=layer;probe_shadow_cube.count=count;probe_shadow_cube.id=handle&0xfffff;probe_shadow_cube.view=*view;
        fprintf(probe_trace,"H\t%llu\tshadow-color-cube\tkey=%s\tslot=%d\tserial=%llu\tmip=%u\tlayer=%u+%u\tmaximum_bytes=%u\n",(unsigned long long)probe_draws,shader->key,slot,(unsigned long long)texture->serial,mip,layer,count,count*128*128*2);}
    unsigned w=dimension(texture->d.width,mip),h=dimension(texture->d.height,mip);
    for(unsigned face=0;face<count;face++){graphics_probe_interest(texture->serial,mip,layer+face);
        for(probe_image *entry=probe_images;entry;entry=entry->next)if(entry->serial==texture->serial&&entry->mip==mip&&entry->layer==layer+face){entry->roi_x=w>128?(w-128)/2:0;entry->roi_y=h>128?(h-128)/2:0;entry->roi_w=128;entry->roi_h=128;break;}}
}
static void graphics_probe_shader(shader_entry *shader) {
    fprintf(probe_trace,"S\t%llu\tstage=%u\tkey=%s\n",(unsigned long long)probe_draws,shader->stage,shader->key);
    for(unsigned i=0;i<shader->count;i++){const shader_binding *binding=&shader->bindings[i];unsigned stage=shader->stage;
        if(binding->kind==3||binding->kind==4){for(unsigned j=0;j<binding->count;j++){int slot=binding->kind==4?shader_image_slot(binding,j):shader_texture_slot(binding,j);uint32_t handle=shader_handle(binding,stage,j);graphics_probe_texture(binding->kind==4?"image":"texture",stage,slot>=0?(unsigned)slot:UINT32_MAX,handle);graphics_probe_shadow_cube(shader,binding,slot,handle);
            if(stage==1&&binding->kind==3&&slot>=0)graphics_probe_material(shader,slot,handle);
            // 显式一帧中额外读 FS slot35 非 BC 图的小区域，定位地面混合输入；不增加压缩材质预算。
            // Add a small non-BC FS slot35 readback in the requested frame to inspect ground-blend inputs without increasing the compressed-material budget.
            if(stage==1&&binding->kind==3&&slot==35){texture_descriptor descriptor=descriptor_texture(handle&0xfffff);if(descriptor.valid){const view_t *view=&descriptor.view;graphics_probe_interest(descriptor.texture.serial,view->set_levels?view->base_level:0,view->set_layers?view->min_layer:0);}}
            // 已确认营地地形颜色/法线缓存：固定 world(8018,9858) 对应 atlas 约(998,998)，各取256²，不跟随移动角色。
            // Confirmed camp terrain color/normal caches: fixed world(8018,9858) maps near atlas(998,998); capture 256-square regions without following the moving character.
            if(stage==1&&binding->kind==3&&(slot==35||slot==36)&&!strcmp(shader->key,"e1c00b41f984949c")){texture_descriptor descriptor=descriptor_texture(handle&0xfffff);if(descriptor.valid&&descriptor.texture.d.format==NVN_FORMAT_RGBA8){const view_t *view=&descriptor.view;unsigned mip=view->set_levels?view->base_level:0,layer=view->set_layers?view->min_layer:0;graphics_probe_interest(descriptor.texture.serial,mip,layer);
                for(probe_image *entry=probe_images;entry;entry=entry->next)if(entry->serial==descriptor.texture.serial&&entry->mip==mip&&entry->layer==layer){entry->roi_x=870>>mip;entry->roi_y=870>>mip;entry->roi_w=256;entry->roi_h=256;break;}}}
            if(stage==1&&binding->kind==3&&(binding->type&255)==2&&slot==8){texture_descriptor descriptor=descriptor_texture(handle&0xfffff);if(descriptor.valid){const char *name=graphics_probe_buffer(3,shader,slot,(uintptr_t)descriptor.texture.mem,descriptor.texture.L.size);
                fprintf(probe_trace,"L\t%llu\tstage=%u\tslot=%d\tserial=%llu\tfmt=%#x\taddr=%#llx\tsize=%llu\tfile_first_use=%s\n",(unsigned long long)probe_draws,stage,slot,(unsigned long long)descriptor.texture.serial,descriptor.texture.d.format,(unsigned long long)(uintptr_t)descriptor.texture.mem,(unsigned long long)descriptor.texture.L.size,name?name:"not-captured");}}
        }
        }else if(binding->kind==1&&binding->slot>=3&&binding->slot<19){const p_bind_buf *buffer=&state.ubo[stage][binding->slot-3];const char *name=NULL;
            if(state.rt.n&&state.rt.color[0]&&state.rt.color[0]->d.format==NVN_FORMAT_R11G11B10F)name=graphics_probe_buffer(0,shader,binding->slot-3,buffer->addr,buffer->size<65536?buffer->size:65536);
            fprintf(probe_trace,"U\t%llu\tstage=%u\tslot=%u\taddr=%#llx\tsize=%llu\tfile=%s\n",(unsigned long long)probe_draws,stage,binding->slot-3,(unsigned long long)buffer->addr,(unsigned long long)buffer->size,name?name:"not-captured");
        }else if(binding->kind==2){unsigned base=stage==0?0x110:stage==5?0x310:0x510;int offset=binding->cb_offset*4,uniform=stage==5&&offset>=0x210&&offset<0x310,slot=(offset-(uniform?0x210:(int)base))/16;
            if(slot>=0&&slot<16){const p_bind_buf *buffer=uniform?&state.ubo[stage][slot]:&state.ssbo[stage][slot];const char *name=NULL;if(stage==1&&!uniform&&(slot==4||slot==5)&&!(binding->flags&1))name=graphics_probe_buffer(2,shader,slot,buffer->addr,buffer->size);
                fprintf(probe_trace,"B\t%llu\tstage=%u\t%s=%d\tflags=%u\taddr=%#llx\tsize=%llu\tfile_first_use=%s\n",(unsigned long long)probe_draws,stage,uniform?"ubo-pointer":"ssbo",slot,binding->flags,(unsigned long long)buffer->addr,(unsigned long long)buffer->size,name?name:"not-captured");}
        }
    }
}
static void graphics_probe_program(void) {if(state.program)for(unsigned i=0;i<state.program->nstage;i++)graphics_probe_shader(program_shader(state.program,i));}
static void graphics_probe_geometry(const p_draw *draw,const p_draw_idx *indexed) {
    if(!state.rt.n||!state.rt.color[0]||state.rt.color[0]->d.format!=NVN_FORMAT_R11G11B10F)return;
    shader_entry *vs=NULL,*fs=NULL;for(unsigned i=0;i<state.program->nstage;i++){shader_entry *s=program_shader(state.program,i);if(s->stage==0)vs=s;else if(s->stage==1)fs=s;}if(!vs)return;
    // 每个 VS 最多 16 次真正绘制，足够覆盖角色部件而不会导出整个顶点池。
    // Capture at most 16 draws per VS, enough for character parts without exporting whole vertex pools.
    // detail_normal 与早先模型共用 VS，用独立 FS 桶捕获前四次，不消耗或重置原 VS 限额。
    // detail_normal shares earlier models' VS; capture its first four draws in a separate FS bucket without consuming or resetting the original VS quota.
    static struct {const char *key;unsigned count;} extra_fs={"b70c638ef69450c5",0};
    if(fs&&!strcmp(fs->key,extra_fs.key)){if(extra_fs.count++>=4)return;}
    else{static struct {char key[17];unsigned count;} seen[32];static unsigned seen_count;unsigned entry=0;while(entry<seen_count&&strcmp(seen[entry].key,vs->key))entry++;
        if(entry==seen_count){if(seen_count==32)return;strcpy(seen[seen_count++].key,vs->key);}if(seen[entry].count++>=16)return;}
    uint64_t first=draw?draw->first:0;int64_t base=indexed?indexed->base_vertex:0;
    if(indexed){unsigned bytes=indexed->itype==0?1:indexed->itype==1?2:4,count=indexed->count<64?indexed->count:64;uint8_t raw[256];uint32_t minimum=UINT32_MAX,maximum=0;
        if(!count||dependencies_unknown(indexed->ibuf,(uint64_t)count*bytes)||snapshot_read(indexed->ibuf,raw,(uint64_t)count*bytes))return;
        for(unsigned i=0;i<count;i++){uint32_t value=0;memcpy(&value,raw+i*bytes,bytes);if(value<minimum)minimum=value;if(value>maximum)maximum=value;}
        if((int64_t)minimum+base<0)return;first=(uint64_t)((int64_t)minimum+base);
        fprintf(probe_trace,"J\t%llu\tindex_addr=%#llx\tindex_bytes=%u\tcount=%d\tbase_vertex=%lld\tbase_instance=%u\tsampled_min=%u\tsampled_max=%u\n",(unsigned long long)probe_draws,(unsigned long long)indexed->ibuf,bytes,indexed->count,(long long)base,indexed->base_instance,minimum,maximum);
    }
    uint32_t end[16]={0};for(unsigned i=0;i<16;i++){va_st a=state.attributes[i];if(!a.fmt||!vs->attributes[i])continue;if(a.stream>=16)continue;int bw,bh;uint32_t bytes=nvn_fmt_bpb(a.fmt,&bw,&bh);if(a.offset+bytes>end[a.stream])end[a.stream]=a.offset+bytes;
        fprintf(probe_trace,"A\t%llu\tkey=%s\tlocation=%u\tfmt=%#x\tstream=%u\toffset=%u\tstride=%u\tdivisor=%u\n",(unsigned long long)probe_draws,vs->key,i,a.fmt,a.stream,a.offset,state.streams[a.stream].stride,state.streams[a.stream].divisor);}
    for(unsigned i=0;i<16;i++)if(end[i]){const NVNbufferRange *buffer=&state.vertices[i];const vs_st *stream=&state.streams[i];uint64_t element=stream->divisor?(indexed?indexed->base_instance:0):first,offset=element*stream->stride,size=stream->stride?stream->stride*15+end[i]:end[i];
        if(!buffer->address||offset>=buffer->size)continue;if(size>buffer->size-offset)size=buffer->size-offset;if(size>4096)size=4096;
        const char *name=graphics_probe_buffer(1,vs,i,buffer->address+offset,size);
        fprintf(probe_trace,"V\t%llu\tkey=%s\tstream=%u\tbase=%#llx\tguest_offset=%llu\tfirst_element=%llu\tsize=%llu\tbound_buffer=%#llx\tbound_offset=%llu\tfile=%s\n",(unsigned long long)probe_draws,vs->key,i,(unsigned long long)buffer->address,(unsigned long long)offset,(unsigned long long)element,(unsigned long long)size,(unsigned long long)(uintptr_t)graphics_commands.vertices[i],(unsigned long long)graphics_commands.offsets[i],name?name:"pending-or-budget");}
}
static void graphics_probe_draw(const p_draw *draw,const p_draw_idx *indexed) {
    if(!graphics_probe_active())return;probe_draws++;fprintf(probe_trace,"D\t%llu\tpass=%llu\tmask=%#x\tblend=%u\tdepth=%u,%u,%u\trange=%g,%g\tcull=%u\tfront=%u\tbias=%g,%g,%g\toffset_en=%u\n",(unsigned long long)probe_draws,(unsigned long long)render_passes,state.mask.mask,(unsigned)state.color.blend_en,(unsigned)state.depth.dt,(unsigned)state.depth.dw,(unsigned)state.depth.df,state.depth_range.n,state.depth_range.f,(unsigned)state.polygon.cull,(unsigned)state.polygon.front,state.polygon_offset.factor,state.polygon_offset.units,state.polygon_offset.clamp,(unsigned)state.polygon.offset_en);
    uint64_t ticket=mtl_last_fence()+1;probe_query *query=probe_queries;
    if(!query||query->ticket!=ticket||query->index!=graphics_active_query){query=calloc(1,sizeof *query);if(!query)die("Vulkan probe query metadata");query->ticket=ticket;query->index=graphics_active_query;query->pass=render_passes;query->first=probe_draws;query->next=probe_queries;probe_queries=query;}query->last=probe_draws;
    fprintf(probe_trace,"E\t%llu\tticket=%llu\tquery=%u\n",(unsigned long long)probe_draws,(unsigned long long)ticket,graphics_active_query);
    for(int i=0;i<state.rt.n;i++)if(state.rt.color[i]){const tex_t *texture=state.rt.color[i];const view_t *view=state.rt.has_cview[i]?&state.rt.cview[i]:NULL;unsigned mip=view&&view->set_levels?view->base_level:0,layer=view&&view->set_layers?view->min_layer:0;
        fprintf(probe_trace,"R\t%llu\tslot=%d\tserial=%llu\tfmt=%#x\tdim=%u,%u,%u\tmip=%u\tlayer=%u\n",(unsigned long long)probe_draws,i,(unsigned long long)texture->serial,texture->d.format,texture->d.width,texture->d.height,texture->d.depth,mip,layer);graphics_probe_interest(texture->serial,mip,layer);}
    if(state.rt.depth){const tex_t *texture=state.rt.depth;const view_t *view=state.rt.has_dview?&state.rt.dview:NULL;unsigned mip=view&&view->set_levels?view->base_level:0,layer=view&&view->set_layers?view->min_layer:0;
        fprintf(probe_trace,"Z\t%llu\tserial=%llu\tfmt=%#x\tdim=%u,%u,%u\tmip=%u\tlayer=%u\n",(unsigned long long)probe_draws,(unsigned long long)texture->serial,texture->d.format,texture->d.width,texture->d.height,texture->d.depth,mip,layer);graphics_probe_interest(texture->serial,mip,layer);}
    graphics_probe_program();graphics_probe_geometry(draw,indexed);
}
static void graphics_probe_dispatch(const p_dispatch *dispatch) {
    if(!graphics_probe_active())return;probe_draws++;fprintf(probe_trace,"C\t%llu\tgroups=%u,%u,%u\n",(unsigned long long)probe_draws,dispatch->x,dispatch->y,dispatch->z);graphics_probe_program();
    shader_entry *shader=state.program?program_shader(state.program,0):NULL;
    if(shader&&!strcmp(shader->key,"552f45c348a61b86")){const p_bind_buf *buffer=&state.ubo[5][10];if(buffer->addr&&buffer->size&&buffer->size<=1048576){uint8_t *bytes=malloc(buffer->size);if(bytes&&!snapshot_read(buffer->addr,bytes,buffer->size)){char path[1200];snprintf(path,sizeof path,"%s/lut-ubo10.bin",probe_directory);FILE *file=fopen(path,"wb");if(file){fwrite(bytes,1,buffer->size,file);fclose(file);}}free(bytes);}}
}
static float graphics_probe_half(uint16_t value) {
    uint32_t sign=(uint32_t)(value&0x8000)<<16,exponent=(value>>10)&31,fraction=value&1023,bits;
    if(!exponent){if(!fraction)bits=sign;else{exponent=113;while(!(fraction&1024)){fraction<<=1;exponent--;}bits=sign|(exponent<<23)|((fraction&1023)<<13);}}
    else bits=sign|((exponent==31?255:exponent+112)<<23)|(fraction<<13);float result;memcpy(&result,&bits,4);return result;
}
static unsigned graphics_probe_values(int format,const uint8_t *data,unsigned bpb,float values[4]) {
    unsigned count=0;
    if(format==0x05||format==0x11||format==0x29){count=bpb/2;for(unsigned i=0;i<count;i++){uint16_t value;memcpy(&value,data+2*i,2);values[i]=graphics_probe_half(value);}}
    else if(format==0x06||format==0x32){uint16_t value;memcpy(&value,data,2);count=1;values[0]=value/65535.f;}
    else if(format==0x33||format==0x35){uint32_t value;memcpy(&value,data,4);count=1;values[0]=(value&0xffffff)/16777215.f;}
    else if(format==0x34||format==0x36){count=1;memcpy(values,data,4);}
    else if(format==0x0a||format==0x16||format==0x2e){count=bpb/4;memcpy(values,data,bpb);}
    else if(format==0x3f){uint32_t packed;memcpy(&packed,data,4);count=3;values[0]=graphics_probe_half((packed&2047)<<4);values[1]=graphics_probe_half(((packed>>11)&2047)<<4);values[2]=graphics_probe_half(((packed>>22)&1023)<<5);}
    else if(format==0x01||format==0x0d||format==0x25||format==0x37||format==0x38){count=bpb;for(unsigned i=0;i<count;i++)values[i]=data[i]/255.f;}
    else if(format==0x3d){uint32_t packed;memcpy(&packed,data,4);count=4;for(unsigned i=0;i<3;i++)values[i]=((packed>>(10*i))&1023)/1023.f;values[3]=(packed>>30)/3.f;}
    return count;
}
// 每份客体存储前缀最多4 KiB；这是完成帧后的独立证据，不冒充此前上传的字节或 GPU 图像。
// Each guest-storage prefix is at most 4 KiB; it is completed-frame evidence, not bytes from an earlier upload or a GPU image.
typedef struct {uint64_t size,hash;unsigned pending,unknown,nonzero;const char *source;char file[160];} probe_source_prefix;
static probe_source_prefix graphics_probe_source_prefix(uintptr_t address,uint64_t size,const char *name) {
    if(size>4096||size>UINTPTR_MAX-address)die("Vulkan probe source prefix bounds");
    probe_source_prefix prefix={.size=size,.hash=14695981039346656037ull};strcpy(prefix.file,"not-captured");prefix.pending=address&&size&&snapshot_pending(address,size);prefix.unknown=address&&size&&dependencies_unknown(address,size);
    prefix.source=!address?"unbound":prefix.unknown?"pending-not-captured":prefix.pending?"completed-snapshot-overlay":"completed-guest-memory";
    if(address&&size&&!prefix.unknown){uint8_t bytes[4096];if(snapshot_read(address,bytes,size)){prefix.source="unreadable-not-captured";return prefix;}
        for(uint64_t i=0;i<size;i++){prefix.nonzero+=bytes[i]!=0;prefix.hash=(prefix.hash^bytes[i])*1099511628211ull;}strcpy(prefix.file,name);char path[1200];snprintf(path,sizeof path,"%s/%s",probe_directory,name);FILE *file=fopen(path,"wb");if(!file)die("Vulkan probe source prefix open");fwrite(bytes,1,size,file);fclose(file);}
    return prefix;
}
static probe_source_prefix graphics_probe_texture_source(image_entry *image,uint64_t offset,unsigned limit,const char *role) {
    const tex_t *texture=&image->texture;if(offset>texture->L.size)die("Vulkan probe texture source offset");uint64_t size=texture->L.size-offset;if(size>limit)size=limit;
    uintptr_t address=(uintptr_t)texture->mem;if(address){if(offset>UINTPTR_MAX-address)die("Vulkan probe texture source address");address+=offset;}
    char name[160];snprintf(name,sizeof name,"texture-source-%llu.bin",(unsigned long long)image->serial);probe_source_prefix prefix=graphics_probe_source_prefix(address,size,name);
    fprintf(probe_trace,"G\tserial=%llu\tfmt=%#x\tmem=%#llx\tpool=%#llx\tpool_offset=%lld\tstorage_size=%llu\tlayer_size=%llu\tflags=%#x\tlevels=%u\tpkg_block_h=%d\thas_layout=%u\tlayout=%02x%02x%02x%02x%02x%02x%02x%02x\ttile_h=%u\ttile_d=%u\tstamp=%llu\tpopulated=%d\ttracked=%d\tdirty=%d\tpending=%u\tunknown=%u\tsource=%s\tprefix_size=%llu\tnonzero=%u\thash=%#llx\tfile=%s\n",
        (unsigned long long)image->serial,texture->d.format,(unsigned long long)(uintptr_t)texture->mem,(unsigned long long)(uintptr_t)texture->pool,(long long)texture->off,(unsigned long long)texture->L.size,(unsigned long long)texture->L.layer_size,texture->d.flags,texture->d.levels,texture->d.pkg_block_h,texture->has_layout,
        texture->layout[0],texture->layout[1],texture->layout[2],texture->layout[3],texture->layout[4],texture->layout[5],texture->layout[6],texture->layout[7],texture->L.tileH,texture->L.tileD,(unsigned long long)image->stamp,image->populated,texture->mem&&nvn_dirty_tracked(texture->mem,texture->L.size),texture->mem&&nvn_dirty_since(texture->mem,texture->L.size,image->stamp),prefix.pending,prefix.unknown,prefix.source,(unsigned long long)size,prefix.nonzero,(unsigned long long)prefix.hash,prefix.file);
    fprintf(probe_trace,"N\ttexture-source-baseline\tserial=%llu\trole=%s\tprefix_offset=%llu\tvalid=%d\tstamp=%llu\tseen=%llu\ttracked=%d\thash=%#llx\tstorage_in_pool=%d\n",(unsigned long long)image->serial,role,(unsigned long long)offset,image->source_valid,(unsigned long long)image->source_stamp,(unsigned long long)image->source_seen,image->source_tracked,(unsigned long long)image->source_hash,texture->mem&&mtl_addr((uintptr_t)texture->mem,texture->L.size,NULL,NULL));return prefix;
}
static void graphics_probe_material_source(image_entry *image) {graphics_probe_texture_source(image,0,4096,"material");}
// 指定 CS 的输入/输出在 Dispatch 前后立即复制；源随后复用也不会改变诊断真值。
// Copy the requested CS inputs/outputs immediately around Dispatch; later source reuse cannot change diagnostic evidence.
static probe_cs_buffer *graphics_probe_cs_copy(unsigned id,unsigned binding,unsigned element,VkDescriptorType type,VkDescriptorBufferInfo source,VkFormat format,unsigned writable,const char *phase) {
    if(!source.buffer||!source.range||source.range==VK_WHOLE_SIZE||source.offset%4||source.range%4||source.range>(8u<<20)||probe_cs_bytes+source.range>(16u<<20))die("Vulkan CS probe buffer range/budget");
    probe_cs_buffer *read=calloc(1,sizeof *read);if(!read)die("Vulkan CS probe metadata");read->dispatch=id;read->binding=binding;read->element=element;read->type=type;read->source=source;read->format=format;read->writable=writable;strcpy(read->phase,phase);read->readback=buffer_create(source.range);probe_cs_bytes+=source.range;
    VkBufferMemoryBarrier before={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.buffer=source.buffer,.offset=source.offset,.size=source.range};
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,1,&before,0,NULL);VkBufferCopy copy={.srcOffset=source.offset,.size=source.range};vkCmdCopyBuffer(command,source.buffer,read->readback.buffer,1,&copy);
    VkBufferMemoryBarrier after=before;after.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;after.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,NULL,1,&after,0,NULL);
    after.buffer=read->readback.buffer;after.offset=0;after.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;after.dstAccessMask=VK_ACCESS_HOST_READ_BIT;vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,NULL,1,&after,0,NULL);
    snprintf(read->name,sizeof read->name,"cs-%u-%s-b%u-e%u.bin",id,phase,binding,element);fprintf(probe_trace,"Y\tcs-buffer\tdispatch=%u\tkey=%s\tphase=%s\tbinding=%u\telement=%u\ttype=%u\tformat=%u\twritable=%u\tbuffer=%#llx\toffset=%llu\trange=%llu\tfile=%s\n",id,probe_cs_key,phase,binding,element,type,format,writable,(unsigned long long)(uintptr_t)source.buffer,(unsigned long long)source.offset,(unsigned long long)source.range,read->name);read->next=probe_cs_buffers;probe_cs_buffers=read;return read;
}
static void graphics_probe_cs_inputs(const struct shader_entry *shader,const VkWriteDescriptorSet *writes,uint32_t count,const uint32_t *dynamic_offsets,unsigned dynamic_count) {
    if(!graphics_probe_active()||!*probe_cs_key||shader->stage!=5||strcmp(shader->key,probe_cs_key)||probe_cs_count==2)return;
    if(probe_cs_pending.id)die("Vulkan CS probe inputs without previous dispatch");probe_cs_pending.id=++probe_cs_count;strcpy(probe_cs_pending.key,shader->key);probe_cs_pending.first=probe_cs_buffers;
    for(unsigned i=0;i<count;i++){const VkWriteDescriptorSet *write=&writes[i];unsigned writable=0;for(unsigned b=0;b<shader->count;b++)if(shader->bindings[b].binding==write->dstBinding){const shader_binding *binding=&shader->bindings[b];writable=binding->kind==2?(binding->flags&1):binding->kind==4?(binding->flags&4):0;break;}
        for(unsigned element=0;element<write->descriptorCount;element++){VkDescriptorBufferInfo source={0};VkFormat format=VK_FORMAT_UNDEFINED;
            if(write->descriptorType==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER||write->descriptorType==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)source=write->pBufferInfo[element];
            // 动态 uniform 的位置不在描述符里；探针必须补上绑定时的偏移，否则记录的是块首而不是真实输入。
            // A dynamic uniform's position is not in the descriptor; the probe must add the bound offset, or it would
            // record the block start instead of the real input.
            else if(write->descriptorType==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC){
                int dynamic_index=shader->binding_plan?shader->binding_plan[i].dynamic_index:-1;
                if(dynamic_index<0||(unsigned)(dynamic_index+element)>=dynamic_count)die("Vulkan CS probe dynamic uniform offset");
                source=write->pBufferInfo[element];source.offset+=dynamic_offsets[dynamic_index+element];
            }
            else if(write->descriptorType==VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER||write->descriptorType==VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER){VkBufferView view=write->pTexelBufferView[element];texel_view_entry *found=NULL;for(unsigned bucket=0;bucket<1024&&!found;bucket++)for(texel_view_entry *entry=texel_view_cache[texel_view_slot][bucket];entry;entry=entry->next)if(entry->view==view){found=entry;break;}if(!found)die("Vulkan CS probe texel view backing unavailable");source=found->buffer;format=found->format;}
            else {fprintf(probe_trace,"Y\tcs-unsupported-descriptor\tdispatch=%u\tkey=%s\tbinding=%u\telement=%u\ttype=%u\n",probe_cs_pending.id,shader->key,write->dstBinding,element,write->descriptorType);continue;}
            graphics_probe_cs_copy(probe_cs_pending.id,write->dstBinding,element,write->descriptorType,source,format,writable,"pre");
        }
    }
}
static void graphics_probe_cs_outputs(const struct shader_entry *shader) {
    if(!probe_cs_pending.id||shader->stage!=5||strcmp(shader->key,probe_cs_pending.key))return;
    probe_cs_buffer *stop=probe_cs_pending.first,*read=probe_cs_buffers;
    while(read!=stop){probe_cs_buffer *next=read->next;if(read->writable)graphics_probe_cs_copy(probe_cs_pending.id,read->binding,read->element,read->type,read->source,read->format,read->writable,"post");read=next;}
    memset(&probe_cs_pending,0,sizeof probe_cs_pending);
}
static void graphics_probe_cs_save(void) {
    if(probe_cs_pending.id)die("Vulkan CS probe missing post-dispatch hook");while(probe_cs_buffers){probe_cs_buffer *read=probe_cs_buffers;probe_cs_buffers=read->next;char path[1200];snprintf(path,sizeof path,"%s/%s",probe_directory,read->name);FILE *file=fopen(path,"wb");if(!file)die("Vulkan CS probe output open");fwrite(read->readback.mapped,1,read->readback.size,file);fclose(file);buffer_destroy(&read->readback);free(read);}
}
static int graphics_probe_storage_overlap(const tex_t *a,const tex_t *b) {
    uintptr_t aa=(uintptr_t)a->mem,bb=(uintptr_t)b->mem;return aa&&bb&&a->L.size&&b->L.size&&a->L.size<=UINTPTR_MAX-aa&&b->L.size<=UINTPTR_MAX-bb&&aa<bb+b->L.size&&bb<aa+a->L.size;
}
static void graphics_probe_storage_alias(const tex_t *source,const tex_t *alias,const view_t *view,int id,const char *origin) {
    uintptr_t start=(uintptr_t)source->mem>(uintptr_t)alias->mem?(uintptr_t)source->mem:(uintptr_t)alias->mem,end=(uintptr_t)source->mem+source->L.size<(uintptr_t)alias->mem+alias->L.size?(uintptr_t)source->mem+source->L.size:(uintptr_t)alias->mem+alias->L.size;
    image_entry *gpu=graphics_probe_image(alias->serial);
    fprintf(probe_trace,"O\tshadow-storage-overlap\tselected=%llu\torigin=%s\tid=%d\tserial=%llu\tfmt=%#x\ttarget=%d\tdim=%u,%u,%u\tlevels=%u\tlayers=%u\tmem=%#llx\tpool=%#llx\tpool_offset=%lld\tstorage_size=%llu\tlayer_size=%llu\tflags=%#x\thas_layout=%u\tviewfmt=%#x\tmip=%d+%d\tlayer=%d+%d\toverlap_address=%#llx\toverlap_size=%llu\tgpu_image=%d\tgpu_populated=%d\tlast_stage=%#x\tlast_access=%#x\n",
        (unsigned long long)source->serial,origin,id,(unsigned long long)alias->serial,alias->d.format,alias->d.target,alias->d.width,alias->d.height,alias->d.depth,alias->d.levels,alias->L.layers,(unsigned long long)(uintptr_t)alias->mem,(unsigned long long)(uintptr_t)alias->pool,(long long)alias->off,(unsigned long long)alias->L.size,(unsigned long long)alias->L.layer_size,alias->d.flags,alias->has_layout,view&&view->set_format?view->format:alias->d.format,view&&view->set_levels?view->base_level:0,view&&view->set_levels?view->nlevels:0,view&&view->set_layers?view->min_layer:0,view&&view->set_layers?view->nlayers:0,(unsigned long long)start,(unsigned long long)(end-start),!!gpu,gpu?gpu->populated:0,gpu?gpu->last_stage:0,gpu?gpu->last_access:0);
}
static void graphics_probe_shadow_source(void) {
    if(!probe_shadow_cube.selected)return;image_entry *image=graphics_probe_image(probe_shadow_cube.serial);if(!image)return;const tex_t *texture=&image->texture;
    unsigned count=probe_shadow_cube.count;if(count>6)count=6;uint64_t level_offset=nvn_tex_level_offset(&texture->d,probe_shadow_cube.mip);
    if(level_offset>=texture->L.layer_size)die("Vulkan probe shadow source mip offset");unsigned limit=texture->L.layer_size-level_offset<4096?texture->L.layer_size-level_offset:4096;
    for(unsigned face=0;face<count;face++){unsigned layer=probe_shadow_cube.layer+face;if(layer>=texture->L.layers||(uint64_t)layer>UINT64_MAX/texture->L.layer_size)die("Vulkan probe shadow source layer");uint64_t offset=layer*texture->L.layer_size+level_offset;if(offset>texture->L.size||limit>texture->L.size-offset)die("Vulkan probe shadow source range");
        uintptr_t address=(uintptr_t)texture->mem;if(address){if(offset>UINTPTR_MAX-address)die("Vulkan probe shadow source address");address+=offset;}
        probe_source_prefix prefix;if(!face)prefix=graphics_probe_texture_source(image,offset,limit,"shadow-cube");else{char name[160];snprintf(name,sizeof name,"texture-source-%llu-m%u-l%u.bin",(unsigned long long)image->serial,probe_shadow_cube.mip,layer);prefix=graphics_probe_source_prefix(address,limit,name);}
        fprintf(probe_trace,"X\tshadow-source-prefix\tserial=%llu\tid=%u\tmip=%u\tlayer=%u\tface=%u\toffset=%llu\taddress=%#llx\tsize=%llu\tsource=%s\tpending=%u\tunknown=%u\tnonzero=%u\thash=%#llx\tfile=%s\n",(unsigned long long)image->serial,probe_shadow_cube.id,probe_shadow_cube.mip,layer,face,(unsigned long long)offset,(unsigned long long)address,(unsigned long long)prefix.size,prefix.source,prefix.pending,prefix.unknown,prefix.nonzero,(unsigned long long)prefix.hash,prefix.file);
    }
    for(image_entry *alias=image_list;alias;alias=alias->next)if(graphics_probe_storage_overlap(texture,&alias->texture))graphics_probe_storage_alias(texture,&alias->texture,NULL,-1,"image");
    // 描述符可能由另一客体线程注册：锁内只复制元数据，解锁后才做查图像、读源或输出。
    // Another guest thread may register descriptors: copy metadata under the lock, then query images/read sources/write output after unlocking.
    typedef struct {int id;texture_descriptor descriptor;} alias_descriptor;alias_descriptor *aliases=NULL;size_t size=0,capacity=0;pthread_mutex_lock(&descriptor_lock);
    for(int id=0;id<texture_capacity;id++)if(texture_descriptors[id].valid&&graphics_probe_storage_overlap(texture,&texture_descriptors[id].texture)){if(size==capacity){capacity=capacity?capacity*2:16;alias_descriptor *grown=realloc(aliases,capacity*sizeof *aliases);if(!grown)die("Vulkan probe alias descriptors allocation");aliases=grown;}aliases[size++]=(alias_descriptor){id,texture_descriptors[id]};}pthread_mutex_unlock(&descriptor_lock);
    for(size_t i=0;i<size;i++)graphics_probe_storage_alias(texture,&aliases[i].descriptor.texture,&aliases[i].descriptor.view,aliases[i].id,"descriptor");free(aliases);
}
static void graphics_probe_capture(uint64_t frame) {
    graphics_probe_configure();if(!probe_frame||probe_done||frame!=probe_frame)return;probe_done=1;mtl_finish();mtl_sub_begin();probe_read *reads=NULL;
    graphics_probe_cs_save();
    graphics_probe_shadow_source();
    for(probe_buffer_read *read=probe_buffers;read;read=read->next){uint64_t aligned=read->offset&~3ull,bytes=((read->offset-aligned)+read->size+3)&~3ull;if(aligned>read->source.capacity||bytes>read->source.capacity-aligned)die("Vulkan probe retained buffer range");
        read->readback=buffer_create(bytes);VkBufferMemoryBarrier source={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.buffer=read->source.buffer,.offset=aligned,.size=bytes};
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,1,&source,0,NULL);VkBufferCopy copy={.srcOffset=aligned,.size=bytes};vkCmdCopyBuffer(command,read->source.buffer,read->readback.buffer,1,&copy);}
    unsigned bc_count=0;uint64_t bc_bytes=0;
    for(int priority=2;priority>=0;priority--)for(probe_image *interest=probe_images;interest;interest=interest->next){if(interest->priority!=(unsigned)priority)continue;image_entry *image=graphics_probe_image(interest->serial);if(!image)continue;unsigned level=interest->mip;uint32_t full_w=dimension(image->texture.d.width,level),full_h=dimension(image->texture.d.height,level),full_d=image->texture.d.target==NVN_TEXTURE_TARGET_3D?dimension(image->texture.d.depth,level):1;int bw,bh,bpb=nvn_fmt_bpb(image->texture.d.format,&bw,&bh),compressed=bw==4&&bh==4;
        if(bpb<=0||bpb>16||(!compressed&&(bw!=1||bh!=1))||level>=image->texture.d.levels||interest->layer>=image->texture.L.layers||(compressed&&(!priority||bc_count>=24)))continue;
        // Vulkan depth-plane copies use D16=2 / D24=4 / D32=4 bytes; combined stencil bytes are separate.
        int depth=!!(image->aspect&VK_IMAGE_ASPECT_DEPTH_BIT);if(depth)bpb=image->texture.d.format==NVN_FORMAT_DEPTH16?2:4;
        probe_read *read=calloc(1,sizeof *read);if(!read)die("Vulkan probe read allocation");read->serial=image->serial;read->texture=image->texture.d;read->mip=level;read->layer=interest->layer;
        unsigned maximum=compressed?(full_w<=1024&&full_h<=1024?1024:256):depth&&full_w<=2048&&full_h<=2048?2048:64;read->w=full_w<maximum?full_w:maximum;read->h=full_h<maximum?full_h:maximum;read->d=full_d<32?full_d:32;read->x=(full_w-read->w)/2;read->y=(full_h-read->h)/2;read->bpb=bpb;
        if(!compressed&&!depth&&interest->roi_w&&interest->roi_h){read->w=full_w<interest->roi_w?full_w:interest->roi_w;read->h=full_h<interest->roi_h?full_h:interest->roi_h;read->x=interest->roi_x>full_w-read->w?full_w-read->w:interest->roi_x;read->y=interest->roi_y>full_h-read->h?full_h-read->h:interest->roi_y;}
        read->bw=bw;read->bh=bh;if(compressed){read->x=read->x/bw*bw;read->y=read->y/bh*bh;}uint64_t bytes=(uint64_t)((read->w+bw-1)/bw)*((read->h+bh-1)/bh)*read->d*bpb;
        if(compressed){if(bc_bytes+bytes>(8u<<20)){free(read);continue;}bc_bytes+=bytes;bc_count++;graphics_probe_material_source(image);}read->buffer=buffer_create(bytes);
        image_access(image,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_TRANSFER_READ_BIT);VkBufferImageCopy copy={.imageSubresource={depth?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT,level,read->layer,1},.imageOffset={(int)read->x,(int)read->y,0},.imageExtent={read->w,read->h,read->d}};
        vkCmdCopyImageToBuffer(command,image->image,VK_IMAGE_LAYOUT_GENERAL,read->buffer.buffer,1,&copy);read->next=reads;reads=read;
    }
    VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);uint64_t read_ticket=mtl_sub_commit();mtl_wait(read_ticket,UINT64_MAX);
    char path[1200];snprintf(path,sizeof path,"%s/images.tsv",probe_directory);FILE *index=fopen(path,"w");if(!index)die("Vulkan probe index open");fputs("serial\tformat\tmip\tlayer\tfull_dimension\tregion\tbytes_per_pixel\tchannels\tfinite\tnan_inf\tzero\tmin\tmax\tmean\tfile\tblock_extent\tbytes_per_block\n",index);
    while(reads){probe_read *read=reads;reads=read->next;const nvn_texdesc *texture=&read->texture;char name[160];snprintf(name,sizeof name,"image-%llu-m%u-l%u-f%x.bin",(unsigned long long)read->serial,read->mip,read->layer,texture->format);snprintf(path,sizeof path,"%s/%s",probe_directory,name);FILE *file=fopen(path,"wb");if(!file)die("Vulkan probe raw image open");fwrite(read->buffer.mapped,1,read->buffer.size,file);fclose(file);
        double sums[4]={0};float minimum[4]={FLT_MAX,FLT_MAX,FLT_MAX,FLT_MAX},maximum[4]={-FLT_MAX,-FLT_MAX,-FLT_MAX,-FLT_MAX};uint64_t finite[4]={0},nonfinite=0,zero=0;unsigned channels=0;uint8_t *data=read->buffer.mapped;
        for(uint64_t offset=0;offset<read->buffer.size;offset+=read->bpb){float values[4];channels=graphics_probe_values(texture->format,data+offset,read->bpb,values);for(unsigned c=0;c<channels;c++){uint32_t bits;memcpy(&bits,&values[c],4);if((bits&0x7f800000)==0x7f800000){nonfinite++;continue;}finite[c]++;zero+=values[c]==0;sums[c]+=values[c];if(values[c]<minimum[c])minimum[c]=values[c];if(values[c]>maximum[c])maximum[c]=values[c];}}
        fprintf(index,"%llu\t%#x\t%u\t%u\t%u,%u,%u\t%u,%u,0+%u,%u,%u\t%u\t%u\t%llu\t%llu\t%llu\t",(unsigned long long)read->serial,texture->format,read->mip,read->layer,texture->width,texture->height,texture->depth,read->x,read->y,read->w,read->h,read->d,read->bw==1&&read->bh==1?read->bpb:0,channels,(unsigned long long)(finite[0]+finite[1]+finite[2]+finite[3]),(unsigned long long)nonfinite,(unsigned long long)zero);
        for(unsigned c=0;c<channels;c++)fprintf(index,"%s%g",c?",":"",finite[c]?minimum[c]:0);fputc('\t',index);for(unsigned c=0;c<channels;c++)fprintf(index,"%s%g",c?",":"",finite[c]?maximum[c]:0);fputc('\t',index);for(unsigned c=0;c<channels;c++)fprintf(index,"%s%g",c?",":"",finite[c]?sums[c]/finite[c]:0);fprintf(index,"\t%s\t%u,%u\t%u\n",name,read->bw,read->bh,read->bpb);buffer_destroy(&read->buffer);free(read);
    }
    while(probe_buffers){probe_buffer_read *read=probe_buffers;probe_buffers=read->next;snprintf(path,sizeof path,"%s/%s",probe_directory,read->name);FILE *file=fopen(path,"wb");if(!file)die("Vulkan probe retained input open");fwrite((uint8_t*)read->readback.mapped+(read->offset&3),1,read->size,file);fclose(file);buffer_destroy(&read->source);buffer_destroy(&read->readback);free(read);}
    fclose(index);if(probe_trace){fclose(probe_trace);probe_trace=NULL;}while(probe_images){probe_image *entry=probe_images;probe_images=entry->next;free(entry);}while(probe_dumps){probe_dump_seen *entry=probe_dumps;probe_dumps=entry->next;free(entry);}while(probe_queries){probe_query *entry=probe_queries;probe_queries=entry->next;free(entry);}LOG("[vulkan-probe] frame=%llu captured directory=%s (one-time readback; exclude from FPS)",(unsigned long long)frame,probe_directory);
}

#endif
