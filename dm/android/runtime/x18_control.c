// 复用普通 x18 跳板的保存／取槽前缀；地址指令重算 ADRP，条件跳转的两条路径分别恢复原寄存器与栈。
// Reuse plain-x18 save / slot-fetch prefix; recompute ADRP and restore original registers / stack on both conditional paths.
#include "x18_control.h"
#include "x18_patch.h"
#include <errno.h>
#include <string.h>

static void put(uint8_t *p, uint32_t word) { for (unsigned b=0;b<4;b++) p[b]=(uint8_t)(word>>(b*8)); }
static int64_t signed_bits(uint32_t n, unsigned bits) {
    return (int64_t)(n & ((1u<<bits)-1)) - ((n & (1u<<(bits-1))) ? (1ll<<bits) : 0);
}
static int branch(uint32_t from, uint32_t to, uint32_t *out) {
    int64_t delta=(int64_t)to-from;
    if (delta%4 || delta<-(1ll<<27) || delta>=(1ll<<27)) return ERANGE;
    *out=0x14000000u|((uint32_t)(delta/4)&0x03ffffffu); return 0;
}
int dm_patch_x18_control(const dm_control_request *r, dm_control_patch *out) {
    if (!r || !out || !r->host_gate || r->original_offset%4 || r->stub_offset%8 ||
        r->stub_offset>UINT32_MAX-96 || r->original_offset>UINT32_MAX-4 ||
        ((uint64_t)r->stub_offset<(uint64_t)r->original_offset+4 &&
         r->original_offset<(uint64_t)r->stub_offset+96)) return EINVAL;
    uint32_t word=r->instruction;
    if ((word&31)!=18) return ENOTSUP;
    int address=(word&0x1f000000u)==0x10000000u;
    int compare=(word&0x7e000000u)==0x34000000u;
    int test=(word&0x7e000000u)==0x36000000u;
    if (!address && !compare && !test) return ENOTSUP;
    dm_x18_request prefix_request={.original_offset=r->original_offset,.stub_offset=r->stub_offset,
        .translated=0xd503201f,.scratch={0,1,2},.host_gate=r->host_gate};
    dm_x18_patch prefix;
    int result=dm_patch_x18(&prefix_request,&prefix);
    if (result) return result;
    dm_control_patch patch={.replacement=prefix.replacement,.site=prefix.site};
    for (unsigned i=0;i<20;i++) put(patch.stub+i*4,0xd503201fu);
    memcpy(patch.stub,prefix.stub,32);
    put(patch.stub+12,0x5800023eu);
    for (unsigned b=0;b<8;b++) patch.stub[80+b]=(uint8_t)((uint64_t)r->host_gate>>(b*8));
    int64_t target;
    if (address) {
        uint32_t imm=((word>>29)&3)|(((word>>5)&0x7ffffu)<<2);
        int64_t delta=signed_bits(imm,21);
        target=(word>>31) ? (int64_t)(r->original_offset&~4095u)+delta*4096 : (int64_t)r->original_offset+delta;
    } else {
        unsigned bits=compare ? 19 : 14;
        target=(int64_t)r->original_offset+signed_bits(word>>5,bits)*4;
    }
    if (target<0 || target>UINT32_MAX) return ERANGE;
    uint32_t back;
    if (branch(r->stub_offset+(address ? 56 : 48),r->original_offset+4,&back)) return ERANGE;
    if (address) {
        int64_t pages=((target&~4095ll)-(int64_t)((r->stub_offset+32)&~4095u))/4096;
        if (pages<-(1ll<<20) || pages>=(1ll<<20)) return ERANGE;
        uint32_t imm=(uint32_t)pages&0x1fffffu;
        put(patch.stub+32,0x90000000u|(imm&3)<<29|(imm>>2)<<5);
        put(patch.stub+36,0x91000000u|((uint32_t)target&4095u)<<10);
        put(patch.stub+40,0xf9000020u);
        put(patch.stub+44,0xa9417fe2u);
        put(patch.stub+48,0xa8d007e0u);
        put(patch.stub+52,0x910003ffu);
        put(patch.stub+56,back);
    } else {
        uint32_t taken;
        if (branch(r->stub_offset+68,(uint32_t)target,&taken)) return ERANGE;
        put(patch.stub+32,(word&(compare ? 0xff000000u : 0xfff80000u))|(6u<<5));
        put(patch.stub+36,0xa9417fe2u);
        put(patch.stub+40,0xa8d007e0u);
        put(patch.stub+44,0x910003ffu);
        put(patch.stub+48,back);
        put(patch.stub+56,0xa9417fe2u);
        put(patch.stub+60,0xa8d007e0u);
        put(patch.stub+64,0x910003ffu);
        put(patch.stub+68,taken);
    }
    *out=patch; return 0;
}
