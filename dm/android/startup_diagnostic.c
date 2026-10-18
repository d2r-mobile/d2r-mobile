// 诊断函数隔离；严格生产准备器不变，所有范围来自原始 MOD0/EH 元数据。
// Diagnostic function quarantine; production preparation stays strict, all ranges derive from original MOD0/EH metadata.
#include "startup_diagnostic.h"
#include "rewrite/prepare.h"
#include "rewrite/exclusive.h"
#include "runtime/accounting.h"
#include "runtime/internal.h"
#include <capstone/capstone.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define TRAP 0xd421e820u
#define MODULES 16
#define EXTENTS 256
struct dm_startup_diagnostic {
    size_t count;dm_rewrite_output prepared[MODULES];
    dm_diag_extent extents[MODULES][EXTENTS];size_t extent_count[MODULES];
    const dm_module_mappings *mapped;dm_gate_site *sites;dm_gate_table gates;
    dm_host *host;dm_memory_pin pins[MODULES];
    pid_t creator;
};
static uint32_t u32(const unsigned char*p){return(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static int range(size_t total,uint64_t off,uint64_t size){return off<=total&&size<=total-off;}
static int relative(size_t total,uint64_t base,int32_t delta,uint64_t *out){
    int64_t address=(int64_t)base+delta;if(address<0||!range(total,(uint64_t)address,0))return EINVAL;*out=(uint64_t)address;return 0;
}
static int leb(const unsigned char*b,uint64_t *p,uint64_t end,uint64_t *out){
    uint64_t value=0;for(unsigned shift=0;shift<64;shift+=7){if(*p>=end)return EINVAL;unsigned c=b[(*p)++];
        if(shift==63&&(c&0x7e))return EINVAL;value|=(uint64_t)(c&127)<<shift;if(!(c&128)){*out=value;return 0;}}
    return EINVAL;
}
static int fde(const dm_nso_image*im,uint64_t address,dm_diag_extent*out){
    const unsigned char*b=im->bytes;size_t n=im->header.image_size;
    if(!range(n,address,16))return EINVAL;uint32_t length=u32(b+address);
    if(length<12||length==UINT32_MAX||!range(n,address+4,length))return EINVAL;
    uint32_t back=u32(b+address+4);if(back>address+4)return EINVAL;uint64_t cie=address+4-back;
    if(!range(n,cie,12))return EINVAL;uint32_t cl=u32(b+cie);if(cl<8||cl==UINT32_MAX||!range(n,cie+4,cl)||u32(b+cie+4))return EINVAL;
    uint64_t p=cie+8,end=cie+4+cl;unsigned version=b[p++];if(version!=1&&version!=3)return ENOTSUP;
    uint64_t aug=p;while(p<end&&b[p])p++;if(p==end||p-aug>16||b[aug]!='z')return ENOTSUP;p++;
    uint64_t ignored,ret;if(leb(b,&p,end,&ignored)||leb(b,&p,end,&ignored))return EINVAL;
    if(version==1){if(p>=end)return EINVAL;ret=b[p++];}else if(leb(b,&p,end,&ret))return EINVAL;
    if(ret!=30)return ENOTSUP;uint64_t bytes;if(leb(b,&p,end,&bytes)||bytes>end-p)return EINVAL;end=p+bytes;unsigned encoding=0;
    for(uint64_t a=aug+1;b[a];a++){
        if(p>=end)return EINVAL;
        if(b[a]=='R')encoding=b[p++];else if(b[a]=='L')p++;
        else if(b[a]=='P'){
            unsigned e=b[p++],format=e&15;size_t width=format==0?8:format==2||format==10?2:format==3||format==11?4:format==4||format==12?8:0;
            if(!width||width>end-p)return ENOTSUP;p+=width;
        }else return ENOTSUP;
    }
    if(p!=end||encoding!=0x1b)return ENOTSUP;
    uint64_t start;if(relative(n,address+8,(int32_t)u32(b+address+8),&start))return EINVAL;
    uint32_t span=u32(b+address+12);uint64_t text=im->header.segments[0].memory_offset,text_size=im->header.segments[0].size;
    if(!span||start%4||span%4||start<text||!range(text_size,start-text,span)||start+span>UINT32_MAX)return EINVAL;
    *out=(dm_diag_extent){(uint32_t)start,(uint32_t)(start+span)};return 0;
}
static int containing(const dm_nso_image*im,uint32_t root,dm_diag_extent*out){
    const unsigned char*b=im->bytes;size_t n=im->header.image_size;if(n<8)return EINVAL;
    uint32_t mod=u32(b+4);if(!range(n,mod,28)||memcmp(b+mod,"MOD0",4))return EINVAL;
    uint64_t begin,end;if(relative(n,mod,(int32_t)u32(b+mod+16),&begin)||relative(n,mod,(int32_t)u32(b+mod+20),&end)||end<begin||end-begin<12)return EINVAL;
    if(memcmp(b+begin,"\1\33\3\73",4))return ENOTSUP;uint32_t count=u32(b+begin+8);
    if(!count||count>(end-begin-12)/8)return EINVAL;
    unsigned found=0;uint32_t previous_end=0;
    for(uint32_t i=0;i<count;i++){
        uint64_t at=begin+12+8ull*i,start,entry;
        if(relative(n,begin,(int32_t)u32(b+at),&start)||relative(n,begin,(int32_t)u32(b+at+4),&entry))return EINVAL;
        dm_diag_extent candidate;int rc=fde(im,entry,&candidate);if(rc)return rc;
        if(start!=candidate.begin||(i&&candidate.begin<previous_end))return EINVAL;previous_end=candidate.end;
        if(root>=candidate.begin&&root<candidate.end){*out=candidate;found++;}
    }
    if(found!=1)return EINVAL;
    if(begin<out->end&&out->begin<end)return ENOTSUP;
    for(uint32_t i=0;i<count;i++){
        uint64_t entry;
        if(relative(n,begin,(int32_t)u32(b+begin+16+8ull*i),&entry))return EINVAL;
        uint64_t cie=entry+4-u32(b+entry+4);
        if((entry<out->end&&out->begin<entry+4+u32(b+entry))||
           (cie<out->end&&out->begin<cie+4+u32(b+cie)))return ENOTSUP;
    }
    return 0;
}
static int load(const char*m){return !strncmp(m,"ldxr",4)||!strncmp(m,"ldaxr",5)||!strncmp(m,"ldxp",4)||!strncmp(m,"ldaxp",5);}
static int store(const char*m){return !strncmp(m,"stxr",4)||!strncmp(m,"stlxr",5)||!strncmp(m,"stxp",4)||!strncmp(m,"stlxp",5);}
static int decode(csh h,cs_insn*i,const dm_nso_image*im,uint32_t off){const uint8_t*p=im->bytes+off;size_t n=4;uint64_t pc=off;return cs_disasm_iter(h,&p,&n,&pc,i)&&i->size==4;}
static int quarantine(const dm_nso_image*im,size_t expected,dm_diag_extent*out,size_t*count){
    csh h;if(cs_open(CS_ARCH_ARM64,CS_MODE_ARM,&h)!=CS_ERR_OK)return ENOMEM;
    cs_insn*ins=cs_malloc(h),*look=cs_malloc(h);dm_decoder*d=dm_decoder_create();int rc=ENOMEM;size_t roots=0;
    if(!ins||!look||!d)goto done;uint32_t end=im->header.segments[0].memory_offset+im->header.segments[0].size,window_end=0;
    for(uint32_t off=im->header.segments[0].memory_offset;off<end;off+=4){
        if(off<window_end||!decode(h,ins,im,off)||!load(ins->mnemonic))continue;
        uint32_t finish=0;for(unsigned j=1;j<16&&off+4*j<end;j++){
            if(!decode(h,look,im,off+4*j))break;if(store(look->mnemonic)){finish=off+4*(j+1);break;}if(load(look->mnemonic))break;
        }
        if(finish){if(finish<end&&decode(h,look,im,finish)&&(!strcmp(look->mnemonic,"cbz")||!strcmp(look->mnemonic,"cbnz")))finish+=4;window_end=finish;continue;}
        if(!dm_exclusive_untouched(im,off,d))continue;roots++;
        dm_diag_extent e;rc=containing(im,off,&e);if(rc)goto done;size_t i=0;while(i<*count&&out[i].begin!=e.begin)i++;
        if(i==*count){if(*count==EXTENTS){rc=E2BIG;goto done;}out[(*count)++]=e;}
    }
    rc=roots==expected?0:ENOTSUP;if(rc)goto done;
    for(uint32_t off=im->header.segments[0].memory_offset;off<end;off+=4){
        uint32_t w=u32(im->bytes+off);int64_t target=-1;size_t width=16;
        if((w&0x3b000000u)==0x18000000u){int32_t delta=(int32_t)((w>>5)&0x7ffffu);if(delta&0x40000)delta-=0x80000;target=(int64_t)off+4ll*delta;}
        else if(w==0x94000002u){target=(int64_t)off+4;width=4;}
        if(target>=0)for(size_t i=0;i<*count;i++)if((uint64_t)target<out[i].end&&out[i].begin<(uint64_t)target+width){rc=ENOTSUP;goto done;}
    }
    if(im->header.image_size>=8){uint32_t mod=u32(im->bytes+4);
        for(size_t i=0;i<*count;i++)if(mod<out[i].end&&out[i].begin<(uint64_t)mod+28){rc=ENOTSUP;goto done;}}
    window_end=0;
    for(uint32_t off=im->header.segments[0].memory_offset;off<end;off+=4){
        if(off<window_end||!decode(h,ins,im,off)||!load(ins->mnemonic))continue;
        uint32_t finish=0;for(unsigned j=1;j<16&&off+4*j<end;j++){
            if(!decode(h,look,im,off+4*j))break;if(store(look->mnemonic)){finish=off+4*(j+1);break;}if(load(look->mnemonic))break;
        }
        if(!finish)continue;
        if(finish<end&&decode(h,look,im,finish)&&(!strcmp(look->mnemonic,"cbz")||!strcmp(look->mnemonic,"cbnz")))finish+=4;
        window_end=finish;
        for(size_t i=0;i<*count;i++)if(off<out[i].end&&out[i].begin<finish&&(off<out[i].begin||finish>out[i].end)){rc=ENOTSUP;goto done;}
    }
 done:if(ins)cs_free(ins,1);if(look)cs_free(look,1);dm_decoder_free(d);cs_close(&h);return rc;
}
int dm_diag_free(dm_startup_diagnostic*s){
    if(!s)return 0;
    if(s->host){pthread_mutex_lock(&s->host->mutex);
        if(s->host->attached||dm_host_sync_busy(s->host)){pthread_mutex_unlock(&s->host->mutex);return EBUSY;}
        for(size_t i=0;i<s->count;i++)dm_host_memory_unpin_locked(s->host,&s->pins[i]);
        pthread_mutex_unlock(&s->host->mutex);}
    for(size_t i=0;i<s->count;i++)dm_rewrite_free(&s->prepared[i]);free(s->sites);free(s);return 0;
}
int dm_diag_prepare(const dm_nso_image*images,size_t count,uintptr_t gate,dm_startup_diagnostic**out){
    if(!images||!out||!count||count>MODULES||!gate)return EINVAL;long page=sysconf(_SC_PAGESIZE);if(page<=0)return EINVAL;
    dm_startup_diagnostic*s=calloc(1,sizeof(*s));if(!s)return ENOMEM;s->count=count;s->creator=getpid();int rc=0;
    for(size_t i=0;i<count;i++){
        const dm_nso_image*im=&images[i];uint64_t stub=((uint64_t)im->header.image_size+(size_t)page-1)&~((uint64_t)page-1);if(stub>UINT32_MAX){rc=EOVERFLOW;goto fail;}
        dm_rewrite_report report;rc=dm_rewrite_prepare(im,(uint32_t)stub,gate,&report,NULL);
        if(rc&&rc!=ENOTSUP)goto fail;
        if(report.window_sites||report.pcrel||report.branches||report.other||report.undecodable||report.emitter_errors||report.unsupported_system||report.metadata_entries||report.literal_conflicts){rc=ENOTSUP;goto fail;}
        dm_nso_image copy={.header=im->header};copy.bytes=malloc(im->header.image_size);if(!copy.bytes){rc=ENOMEM;goto fail;}memcpy(copy.bytes,im->bytes,im->header.image_size);
        if(report.unmatched)rc=quarantine(im,report.unmatched,s->extents[i],&s->extent_count[i]);else rc=0;
        if(!rc){for(size_t j=0;j<s->extent_count[i];j++)for(uint32_t off=s->extents[i][j].begin;off<s->extents[i][j].end;off+=4){uint32_t trap=TRAP;memcpy(copy.bytes+off,&trap,4);}
            rc=dm_rewrite_prepare(&copy,(uint32_t)stub,gate,&report,&s->prepared[i]);}
        free(copy.bytes);if(rc)goto fail;
    }
    *out=s;return 0;
 fail:dm_diag_free(s);return rc;
}
static int compare(const void*a,const void*b){uintptr_t x=((const dm_gate_site*)a)->return_address,y=((const dm_gate_site*)b)->return_address;return x<y?-1:x>y;}
int dm_diag_map(dm_startup_diagnostic*s,dm_host*h){
    if(!s||!h||s->mapped)return EINVAL;if(getpid()==s->creator)return EPERM;dm_nso_image images[MODULES];dm_image_code codes[MODULES];size_t count=0;
    for(size_t i=0;i<s->count;i++){images[i]=s->prepared[i].image;codes[i]=s->prepared[i].code;count+=s->prepared[i].count;}
    dm_gate_site*sites=calloc(count?count:1,sizeof(*sites));if(!sites)return ENOMEM;
    const dm_module_mappings*mapped=NULL;int rc=dm_host_map_modules(h,images,codes,s->count,NULL,0,&mapped);if(rc){free(sites);return rc;}
    pthread_mutex_lock(&h->mutex);
    for(size_t i=0;i<s->count;i++){
        rc=dm_host_memory_read_pin_locked(h,&s->pins[i],(uintptr_t)mapped->mappings[i].base+images[i].header.segments[0].memory_offset,1);
        if(!rc&&!s->pins[i].active)rc=EINVAL;
        if(rc){for(size_t j=0;j<=i;j++)dm_host_memory_unpin_locked(h,&s->pins[j]);pthread_mutex_unlock(&h->mutex);free(sites);return rc;}
    }
    pthread_mutex_unlock(&h->mutex);s->host=h;
    size_t at=0;for(size_t i=0;i<s->count;i++)for(size_t j=0;j<s->prepared[i].count;j++){sites[at]=s->prepared[i].sites[j];sites[at++].return_address+=(uintptr_t)mapped->mappings[i].base;}
    qsort(sites,count,sizeof(*sites),compare);s->mapped=mapped;s->sites=sites;s->gates=(dm_gate_table){sites,count};return 0;
}
int dm_diag_address(const dm_startup_diagnostic*s,size_t module,uint32_t offset,size_t size,uintptr_t*out){
    if(!s||!s->mapped||!out||module>=s->count||!size||offset%4||getpid()==s->creator)return EINVAL;
    dm_nso_segment text=s->prepared[module].image.header.segments[0];
    if(offset<text.memory_offset||!range(text.size,offset-text.memory_offset,size))return EINVAL;
    uintptr_t address=(uintptr_t)s->mapped->mappings[module].base+offset;int allowed=0;
    pthread_mutex_lock(&s->host->mutex);
    for(size_t i=0;i<s->host->executable_count;i++){dm_host_region r=s->host->executable[i];if(address>=r.base&&range(r.size,address-r.base,size))allowed=1;}
    pthread_mutex_unlock(&s->host->mutex);if(!allowed)return EINVAL;*out=address;return 0;
}
int dm_diag_in_child(const dm_startup_diagnostic*s){return s&&getpid()!=s->creator;}
uintptr_t dm_diag_setup_site(const dm_startup_diagnostic*s){
    if(!s||!s->mapped)return 0;uintptr_t found=0;
    for(size_t i=0;i<s->gates.count;i++)if(s->sites[i].kind==DM_GATE_SVC&&s->sites[i].argument==0xfffe){if(found)return 0;found=s->sites[i].return_address;}
    return found;
}
const dm_gate_table*dm_diag_gates(const dm_startup_diagnostic*s){return s&&s->mapped?&s->gates:NULL;}
size_t dm_diag_quarantines(const dm_startup_diagnostic*s,size_t module,const dm_diag_extent**out){if(!s||module>=s->count||!out)return 0;*out=s->extents[module];return s->extent_count[module];}
