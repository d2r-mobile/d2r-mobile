// 为 ELF 目标适配上游 ARM64 调用门的符号名，寄存器保存逻辑直接复用 host/entry.S。
// Adapt upstream ARM64 gate symbol names for ELF targets; reuse register preservation directly from host/entry.S.
#if defined(__ELF__)
#define _unimpl_stubs unimpl_stubs
#define _unimpl_entry unimpl_entry
#if defined(DM_RUNTIME_BRIDGE)
#define _unimpl_c dm_bridge_fatal_entry
#else
#define _unimpl_c unimpl_c
#endif
#define _dm_gate dm_gate
#if defined(DM_RUNTIME_BRIDGE)
#define _gate_dispatch dm_bridge_gate_entry
#else
#define _gate_dispatch gate_dispatch
#endif
#define _dm_thread_enter dm_thread_enter
#if defined(DM_RUNTIME_BRIDGE)
#define _dm_thread_returned dm_bridge_fatal_entry
#else
#define _dm_thread_returned dm_thread_returned
#endif
#endif
#if defined(DM_RUNTIME_BRIDGE) && defined(__APPLE__)
#define _gate_dispatch _dm_bridge_gate_entry
#define _unimpl_c _dm_bridge_fatal_entry
#define _dm_thread_returned _dm_bridge_fatal_entry
#endif
#include "../host/entry.S"
#if defined(__ELF__) && defined(DM_RUNTIME_BRIDGE)
.hidden unimpl_entry
#endif
#if defined(__ELF__)
.section .note.GNU-stack,"",%progbits
#endif
