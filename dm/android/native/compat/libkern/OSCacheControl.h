// 清理 AArch64 指令缓存，供宿主诊断跳板使用。| Flush the AArch64 instruction cache for host diagnostic trampolines.
#pragma once
#include <stddef.h>
static inline void sys_icache_invalidate(void *start, size_t length) {
    __builtin___clear_cache((char *)start, (char *)start + length);
}
