// 调用门 SIMD / 浮点状态与 x18 验证；全部输入是合成数据，未调用游戏代码。
// Gate SIMD / floating-point state and x18 verification; all inputs are synthetic, no game code is called.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

void gate_vectors_capture(uint8_t *vectors, uint64_t *flags);
void dm_thread_returned(void) { abort(); }
void unimpl_c(void) { abort(); }

int main(void) {
    // Given：每个向量不同的字节图案以及不同的处理器标志。
    // Given: a distinct byte pattern per vector and distinct processor flags.
    _Alignas(16) uint8_t vectors[32][16] = {{0}};
    uint64_t flags[5] = {0};
    // When：经真实 ARM64 调用门执行故意破坏状态的回调。
    // When: run a deliberately clobbering callback through the real ARM64 gate.
    gate_vectors_capture(&vectors[0][0], flags);
    // Then：全 128 位 SIMD、浮点状态、NZCV、平台 x18 都保持原值。
    // Then: full 128-bit SIMD, floating-point state, NZCV and platform x18 retain their values.
    for (unsigned n = 0; n < 32; n++) for (unsigned b = 0; b < 16; b++) {
        if (vectors[n][b] != n + 1) {
            fprintf(stderr, "FAIL: v%u byte %u = %u\n", n, b, vectors[n][b]); return 1;
        }
    }
    if (flags[0] != 0x08000000 || flags[1] != 0x00800000 ||
        flags[2] != 0x60000000 || flags[3] != flags[4]) {
        fputs("FAIL: FPSR / FPCR / NZCV / x18 preservation\n", stderr); return 1;
    }
    puts("PASS: all 32 full SIMD registers, FPSR, FPCR, NZCV and reserved x18");
    return 0;
}
