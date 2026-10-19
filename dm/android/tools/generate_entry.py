#!/usr/bin/env python3
"""保留上游完整寄存器门，为 ELF TLS 生成专用上下文入口。
Preserve the upstream full-register gate and generate a dedicated ELF TLS context entry.
"""
from pathlib import Path
import re
import sys

source = Path(__file__).resolve().parents[2] / "host/entry.S"
text = source.read_text()
symbols = ["unimpl_stubs", "unimpl_entry", "unimpl_c", "dm_gate", "gate_dispatch", "dm_thread_enter", "dm_thread_returned"]
for symbol in symbols:
    text = re.sub(r"\b_" + symbol + r"\b", symbol, text)
helper = """
#include "runtime_context_cache.h"
// TLSDESC 解析器保留除 x0 和条件标志之外的全部通用 / SIMD 寄存器。
// TLSDESC resolvers preserve every general-purpose / SIMD register except x0 and condition flags.
// https://android.googlesource.com/platform/bionic/+/refs/heads/main/linker/arch/arm64/tlsdesc_resolver.S
// 本入口只输出 x16；保存自身借用的 x0 / x1 / x2 / lr，并完整保存标志与浮点控制状态。
// This entry returns only x16; save its scratch x0 / x1 / x2 / lr and all flag / floating-point control state.
// 直接调用描述符中的解析器，不调用普通 C 函数或经过可破坏 x16 / x17 的 PLT。
// Call the descriptor resolver directly, avoiding ordinary C calls and PLT clobbers of x16 / x17.
    .hidden dm_android_context_storage
    .hidden dm_android_context_entry
    .globl dm_android_context_entry
    .type dm_android_context_entry, %function
    .p2align 2
dm_android_context_entry:
    sub sp, sp, #64
    stp x0, x1, [sp, #0]
    // 只查询当前TP；不驱逐及仅所有者撤销保证命中项保持有效。
    // Query only the current TP: no eviction and owner-only withdrawal keep a hit valid.
    // 快路异或及零分支不修改条件标志；只有慢路保存和恢复 NZCV。
    // XOR and zero branches preserve flags on hits; only the slow path saves/restores NZCV.
    mrs x16, tpidr_el0
    eor x0, x16, x16, lsr #DM_ANDROID_CONTEXT_CACHE_HASH_SHIFT
    lsr x0, x0, #4
    and x0, x0, #(DM_ANDROID_CONTEXT_CACHE_SLOTS - 1)
    adrp x1, dm_android_context_cache
    add x1, x1, :lo12:dm_android_context_cache
    add x1, x1, x0, lsl #DM_ANDROID_CONTEXT_CACHE_ENTRY_SHIFT
    add x1, x1, #8
    ldar x0, [x1]
    eor x0, x0, x16
    cbnz x0, .Lcontext_cache_miss
    ldr x16, [x1, #8]
    b .Lcontext_restore_integer
.Lcontext_cache_miss:
    // 命中不调用也不用x2；只有慢路保存x2/lr和控制状态。
    // Hits neither call nor use x2; only the slow path saves x2/lr and control state.
    stp x2, x30, [sp, #16]
    mrs x2, nzcv
    str x2, [sp, #32]
    mrs x2, fpsr
    str x2, [sp, #40]
    mrs x2, fpcr
    str x2, [sp, #48]
    adrp x0, :tlsdesc:dm_android_context_storage
    ldr x1, [x0, :tlsdesc_lo12:dm_android_context_storage]
    add x0, x0, :tlsdesc_lo12:dm_android_context_storage
    .tlsdesccall dm_android_context_storage
    blr x1
    mrs x16, tpidr_el0
    add x16, x16, x0
    // 通常解析器不改变浮点状态；相同时避免代价很高的控制寄存器写入。
    // Resolvers normally leave FP state intact; avoid costly control-register writes when unchanged.
    mrs x2, fpcr
    ldr x1, [sp, #48]
    cmp x2, x1
    b.eq 1f
    msr fpcr, x1
1:  mrs x2, fpsr
    ldr x1, [sp, #40]
    cmp x2, x1
    b.eq 2f
    msr fpsr, x1
2:
    ldr x2, [sp, #32]
    msr nzcv, x2
    ldp x2, x30, [sp, #16]
.Lcontext_restore_integer:
    ldp x0, x1, [sp, #0]
    add sp, sp, #64
    ret
    .size dm_android_context_entry, .-dm_android_context_entry
"""
Path(sys.argv[1]).write_text(text + "\n" + helper + '\n.section .note.GNU-stack,"",%progbits\n')
