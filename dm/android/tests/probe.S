// MIT-licensed Android compatibility implementation.
.text
.globl probe
probe:
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    stp x19, x20, [sp, #-16]!
    mov x19, x0
    fmov d0, #2.0
    cmp x1, #123
    mrs x6, nzcv
    str x6, [x19, #48]
    mov x18, x1
    add x18, x18, #7
    mrs x2, tpidr_el0
    msr tpidr_el0, x18
    mrs x3, tpidr_el0
    mrs x4, tpidrro_el0
    mrs x20, cntpct_el0
    svc #0x42
    str x0, [x19, #0]
    str x18, [x19, #8]
    str x3, [x19, #16]
    str x4, [x19, #24]
    str x20, [x19, #32]
    str d0, [x19, #40]
    mrs x6, nzcv
    str x6, [x19, #56]
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    ret
