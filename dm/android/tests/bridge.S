// 自有寄存器哨兵验证真实改写门；退出后的哨兵永远不得执行。
// Owned register sentinels validate real rewritten gates; the post-exit sentinel must never execute.
#if defined(__APPLE__)
#define ENTRY _dm_test_bridge_guest
#define FAIL _dm_test_bridge_fail
#else
#define ENTRY dm_test_bridge_guest
#define FAIL dm_test_bridge_fail
#endif
.text
.p2align 2
.macro VERIFY
    mov x9,sp
    ldr x10,[x19,#32]
    cmp x9,x10
    b.ne 9f
    ldr x10,[x19,#40]
    cmp x18,x10
    b.ne 9f
    .irp n,20,21,22,23,24,25,26,27,28,29
    cmp x\n,#\n
    b.ne 9f
    .endr
    mrs x9,fpcr
    cmp x9,#0x400000
    b.ne 9f
    .irp n,8,9,10,11,12,13,14,15
    fmov x9,d\n
    cmp x9,#\n
    b.ne 9f
    .endr
.endm
.globl ENTRY
ENTRY:
    mov x19,x0
    ldr x9,[x19,#104]
    cbz x9,1f
    mov sp,x9
1:  mov x9,sp
    str x9,[x19,#32]
    str x18,[x19,#40]
    .irp n,20,21,22,23,24,25,26,27,28,29
    mov x\n,#\n
    .endr
    .irp n,8,9,10,11,12,13,14,15
    mov x9,#\n
    fmov d\n,x9
    .endr
    mov x9,#0x400000
    msr fpcr,x9
    mov w1,#0xffff8000
    ldr x16,[x19]
    blr x16
    VERIFY
    cbnz x0,9f
    str x1,[x19,#48]
    ldr x16,[x19,#8]
    blr x16
    VERIFY
    str x0,[x19,#56]
    mov x0,#0
    add x1,x19,#80
    mov x2,#1
    mov x3,#-1
    ldr x16,[x19,#16]
    blr x16
    VERIFY
    cbnz x0,9f
    mov x9,#1
    str x9,[x19,#64]
    ldr x16,[x19,#24]
    blr x16
    mov x9,#1
    str x9,[x19,#72]
9:  mov x3,x0
    mov x0,x19
    mov x1,x9
    mov x2,x10
    bl FAIL
    brk #0xb18
#if defined(__ELF__)
.section .note.GNU-stack,"",%progbits
#endif
