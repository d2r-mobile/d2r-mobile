// 安卓构建：改写代码后刷指令缓存。
// Android build: flush the instruction cache after rewriting code.
#pragma once
#include <stddef.h>
static inline void sys_icache_invalidate(void *start, size_t len) { __builtin___clear_cache((char *)start, (char *)start + len); }
static inline void sys_dcache_flush(void *start, size_t len) { __builtin___clear_cache((char *)start, (char *)start + len); }
