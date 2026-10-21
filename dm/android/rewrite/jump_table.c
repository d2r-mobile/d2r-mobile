// 识别 ADR x18、无符号 byte/halfword 索引、ADD x18,x18,index,lsl2 与 BR；仅处理直线控制流。
// Recognize ADR x18, unsigned byte/halfword index, ADD x18,x18,index,lsl2 and BR; handle straight-line control flow only.
#include "jump_table.h"
#include <capstone/capstone.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
static int number(csh h,unsigned reg){
    const char *n=cs_reg_name(h,reg);unsigned v;
    return n && (n[0]=='x'||n[0]=='w') && sscanf(n+1,"%u",&v)==1 && v<31 ? (int)v:-1;
}
int dm_recognize_jump(const dm_nso_image *im,uint32_t off,dm_jump_table *out){
    if(!im || !im->bytes || !out || off%4)return EINVAL;
    uint64_t start=im->header.segments[0].memory_offset,end=start+im->header.segments[0].size;
    if(end>im->header.image_size || off<start+12 || (uint64_t)off+4>end)return EINVAL;
    uint32_t first=off-(uint32_t)start>32 ? off-32:(uint32_t)start;
    csh h;if(cs_open(CS_ARCH_ARM64,CS_MODE_ARM,&h)!=CS_ERR_OK)return ENOMEM;
    if(cs_option(h,CS_OPT_DETAIL,CS_OPT_ON)!=CS_ERR_OK){cs_close(&h);return ENOTSUP;}
    cs_insn *list=NULL;size_t n=cs_disasm(h,im->bytes+first,off-first+4,first,9,&list);
    int result=ENOTSUP;dm_jump_table table={0};
    if(!n || list[n-1].address!=off || list[n-1].id!=ARM64_INS_BR ||
        list[n-1].detail->arm64.op_count!=1 || number(h,list[n-1].detail->arm64.operands[0].reg)!=18)goto done;
    int index=-1;size_t add=n,load=n,adr=n;
    for(size_t i=n-1;i-->0;){
        cs_insn *ins=&list[i];cs_arm64 *a=&ins->detail->arm64;
        if(cs_insn_group(h,ins,CS_GRP_JUMP)||cs_insn_group(h,ins,CS_GRP_CALL)||cs_insn_group(h,ins,CS_GRP_RET))break;
        cs_regs reads,writes;uint8_t nr,nw;
        if(cs_regs_access(h,ins,reads,&nr,writes,&nw)!=CS_ERR_OK)break;
        int writes18=0,writes_index=0;
        for(unsigned j=0;j<nw;j++){int v=number(h,writes[j]);writes18|=v==18;writes_index|=index>=0 && v==index;}
        if(add==n && writes18){
            if(ins->id!=ARM64_INS_ADD || a->op_count!=3 || a->operands[0].type!=ARM64_OP_REG ||
                a->operands[1].type!=ARM64_OP_REG || a->operands[2].type!=ARM64_OP_REG ||
                number(h,a->operands[0].reg)!=18 || number(h,a->operands[1].reg)!=18 ||
                a->operands[2].shift.type!=ARM64_SFT_LSL || a->operands[2].shift.value!=2)break;
            index=number(h,a->operands[2].reg);if(index<0 || index==18)break;add=i;continue;
        }
        if(add!=n && adr==n && writes18){
            if(ins->id!=ARM64_INS_ADR || a->op_count!=2 || a->operands[1].type!=ARM64_OP_IMM)break;
            int64_t target=a->operands[1].imm;
            if(target<0 || (uint64_t)target>UINT32_MAX || target%4)break;
            table.table_offset=(uint32_t)target;adr=i;
        }
        if(add!=n && load==n && writes_index){
            if((ins->id!=ARM64_INS_LDRB && ins->id!=ARM64_INS_LDRH) || a->op_count<2 ||
                a->operands[0].type!=ARM64_OP_REG || number(h,a->operands[0].reg)!=index ||
                a->operands[1].type!=ARM64_OP_MEM)break;
            table.entries=ins->id==ARM64_INS_LDRB ? 256:65536;load=i;
        }
        if(adr!=n && load!=n){
            if(table.table_offset>=start && (uint64_t)table.table_offset+4ull*table.entries<=end){*out=table;result=0;}break;
        }
    }
done:
    if(list)cs_free(list,n);cs_close(&h);return result;
}
