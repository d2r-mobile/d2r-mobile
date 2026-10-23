// 安卓构建：malloc_zone_statistics（宿主只用 size_in_use 作内存读数）= bionic mallinfo 的在用字节。
// Android build: malloc_zone_statistics (the host only uses size_in_use as a memory reading) = the in-use bytes from bionic mallinfo.
#pragma once
#include <malloc.h>
#include <stddef.h>
typedef struct { unsigned blocks_in_use; size_t size_in_use, max_size_in_use, size_allocated; } malloc_statistics_t;
static inline void malloc_zone_statistics(void *zone, malloc_statistics_t *s) {
    (void)zone; struct mallinfo mi = mallinfo();
    s->blocks_in_use = 0; s->size_in_use = mi.uordblks; s->max_size_in_use = 0; s->size_allocated = mi.arena;
}
