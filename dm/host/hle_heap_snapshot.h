// #253 protected guest-heap snapshot lease. Kept beside the HLE heap so
// resize/protection changes and the copy helper share one state machine.
// Android only: the snapshot helper has no caller on the Apple host.
#ifndef DM_HLE_HEAP_SNAPSHOT_H
#define DM_HLE_HEAP_SNAPSHOT_H
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>

#ifdef __ANDROID__
// g_heap/g_heap_cap are owned by hle_svc.c and included before this header.
static _Atomic size_t g_heap_cur;
static _Atomic int g_heap_mem_ready;
static pthread_rwlock_t g_heap_map_lock = PTHREAD_RWLOCK_INITIALIZER;
// A failed mprotect may have changed only part of a VMA. Disable leases for
// the rest of the process; a later successful delta must not re-enable them.
static int g_heap_leases_enabled = 1;

static size_t dm_heap_snapshot_size(void) {
    return atomic_load_explicit(&g_heap_cur, memory_order_relaxed);
}

void dm_heap_snapshot_ready(void) {
    atomic_store_explicit(&g_heap_mem_ready, 1, memory_order_release);
}

int dm_guest_heap_readable(uint64_t address, size_t size) {
    if (!atomic_load_explicit(&g_heap_mem_ready, memory_order_acquire)) return 0;
    uint64_t base = (uint64_t)(uintptr_t)g_heap;
    size_t current = dm_heap_snapshot_size();
    return g_heap && address >= base && size <= current && address - base <= current - size;
}

// 只在持有源权限租约时复制；目的缓冲区由调用方独占且与源不重叠。
// Copy only while holding the source permission lease; the caller owns a
// complete, writable, non-overlapping destination.
int dm_android_guest_heap_copy_owned(uintptr_t source, void *destination, size_t size) {
    int saved_errno = errno, copied = 0;
    if (!size || !destination || !atomic_load_explicit(&g_heap_mem_ready, memory_order_acquire)) goto done;
    if (pthread_rwlock_tryrdlock(&g_heap_map_lock)) goto done;
    uintptr_t base = (uintptr_t)g_heap;
    size_t current = dm_heap_snapshot_size();
    if (g_heap_leases_enabled && source >= base && size <= current && source - base <= current - size) {
        memcpy(destination, (const void *)source, size);
        copied = 1;
    }
    pthread_rwlock_unlock(&g_heap_map_lock);
done:
    errno = saved_errno;
    return copied;
}

// Resize the readable/writable heap extent atomically with respect to the
// snapshot lease. Returns 0 on success; on failure leases stay closed.
int dm_heap_snapshot_resize(size_t size) {
    int rc = 0;
    if (pthread_rwlock_wrlock(&g_heap_map_lock)) return -1;
    size_t previous = dm_heap_snapshot_size();
    if (size > g_heap_cap) {
        errno = EINVAL;
        rc = -1;
    } else if (size > previous) {
        rc = mprotect(g_heap + previous, size - previous, PROT_READ | PROT_WRITE);
    } else if (size < previous) {
        rc = mprotect(g_heap + size, previous - size, PROT_NONE);
    }
    if (rc) {
        g_heap_leases_enabled = 0;
    } else {
        atomic_store_explicit(&g_heap_cur, size, memory_order_release);
    }
    pthread_rwlock_unlock(&g_heap_map_lock);
    return rc;
}
#else
static size_t g_heap_cur;
static size_t dm_heap_snapshot_size(void) { return g_heap_cur; }
// Apple host keeps the original range observation used by diag.c. It is not a
// permission lease and must not be used to authorize an unguarded copy.
int dm_guest_heap_readable(uint64_t address, size_t size) {
    uint64_t base = (uint64_t)(uintptr_t)g_heap;
    size_t current = dm_heap_snapshot_size();
    return g_heap && address >= base && size <= current && address - base <= current - size;
}
#endif
#endif
