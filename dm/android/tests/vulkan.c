// 实际 GPU 回归：核对计算着色器的逐像素图案，以及离屏图像清除/布局转换/拷贝的全部像素。
// Real GPU regression: check the compute shader pattern per pixel and all pixels from offscreen clear / transitions / copy.
#include "../vulkan/vk_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
#define VK_CHECK(call) do { VkResult r = (call); if (r != VK_SUCCESS) { fprintf(stderr, "FAIL: %s = %d\n", #call, r); exit(1); } } while (0)
int main(int argc, char **argv) {
    CHECK(argc == 4);
    dm_vk vk = {0}; VK_CHECK(dm_vk_create(&vk));
    printf("GPU: %s Vulkan %u.%u.%u\n", vk.properties.deviceName, VK_VERSION_MAJOR(vk.properties.apiVersion),
        VK_VERSION_MINOR(vk.properties.apiVersion), VK_VERSION_PATCH(vk.properties.apiVersion));
    uint8_t bytes[13 * 11 * 4]; memset(bytes, 0xa5, sizeof bytes);
    dm_vk_pixels pixels = {.width = 13, .height = 11, .bytes = bytes, .capacity = sizeof bytes};
    VK_CHECK(dm_vk_compute_pixels(&vk, argv[1], &pixels));
    for (unsigned y = 0; y < 11; y++) for (unsigned x = 0; x < 13; x++) {
        uint8_t *p = bytes + (y * 13 + x) * 4;
        CHECK(p[0] == x * 17 && p[1] == y * 17 && p[2] == 0 && p[3] == 255);
    }
    VK_CHECK(dm_vk_clear_pixels(&vk, &pixels));
    for (size_t i = 0; i < sizeof bytes; i += 4)
        CHECK(bytes[i] == 255 && bytes[i + 1] == 0 && bytes[i + 2] == 255 && bytes[i + 3] == 255);
    const unsigned sizes[][2]={{1,1},{13,11},{37,19}};
    for(unsigned i=0;i<3;i++){
        size_t count=(size_t)sizes[i][0]*sizes[i][1]*4;uint8_t*draw=malloc(count+16);CHECK(draw);memset(draw,0x57,count+16);
        dm_vk_pixels target={.width=sizes[i][0],.height=sizes[i][1],.bytes=draw,.capacity=count};
        VK_CHECK(dm_vk_draw_pixels(&vk,argv[2],argv[3],&target));
        for(unsigned y=0;y<target.height;y++)for(unsigned x=0;x<target.width;x++){
            uint8_t*p=draw+((size_t)y*target.width+x)*4;
            CHECK(p[0]==(x&15u)*17&&p[1]==(y&15u)*17&&p[2]==((x^y)&1u)*255&&p[3]==255);
        }
        for(size_t j=count;j<count+16;j++)CHECK(draw[j]==0x57);free(draw);
    }
    memset(bytes, 0x39, sizeof bytes);
    pixels.capacity = 1;
    CHECK(dm_vk_compute_pixels(&vk, argv[1], &pixels) != VK_SUCCESS);
    CHECK(dm_vk_clear_pixels(&vk, &pixels) != VK_SUCCESS);
    CHECK(dm_vk_draw_pixels(&vk,argv[2],argv[3],&pixels)!=VK_SUCCESS);
    for (size_t i = 0; i < sizeof bytes; i++) CHECK(bytes[i] == 0x39);
    pixels.capacity = sizeof bytes;
    CHECK(dm_vk_compute_pixels(&vk, "/nonexistent-dm-shader", &pixels) != VK_SUCCESS);
    CHECK(dm_vk_draw_pixels(&vk,"/nonexistent-dm-shader",argv[3],&pixels)!=VK_SUCCESS);
    CHECK(dm_vk_draw_pixels(&vk,argv[2],"/nonexistent-dm-shader",&pixels)!=VK_SUCCESS);
    dm_vk_destroy(&vk); CHECK(!vk.device && !vk.instance);
    puts("PASS: actual GPU compute, graphics pipeline / render pass / fullscreen draw, per-pixel readback, transfers / synchronization and bounds");
    return 0;
}
