// 按原表步长生成落地 B，先恢复全部暂存寄存器与栈，再跳至原目标；x18 客体槽不变。
// Generate landing B instructions at the original table stride; restore scratch registers / stack before branching to original targets; guest x18 stays unchanged.
#include "x18_jump.h"
#include "x18_patch.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
static void put(uint8_t *p,uint32_t w){for(unsigned b=0;b<4;b++)p[b]=(uint8_t)(w>>(8*b));}
static int branch(uint32_t from,uint32_t to,uint32_t *out){
    int64_t delta=(int64_t)to-from;
    if(delta%4 || delta<-(1ll<<27) || delta>=(1ll<<27))return ERANGE;
    *out=0x14000000u|((uint32_t)(delta/4)&0x03ffffffu);return 0;
}
int dm_patch_x18_jump(const dm_jump_request *r,dm_jump_patch *out){
    if(!r || !out || !r->host_gate || r->original_offset%4 || r->stub_offset%8 || r->table_offset%4 ||
        (r->entries!=256 && r->entries!=65536))return EINVAL;
    uint64_t size=128ull+16ull*r->entries;
    if((uint64_t)r->stub_offset+size>UINT32_MAX || (uint64_t)r->table_offset+4ull*r->entries>UINT32_MAX ||
        ((uint64_t)r->stub_offset<(uint64_t)r->original_offset+4 && r->original_offset<(uint64_t)r->stub_offset+size) ||
        ((uint64_t)r->stub_offset<(uint64_t)r->table_offset+4ull*r->entries && r->table_offset<(uint64_t)r->stub_offset+size))return EINVAL;
    dm_x18_request prefix_request={.original_offset=r->original_offset,.stub_offset=r->stub_offset,
        .translated=0xd503201f,.scratch={0,1,2},.host_gate=r->host_gate};
    dm_x18_patch prefix;int result=dm_patch_x18(&prefix_request,&prefix);if(result)return result;
    int64_t pages=((int64_t)(r->table_offset&~4095u)-(int64_t)((r->stub_offset+32)&~4095u))/4096;
    if(pages<-(1ll<<20) || pages>=(1ll<<20))return ERANGE;
    dm_jump_patch patch={.replacement=prefix.replacement,.size=(size_t)size,.site=prefix.site};
    patch.stub=calloc(1,patch.size);if(!patch.stub)return ENOMEM;
    for(unsigned i=0;i<32;i++)put(patch.stub+4*i,0xd503201f);
    memcpy(patch.stub,prefix.stub,32);put(patch.stub+12,0x580002be);
    uint32_t imm=(uint32_t)pages&0x1fffffu;
    put(patch.stub+32,0x90000002u|(imm&3)<<29|(imm>>2)<<5);
    put(patch.stub+36,0x91000042u|(r->table_offset&4095u)<<10);
    put(patch.stub+40,0xcb020000u);
    put(patch.stub+44,0xd340fc02u|((r->entries==256 ? 10u:18u)<<16));
    put(patch.stub+48,0xb5000202u);
    put(patch.stub+52,0x370001e0u);put(patch.stub+56,0x370801c0u);
    put(patch.stub+60,0x10000222u);put(patch.stub+64,0x8b020000u);put(patch.stub+68,0xd61f0000u);
    put(patch.stub+112,0xd4200000u);
    for(unsigned b=0;b<8;b++)patch.stub[96+b]=(uint8_t)((uint64_t)r->host_gate>>(8*b));
    uint32_t tails=128+4*r->entries;
    for(uint32_t i=0;i<r->entries;i++){
        uint32_t landing=128+4*i,tail=tails+12*i,a,b;
        if(branch(r->stub_offset+landing,r->stub_offset+tail,&a) ||
            branch(r->stub_offset+tail+8,r->table_offset+4*i,&b)){free(patch.stub);return ERANGE;}
        put(patch.stub+landing,a);put(patch.stub+tail,0xa9417fe2u);put(patch.stub+tail+4,0xa8d007e0u);put(patch.stub+tail+8,b);
    }
    *out=patch;return 0;
}
void dm_jump_patch_free(dm_jump_patch *p){if(p){free(p->stub);memset(p,0,sizeof(*p));}}
