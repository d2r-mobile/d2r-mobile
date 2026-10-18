// 离线替换核对测试；使用合成指令，特殊分支、PC 相对与独占指令必须明确拒绝。
// Offline substitution tests; synthetic instructions, with explicit rejection of special branches, PC-relative and exclusive instructions.
#include "../rewrite/decode.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c); abort(); } } while (0)
static void put(uint8_t *p, uint32_t word) { for (unsigned b=0;b<4;b++) p[b]=(uint8_t)(word>>(b*8)); }
int main(int argc, char **argv) {
    if (argc == 3 && (!strcmp(argv[1],"--fixture")||!strcmp(argv[1],"--safe-fixture"))) {
        uint32_t code[] = {0xd28000f2,0xd4000021,0xc85f7c12,0x91000652,0xc8017c12,
            0x35ffffa1,0x10000012,0xb4000052,0xd61f0240,0xffffffff,0xd503201f};
        if(!strcmp(argv[1],"--safe-fixture"))code[8]=code[9]=0xd503201f;
        uint8_t file[0x100 + sizeof(code)] = {0};
        memcpy(file,"NSO0",4);
        put(file+0x10,0x100); put(file+0x18,(uint32_t)sizeof(code)); put(file+0x60,(uint32_t)sizeof(code));
        for (unsigned i=1;i<3;i++) { put(file+0x10+i*16,(uint32_t)sizeof(file)); put(file+0x14+i*16,i*0x4000); }
        for (unsigned i=0;i<sizeof(code)/sizeof(*code);i++) put(file+0x100+i*4,code[i]);
        FILE *out=fopen(argv[2],"wb"); CHECK(out); CHECK(fwrite(file,1,sizeof(file),out)==sizeof(file)); CHECK(!fclose(out));
        return 0;
    }
    CHECK(argc == 1);
    dm_decoder *d = dm_decoder_create(); CHECK(d);
    const uint32_t supported[] = {0xd28000f2,0xab000252,0xf9000032,0x12800012,0xf94003f2,0xa9bf03f2,0xa8c103f2,0xf9400240,0xd2800252,0x910003f2};
    for (unsigned i = 0; i < sizeof(supported)/sizeof(*supported); i++) {
        dm_decode_input in = {.word = supported[i], .offset = 64};
        dm_x18_request out;
        CHECK(!dm_decode_x18(d,&in,&out));
        CHECK(out.original_offset == 64 && out.translated != in.word);
        CHECK(out.scratch[0] != out.scratch[1] && out.scratch[1] != out.scratch[2] && out.scratch[0] != out.scratch[2]);
        CHECK(out.sp_delta == (i == 5 ? -16 : i == 6 ? 16 : 0));
    }
    const uint32_t special[] = {0x10000012,0x90000012,0xb4000052,0xd61f0240,0xc85f7c12,
        0xf85003f2,0xf8606bf2};
    for (unsigned i = 0; i < sizeof(special)/sizeof(*special); i++) {
        dm_decode_input in = {.word=special[i],.offset=64};
        dm_x18_request out = {.translated=123};
        CHECK(dm_decode_x18(d,&in,&out) == ENOTSUP && out.translated == 123);
    }
    dm_decode_input nop = {.word=0xd503201f}; dm_x18_request out;
    CHECK(dm_decode_x18(d,&nop,&out) == ENOENT);
    nop.word = 0xd2800240; CHECK(dm_decode_x18(d,&nop,&out) == ENOENT);
    nop.word = 0xffffffff; CHECK(dm_decode_x18(d,&nop,&out) == EILSEQ);
    dm_decoder_free(d);
    puts("PASS: verified scratch substitution, SP pre/post-index, x18 base, unsupported branch/PC-relative/atomic rejection");
    return 0;
}
