// 审计与内存产物共用同一扫描；依据原 scan.c / 各跳板生成器，始终从未修改的输入解码。
// Share one scan for auditing and in-memory artifacts; based on scan.c / stub emitters, always decode unchanged input.
#include "prepare.h"
#include "decode.h"
#include "jump_table.h"
#include "window_decode.h"
#include "exclusive.h"
#include "../runtime/patch.h"
#include "../runtime/x18_control.h"
#include "../runtime/x18_jump.h"
#include <capstone/capstone.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static uint32_t word_at(const uint8_t*p){return(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static int64_t sign_bits(uint32_t n,unsigned bits){return(int64_t)(n&((1u<<bits)-1))-((n&(1u<<(bits-1)))?(1ll<<bits):0);}
// 只标记有读取证据且无已知直接入口的两种单字文字池；不把反汇编失败或展开表缺口当数据证据。
// Mark only two single-word literal forms with read evidence and no known direct entry; failed decoding / unwind gaps are not data evidence.
static uint8_t*literal_map(const dm_nso_image*im,uint32_t start,uint32_t end,uint32_t mod_start,uint32_t mod_end){
    size_t count=(end-start)/4;uint8_t*flags=calloc(count?count:1,1);if(!flags)return NULL;
    for(uint32_t off=start;off<end;off+=4){
        if(off>=mod_start&&off<mod_end)continue;
        uint32_t w=word_at(im->bytes+off);unsigned bits=0;
        if((w&0x7c000000u)==0x14000000u)bits=26;
        else if((w&0x7e000000u)==0x34000000u||(w&0xff000010u)==0x54000000u)bits=19;
        else if((w&0x7e000000u)==0x36000000u)bits=14;
        if(bits){int64_t target=(int64_t)off+4*sign_bits(bits==26?w:w>>5,bits);
            if(target>=start&&target<end)flags[(target-start)/4]|=2;}
        if((w&0xff000000u)==0x18000000u&&(w&31)!=31){
            int64_t target=(int64_t)off+4*sign_bits(w>>5,19);
            if(target>off&&target<end)flags[(target-start)/4]|=1;
        }
        if((w&0x1f000000u)==0x10000000u){
            int64_t delta=sign_bits(((w>>5)&0x7ffffu)<<2|((w>>29)&3),21);
            int page=(w>>31)!=0;int64_t target=page?((int64_t)off&~4095ll)+delta*4096:(int64_t)off+delta;
            int64_t limit=target+(page?4096:1);
            if(target<start)target=start;if(limit>end)limit=end;
            for(int64_t p=target; p<limit;p+=4)if(p>=start&&p<end)flags[(p-start)/4]|=2;
        }
    }
    for(uint32_t off=start;off<end;off+=4){
        if(off>=mod_start&&off<mod_end)continue;
        size_t index=(off-start)/4;int pool=0,relative=0;
        if(off>=start+4&&off+4<end&&!(off-4<mod_end&&mod_start<off+8)&&(flags[index]&1)&&word_at(im->bytes+off-4)==0xd65f03c0u&&
            word_at(im->bytes+off+4)==0xd503201fu)pool=1;
        if(off>=start+4&&end-off>=16&&!(off-4<mod_end&&mod_start<off+16)&&word_at(im->bytes+off-4)==0x94000002u){
            uint32_t load=word_at(im->bytes+off+4),extend=word_at(im->bytes+off+8),add=word_at(im->bytes+off+12);
            unsigned src=load&31,dst=extend&31;
            int64_t target=(int64_t)off+(int32_t)word_at(im->bytes+off);
            uint64_t data_start=im->header.segments[2].memory_offset;
            uint64_t data_end=data_start+im->header.segments[2].size+im->header.bss_size;
            if((load&0xffffffe0u)==0xb94003c0u&&src<30&&dst<30&&
                extend==(0x93407c00u|src<<5|dst)&&(add&0xffffffe0u)==(0x8b1e0000u|dst<<5)&&
                (add&31)!=31&&target>=(int64_t)data_start&&target+4<=(int64_t)data_end&&
                target+4<=(int64_t)im->header.image_size)relative=1;
        }
        if(pool||relative)flags[index]|=(flags[index]&2)?8:4;
    }
    return flags;
}
static int is_load(const char*m){return !strncmp(m,"ldxr",4)||!strncmp(m,"ldaxr",5)||!strncmp(m,"ldxp",4)||!strncmp(m,"ldaxp",5);}
static int is_store(const char*m){return !strncmp(m,"stxr",4)||!strncmp(m,"stlxr",5)||!strncmp(m,"stxp",4)||!strncmp(m,"stlxp",5);}
static int instruction(csh h,cs_insn*i,const dm_nso_image*im,uint32_t off,uint32_t end){
    if(off>end||end-off<4)return 0;const uint8_t*p=im->bytes+off;size_t n=4;uint64_t pc=off;
    return cs_disasm_iter(h,&p,&n,&pc,i)&&i->size==4;
}
typedef struct{dm_rewrite_output output;dm_rewrite_report report;size_t capacity,site_capacity;uint32_t next;int materialize;}builder;
static int append(builder*b,uint32_t off,uint32_t replacement,const uint8_t*stub,size_t size,dm_gate_site site,uint32_t literal){
    if(!size||size%8||b->next>DM_NSO_MAX_IMAGE||size>DM_NSO_MAX_IMAGE-b->next)return EOVERFLOW;
    if(b->materialize){
        size_t used=b->output.code.size;
        if(size>SIZE_MAX-used)return EOVERFLOW;
        if(used+size>b->capacity){
            size_t capacity=b->capacity?b->capacity:4096;
            while(capacity<used+size)capacity*=2;
            void*p=realloc((void*)b->output.code.bytes,capacity);if(!p)return ENOMEM;
            b->output.code.bytes=p;b->capacity=capacity;
        }
        if(b->output.count==b->site_capacity){
            size_t capacity=b->site_capacity?b->site_capacity*2:64;
            if(capacity>SIZE_MAX/sizeof(dm_gate_site))return EOVERFLOW;
            void*p=realloc(b->output.sites,capacity*sizeof(dm_gate_site));if(!p)return ENOMEM;
            b->output.sites=p;p=realloc(b->output.gate_literals,capacity*sizeof(uint32_t));if(!p)return ENOMEM;
            b->output.gate_literals=p;b->site_capacity=capacity;
        }
        memcpy((uint8_t*)b->output.code.bytes+used,stub,size);b->output.code.size+=size;
        b->output.gate_literals[b->output.count]=(uint32_t)used+literal;
        b->output.sites[b->output.count++]=site;
        for(unsigned n=0;n<4;n++)b->output.image.bytes[off+n]=(uint8_t)(replacement>>(8*n));
    }
    b->next+=(uint32_t)size;b->report.stub_bytes+=size;b->report.sites++;return 0;
}
static int system_not_native(csh h,const cs_insn*ins,uint32_t word){
    if(ins->id==ARM64_INS_MRS||ins->id==ARM64_INS_MSR){
        switch(word&0xffffffe0u){
        case 0xd53b4200u:case 0xd51b4200u:case 0xd53b4400u:case 0xd51b4400u:
        // 原生缓存维护需要宿主 CTR；依据 Android compiler-rt clear_cache.c，不保存 Switch／开发机常量。
        // Native cache maintenance needs host CTR; based on Android compiler-rt clear_cache.c, never store Switch / development-host constants.
        case 0xd53b4420u:case 0xd51b4420u:case 0xd53b0020u:return 0;
        default:return 1;
        }
    }
    return cs_insn_group(h,ins,CS_GRP_PRIVILEGE);
}
int dm_rewrite_prepare(const dm_nso_image*image,uint32_t stub_offset,uintptr_t host_gate,
                       dm_rewrite_report*report,dm_rewrite_output*out){
    if(!image||!image->bytes||!report||!host_gate||!image->header.image_size||image->header.image_size>DM_NSO_MAX_IMAGE||
        stub_offset<image->header.image_size||stub_offset>DM_NSO_MAX_IMAGE||stub_offset%8)return EINVAL;
    uint32_t start=image->header.segments[0].memory_offset,size=image->header.segments[0].size;
    if(start%4||size%4||start>image->header.image_size||size>image->header.image_size-start)return EINVAL;
    builder b={.next=stub_offset,.materialize=out!=NULL};b.output.code.offset=stub_offset;
    csh handle=0;cs_insn*ins=NULL,*look=NULL;dm_decoder*decoder=NULL;uint8_t*literals=NULL;int status=ENOMEM;
    if(cs_open(CS_ARCH_ARM64,CS_MODE_ARM,&handle)!=CS_ERR_OK)goto done;
    if(cs_option(handle,CS_OPT_DETAIL,CS_OPT_ON)!=CS_ERR_OK)goto done;
    ins=cs_malloc(handle);look=cs_malloc(handle);decoder=dm_decoder_create();
    if(!ins||!look||!decoder)goto done;
    if(out){b.output.image.header=image->header;b.output.image.bytes=malloc(image->header.image_size);
        if(!b.output.image.bytes)goto done;memcpy(b.output.image.bytes,image->bytes,image->header.image_size);}
    uint32_t end=start+size,window_end=0,mod_start=0,mod_end=0;int window_supported=0;
    dm_nso_image view=*image;dm_module module;
    if(dm_module_parse(&view,0,&module)==DM_MODULE_OK){
        uint32_t mod=word_at(image->bytes+4);
        if(mod>=start&&(uint64_t)mod+28<=end){mod_start=mod;mod_end=mod+28;
            for(uint32_t off=start;off<end;off+=4){
                uint32_t w=word_at(image->bytes+off);unsigned bits=0;
                if((w&0x7c000000u)==0x14000000u)bits=26;
                else if((w&0x7e000000u)==0x34000000u||(w&0xff000010u)==0x54000000u)bits=19;
                else if((w&0x7e000000u)==0x36000000u)bits=14;
                if(!bits)continue;uint32_t n=(bits==26?w:w>>5)&((1u<<bits)-1);
                int64_t delta=(int64_t)n-((n&(1u<<(bits-1)))?(1ll<<bits):0),target=(int64_t)off+delta*4;
                if(target>=mod_start&&target<mod_end)b.report.metadata_entries++;
            }
        }
    }
    literals=literal_map(image,start,end,mod_start,mod_end);if(!literals)goto done;
    for(uint32_t off=start;off<end;off+=4){
        if(off>=mod_start&&off<mod_end){b.report.metadata_words++;continue;}
        if(literals[(off-start)/4]&8){b.report.literal_conflicts++;continue;}
        if(literals[(off-start)/4]&4){b.report.literal_words++;continue;}
        if(!instruction(handle,ins,image,off,end)){
            // ARM ARM C4.1：该字的顶层 op1=0011 未分配；原样保留陷阱行为，不能把任意未知指令当填充。
            // ARM ARM C4.1: this word has unallocated top-level op1=0011; retain its trap behavior, never treat arbitrary unknown instructions as padding.
            if(word_at(image->bytes+off)==0xe7ffdefeu)b.report.retained_traps++;
            else b.report.undecodable++;continue;
        }
        if(is_load(ins->mnemonic)&&off>=window_end){
            uint32_t finish=0;int maybe18=strstr(ins->op_str,"x18")!=NULL||strstr(ins->op_str,"w18")!=NULL;
            window_supported=0;
            for(unsigned n=1;n<16&&off+n*4<end;n++){
                if(!instruction(handle,look,image,off+n*4,end))break;
                maybe18|=strstr(look->op_str,"x18")!=NULL||strstr(look->op_str,"w18")!=NULL;
                if(is_store(look->mnemonic)){finish=off+(n+1)*4;break;}if(is_load(look->mnemonic))break;
            }
            if(finish){
                if(instruction(handle,look,image,finish,end)&&(!strcmp(look->mnemonic,"cbz")||!strcmp(look->mnemonic,"cbnz"))){
                    maybe18|=strstr(look->op_str,"x18")!=NULL||strstr(look->op_str,"w18")!=NULL;finish+=4;}
                window_end=finish;b.report.windows++;
                if(maybe18){
                    dm_window_source source={image,off,(finish-off)/4};dm_window_request decoded;
                    if(!dm_decode_window(&source,&decoded)){
                        decoded.stub_offset=b.next;decoded.host_gate=host_gate;dm_window_patch patch;
                        if(!dm_patch_x18_window(&decoded,&patch)){
                            status=append(&b,off,patch.replacement,patch.stub,patch.size,patch.site,(uint32_t)patch.size-8);if(status)goto done;
                            window_supported=1;b.report.rewritten_windows++;
                        }else b.report.emitter_errors++;
                    }
                }
            }else if(!dm_exclusive_untouched(image,off,decoder))b.report.native_exclusive++;
            else b.report.unmatched++;
        }
        uint32_t word=word_at(image->bytes+off);
        dm_patch_request request={.instruction=word,.original_offset=off,.stub_offset=b.next,.host_gate=host_gate};
        dm_patch_output system;int sr=dm_patch_system(&request,&system);
        if(!sr){
            if(off<window_end){if(window_supported)b.report.rewritten_sites++;else b.report.window_sites++;}
            else{status=append(&b,off,system.replacement,system.stub,sizeof(system.stub),system.site,24);if(status)goto done;b.report.systems++;}
            continue;
        }
        if(sr!=ENOTSUP){b.report.emitter_errors++;continue;}
        if(system_not_native(handle,ins,word)){b.report.unsupported_system++;continue;}
        dm_control_request control_request={.instruction=word,.original_offset=off,.stub_offset=b.next,.host_gate=host_gate};
        dm_control_patch control;int cr=dm_patch_x18_control(&control_request,&control);
        if(!cr){
            if(off<window_end){if(window_supported)b.report.rewritten_sites++;else b.report.window_sites++;}
            else{status=append(&b,off,control.replacement,control.stub,sizeof(control.stub),control.site,80);if(status)goto done;
                if((word&0x1f000000u)==0x10000000u)b.report.addresses++;else b.report.conditionals++;}continue;
        }
        if(cr!=ENOTSUP){b.report.emitter_errors++;continue;}
        dm_decode_input input={.word=word,.offset=off};dm_x18_request decoded;int result=dm_decode_x18(decoder,&input,&decoded);
        if(result==ENOENT)continue;
        if(off<window_end){if(window_supported)b.report.rewritten_sites++;else b.report.window_sites++;continue;}
        if(!result){
            decoded.stub_offset=b.next;decoded.host_gate=host_gate;dm_x18_patch patch;
            if(dm_patch_x18(&decoded,&patch))b.report.emitter_errors++;
            else{status=append(&b,off,patch.replacement,patch.stub,sizeof(patch.stub),patch.site,56);if(status)goto done;b.report.plain++;}
        }else if(ins->id==ARM64_INS_ADR||ins->id==ARM64_INS_ADRP)b.report.pcrel++;
        else if(cs_insn_group(handle,ins,CS_GRP_JUMP)||cs_insn_group(handle,ins,CS_GRP_CALL)||cs_insn_group(handle,ins,CS_GRP_RET)){
            dm_jump_table table;int supported=0;
            if(word==0xd61f0240u&&!dm_recognize_jump(image,off,&table)){
                dm_jump_request jump={.original_offset=off,.stub_offset=b.next,.table_offset=table.table_offset,
                    .entries=table.entries,.host_gate=host_gate};dm_jump_patch patch={0};
                if(!dm_patch_x18_jump(&jump,&patch)){
                    status=append(&b,off,patch.replacement,patch.stub,patch.size,patch.site,96);
                    dm_jump_patch_free(&patch);if(status)goto done;supported=1;b.report.jumps++;
                }else b.report.emitter_errors++;
            }if(!supported)b.report.branches++;
        }else b.report.other++;
    }
    status=(b.report.window_sites||b.report.unmatched||b.report.pcrel||b.report.branches||b.report.other||
        b.report.undecodable||b.report.emitter_errors||b.report.unsupported_system||b.report.metadata_entries||b.report.literal_conflicts)?ENOTSUP:0;
    if(!status&&out){
        if(!b.output.code.size)b.output.code=(dm_image_code){0};
        *out=b.output;memset(&b.output,0,sizeof(b.output));
    }
done:
    *report=b.report;dm_rewrite_free(&b.output);dm_decoder_free(decoder);free(literals);
    if(ins)cs_free(ins,1);if(look)cs_free(look,1);if(handle)cs_close(&handle);return status;
}
