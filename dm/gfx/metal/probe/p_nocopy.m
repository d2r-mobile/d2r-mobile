// 探针：游戏内存上建零拷贝 MTLBuffer（边界外扩到 16 KiB 页、允许重叠），GPU 写 / CPU 读互通。
// Probe: create zero-copy MTLBuffers over game memory (boundaries expanded outward to 16 KiB pages, overlap allowed), with GPU writes / CPU reads visible to each other.
#import <Metal/Metal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
int main(void) {
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
    size_t pg = getpagesize();
    uint8_t *mem; posix_memalign((void **)&mem, pg, 8 * pg);
    memset(mem, 0, 8 * pg);
    // 两个「池」：A = [pg+0x1000, 3pg+0x800)、B = [2pg+0x2000, 5pg)，都不按页对齐，且外扩后重叠于第 2–3 页
    // Two "pools": A = [pg+0x1000, 3pg+0x800), B = [2pg+0x2000, 5pg), neither page-aligned, and after expanding outward they overlap on pages 2–3
    uint8_t *a0 = mem + pg + 0x1000, *a1 = mem + 3 * pg + 0x800, *b0 = mem + 2 * pg + 0x2000, *b1 = mem + 5 * pg;
    #define DOWN(p) ((uint8_t *)((uintptr_t)(p) & ~(uintptr_t)(pg - 1)))
    #define UP(p) ((uint8_t *)(((uintptr_t)(p) + pg - 1) & ~(uintptr_t)(pg - 1)))
    id<MTLBuffer> A = [dev newBufferWithBytesNoCopy:DOWN(a0) length:UP(a1) - DOWN(a0) options:MTLResourceStorageModeShared deallocator:nil];
    id<MTLBuffer> B = [dev newBufferWithBytesNoCopy:DOWN(b0) length:UP(b1) - DOWN(b0) options:MTLResourceStorageModeShared deallocator:nil];
    id<MTLBuffer> bad = [dev newBufferWithBytesNoCopy:a0 length:pg options:MTLResourceStorageModeShared deallocator:nil];
    printf("页大小 %zu；A %s B %s；未对齐起点建缓冲 %s\n", pg, A ? "成功" : "失败", B ? "成功" : "失败", bad ? "成功（与预期不符）" : "失败（符合预期）");
    // GPU：从 A 的 a0 处拷 4096 字节到 B 的 b0 处（两块的落点都在重叠页之外/之内各一）
    // GPU: copy 4096 bytes from a0 in A to b0 in B (of the two blocks' landing points, one is outside and one is inside the overlapping pages)
    for (int i = 0; i < 4096; i++) a0[i] = (uint8_t)(i * 7 + 3);
    id<MTLCommandQueue> q = [dev newCommandQueue];
    id<MTLCommandBuffer> cb = [q commandBuffer];
    id<MTLBlitCommandEncoder> bl = [cb blitCommandEncoder];
    [bl copyFromBuffer:A sourceOffset:a0 - DOWN(a0) toBuffer:B destinationOffset:b0 - DOWN(b0) size:4096];
    // 再在重叠区内：经 B 写、经 A 读——把 B 中 b0+0x1000 起 256 字节填 0xAB
    // Then within the overlap region: write through B, read through A - fill 256 bytes of B starting at b0+0x1000 with 0xAB
    [bl fillBuffer:B range:NSMakeRange(b0 - DOWN(b0) + 0x1000, 256) value:0xAB];
    [bl endEncoding]; [cb commit]; [cb waitUntilCompleted];
    int ok1 = !memcmp(a0, b0, 4096), ok2 = 1;
    for (int i = 0; i < 256; i++) ok2 &= b0[0x1000 + i] == 0xAB;
    uint8_t *viaA = (uint8_t *)A.contents + (b0 + 0x1000 - DOWN(a0));
    int ok3 = viaA[0] == 0xAB && viaA[255] == 0xAB;
    printf("GPU 拷贝 A→B 4096 B 全等：期望 1 → 真结果 %d\nGPU 填充经 CPU 原指针读到：期望 1 → 真结果 %d\n同一字节经 A 的 contents 读到：期望 1 → 真结果 %d\n", ok1, ok2, ok3);
    return !(A && B && !bad && ok1 && ok2 && ok3);
}
