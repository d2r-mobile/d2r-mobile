// gate_call(in, out)：保存宿主被调用者保存寄存器 → in[0..29] 装进 x0..x29（跳过 x18：苹果平台寄存器）→ blr _dm_gate
// → x0..x30 存进 out[0..30]、NZCV 存 out[31] → 恢复宿主寄存器返回。out 指针放在栈上（调用期间所有通用寄存器都被占用）。
// gate_call(in, out): save the host's callee-saved registers → load in[0..29] into x0..x29 (skipping x18: the Apple platform register) → blr _dm_gate
// → store x0..x30 into out[0..30] and NZCV into out[31] → restore the host registers and return. The out pointer is kept on the stack (all general-purpose registers are occupied during the call).
    .text
    .globl _gate_call
    .p2align 2
_gate_call:
    stp x29, x30, [sp, #-112]!
    stp x19, x20, [sp, #16]
    stp x21, x22, [sp, #32]
    stp x23, x24, [sp, #48]
    stp x25, x26, [sp, #64]
    stp x27, x28, [sp, #80]
    str x1, [sp, #96]                 // out
    mov x16, x0                       // in
    ldp x0, x1, [x16, #0]
    ldp x2, x3, [x16, #16]
    ldp x4, x5, [x16, #32]
    ldp x6, x7, [x16, #48]
    ldp x8, x9, [x16, #64]
    ldp x10, x11, [x16, #80]
    ldp x12, x13, [x16, #96]
    ldr x14, [x16, #112]
    ldr x15, [x16, #120]
    ldr x17, [x16, #136]
    ldr x19, [x16, #152]
    ldp x20, x21, [x16, #160]
    ldp x22, x23, [x16, #176]
    ldp x24, x25, [x16, #192]
    ldp x26, x27, [x16, #208]
    ldp x28, x29, [x16, #224]
    ldr x16, [x16, #128]              // 最后装 x16 本身 | Load x16 itself last
    cmp xzr, xzr                      // NZCV = Z|C（0x60000000），返回后核门是否恢复 | NZCV = Z|C (0x60000000); after return, check whether the gate restored it
    bl _dm_gate                       // x30 = 返回点（门标识） | x30 = return point (gate identifier)
    str x16, [sp, #-16]!              // 暂存 x16，拿 out 指针 | Stash x16, get the out pointer
    ldr x16, [sp, #112]
    stp x0, x1, [x16, #0]
    stp x2, x3, [x16, #16]
    stp x4, x5, [x16, #32]
    stp x6, x7, [x16, #48]
    stp x8, x9, [x16, #64]
    stp x10, x11, [x16, #80]
    stp x12, x13, [x16, #96]
    stp x14, x15, [x16, #112]
    ldr x0, [sp], #16                 // 原 x16 | Original x16
    str x0, [x16, #128]
    str x17, [x16, #136]
    str x19, [x16, #152]
    stp x20, x21, [x16, #160]
    stp x22, x23, [x16, #176]
    stp x24, x25, [x16, #192]
    stp x26, x27, [x16, #208]
    stp x28, x29, [x16, #224]
    str x30, [x16, #240]
    mrs x0, nzcv                      // 存取指令不改标志位，此处仍是门返回时的 NZCV | Load/store instructions do not change the flags; here it is still the NZCV as of gate return
    str x0, [x16, #248]
    ldp x19, x20, [sp, #16]
    ldp x21, x22, [sp, #32]
    ldp x23, x24, [sp, #48]
    ldp x25, x26, [sp, #64]
    ldp x27, x28, [sp, #80]
    ldp x29, x30, [sp], #112
    ret
