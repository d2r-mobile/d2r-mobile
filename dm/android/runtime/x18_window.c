// 复制独占窗口并重定向内部／外部条件分支；顺序落出固定使用出口零，不在原子操作中间调用 C。
// Copy exclusive windows and redirect internal/external conditional branches; fall-through always uses exit zero, with no C call inside atomic operations.
#include "x18_window.h"
#include "x18_patch.h"
#include <errno.h>
#include <string.h>
static void put(uint8_t*p,uint32_t w){for(unsigned b=0;b<4;b++)p[b]=(uint8_t)(w>>(8*b));}
static int64_t signed_bits(uint32_t n,unsigned bits){return(int64_t)(n&((1u<<bits)-1))-((n&(1u<<(bits-1)))?(1ll<<bits):0);}
static int branch(uint32_t from,uint32_t to,uint32_t*out){
    int64_t d=(int64_t)to-from;if(d%4||d<-(1ll<<27)||d>=(1ll<<27))return ERANGE;
    *out=0x14000000u|((uint32_t)(d/4)&0x03ffffffu);return 0;
}
int dm_patch_x18_window(const dm_window_request*r,dm_window_patch*out){
    if(!r||!out||!r->count||r->count>DM_WINDOW_MAX||!r->host_gate||r->original_offset%4||r->stub_offset%8||
        r->original_offset>UINT32_MAX-4*r->count||r->stub_offset>UINT32_MAX-512)return EINVAL;
    uint32_t hi=r->original_offset+4*r->count;
    if((uint64_t)r->stub_offset<hi && r->original_offset<(uint64_t)r->stub_offset+512)return EINVAL;
    dm_x18_request prefix_request={.original_offset=r->original_offset,.stub_offset=r->stub_offset,
        .translated=0xd503201f,.scratch={r->scratch[0],r->scratch[1],r->scratch[2]},.host_gate=r->host_gate};
    dm_x18_patch prefix;int result=dm_patch_x18(&prefix_request,&prefix);if(result)return result;
    dm_window_patch patch={.replacement=prefix.replacement,.site=prefix.site};memcpy(patch.stub,prefix.stub,32);
    unsigned s=r->scratch[0],t=r->scratch[1],u=r->scratch[2];
    uint32_t exits[DM_WINDOW_MAX+1]={hi},exit_count=1,exit_base=32+4*r->count;
    for(uint32_t i=0;i<r->count;i++){
        uint32_t old=r->original[i],word=r->translated[i],bits=0,mask=0;
        if((old&0x7e000000u)==0x34000000u){bits=19;mask=0x00ffffe0u;}
        else if((old&0x7e000000u)==0x36000000u){bits=14;mask=0x0007ffe0u;}
        else if((old&0xff000010u)==0x54000000u){bits=19;mask=0x00ffffe0u;}
        else if((old&0xfc000000u)==0x14000000u){bits=26;mask=0x03ffffffu;}
        if(bits){
            int64_t target=(int64_t)r->original_offset+i*4+signed_bits(bits==26?old:old>>5,bits)*4;
            if(target<0||target>UINT32_MAX)return ERANGE;
            uint32_t relocated;
            if((uint64_t)target>=r->original_offset&&(uint64_t)target<hi)relocated=r->stub_offset+32+(uint32_t)target-r->original_offset;
            else{uint32_t index=0;while(index<exit_count&&exits[index]!=(uint32_t)target)index++;
                if(index==exit_count)exits[exit_count++]=(uint32_t)target;
                relocated=r->stub_offset+exit_base+16*index;
            }
            uint32_t pc=r->stub_offset+32+i*4;int64_t delta=(int64_t)relocated-pc;
            if(delta%4||delta/4<-(1ll<<(bits-1))||delta/4>=(1ll<<(bits-1)))return ERANGE;
            word=(word&~mask)|(((uint32_t)(delta/4)&((1u<<bits)-1))<<(bits==26?0:5));
        }
        put(patch.stub+32+i*4,word);
    }
    for(uint32_t i=0;i<exit_count;i++){
        uint32_t off=exit_base+16*i,jump;
        if(branch(r->stub_offset+off+12,exits[i],&jump))return ERANGE;
        put(patch.stub+off,0xf9000000u|t<<5|s);
        put(patch.stub+off+4,0xa9410000u|31u<<10|31u<<5|u);
        put(patch.stub+off+8,0xa8d00000u|t<<10|31u<<5|s);
        put(patch.stub+off+12,jump);
    }
    uint32_t literal=(exit_base+16*exit_count+7)&~7u;patch.size=literal+8;
    if(patch.size>sizeof(patch.stub))return ERANGE;
    put(patch.stub+12,0x5800001eu|((literal-12)/4)<<5);
    for(unsigned b=0;b<8;b++)patch.stub[literal+b]=(uint8_t)((uint64_t)r->host_gate>>(8*b));
    *out=patch;return 0;
}
