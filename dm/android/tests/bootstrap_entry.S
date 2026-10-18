// 自有 SDK 式入口：记录原始 SP 并尾调用顶部回调，用于验证保留区与参数，不含游戏指令。
// Owned SDK-like entry: record raw SP and tail-call the top callback to verify reserve/argument handling; contains no game instructions.
#if defined(__ELF__)
#define ENTRY dm_test_sdk_entry
#else
#define ENTRY _dm_test_sdk_entry
#endif
.text
.p2align 2
.globl ENTRY
ENTRY:
    mov x9, sp
    str x9, [x0, #0x28]
    ldr x16, [sp]
    br x16
#if defined(__ELF__)
.section .note.GNU-stack,"",%progbits
#endif
