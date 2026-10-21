// 独占路径检查回归；依据 exclusive.h，用合成分支证明全部路径检查与保守拒绝，不包含游戏数据。
// Exclusive-path regressions; based on exclusive.h, synthetic branches check all-path coverage and conservative rejection, without game data.
#include "../rewrite/exclusive.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(c)do{if(!(c)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);abort();}}while(0)
static void put(uint8_t*p,uint32_t w){for(unsigned b=0;b<4;b++)p[b]=(uint8_t)(w>>(8*b));}
int main(void){
    dm_nso_image im={.header.image_size=4096};im.header.segments[0].size=4096;
    im.bytes=calloc(1,4096);CHECK(im.bytes);dm_decoder*d=dm_decoder_create();CHECK(d);
    const uint32_t words[]={0xc85f7c02,0xb4000062,0xd5033f5f,0xd65f03c0,0xc8037c02,0xd65f03c0};
    for(unsigned i=0;i<6;i++)put(im.bytes+4*i,words[i]);
    CHECK(!dm_exclusive_untouched(&im,0,d));
    put(im.bytes+12,0xd4000021);CHECK(!dm_exclusive_untouched(&im,0,d));
    put(im.bytes+16,0xd4000021);CHECK(!dm_exclusive_untouched(&im,0,d));
    put(im.bytes+16,0x94000002);put(im.bytes+24,0xd4000021);
    CHECK(!dm_exclusive_untouched(&im,0,d));
    put(im.bytes+24,0xd503201f);CHECK(dm_exclusive_untouched(&im,0,d)==ENOTSUP);
    put(im.bytes+16,0x94000002);put(im.bytes+24,0xd53bd040);
    CHECK(dm_exclusive_untouched(&im,0,d)==ENOTSUP);
    const uint32_t rejected[]={0x91000652,0x94000001,0xd65f03c0,0xffffffff,0x14000400,0xd53bd040,0x97fffffa,0xd63f0000};
    for(unsigned i=0;i<sizeof(rejected)/sizeof(*rejected);i++){
        put(im.bytes+16,rejected[i]);CHECK(dm_exclusive_untouched(&im,0,d)==ENOTSUP);
    }
    put(im.bytes+16,0x14000000);CHECK(!dm_exclusive_untouched(&im,0,d));
    put(im.bytes+16,0xc85f7c12);CHECK(!dm_exclusive_untouched(&im,0,d));
    put(im.bytes,0xc85f7c12);CHECK(dm_exclusive_untouched(&im,0,d)==ENOTSUP);
    put(im.bytes,words[0]);
    for(unsigned i=1;i<300;i++)put(im.bytes+4*i,0xd503201f);
    put(im.bytes+1200,0xd5033f5f);CHECK(dm_exclusive_untouched(&im,0,d)==ENOTSUP);
    dm_decoder_free(d);dm_nso_free(&im);
    puts("PASS: exclusive CFG branch paths, clear/store terminals, cycles, replacement loads, unsafe sites and bounded rejection");
    return 0;
}
