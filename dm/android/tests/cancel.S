// 纯汇编客体通过真实改写 SVC 取消并等待，再从终止门退出。
// Pure assembly guest cancels and waits through real rewritten SVCs, then exits through the terminal gate.
#if defined(__APPLE__)
#define ENTRY _dm_cancel_guest
#else
#define ENTRY dm_cancel_guest
#endif
.text
.p2align 2
.macro VERIFY
    mov x9,sp
    ldr x10,[x19,#40]
    cmp x9,x10
    b.ne 9f
    ldr x10,[x19,#48]
    cmp x18,x10
    b.ne 9f
.endm
.globl ENTRY
ENTRY:
    mov x19,x0
    mov x9,sp
    str x9,[x19,#40]
    str x18,[x19,#48]
    ldr x10,[x19,#32]
    cmp x9,x10
    b.ne 9f
    ldr x9,[x19,#96]
    cbz x9,1f
    ldr x0,[x19,#88]
    movk x0,#1,lsl #32
    ldr x16,[x19]
    blr x16
    VERIFY
    cbnz x0,9f
1:  add x1,x19,#24
    mov x2,#1
    mov x3,#-1
    ldr x16,[x19,#8]
    blr x16
    VERIFY
    mov x9,#0xec01
    cmp x0,x9
    b.ne 9f
    str x0,[x19,#56]
    str x1,[x19,#64]
    mov x9,#1
    str x9,[x19,#72]
    ldr x16,[x19,#16]
    blr x16
    mov x9,#1
    str x9,[x19,#80]
9:  brk #0xc19
#if defined(__ELF__)
.section .note.GNU-stack,"",%progbits
#endif
