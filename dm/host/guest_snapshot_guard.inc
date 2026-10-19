// #253 host snapshot guard: ordinary host writers hold a shared lease across
// prepare and stores; snapshots only try exclusive access and never wait.
// Fault handlers never take this lock. No queue/GPU waits or guest callbacks
// while held; order is queue -> host write lock -> dirty lock.
#include <errno.h>
#include <limits.h>
#include <pthread.h>

static pthread_rwlock_t g_guest_host_write_lock = PTHREAD_RWLOCK_INITIALIZER;
static _Thread_local unsigned g_guest_host_write_depth, g_guest_snapshot_depth;

void dm_guest_host_write_begin(void *addr, size_t len) {
    int saved_errno = errno;
    if (g_guest_snapshot_depth) die("host write inside guest snapshot");
    if (g_guest_host_write_depth == UINT_MAX) die("host write nesting overflow");
    if (!g_guest_host_write_depth) {
        int e = pthread_rwlock_rdlock(&g_guest_host_write_lock);
        if (e) die("host write rdlock failed: %d", e);
    }
    g_guest_host_write_depth++;
    dm_guest_write_prepare(addr, len);  // exactly once per begin, including nesting
    errno = saved_errno;
}

void dm_guest_host_write_end(void) {
    int saved_errno = errno;
    if (!g_guest_host_write_depth || g_guest_snapshot_depth) die("unpaired host write end");
    if (!--g_guest_host_write_depth) {
        int e = pthread_rwlock_unlock(&g_guest_host_write_lock);
        if (e) die("host write unlock failed: %d", e);
    }
    errno = saved_errno;
}

int dm_guest_snapshot_try_begin(void) {
    int saved_errno = errno;
    if (g_guest_host_write_depth) { errno = saved_errno; return 0; }
    if (g_guest_snapshot_depth == UINT_MAX) die("guest snapshot nesting overflow");
    if (!g_guest_snapshot_depth) {
        int e = pthread_rwlock_trywrlock(&g_guest_host_write_lock);
        if (e == EBUSY) { errno = saved_errno; return 0; }
        if (e) die("guest snapshot trywrlock failed: %d", e);
    }
    g_guest_snapshot_depth++;
    errno = saved_errno;
    return 1;
}

void dm_guest_snapshot_end(void) {
    int saved_errno = errno;
    if (!g_guest_snapshot_depth || g_guest_host_write_depth) die("unpaired guest snapshot end");
    if (!--g_guest_snapshot_depth) {
        int e = pthread_rwlock_unlock(&g_guest_host_write_lock);
        if (e) die("guest snapshot unlock failed: %d", e);
    }
    errno = saved_errno;
}
