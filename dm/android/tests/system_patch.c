// 真正执行改写后的合成 ARM64 系统指令，验证客体 TLS / 计时与宿主 Bionic/POSIX TLS 互不干扰。
// Execute rewritten synthetic ARM64 system instructions; verify guest TLS / clocks do not interfere with host Bionic / POSIX TLS.
#include "../runtime/patch.h"
#include "../image_map.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); abort(); } } while (0)
static void put_word(uint8_t *where, uint32_t word) {
    for (unsigned b = 0; b < 4; b++) where[b] = (uint8_t)(word >> (b * 8));
}

static void rejects_patch_when_instruction_unsupported(void) {
    dm_patch_request request = {.instruction = 0xd503201f, .original_offset = 0, .stub_offset = 128, .host_gate = 1};
    dm_patch_output out = {.replacement = 0xabcdef};
    CHECK(dm_patch_system(&request, &out) == ENOTSUP && out.replacement == 0xabcdef);
    request.instruction = 0xd4000001; request.stub_offset = 0x80000000;
    CHECK(dm_patch_system(&request, &out) == ERANGE && out.replacement == 0xabcdef);
    request.stub_offset = 0; CHECK(dm_patch_system(&request, &out) == EINVAL);
    request.stub_offset = 130; CHECK(dm_patch_system(&request, &out) == EINVAL);
}

static void svc_return_clears_monitor_without_changing_gate_layout(void) {
    dm_patch_request request={.instruction=0xd40003c1,.original_offset=0,.stub_offset=128,.host_gate=1};
    dm_patch_output out;
    CHECK(!dm_patch_system(&request,&out));
    const uint8_t clear[]={0x5f,0x3f,0x03,0xd5};
    CHECK(!memcmp(out.stub+16,clear,4));
    CHECK(out.site.kind==DM_GATE_SVC&&out.site.return_address==140);
    CHECK(out.stub[24]==1&&out.stub[25]==0);
    request.instruction=0xd53bd040;
    CHECK(!dm_patch_system(&request,&out));
    CHECK(memcmp(out.stub+16,clear,4));
}

