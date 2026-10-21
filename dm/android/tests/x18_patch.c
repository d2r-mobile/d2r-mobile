// x18 原生算术、访存和 SP 写回测试；使用手写 ARM64 指令，不含游戏数据，不使用 CPU 模拟器。
// Native x18 arithmetic, memory and SP-writeback tests; handwritten ARM64 instructions, no game data or CPU emulator.
#include "../runtime/x18_patch.h"
#include "../image_map.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef DM_TEST_DECODE
#include "../rewrite/decode.h"
#endif
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); abort(); } } while (0)
#if defined(__aarch64__)
static void put(uint8_t *p, uint32_t value) { for (unsigned b = 0; b < 4; b++) p[b] = (uint8_t)(value >> (b * 8)); }
#endif

int main(void) {
    dm_x18_request request = {.translated = 0xd28000ec, .stub_offset = 128, .scratch = {12,13,14}, .host_gate = 1};
    dm_x18_patch patch = {.replacement = 123};
    CHECK(!dm_patch_x18(&request, &patch));
    request.scratch[1] = 12;
    CHECK(dm_patch_x18(&request, &patch) == EINVAL);
    request.scratch[1] = 13; request.sp_delta = -144;
    CHECK(dm_patch_x18(&request, &patch) == EINVAL);
    request.sp_delta = 8; CHECK(dm_patch_x18(&request, &patch) == EINVAL);
    request.sp_delta = 0; request.stub_offset = 0x80000000;
    CHECK(dm_patch_x18(&request, &patch) == ERANGE);
#if defined(__aarch64__)
    extern void dm_gate(void);
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    dm_nso_image image = {.header.image_size = page};
    image.header.segments[0].size = 128 + 12 * 64;
    image.header.segments[1].memory_offset = (uint32_t)page;
    image.header.segments[2].memory_offset = (uint32_t)page;
    image.bytes = calloc(1, page); CHECK(image.bytes);
    const uint32_t program[] = {
        0xa9bf7bfd,0xd10043ff,0xf90003e0,0xd2800c69,0xd2800c8a,0xd2800cab,
        0xd28000f2,0xab000252,0xf9000032,0x12800012,0xf9000432,0xf94003f2,
        0xf9000832,0xa9bf03f2,0xa8c103f2,0xf9000c32,
        0xaa1203fd,0xf900203d,0xaa1203fe,0xf900243e,
        0xf9001029,0xf900142a,0xf900182b,0xd53b4202,0xf9001c22,
        0x910043ff,0xa8c17bfd,0xd65f03c0
    };
    for (unsigned i = 0; i < sizeof(program) / sizeof(*program); i++) put(image.bytes + i * 4, program[i]);
    dm_gate_site sites[14];
#ifdef DM_TEST_DECODE
    dm_decoder *decoder = dm_decoder_create(); CHECK(decoder);
#endif
    for (unsigned i = 0; i < 12; i++) {
        unsigned index = i < 11 ? i + 6 : 18;
#ifdef DM_TEST_DECODE
        dm_decode_input in = {.word = program[index], .offset = index * 4};
        CHECK(!dm_decode_x18(decoder, &in, &request));
        request.stub_offset = 128 + i * 64; request.host_gate = (uintptr_t)dm_gate;
#else
        uint32_t word = program[index];
        if ((word & 31) == 18) word = (word & ~31u) | 12;
        if (((word >> 5) & 31) == 18) word = (word & ~(31u << 5)) | 12u << 5;
        if (((word >> 16) & 31) == 18) word = (word & ~(31u << 16)) | 12u << 16;
        if (index == 11 || index == 13 || index == 14) word = (word & ~(31u << 5)) | 14u << 5;
        request = (dm_x18_request){.translated = word, .original_offset = index * 4, .stub_offset = 128 + i * 64,
            .scratch = {12,13,14}, .host_gate = (uintptr_t)dm_gate, .sp_delta = index == 13 ? -16 : index == 14 ? 16 : 0};
#endif
        CHECK(!dm_patch_x18(&request, &patch));
        put(image.bytes + index * 4, patch.replacement);
        memcpy(image.bytes + request.stub_offset, patch.stub, sizeof(patch.stub));
        sites[i] = patch.site;
    }
#ifdef DM_TEST_DECODE
    const uint32_t stack_program[]={0xa9bf7bfd,0x910003e2,0x910003f2,0xaa1203e0,0xcb020000,0xa8c17bfd,0xd65f03c0};
    for(unsigned i=0;i<7;i++)put(image.bytes+1024+4*i,stack_program[i]);
    for(unsigned i=0;i<2;i++){
        dm_decode_input in={.word=stack_program[i+2],.offset=1032+i*4};
        CHECK(!dm_decode_x18(decoder,&in,&request));request.stub_offset=896+i*64;request.host_gate=(uintptr_t)dm_gate;
        CHECK(!dm_patch_x18(&request,&patch));put(image.bytes+in.offset,patch.replacement);
        memcpy(image.bytes+request.stub_offset,patch.stub,sizeof(patch.stub));sites[12+i]=patch.site;
    }
    image.header.segments[0].size=1088;
    dm_decoder_free(decoder);
#endif
    dm_image_mapping mapping = {0}; CHECK(dm_image_map(&image, &mapping) == DM_IMAGE_OK);
    unsigned site_count=12;
#ifdef DM_TEST_DECODE
    site_count=14;
#endif
    for (unsigned i = 0; i < site_count; i++) sites[i].return_address += (uintptr_t)mapping.base;
    dm_gate_table table = {.sites = sites, .count = site_count};
    dm_host *host = dm_host_create(4u * 1024u * 1024u); CHECK(host);
    CHECK(!dm_guest_attach(host, &table));
    uint64_t values[10] = {0};
    typedef void (*entry_fn)(uint64_t, uint64_t *);
    const uint64_t seeds[] = {35,UINT64_MAX-6};
    for (unsigned i = 0; i < 2; i++) {
        uintptr_t platform_before, platform_after;
        __asm__ volatile("mov %0, x18" : "=r"(platform_before));
        ((entry_fn)(void *)mapping.base)(seeds[i], values);
        __asm__ volatile("mov %0, x18" : "=r"(platform_after));
        CHECK(platform_before == platform_after);
        CHECK(values[0] == seeds[i] + 7 && values[1] == UINT32_MAX && values[2] == seeds[i] && values[3] == seeds[i]);
        CHECK(values[4] == 99 && values[5] == 100 && values[6] == 101);
        CHECK((values[7] >> 28) == (i ? 6 : 0) && values[8] == seeds[i] && values[9] == seeds[i]);
        CHECK(dm_guest_current()->slots[DM_SLOT_X18] == seeds[i]);
    }
#ifdef DM_TEST_DECODE
    typedef uint64_t(*stack_fn)(void);
    CHECK(((stack_fn)(void*)(mapping.base+1024))()==0);
#endif
    CHECK(!dm_guest_detach()); CHECK(!dm_host_destroy(host));
    CHECK(!dm_image_unmap(&mapping)); dm_nso_free(&image);
#endif
    puts("PASS: x18 native arithmetic, 32-bit zero extension, memory, original-SP access, pre/post-index stack writeback");
    return 0;
}
