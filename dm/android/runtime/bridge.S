// 调用前仅用汇编查找 TP 注册表；原生服务帧位于 RUN 保存帧下方，拒绝重入。
// Look up the TP registry in assembly before calling C; native service frames sit below RUN, with reentry rejected.
#include "bridge.h"
#if defined(__APPLE__)
#define SYM(x) _##x
#else
#define SYM(x) x
#endif
.text
.p2align 2
#if defined(__ELF__)
.hidden dm_bridge_gate_entry
.hidden dm_bridge_import_entry
#endif
.globl SYM(dm_bridge_gate_entry)
SYM(dm_bridge_gate_entry):
    mov w17, #0
    b .Lentry
.globl SYM(dm_bridge_import_entry)
SYM(dm_bridge_import_entry):
.Lentry:
#if defined(__APPLE__)
    mrs x9, tpidrro_el0
    and x9, x9, #-8
    adrp x10, _dm_bridge_records@PAGE
    add x10, x10, _dm_bridge_records@PAGEOFF
#else
    mrs x9, tpidr_el0
    adrp x10, dm_bridge_records
    add x10, x10, :lo12:dm_bridge_records
#endif
    cbz x9, .Ltrap
    mov w11, #DM_BRIDGE_SLOTS
.Llookup:
    ldar x12, [x10]
    cmp x12, x9
    b.eq .Lfound
    add x10, x10, #DM_BRIDGE_RECORD_SIZE
    subs w11, w11, #1
    b.ne .Llookup
    b .Ltrap
.Lfound:
    ldr w11, [x10, #88]
    cmp w11, #DM_BRIDGE_GUEST
    b.hi .Ltrap
    ldr x12, [x10, #80]
    cbnz x12, .Ltrap
    mov x12, sp
    tst x12, #15
    b.ne .Ltrap
    cbnz w11, .Lguest
    ldp x13, x14, [x10, #24]
    cmp x12, x14
    b.hi .Ltrap
    sub x15, x12, #DM_BRIDGE_FRAME_SIZE
    cmp x15, x13
    b.lo .Ltrap
    b .Lclaim
.Lguest:
    ldp x13, x14, [x10, #40]
    cmp x12, x13
    b.lo .Ltrap
    cmp x12, x14
    b.hi .Ltrap
    ldr w13, [x10, #92]
    cbz w13, .Lheadroom_ok
    ldr x13, [x10, #40]
    sub x13, x12, x13
    cmp x13, #32768
    b.lo .Ltrap
.Lheadroom_ok:
    cbnz w17, .Lguest_run
    cmp x1, x12
    b.ne .Ltrap
    add x13, x12, #800
    cmp x13, x14
    b.hi .Ltrap
.Lguest_run:
    ldr x13, [x10, #16]
    cbz x13, .Ltrap
    ldr x13, [x13]
    tst x13, #15
    b.ne .Ltrap
    sub x15, x13, #DM_BRIDGE_FRAME_SIZE
    ldr x14, [x10, #24]
    cmp x15, x14
    b.lo .Ltrap
    add x13, x13, #256
    ldr x14, [x10, #32]
    cmp x13, x14
    b.hi .Ltrap
.Lclaim:
    // 先封闭域，再写可重用帧；信号处理器若误入调用门会直接陷阱。
    // Close the domain before writing the reusable frame; unsupported signal reentry traps.
    mov w13, #DM_BRIDGE_SERVICE
    str w13, [x10, #88]
    str x15, [x10, #80]
    stp x12, x30, [x15]
    stp x10, x11, [x15, #16]
    stp wzr, w17, [x15, #32]
    str xzr, [x15, #40]
    mrs x13, fpcr
    mrs x14, fpsr
    stp x13, x14, [x15, #48]
    stp x18, x29, [x15, #64]
    stp x0, x1, [x15, #80]
    stp x2, x3, [x15, #96]
    stp x4, x5, [x15, #112]
    stp x6, x7, [x15, #128]
    str x8, [x15, #144]
    mov x0, x15
    ldr x10, [x0, #16]
    ldr w10, [x10, #92]
    cbz w10, .Lsource_done
    ldr x10, [x0, #24]
    cbz x10, .Lsource_done
    bl SYM(dm_bridge_source)
.Lsource_done:
    mov sp, x0
    ldr x10, [x0, #24]
    cbz x10, .Lnative_ready
    ldr x10, [x0, #16]
    ldr x10, [x10, #16]
    ldr x10, [x10]
    ldr x18, [x10, #96]
    ldp x11, x12, [x10, #104]
    msr fpcr, x11
    msr fpsr, x12
    ldr x29, [x10, #80]
.Lnative_ready:
    bl SYM(dm_bridge_arrive)
    mov x0, sp
    bl SYM(dm_bridge_depart)
    ldr x10, [x0, #16]
    ldr w11, [x0, #32]
    cbnz w11, .Lterminal
    ldr x12, [x0]
    mov sp, x12
    ldr x10, [x0, #16]
    ldr w10, [x10, #92]
    cbz w10, .Lresume_done
    ldr x10, [x0, #24]
    cbz x10, .Lresume_done
    bl SYM(dm_bridge_resume)
.Lresume_done:
    mov x16, x0
    ldp x11, x12, [x16, #48]
    msr fpcr, x11
    msr fpsr, x12
    ldp x18, x29, [x16, #64]
    ldr x30, [x16, #8]
    ldr x0, [x16, #40]
    ldr x10, [x16, #16]
    ldr w11, [x16, #24]
    str xzr, [x10, #80]
    str w11, [x10, #88]
    ret
.Lterminal:
    ldr w11, [x0, #24]
    cmp w11, #DM_BRIDGE_GUEST
    b.ne .Ltrap
    mov w11, #DM_BRIDGE_EXITING
    str w11, [x10, #88]
    str xzr, [x10, #80]
    ldr w11, [x10, #92]
    cbnz w11, .Lretire_sanitized
    // 生产终止直接回原生 RUN，不在小客体栈调用 C 退休辅助函数。
    // Production termination leaves native RUN directly, without a C retirement helper on the small guest stack.
    ldr x0, [x10, #16]
    ldr x0, [x0]
    b SYM(dm_thread_stack_leave)
.Lretire_sanitized:
    ldr x12, [x0]
    mov sp, x12
    bl SYM(dm_bridge_retire)
    b SYM(dm_thread_stack_leave)
// 未解析导入／旧式返回的有界终止，不调用宿主 C；导入入口此前保存了 96 字节。
// Bounded unresolved-import/legacy-return termination without host C; the import entry already saved 96 bytes.
.globl SYM(dm_bridge_fatal_entry)
#if defined(__ELF__)
.hidden dm_bridge_fatal_entry
#endif
SYM(dm_bridge_fatal_entry):
    brk #0xd19
    b SYM(dm_bridge_fatal_entry)
.Ltrap:
    brk #0xd18
#if defined(__ELF__)
.section .note.GNU-stack,"",%progbits
#endif
