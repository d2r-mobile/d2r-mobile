// 合成双模块链接与恶意元数据测试；依据 ELF64 RELA，不包含游戏内容。
// Synthetic two-module linking and hostile metadata tests; based on ELF64 RELA, contains no game content.
#include "../module.h"
#include "../module_map.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "generated_entry.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
static void put32(uint8_t *p, uint32_t v) { for (unsigned b = 0; b < 4; b++) p[b] = (uint8_t)(v >> (b * 8)); }
static void put64(uint8_t *p, uint64_t v) { for (unsigned b = 0; b < 8; b++) p[b] = (uint8_t)(v >> (b * 8)); }
static uint64_t get64(const uint8_t *p) {
    uint64_t v = 0; for (unsigned b = 0; b < 8; b++) v |= (uint64_t)p[b] << (b * 8); return v;
}
static dm_nso_image fixture(int defined) {
    dm_nso_image im = {.header = {.image_size = 0x10000, .bss_size = 0x4000}};
    im.header.segments[0] = (dm_nso_segment){.size = 0x4000};
    im.header.segments[1] = (dm_nso_segment){.memory_offset = 0x4000, .size = 0x4000};
    im.header.segments[2] = (dm_nso_segment){.memory_offset = 0x8000, .size = 0x4000};
    im.bytes = calloc(1, im.header.image_size); CHECK(im.bytes);
    put32(im.bytes + 4, 8); memcpy(im.bytes + 8, "MOD0", 4); put32(im.bytes + 12, 0x8000 - 8);
    const uint64_t tags[][2] = {{4,0x4000},{5,0x4200},{6,0x4100},{10,16},{11,24},
        {7,0x4300},{8,72},{9,24},{23,0x4380},{2,24},{20,7},{12,0x30},{25,0x8100},{27,8},{0,0}};
    for (size_t i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
        put64(im.bytes + 0x8000 + i * 16, tags[i][0]); put64(im.bytes + 0x8008 + i * 16, tags[i][1]);
    }
    put32(im.bytes + 0x4000, 1); put32(im.bytes + 0x4004, 3);
    put32(im.bytes + 0x4008, 1); put32(im.bytes + 0x4010, 2);
    memcpy(im.bytes + 0x4200, "\0shared\0weak\0", 13);
    uint8_t *s = im.bytes + 0x4118;
    put32(s, 1); s[4] = 0x11; s[6] = (uint8_t)defined; put64(s + 8, defined ? 0x8200 : 0); put64(s + 16, defined ? 8 : 0);
    s += 24; put32(s, 8); s[4] = 0x21;
    const uint64_t relas[][3] = {{0x8300,1027,0x30},{0x8308,((uint64_t)1<<32)|257,5},
        {0x8310,((uint64_t)2<<32)|1025,0}};
    for (unsigned i = 0; i < 3; i++) for (unsigned j = 0; j < 3; j++)
        put64(im.bytes + 0x4300 + i * 24 + j * 8, relas[i][j]);
    put64(im.bytes + 0x4380, 0x8318); put64(im.bytes + 0x4388, ((uint64_t)1<<32)|1026); put64(im.bytes + 0x4390, 7);
    return im;
}
static void parse(dm_nso_image *image, uint64_t base, dm_module *m) {
    CHECK(dm_module_parse(image, base, m) == DM_MODULE_OK);
}
int DM_NO_UBSAN_FUNCTION main(void) {
    dm_nso_image images[2] = {fixture(0),fixture(1)};
    dm_module m[2]; parse(&images[0], 0x100000000ull, &m[0]); parse(&images[1], 0x200000000ull, &m[1]);
    dm_link_plan p = {0};
    CHECK(dm_link_plan_build(m, 2, &p) == DM_MODULE_OK);
    CHECK(p.count == 8 && p.relative == 2 && p.absolute == 4 && p.jump == 2 && p.weak_zero == 2 && !p.unresolved);
    CHECK(get64(images[0].bytes + 0x8300) == 0);
    dm_symbol_override imports[2]={{"shared",0x123456780ull},{"shared",0x123456780ull}};
    dm_link_plan overridden={0};
    CHECK(dm_link_plan_build_imports(m,2,imports,2,&overridden)==DM_MODULE_FORMAT&&!overridden.writes);
    CHECK(dm_link_plan_build_imports(m,2,imports,1,&overridden)==DM_MODULE_OK);
    CHECK(overridden.host_imports==2);
    CHECK(overridden.writes[1].value==imports[0].address+5);
    CHECK(overridden.writes[5].value==m[1].base+0x8205);
    dm_link_plan_free(&overridden);
    dm_module_mappings mapped = {0};
    CHECK(dm_modules_map(images, 2, &mapped) == DM_MODULE_OK);
    CHECK(mapped.count == 2 && mapped.relocations == 8);
    CHECK(get64(mapped.mappings[0].base + 0x8300) == (uintptr_t)mapped.mappings[0].base + 0x30);
    CHECK(get64(mapped.mappings[0].base + 0x8308) == (uintptr_t)mapped.mappings[1].base + 0x8205);
    CHECK(get64(images[0].bytes + 0x8300) == 0);
    CHECK(!dm_modules_unmap(&mapped) && !mapped.mappings && !mapped.count);
    const uint8_t stubs[]={0x40,0x05,0x80,0xd2,0xc0,0x03,0x5f,0xd6};
    dm_image_code codes[2]={{.offset=0x10000,.size=sizeof(stubs),.bytes=stubs},{0}};
    put32(images[0].bytes,0x14004000);
    CHECK(dm_modules_map_with_code(images,codes,2,&mapped)==DM_MODULE_OK);
    CHECK(!memcmp(mapped.mappings[0].base+0x10000,stubs,sizeof(stubs)));
    CHECK(get64(mapped.mappings[0].base+0x8308)==(uintptr_t)mapped.mappings[1].base+0x8205);
    CHECK(get64(images[0].bytes+0x8308)==0);
#if defined(__aarch64__)
    CHECK(((uint64_t(*)(void))(void*)mapped.mappings[0].base)()==42);
#endif
    CHECK(!dm_modules_unmap(&mapped));
    mapped.count=99;
    codes[1]=(dm_image_code){.offset=0x10000,.size=sizeof(stubs)};
    CHECK(dm_modules_map_with_code(images,codes,2,&mapped)==DM_MODULE_MAPPING);
    CHECK(mapped.count==99&&!mapped.mappings&&get64(images[0].bytes+0x8308)==0);
    codes[1]=(dm_image_code){.offset=1};
    CHECK(dm_modules_map_with_code(images,codes,2,&mapped)==DM_MODULE_MAPPING);
    CHECK(mapped.count==99&&!mapped.mappings);
    mapped.count = 99;
    CHECK(dm_modules_map(images, 1, &mapped) == DM_MODULE_UNRESOLVED);
    CHECK(mapped.count == 99 && !mapped.mappings);
    mapped.count = 0;
    put64(images[1].bytes + 0x8318, 99);
    CHECK(dm_link_apply(m, 2, &p) == DM_MODULE_STALE);
    CHECK(get64(images[0].bytes + 0x8300) == 0);
    put64(images[1].bytes + 0x8318, 0);
    CHECK(dm_link_apply(m, 2, &p) == DM_MODULE_OK);
    CHECK(get64(images[0].bytes + 0x8300) == m[0].base + 0x30);
    CHECK(get64(images[0].bytes + 0x8308) == m[1].base + 0x8205);
    CHECK(get64(images[0].bytes + 0x8310) == 0);
    CHECK(get64(images[0].bytes + 0x8318) == m[1].base + 0x8207);
    CHECK(dm_link_apply(m, 2, &p) == DM_MODULE_STALE);
    dm_link_plan_free(&p);
    CHECK(dm_link_plan_build(m, 1, &p) == DM_MODULE_OK && p.unresolved == 2);
    CHECK(dm_link_apply(m, 1, &p) == DM_MODULE_UNRESOLVED);
    dm_link_plan_free(&p);

    // 表边界、名称终止、哈希循环、错误重定位类型与 RX 写入都必须拒绝，且输出保持不变。
    // Reject table bounds, unterminated names, hash cycles, invalid relocation types and RX writes without changing outputs.
    dm_module sentinel = {.base = 42};
    put32(images[0].bytes + 4, UINT32_MAX);
    CHECK(dm_module_parse(&images[0], 0, &sentinel) == DM_MODULE_BOUNDS && sentinel.base == 42);
    put32(images[0].bytes + 4, 8);
    put64(images[0].bytes + 0x8008 + 2 * 16, 0xfff0);
    CHECK(dm_module_parse(&images[0], 0, &sentinel) == DM_MODULE_BOUNDS);
    put64(images[0].bytes + 0x8008 + 2 * 16, 0x4100);
    memset(images[0].bytes + 0x4200, 'x', 16);
    CHECK(dm_module_parse(&images[0], 0, &sentinel) == DM_MODULE_FORMAT);
    memcpy(images[0].bytes + 0x4200, "\0shared\0weak\0", 13);
    parse(&images[0], m[0].base, &m[0]);
    put64(images[0].bytes + 0x4300, 0x30);
    CHECK(dm_link_plan_build(m, 2, &p) == DM_MODULE_BOUNDS && !p.writes);
    put64(images[0].bytes + 0x4300, 0x8300);
    put64(images[0].bytes + 0x4308, 999);
    CHECK(dm_link_plan_build(m, 2, &p) == DM_MODULE_UNSUPPORTED);
    put64(images[0].bytes + 0x4308, 1027);
    put64(images[0].bytes + 0x4318, 0x8300);
    CHECK(dm_link_plan_build(m, 2, &p) == DM_MODULE_FORMAT);
    put64(images[0].bytes + 0x4318, 0x8308);
    put64(images[0].bytes + 0x4318, 0x8304);
    CHECK(dm_link_plan_build(m, 2, &p) == DM_MODULE_FORMAT);
    put64(images[0].bytes + 0x4318, 0x8308);
    put32(images[0].bytes + 0x4014, 1);
    CHECK(dm_link_plan_build(m, 1, &p) == DM_MODULE_FORMAT);
    put32(images[0].bytes + 0x4014, 0);
    // SHN_ABS 不加映像基址；负 RELA addend 按 ELF64 无符号模运算处理。
    // SHN_ABS never adds the image base; negative RELA addends use ELF64 unsigned modular arithmetic.
    images[1].bytes[0x411e] = 0xf1; images[1].bytes[0x411f] = 0xff;
    put64(images[1].bytes + 0x4120, 500); put64(images[0].bytes + 0x4328, UINT64_MAX - 4);
    parse(&images[1], m[1].base, &m[1]);
    CHECK(dm_link_plan_build(m, 2, &p) == DM_MODULE_OK);
    CHECK(p.writes[1].value == 495);
    dm_link_plan_free(&p);
    put64(images[0].bytes + 0x4300, 0x8403);
    CHECK(dm_link_plan_build(m, 2, &p) == DM_MODULE_OK);
    CHECK(dm_link_apply(m, 2, &p) == DM_MODULE_OK);
    CHECK(get64(images[0].bytes + 0x8403) == m[0].base + 0x30);
    dm_link_plan_free(&p);
    images[1].bytes[0x411d] = 2;
    CHECK(dm_link_plan_build(m, 2, &p) == DM_MODULE_OK && p.unresolved == 2);
    dm_link_plan_free(&p);
    images[1].bytes[0x411d] = 0;
    images[0].bytes[0x411c] = 0x16;
    CHECK(dm_link_plan_build(m, 2, &p) == DM_MODULE_UNSUPPORTED);
    images[0].bytes[0x411c] = 0x11;
    images[1].bytes[0x411c] = 0x21;
    CHECK(dm_link_plan_build(m, 2, &p) == DM_MODULE_OK && !p.unresolved);
    dm_link_plan_free(&p);
    dm_nso_image strong = fixture(1);
    dm_module three[3] = {m[0], m[1], {0}};
    parse(&strong, 0x300000000ull, &three[2]);
    CHECK(dm_link_plan_build(three, 3, &p) == DM_MODULE_OK && !p.unresolved);
    CHECK(p.writes[0].offset == 0x8308 && p.writes[0].value == three[2].base + 0x8200 - 5);
    dm_link_plan_free(&p); dm_nso_free(&strong);
    dm_module_result overflow = dm_module_parse(&images[0], UINT64_MAX - 1, &sentinel);
    CHECK(overflow == DM_MODULE_BOUNDS && sentinel.base == 42);
    for (unsigned i = 0; i < 2; i++) dm_nso_free(&images[i]);
    puts("PASS: cross-module ABS64/GLOB_DAT/JUMP_SLOT/RELATIVE, weak/strong imports, transactional writes, hostile metadata");
    return 0;
}
