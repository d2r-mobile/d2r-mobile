// 复用上游寄存器测试台，ELF 目标仅适配符号名。
// Reuse the upstream register test harness, adapting only symbol names on ELF targets.
#if defined(__ELF__)
#define _gate_call gate_call
#define _dm_gate dm_gate
#endif
#include "../../verify/gatefn/gate_tramp.S"
#if defined(__ELF__)
.section .note.GNU-stack,"",%progbits
#endif
