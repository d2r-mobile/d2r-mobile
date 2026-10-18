// 选择全窗口未使用的寄存器并核对每条替换；独占加载／存储间仅复制原生指令，不插入宿主门。
// Choose registers unused across the whole window and verify every substitution; copy only native instructions between exclusive load/store, without host gates.
#include "window_decode.h"
#include <capstone/capstone.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t get(const uint8_t*p){return(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static int reg_number(csh h,unsigned reg){const char*n=cs_reg_name(h,reg);if(!n)return-1;
    if(!strcmp(n,"sp")||!strcmp(n,"wsp"))return 31;if(!strcmp(n,"fp"))return 29;if(!strcmp(n,"lr"))return 30;
    if((n[0]!='x'&&n[0]!='w')||!isdigit((unsigned char)n[1]))return-1;char*end;long v=strtol(n+1,&end,10);return*end||v>30?-1:(int)v;}
static int rename18(const char*in,char out[256],unsigned scratch){
    size_t used=0;while(*in){char token[64];size_t n=0;
        if(!isalnum((unsigned char)*in)&&*in!='_'){if(used==255)return E2BIG;out[used++]=*in++;continue;}
        while(isalnum((unsigned char)*in)||*in=='_'){if(n==63)return E2BIG;token[n++]=*in++;}token[n]=0;
        if(!strcmp(token,"x18")||!strcmp(token,"w18"))snprintf(token,sizeof(token),"%c%u",token[0],scratch);
        n=strlen(token);if(n>255-used)return E2BIG;memcpy(out+used,token,n);used+=n;
    }out[used]=0;return 0;
}
static int64_t sign_bits(uint32_t n,unsigned bits){return(int64_t)(n&((1u<<bits)-1))-((n&(1u<<(bits-1)))?(1ll<<bits):0);}
static unsigned branch_bits(uint32_t w){
    if((w&0x7c000000u)==0x14000000u)return 26;
    if((w&0x7e000000u)==0x34000000u||(w&0xff000010u)==0x54000000u)return 19;
    if((w&0x7e000000u)==0x36000000u)return 14;return 0;
}
int dm_decode_window(const dm_window_source*source,dm_window_request*out){
    if(!source||!source->image||!source->image->bytes||!out||!source->count||source->count>DM_WINDOW_MAX||source->offset%4)return EINVAL;
    const dm_nso_image*im=source->image;uint64_t start=im->header.segments[0].memory_offset,end=start+im->header.segments[0].size;
    uint64_t hi=(uint64_t)source->offset+4*source->count;
    if(end>im->header.image_size||source->offset<start||hi>end)return EINVAL;
    csh h;if(cs_open(CS_ARCH_ARM64,CS_MODE_ARM,&h)!=CS_ERR_OK)return ENOMEM;
    if(cs_option(h,CS_OPT_DETAIL,CS_OPT_ON)!=CS_ERR_OK){cs_close(&h);return ENOTSUP;}
    cs_insn*list=NULL;size_t count=cs_disasm(h,im->bytes+source->offset,4*source->count,source->offset,DM_WINDOW_MAX,&list);
    cs_insn*candidate=cs_malloc(h);int result=ENOTSUP,has18=0,stores=0;uint32_t used=0;
    if(!candidate){result=ENOMEM;goto done;}if(count!=source->count)goto done;
    if(strncmp(list[0].mnemonic,"ldxr",4)&&strncmp(list[0].mnemonic,"ldaxr",5)&&
        strncmp(list[0].mnemonic,"ldxp",4)&&strncmp(list[0].mnemonic,"ldaxp",5))goto done;
    for(size_t i=0;i<count;i++){
        cs_insn*ins=&list[i];cs_arm64*a=&ins->detail->arm64;uint32_t w=get(ins->bytes);
        int load=!strncmp(ins->mnemonic,"ldxr",4)||!strncmp(ins->mnemonic,"ldaxr",5)||!strncmp(ins->mnemonic,"ldxp",4)||!strncmp(ins->mnemonic,"ldaxp",5);
        int store=!strncmp(ins->mnemonic,"stxr",4)||!strncmp(ins->mnemonic,"stlxr",5)||!strncmp(ins->mnemonic,"stxp",4)||!strncmp(ins->mnemonic,"stlxp",5);
        if(i&&load)goto done;if(store){stores++;if(i+2<count)goto done;}
        if(ins->id==ARM64_INS_ADR||ins->id==ARM64_INS_ADRP||ins->id==ARM64_INS_MRS||ins->id==ARM64_INS_MSR||
            cs_insn_group(h,ins,CS_GRP_CALL)||cs_insn_group(h,ins,CS_GRP_RET)||cs_insn_group(h,ins,CS_GRP_INT)||
            (cs_insn_group(h,ins,CS_GRP_JUMP)&&!branch_bits(w)))goto done;
        if((ins->id==ARM64_INS_LDR||ins->id==ARM64_INS_LDRSW||ins->id==ARM64_INS_PRFM)&&a->op_count>1&&a->operands[1].type==ARM64_OP_IMM)goto done;
        cs_regs reads,writes;uint8_t nr,nw;if(cs_regs_access(h,ins,reads,&nr,writes,&nw)!=CS_ERR_OK)goto done;
        for(unsigned j=0;j<(unsigned)nr+nw;j++){int n=reg_number(h,j<nr?reads[j]:writes[j-nr]);
            if(n==31)goto done;if(n>=0){used|=1u<<n;has18|=n==18;}}
        for(unsigned j=0;j<a->op_count;j++){
            const cs_arm64_op*op=&a->operands[j];unsigned regs[2]={ARM64_REG_INVALID,ARM64_REG_INVALID};
            if(op->type==ARM64_OP_REG)regs[0]=op->reg;
            else if(op->type==ARM64_OP_MEM){regs[0]=op->mem.base;regs[1]=op->mem.index;}
            for(unsigned k=0;k<2;k++){int n=reg_number(h,regs[k]);if(n==31)goto done;if(n>=0){used|=1u<<n;has18|=n==18;}}
        }
    }
    if(!has18){result=ENOENT;goto done;}if(stores!=1)goto done;
    dm_window_request request={.original_offset=source->offset,.count=source->count};unsigned picked=0;
    for(unsigned i=0;i<16&&picked<3;i++)if(!(used&(1u<<i)))request.scratch[picked++]=i;if(picked!=3)goto done;
    for(size_t i=0;i<count;i++){
        uint32_t word=get(list[i].bytes);request.original[i]=word;char desired[256];
        if(rename18(list[i].op_str,desired,request.scratch[0]))goto done;
        if(!strcmp(desired,list[i].op_str)){request.translated[i]=word;continue;}
        int matched=0;const unsigned positions[]={0,5,10,16};
        for(unsigned subset=1;subset<16&&!matched;subset++){
            uint32_t next=word;int valid=1;for(unsigned j=0;j<4;j++)if(subset&(1u<<j)){
                unsigned p=positions[j];if(((word>>p)&31)!=18){valid=0;break;}next=(next&~(31u<<p))|request.scratch[0]<<p;}
            if(!valid)continue;uint8_t bytes[4];for(unsigned b=0;b<4;b++)bytes[b]=(uint8_t)(next>>(8*b));
            const uint8_t*ptr=bytes;size_t n=4;uint64_t pc=list[i].address;
            if(cs_disasm_iter(h,&ptr,&n,&pc,candidate)&&candidate->size==4&&!strcmp(candidate->mnemonic,list[i].mnemonic)&&!strcmp(candidate->op_str,desired)){
                request.translated[i]=next;matched=1;}
        }if(!matched)goto done;
    }
    for(uint64_t off=start;off+4<=end;off+=4){
        if(off>=source->offset&&off<hi)continue;uint32_t word=get(im->bytes+off);unsigned bits=branch_bits(word);if(!bits)continue;
        int64_t target=(int64_t)off+sign_bits(bits==26?word:word>>5,bits)*4;
        if(target>(int64_t)source->offset&&target<(int64_t)hi)goto done;
    }
    *out=request;result=0;
done:
    if(candidate)cs_free(candidate,1);if(list)cs_free(list,count);cs_close(&h);return result;
}
