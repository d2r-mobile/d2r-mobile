// 自有客体仅用汇编调用六种导入，再经真实 SVC 门退出；每次返回检查 SP 和非易失状态。
// Owned guest calls all six imports only from assembly, then exits through the real SVC gate; check SP and nonvolatile state after each return.
#if defined(__ELF__)
#define ENTRY dm_test_import_guest
#define EXIT_RETURN dm_test_import_exit_return
#define GATE dm_gate
#define FINISH dm_test_import_finish
#else
#define ENTRY _dm_test_import_guest
#define EXIT_RETURN _dm_test_import_exit_return
#define GATE _dm_gate
#define FINISH _dm_test_import_finish
#endif
.text
.p2align 2
.macro CHECK_ABI
    mov x9, sp
    ldr x10, [x19, #64]
    cmp x9, x10
    b.ne 9f
    ldr x10, [x19, #72]
    cmp x18, x10
    b.ne 9f
    .irp n,20,21,22,23,24,25,26,27,28,29
        cmp x\n, #\n
        b.ne 9f
    .endr
    .irp n,8,9,10,11,12,13,14,15
        fmov x9, d\n
        cmp x9, #\n
        b.ne 9f
    .endr
.endm
.macro CALL offset
    ldr x16, [x19, #\offset]
    mov x8, #0x818
    blr x16
    CHECK_ABI
.endm
.globl ENTRY
ENTRY:
    mov x19, x0
    mov x9, sp
    str x9, [x19, #64]
    str x18, [x19, #72]
    .irp n,20,21,22,23,24,25,26,27,28,29
        mov x\n, #\n
    .endr
    .irp n,8,9,10,11,12,13,14,15
        mov x9, #\n
        fmov d\n, x9
    .endr
    add x0, x19, #80
    CALL 0
    cbnz x0, 9f
    ldr x9, [x19, #80]
    cmp x9, #4096
    b.ne 9f
    ldr x0, [x19, #56]
    mov x1, #0x1234
    mov x2, #0x5678
    CALL 8
    cbnz x0, 9f
    add x0, x19, #88
    ldr x1, [x19, #48]
    mov x2, #1
    movk x2, #1, lsl #32
    CALL 16
    cbnz x0, 9f
    add x0, x19, #104
    ldr x1, [x19, #88]
    mov x2, #2
    add x3, x19, #112
    mov x4, #8
    CALL 24
    cbnz x0, 9f
    ldr x9, [x19, #104]
    cmp x9, #8
    b.ne 9f
    add x0, x19, #96
    ldr x1, [x19, #88]
    CALL 32
    cbnz x0, 9f
    ldr x9, [x19, #96]
    cmp x9, #10
    b.ne 9f
    add x0, x19, #104
    ldr x1, [x19, #88]
    mov x2, #-1
    add x3, x19, #112
    mov x4, #8
    CALL 24
    cmp x0, #0x2a2
    b.ne 9f
    ldr x9, [x19, #104]
    cmp x9, #8
    b.ne 9f
    ldr x0, [x19, #88]
    CALL 40
    cbnz x0, 9f
    add x0, x19, #96
    ldr x1, [x19, #88]
    CALL 32
    cmp x0, #0x2a2
    b.ne 9f
    ldr x9, [x19, #96]
    cmp x9, #10
    b.ne 9f
    mov x9, #1
    str x9, [x19, #128]
.globl FINISH
FINISH:
    stp x29, x30, [sp, #-16]!
    bl GATE
.globl EXIT_RETURN
EXIT_RETURN:
    brk #1
9:  brk #2
#if defined(__ELF__)
.section .note.GNU-stack,"",%progbits
#endif