#if defined(__aarch64__)
void dm_gate(void);
// 故意在宿主门重新建立预留，验证 SVC 返回清除而非依赖 C 调用偶然使 STXR 失败。
// Deliberately rearm the reservation in the host gate, checking SVC return clearing rather than incidental STXR failure across C calls.
__attribute__((naked)) static void reservation_gate(void){__asm__ volatile("ldxr x9, [x0]\nret");}
static void svc_clears_a_reservation_rearmed_by_host(void){
    size_t page=(size_t)sysconf(_SC_PAGESIZE);
    dm_nso_image image={.header.image_size=page};image.header.segments[0].size=160;
    image.header.segments[1].memory_offset=(uint32_t)page;
    image.header.segments[2].memory_offset=(uint32_t)page;
    image.bytes=calloc(1,page);CHECK(image.bytes);
    const uint32_t program[]={0xa9bf7bfd,0xd40003c1,0xc8027c01,0x2a0203e0,0xa8c17bfd,0xd65f03c0};
    for(unsigned i=0;i<sizeof(program)/sizeof(*program);i++)put_word(image.bytes+4*i,program[i]);
    dm_patch_request request={.instruction=program[1],.original_offset=4,.stub_offset=128,.host_gate=(uintptr_t)reservation_gate};
    dm_patch_output patch;CHECK(!dm_patch_system(&request,&patch));
    put_word(image.bytes+4,patch.replacement);memcpy(image.bytes+128,patch.stub,sizeof(patch.stub));
    dm_image_mapping mapped={0};CHECK(!dm_image_map(&image,&mapped));
    uint64_t value=11;
    CHECK(((uint32_t(*)(uint64_t*,uint64_t))(void*)mapped.base)(&value,22)!=0&&value==11);
    CHECK(!dm_image_unmap(&mapped));dm_nso_free(&image);
}
static void executes_address_svc_through_gate(void){
    size_t page=(size_t)sysconf(_SC_PAGESIZE);
    dm_nso_image image={.header.image_size=page};image.header.segments[0].size=192;
    image.header.segments[1].memory_offset=(uint32_t)page;image.header.segments[2].memory_offset=(uint32_t)page;
    image.bytes=calloc(1,page);CHECK(image.bytes);dm_gate_site sites[2];
    for(unsigned i=0;i<2;i++){
        uint32_t off=i*32,svc=0xd4000001u|((0x34u+i)<<5);
        put_word(image.bytes+off,0xa9bf7bfd);put_word(image.bytes+off+4,svc);
        put_word(image.bytes+off+8,0xa8c17bfd);put_word(image.bytes+off+12,0xd65f03c0);
        dm_patch_request request={.instruction=svc,.original_offset=off+4,.stub_offset=128+i*32,.host_gate=(uintptr_t)dm_gate};
        dm_patch_output patch;CHECK(!dm_patch_system(&request,&patch));
        put_word(image.bytes+off+4,patch.replacement);memcpy(image.bytes+request.stub_offset,patch.stub,sizeof(patch.stub));sites[i]=patch.site;
    }
    dm_image_mapping mapped={0};CHECK(!dm_image_map(&image,&mapped));
    for(unsigned i=0;i<2;i++)sites[i].return_address+=(uintptr_t)mapped.base;
    dm_gate_table table={sites,2};dm_host*h=dm_host_create(2u*1024u*1024u);CHECK(h);
    uint64_t r[31]={[1]=2u*1024u*1024u};CHECK(!dm_host_svc(h,1,r));uint32_t*value=(uint32_t*)(uintptr_t)r[1];
    CHECK(!dm_guest_attach(h,&table));typedef uint64_t(*fn)(uint32_t*,unsigned,uint32_t,int64_t);
    fn wait=(fn)(void*)mapped.base,signal=(fn)(void*)(mapped.base+32);
    CHECK(wait(value,2,0,0)==0xea01);CHECK(!signal(value,1,0,1)&&*value==1);
    CHECK(wait(value,1,2,0)==0xea01&&*value==0);
    CHECK(!dm_guest_detach());CHECK(!dm_host_destroy(h));CHECK(!dm_image_unmap(&mapped));dm_nso_free(&image);
}
static void executes_system_instructions_when_rewritten(void) {
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    dm_nso_image image = {.header.image_size = page};
    image.header.segments[0].size = 128 + 10 * 32;
    image.header.segments[1].memory_offset = (uint32_t)page;
    image.header.segments[2].memory_offset = (uint32_t)page;
    image.bytes = calloc(1, page); CHECK(image.bytes);
    const uint32_t program[] = {
        0xa9bf7bfd, 0x910003fd, 0xd53bd040, 0xf9000040,
        0xd51bd041, 0xd53bd040, 0xf9000440, 0xd53bd052,
        0xd53bd060, 0xf9000840, 0xd53be040, 0xf9000c40,
        0xd40003c1, 0xf9001040, 0xd53bd05d, 0xf900145d,
        0xd53bd05e, 0xf900185e, 0xd51bd05e, 0xa8c17bfd, 0xd65f03c0
    };
    for (unsigned i = 0; i < sizeof program / sizeof *program; i++) put_word(image.bytes + i * 4, program[i]);
    const unsigned offsets[] = {8, 16, 20, 28, 32, 40, 48, 56, 64, 72};
    dm_gate_site sites[10];
    for (unsigned i = 0; i < 10; i++) {
        dm_patch_request request = {.instruction = program[offsets[i] / 4], .original_offset = offsets[i],
            .stub_offset = 128 + i * 32, .host_gate = (uintptr_t)dm_gate};
        dm_patch_output patch;
        CHECK(!dm_patch_system(&request, &patch));
        put_word(image.bytes + request.original_offset, patch.replacement);
        memcpy(image.bytes + request.stub_offset, patch.stub, sizeof patch.stub);
        sites[i] = patch.site;
    }
    dm_image_mapping mapping = {0}; CHECK(dm_image_map(&image, &mapping) == DM_IMAGE_OK);
    for (unsigned i = 0; i < 10; i++) sites[i].return_address += (uintptr_t)mapping.base;
    const dm_gate_table table = {.sites = sites, .count = 10};
    dm_host *host = dm_host_create(8u * 1024u * 1024u); CHECK(host);
    CHECK(!dm_guest_attach(host, &table));
    dm_guest_context *context = dm_guest_current();
    uint64_t original_tls = context->slots[DM_SLOT_TLS], original_tlr = context->slots[DM_SLOT_TLR];
    typedef void (*entry_fn)(uint64_t, uint64_t, uint64_t *);
    uint64_t values[7] = {0};
    ((entry_fn)(void *)mapping.base)(0, 0x123456789, values);
    CHECK(values[0] == original_tls && values[1] == 0x123456789 && values[2] == original_tlr);
    CHECK(values[3] > 0 && values[4] >= values[3]);
    CHECK(values[5] == 0x123456789 && values[6] == 0x123456789);
    CHECK(context == dm_guest_current() && context->slots[DM_SLOT_X18] == 0x123456789);
    CHECK(!dm_guest_detach()); CHECK(!dm_host_destroy(host));
    CHECK(!dm_image_unmap(&mapping)); dm_nso_free(&image);
}
#endif

int main(void) {
    rejects_patch_when_instruction_unsupported();
    svc_return_clears_monitor_without_changing_gate_layout();
#if defined(__aarch64__)
    svc_clears_a_reservation_rearmed_by_host();
    executes_address_svc_through_gate();
    executes_system_instructions_when_rewritten();
    puts("PASS: rewritten MRS/MSR guest TLS, virtual x18, TLR, counter and SVC tick execute natively");
#endif
    return 0;
}
