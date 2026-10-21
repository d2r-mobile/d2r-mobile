// 客体线程栈切换与原生返回；依据 AAPCS64 保存宿主非易失寄存器，退出时先恢复宿主栈再回收客体栈。
// Guest-thread stack switch / native return; preserve host nonvolatile AAPCS64 registers and restore its stack before releasing guest memory.
#if defined(__ELF__)
#define RUN dm_thread_stack_run
#define LEAVE dm_thread_stack_leave
#define ENTER dm_thread_stack_enter
#else
#define RUN _dm_thread_stack_run
#define LEAVE _dm_thread_stack_leave
#define ENTER _dm_thread_stack_enter
#endif
.text
.p2align 2
.globl RUN
RUN:
    sub sp, sp, #256
    stp x19, x20, [sp, #0]
    stp x21, x22, [sp, #16]
    stp x23, x24, [sp, #32]
    stp x25, x26, [sp, #48]
    stp x27, x28, [sp, #64]
    stp x29, x30, [sp, #80]
    str x18, [sp, #96]
    mrs x9, fpcr
    str x9, [sp, #104]
    mrs x9, fpsr
    str x9, [sp, #112]
    stp q8, q9, [sp, #128]
    stp q10, q11, [sp, #160]
    stp q12, q13, [sp, #192]
    stp q14, q15, [sp, #224]
    mov x9, sp
    str x9, [x3]
    mov x16, x0
    mov x0, x1
    mov sp, x2
    mov x29, xzr
    adr x30, 1f
    br x16
1:  brk #0
    b 1b
.p2align 2
.globl ENTER
ENTER:
    mov x16, x0
    mov x0, x1
    mov sp, x2
    mov x29, xzr
    adr x30, 2f
    br x16
2:  brk #0
    b 2b
.p2align 2
.globl LEAVE
LEAVE:
    mov sp, x0
    ldp x19, x20, [sp, #0]
    ldp x21, x22, [sp, #16]
    ldp x23, x24, [sp, #32]
    ldp x25, x26, [sp, #48]
    ldp x27, x28, [sp, #64]
    ldp x29, x30, [sp, #80]
    ldr x18, [sp, #96]
    ldr x9, [sp, #104]
    msr fpcr, x9
    ldr x9, [sp, #112]
    msr fpsr, x9
    ldp q8, q9, [sp, #128]
    ldp q10, q11, [sp, #160]
    ldp q12, q13, [sp, #192]
    ldp q14, q15, [sp, #224]
    add sp, sp, #256
    ret
#if defined(__ELF__)
.section .note.GNU-stack,"",%progbits
#endif
