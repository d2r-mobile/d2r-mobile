// PC 相对和条件跳转原生执行测试；合成模块覆盖双向位移、两条分支路径与跨页地址，不含游戏内容。
// Native PC-relative / conditional execution tests; synthetic modules cover signed offsets, both branch paths and cross-page addresses, without game content.
#include "../runtime/x18_control.h"
#include "../runtime/x18_patch.h"
#include "../image_map.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c); abort(); } } while (0)

#if defined(__aarch64__)
extern void dm_gate(void);
static void put(uint8_t *p, uint32_t w) { for (unsigned b=0;b<4;b++) p[b]=(uint8_t)(w>>(8*b)); }
static dm_nso_image image_create(size_t page) {
    dm_nso_image image={.header.image_size=page*4};
    image.header.segments[0].size=(uint32_t)(page*4);
    image.header.segments[1].memory_offset=(uint32_t)(page*4);
    image.header.segments[2].memory_offset=(uint32_t)(page*4);
    image.bytes=calloc(1,page*4); CHECK(image.bytes); return image;
}
static uint32_t address_word(int page_relative, uint32_t from, uint32_t to) {
    int64_t delta=page_relative ? ((int64_t)(to&~4095u)-(from&~4095u))/4096 : (int64_t)to-from;
    CHECK(delta>=-(1ll<<20) && delta<(1ll<<20));
    uint32_t imm=(uint32_t)delta&0x1fffffu;
    return (page_relative ? 0x90000012u : 0x10000012u)|(imm&3)<<29|(imm>>2)<<5;
}
static void address_cases(size_t page) {
    for (unsigned variant=0;variant<4;variant++) {
        dm_nso_image image=image_create(page);
        uint32_t entry=(uint32_t)page+64, stub=(uint32_t)(page*3)+128;
        uint32_t target=variant&1 ? 123 : (uint32_t)(page*2)+4079;
        if (variant>=2) target&=~4095u;
        put(image.bytes+entry,0xa9bf7bfd);
        put(image.bytes+entry+8,0xaa1203e0);
        put(image.bytes+entry+12,0xa8c17bfd); put(image.bytes+entry+16,0xd65f03c0);
        dm_control_request request={.instruction=address_word(variant>=2,entry+4,target),
            .original_offset=entry+4,.stub_offset=stub,.host_gate=(uintptr_t)dm_gate};
        dm_control_patch control; CHECK(!dm_patch_x18_control(&request,&control));
        put(image.bytes+entry+4,control.replacement); memcpy(image.bytes+stub,control.stub,sizeof(control.stub));
        dm_x18_request read={.translated=0xaa0103e0,.original_offset=entry+8,.stub_offset=stub+128,
            .scratch={1,2,3},.host_gate=(uintptr_t)dm_gate};
        dm_x18_patch move; CHECK(!dm_patch_x18(&read,&move));
        put(image.bytes+entry+8,move.replacement); memcpy(image.bytes+stub+128,move.stub,sizeof(move.stub));
        dm_image_mapping mapping={0}; CHECK(dm_image_map(&image,&mapping)==DM_IMAGE_OK);
        dm_gate_site sites[]={control.site,move.site};
        for (unsigned i=0;i<2;i++) sites[i].return_address+=(uintptr_t)mapping.base;
        dm_gate_table table={sites,2}; dm_host *host=dm_host_create(4u*1024u*1024u); CHECK(host);
        CHECK(!dm_guest_attach(host,&table));
        typedef uint64_t (*entry_fn)(void);
        uint64_t result=((entry_fn)(void *)(mapping.base+entry))();
        CHECK(result==(uintptr_t)mapping.base+target && dm_guest_current()->slots[DM_SLOT_X18]==result);
        CHECK(!dm_guest_detach()); CHECK(!dm_host_destroy(host));
        CHECK(!dm_image_unmap(&mapping)); dm_nso_free(&image);
    }
}
static void conditional_cases(size_t page) {
    const unsigned bits[]={0,31,32,63};
    for (unsigned variant=0;variant<12;variant++) for (unsigned backward=0;backward<2;backward++) {
        dm_nso_image image=image_create(page);
        uint32_t entry=(uint32_t)page+64, original=entry+8, stub=(uint32_t)(page*3)+128;
        uint32_t target=backward ? entry-32 : entry+28;
        put(image.bytes+entry,0xa9bf7bfd); put(image.bytes+entry+4,0xeb1f03ff);
        put(image.bytes+entry+12,0xd2800160); put(image.bytes+entry+16,0xa8c17bfd); put(image.bytes+entry+20,0xd65f03c0);
        put(image.bytes+target,0xd28002c0); put(image.bytes+target+4,0xa8c17bfd); put(image.bytes+target+8,0xd65f03c0);
        uint32_t opcode; unsigned bit=0;
        if (variant<4) opcode=(variant&2 ? 0xb4000000u : 0x34000000u)|((variant&1)<<24);
        else {
            bit=bits[(variant-4)/2];
            opcode=0x36000000u|((variant&1)<<24)|((bit&31)<<19)|((bit>>5)<<31);
        }
        unsigned width=variant<4 ? 19 : 14;
        uint32_t immediate=(uint32_t)(((int64_t)target-original)/4)&((1u<<width)-1);
        dm_control_request request={.instruction=opcode|immediate<<5|18,.original_offset=original,
            .stub_offset=stub,.host_gate=(uintptr_t)dm_gate};
        dm_control_patch control; CHECK(!dm_patch_x18_control(&request,&control));
        put(image.bytes+original,control.replacement); memcpy(image.bytes+stub,control.stub,sizeof(control.stub));
        dm_image_mapping mapping={0}; CHECK(dm_image_map(&image,&mapping)==DM_IMAGE_OK);
        dm_gate_site site=control.site; site.return_address+=(uintptr_t)mapping.base;
        dm_gate_table table={&site,1}; dm_host *host=dm_host_create(4u*1024u*1024u); CHECK(host);
        CHECK(!dm_guest_attach(host,&table));
        uint64_t inputs[]={0,1,1ull<<32,UINT64_MAX,1ull<<bit};
        for (unsigned i=0;i<5;i++) {
            uint64_t value=inputs[i]; dm_guest_current()->slots[DM_SLOT_X18]=value;
            int nonzero=variant<4 ? (variant&2 ? value!=0 : (uint32_t)value!=0) : ((value>>bit)&1)!=0;
            int taken=(variant&1) ? nonzero : !nonzero;
            typedef uint64_t (*entry_fn)(void);
            uint64_t result=((entry_fn)(void *)(mapping.base+entry))();
            uint64_t nzcv; __asm__ volatile("mrs %0, nzcv" : "=r"(nzcv));
            CHECK(result==(uint64_t)(taken ? 22 : 11));
            CHECK((nzcv>>28)==6 && dm_guest_current()->slots[DM_SLOT_X18]==value);
        }
        CHECK(!dm_guest_detach()); CHECK(!dm_host_destroy(host));
        CHECK(!dm_image_unmap(&mapping)); dm_nso_free(&image);
    }
}
#endif
int main(void) {
    dm_control_request request={.instruction=0x10000012,.stub_offset=128,.host_gate=1};
    dm_control_patch out={.replacement=123};
    CHECK(!dm_patch_x18_control(&request,&out));
    request.instruction=0xd61f0240; CHECK(dm_patch_x18_control(&request,&out)==ENOTSUP);
    request.instruction=0x10000000; CHECK(dm_patch_x18_control(&request,&out)==ENOTSUP);
    request.instruction=0x10fffff2; CHECK(dm_patch_x18_control(&request,&out)==ERANGE);
    request.instruction=0x10000012; request.stub_offset=0x80000000;
    CHECK(dm_patch_x18_control(&request,&out)==ERANGE);
    request.stub_offset=8; request.original_offset=64; CHECK(dm_patch_x18_control(&request,&out)==EINVAL);
#if defined(__aarch64__)
    size_t page=(size_t)sysconf(_SC_PAGESIZE); CHECK(page>=4096);
    address_cases(page); conditional_cases(page);
#endif
    puts("PASS: ADR/ADRP signed cross-page targets; W/X CBZ/CBNZ and TBZ/TBNZ bits 0/31/32/63, both paths/directions, NZCV");
    return 0;
}
