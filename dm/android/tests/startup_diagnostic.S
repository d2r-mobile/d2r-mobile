// 受控阶段入口：先经调用门安装本线程信号栈，再调用阶段并终止。
// Controlled stage entry: install this thread's signal stack through the gate before stage call and terminal exit.
#if defined(__APPLE__)
#define S(x) _##x
#else
#define S(x) x
#endif
.text
.p2align 2
.global S(dm_diag_template_begin)
.global S(dm_diag_template_end)
S(dm_diag_template_begin):
    mov x19,x0
    svc #0xfffe
    ldr x16,[x19]
    mov x0,x1
    mov x1,xzr
    blr x16
    str x0,[x19,#8]
    mov x9,#1
    str x9,[x19,#16]
    svc #0x0a
    brk #0xf42
S(dm_diag_template_end):
#if !defined(__APPLE__)
.section .note.GNU-stack,"",%progbits
#endif
