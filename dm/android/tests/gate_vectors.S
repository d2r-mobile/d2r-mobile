// 合成门测试：故意破坏全部 SIMD 与标志，验证上游调用门恢复；按 AAPCS64 保存测试调用者状态。
// Synthetic gate test: deliberately clobber all SIMD / flags and verify gate restoration; preserve the test caller per AAPCS64.
#if defined(__ELF__)
#define CAPTURE gate_vectors_capture
#define DISPATCH gate_dispatch
#define GATE dm_gate
#else
#define CAPTURE _gate_vectors_capture
#define DISPATCH _gate_dispatch
#define GATE _dm_gate
#endif
.text
.p2align 2
.globl CAPTURE
CAPTURE:
    stp x29, x30, [sp, #-192]!
    stp q8, q9, [sp, #16]
    stp q10, q11, [sp, #48]
    stp q12, q13, [sp, #80]
    stp q14, q15, [sp, #112]
    stp x0, x1, [sp, #144]
    mrs x9, fpsr
    str x9, [sp, #160]
    mrs x9, fpcr
    str x9, [sp, #168]
    mrs x9, nzcv
    str x9, [sp, #176]
    str x18, [x1, #24]
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
        movi v\n\().16b, #(\n + 1)
    .endr
    mov x9, #0x08000000
    msr fpsr, x9
    mov x9, #0x00800000
    msr fpcr, x9
    mov x9, #0x60000000
    msr nzcv, x9
    bl GATE
    ldp x0, x1, [sp, #144]
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
        str q\n, [x0, #(\n * 16)]
    .endr
    mrs x9, fpsr
    str x9, [x1]
    mrs x9, fpcr
    str x9, [x1, #8]
    mrs x9, nzcv
    str x9, [x1, #16]
    str x18, [x1, #32]
    ldr x9, [sp, #160]
    msr fpsr, x9
    ldr x9, [sp, #168]
    msr fpcr, x9
    ldr x9, [sp, #176]
    msr nzcv, x9
    ldp q8, q9, [sp, #16]
    ldp q10, q11, [sp, #48]
    ldp q12, q13, [sp, #80]
    ldp q14, q15, [sp, #112]
    ldp x29, x30, [sp], #192
    ret

// 该测试回调故意不遵守 SIMD 被调用者保存约定，用来发现门的任何漏存漏恢复。
// This test callback deliberately violates SIMD callee preservation to catch any missing gate saves / restores.
.globl DISPATCH
.p2align 2
DISPATCH:
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
        movi v\n\().16b, #0
    .endr
    msr fpsr, xzr
    msr fpcr, xzr
    msr nzcv, xzr
    mov x0, #0
    ret
#if defined(__ELF__)
.section .note.GNU-stack,"",%progbits
#endif
