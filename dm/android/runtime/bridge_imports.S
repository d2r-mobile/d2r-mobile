// 六个普通 nn::fs 导入保留参数和 LR，仅传递类型 ID；共用桥在调用 C 前切回宿主栈。
// Six ordinary nn::fs imports preserve arguments and LR, passing only a type ID; the shared bridge switches to the native stack before C.
#if defined(__ELF__)
#define ENTRY(name) dm_bridge_romfs_##name
#define BRIDGE dm_bridge_import_entry
#else
#define ENTRY(name) _dm_bridge_romfs_##name
#define BRIDGE _dm_bridge_import_entry
#endif
.text
.p2align 2
.macro IMPORT name, id
.globl \name
\name:
    mov w17, #\id
    b BRIDGE
.endm
IMPORT ENTRY(query), 1
IMPORT ENTRY(mount), 2
IMPORT ENTRY(open), 3
IMPORT ENTRY(read), 4
IMPORT ENTRY(size), 5
IMPORT ENTRY(close), 6
#if defined(__ELF__)
.section .note.GNU-stack,"",%progbits
#endif
