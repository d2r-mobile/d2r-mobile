// 比较上下文入口前后的全部寄存器；x18 始终保留 Android 的真实平台值。
// Compare every register across the context entry; x18 always keeps Android's real platform value.
.text
.hidden dm_android_context_entry
.globl dm_android_context_register_probe
.type dm_android_context_register_probe, %function

// 两份快照各 800 字节：31 个 x、sp、32 个 q、NZCV / FPSR / FPCR、填充。
// Two 800-byte snapshots: 31 x registers, sp, 32 q registers, NZCV / FPSR / FPCR, padding.
.macro snapshot
    stp x0, x1, [x16, #0]
    stp x2, x3, [x16, #16]
    stp x4, x5, [x16, #32]
    stp x6, x7, [x16, #48]
    stp x8, x9, [x16, #64]
    stp x10, x11, [x16, #80]
    stp x12, x13, [x16, #96]
    stp x14, x15, [x16, #112]
    stp x16, x17, [x16, #128]
    stp x18, x19, [x16, #144]
    stp x20, x21, [x16, #160]
    stp x22, x23, [x16, #176]
    stp x24, x25, [x16, #192]
    stp x26, x27, [x16, #208]
    stp x28, x29, [x16, #224]
    str x30, [x16, #240]
    mov x17, sp
    str x17, [x16, #248]
    stp q0, q1, [x16, #256]
    stp q2, q3, [x16, #288]
    stp q4, q5, [x16, #320]
    stp q6, q7, [x16, #352]
    stp q8, q9, [x16, #384]
    stp q10, q11, [x16, #416]
    stp q12, q13, [x16, #448]
    stp q14, q15, [x16, #480]
    stp q16, q17, [x16, #512]
    stp q18, q19, [x16, #544]
    stp q20, q21, [x16, #576]
    stp q22, q23, [x16, #608]
    stp q24, q25, [x16, #640]
    stp q26, q27, [x16, #672]
    stp q28, q29, [x16, #704]
    stp q30, q31, [x16, #736]
    mrs x17, nzcv
    str x17, [x16, #768]
    mrs x17, fpsr
    str x17, [x16, #776]
    mrs x17, fpcr
    str x17, [x16, #784]
    ldr x17, [x16, #136]
.endm

dm_android_context_register_probe:
    sub sp, sp, #304
    stp x19, x20, [sp, #0]
    stp x21, x22, [sp, #16]
    stp x23, x24, [sp, #32]
    stp x25, x26, [sp, #48]
    stp x27, x28, [sp, #64]
    stp x29, x30, [sp, #80]
    stp q8, q9, [sp, #96]
    stp q10, q11, [sp, #128]
    stp q12, q13, [sp, #160]
    stp q14, q15, [sp, #192]
    str x0, [sp, #224]
    mrs x9, nzcv
    str x9, [sp, #232]
    mrs x9, fpsr
    str x9, [sp, #240]
    mrs x9, fpcr
    str x9, [sp, #248]
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,17,19,20,21,22,23,24,25,26,27,28,29
    mov x\n, #(0x1200 + \n)
    .endr
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    movi v\n\().16b, #(\n + 1)
    .endr
    mov x16, #0xa0000000
    msr nzcv, x16
    mov x16, #0x1f
    msr fpsr, x16
    mov x16, #0x400000
    msr fpcr, x16
    adr x30, .Lcontext_return
    ldr x16, [sp, #224]
    snapshot
    bl dm_android_context_entry
.Lcontext_return:
    // 先记返回的上下文地址，再借 x16 获取第二份快照。
    // Record the returned context address before borrowing x16 for the second snapshot.
    str x16, [sp, #256]
    ldr x16, [sp, #224]
    add x16, x16, #800
    snapshot
    ldr x17, [sp, #256]
    str x17, [x16, #128]
    ldr x9, [sp, #248]
    msr fpcr, x9
    ldr x9, [sp, #240]
    msr fpsr, x9
    ldr x9, [sp, #232]
    msr nzcv, x9
    ldp q8, q9, [sp, #96]
    ldp q10, q11, [sp, #128]
    ldp q12, q13, [sp, #160]
    ldp q14, q15, [sp, #192]
    ldp x19, x20, [sp, #0]
    ldp x21, x22, [sp, #16]
    ldp x23, x24, [sp, #32]
    ldp x25, x26, [sp, #48]
    ldp x27, x28, [sp, #64]
    ldp x29, x30, [sp, #80]
    add sp, sp, #304
    ret
.size dm_android_context_register_probe, .-dm_android_context_register_probe
.section .note.GNU-stack,"",%progbits
