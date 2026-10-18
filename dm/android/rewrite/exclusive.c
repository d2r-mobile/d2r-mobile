// 最多遍历 256 个不同指令及起点前后 4 KiB；依据 exclusive.h，以原字节解码，所有分支路径都必须可检查。
// Visit at most 256 distinct instructions within 4 KiB of the root; based on exclusive.h, decode original bytes and inspect every branch path.
#include "exclusive.h"
#include <capstone/capstone.h>
#include <errno.h>
#include <string.h>
static int load(const char*m){return !strncmp(m,"ldxr",4)||!strncmp(m,"ldaxr",5)||!strncmp(m,"ldxp",4)||!strncmp(m,"ldaxp",5);}
static int store(const char*m){return !strncmp(m,"stxr",4)||!strncmp(m,"stlxr",5)||!strncmp(m,"stxp",4)||!strncmp(m,"stlxp",5);}
static int64_t sign_bits(uint32_t n,unsigned bits){return(int64_t)(n&((1u<<bits)-1))-((n&(1u<<(bits-1)))?(1ll<<bits):0);}
static uint32_t word_at(const uint8_t*p){return(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static int svc(uint32_t word){return(word&0xffe0001fu)==0xd4000001u;}
int dm_exclusive_untouched(const dm_nso_image*im,uint32_t root,dm_decoder*decoder){
    if(!im||!im->bytes||!decoder||root%4)return EINVAL;
    uint64_t start=im->header.segments[0].memory_offset,end=start+im->header.segments[0].size;
    if(end>im->header.image_size||root<start||(uint64_t)root+4>end)return EINVAL;
    csh h;if(cs_open(CS_ARCH_ARM64,CS_MODE_ARM,&h)!=CS_ERR_OK)return ENOMEM;
    if(cs_option(h,CS_OPT_DETAIL,CS_OPT_ON)!=CS_ERR_OK){cs_close(&h);return ENOTSUP;}
    cs_insn*ins=cs_malloc(h);int result=ENOTSUP;if(!ins){cs_close(&h);return ENOMEM;}
    uint32_t queue[256]={root};size_t count=1;
    for(size_t i=0;i<count;i++){
        uint32_t off=queue[i];const uint8_t*p=im->bytes+off;size_t length=4;uint64_t pc=off;
        if(!cs_disasm_iter(h,&p,&length,&pc,ins)||ins->size!=4)goto done;
        if(!i&&!load(ins->mnemonic))goto done;
        if(i&&(load(ins->mnemonic)||ins->id==ARM64_INS_CLREX))continue;
        uint32_t word=word_at(im->bytes+off);
        // 系统跳板在返回前执行 CLREX；仅接受直接 SVC 或直接调用首指令为 SVC 的入口。
        // System stubs execute CLREX before returning; accept only direct SVC or a direct call whose first instruction is SVC.
        if(i&&svc(word))continue;
        if(i&&(word&0xfc000000u)==0x94000000u){
            int64_t target=(int64_t)off+4*sign_bits(word,26);
            if(target>=(int64_t)start&&target+4<=(int64_t)end&&svc(word_at(im->bytes+target)))continue;
        }
        dm_decode_input input={word,off};dm_x18_request decoded;
        if(dm_decode_x18(decoder,&input,&decoded)!=ENOENT||ins->id==ARM64_INS_MRS||ins->id==ARM64_INS_MSR||
            cs_insn_group(h,ins,CS_GRP_CALL)||cs_insn_group(h,ins,CS_GRP_RET)||
            cs_insn_group(h,ins,CS_GRP_INT)||cs_insn_group(h,ins,CS_GRP_PRIVILEGE))goto done;
        if(store(ins->mnemonic))continue;
        int64_t successors[2]={(int64_t)off+4,0};unsigned n=1,bits=0;
        if((word&0xfc000000u)==0x14000000u)bits=26;
        else if((word&0x7e000000u)==0x34000000u||(word&0xff000010u)==0x54000000u)bits=19;
        else if((word&0x7e000000u)==0x36000000u)bits=14;
        if(bits){int64_t target=(int64_t)off+4*sign_bits(bits==26?word:word>>5,bits);
            if(bits==26)successors[0]=target;else successors[n++]=target;}
        else if(cs_insn_group(h,ins,CS_GRP_JUMP))goto done;
        for(unsigned j=0;j<n;j++){
            int64_t next=successors[j],distance=next-root;
            if(next<(int64_t)start||next+4>(int64_t)end||distance < -4096||distance>4096)goto done;
            size_t k=0;while(k<count&&queue[k]!=(uint32_t)next)k++;
            if(k<count)continue;if(count==256)goto done;queue[count++]=(uint32_t)next;
        }
    }
    result=0;
done:cs_free(ins,1);cs_close(&h);return result;
}
