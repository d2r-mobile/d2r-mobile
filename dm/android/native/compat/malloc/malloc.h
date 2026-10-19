// Android 分配器在用字节数；不伪造未提供的分配器统计。
// Android allocator bytes in use; do not fabricate unavailable allocator statistics.
#pragma once
#include <malloc.h>
#include <stddef.h>
typedef struct { size_t size_in_use; } malloc_statistics_t;
static inline void malloc_zone_statistics(void *zone, malloc_statistics_t *statistics) {
    (void)zone; statistics->size_in_use = mallinfo().uordblks;
}
