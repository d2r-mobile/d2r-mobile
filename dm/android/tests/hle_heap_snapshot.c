// #253 owned regression for the protected heap-snapshot lease used by
// hle_svc.c. Compiles the production header directly; no mirrored policy.
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static uint8_t *g_heap;
static size_t g_heap_cap;
#define __ANDROID__ 1
#include "hle_heap_snapshot.h"

typedef struct { pthread_mutex_t mutex; pthread_cond_t condition; int waiting; } gate;
static void gate_wait(gate *g) {
    pthread_mutex_lock(&g->mutex);
    if (++g->waiting == 2) pthread_cond_broadcast(&g->condition);
    else while (g->waiting < 2) pthread_cond_wait(&g->condition, &g->mutex);
    pthread_mutex_unlock(&g->mutex);
}
static gate copy_gate;
static int threaded_copy_result;
static void *copy_under_writer(void *arg) {
    (void)arg;
    gate_wait(&copy_gate);
    threaded_copy_result = dm_android_guest_heap_copy_owned((uintptr_t)g_heap, (uint8_t[8]){0}, 8);
    return NULL;
}

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)

int main(void) {
    long page_long = sysconf(_SC_PAGESIZE);
    CHECK(page_long > 0);
    if (page_long <= 0) return 1;
    size_t page = (size_t)page_long, cap = page * 4;
    uint8_t *base = mmap(NULL, cap, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    CHECK(base != MAP_FAILED);
    if (base == MAP_FAILED) return 1;
    g_heap = base;
    g_heap_cap = cap;
    dm_heap_snapshot_ready();
    CHECK(dm_heap_snapshot_resize(page * 2) == 0);
    memset(base, 0x6b, page * 2);

    uint8_t destination[64];
    memset(destination, 0xa5, sizeof destination);
    CHECK(dm_android_guest_heap_copy_owned((uintptr_t)base + 16, destination, sizeof destination) == 1);
    CHECK(destination[0] == 0x6b && destination[63] == 0x6b);
    CHECK(dm_guest_heap_readable((uint64_t)(uintptr_t)base + 16, sizeof destination) == 1);

    // Outside the current extent and near-address overflow must refuse without
    // touching the caller's destination or changing errno.
    memset(destination, 0xa5, sizeof destination);
    errno = E2BIG;
    CHECK(dm_android_guest_heap_copy_owned((uintptr_t)base + page * 2, destination, sizeof destination) == 0);
    CHECK(errno == E2BIG);
    CHECK(destination[0] == 0xa5 && destination[63] == 0xa5);
    CHECK(dm_android_guest_heap_copy_owned(UINTPTR_MAX - 8, destination, sizeof destination) == 0);
    CHECK(destination[0] == 0xa5 && destination[63] == 0xa5);

    // Shrink closes the old tail; a later copy of that tail must refuse.
    CHECK(dm_heap_snapshot_resize(page) == 0);
    memset(destination, 0xa5, sizeof destination);
    CHECK(dm_android_guest_heap_copy_owned((uintptr_t)base + page, destination, sizeof destination) == 0);
    CHECK(destination[0] == 0xa5);
    CHECK(dm_android_guest_heap_copy_owned((uintptr_t)base, destination, sizeof destination) == 1);

    // Genuine cross-thread contention: the writer owns the lifecycle lock in
    // this thread while another thread attempts the snapshot copy. The gate is
    // cond/barrier based; no sleep or timing assumption.
    copy_gate = (gate){PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0};
    pthread_t copy_thread;
    CHECK(pthread_rwlock_wrlock(&g_heap_map_lock) == 0);
    CHECK(pthread_create(&copy_thread, NULL, copy_under_writer, NULL) == 0);
    gate_wait(&copy_gate);
    CHECK(pthread_join(copy_thread, NULL) == 0);
    CHECK(threaded_copy_result == 0);
    CHECK(pthread_rwlock_unlock(&g_heap_map_lock) == 0);

    // Protection failure closes leases permanently; no later copy is accepted.
    uint8_t *saved = g_heap;
    g_heap = base + 1;   // unaligned, mprotect must fail without touching the mapping
    CHECK(dm_heap_snapshot_resize(page * 2) != 0);
    CHECK(dm_android_guest_heap_copy_owned((uintptr_t)saved, destination, sizeof destination) == 0);
    g_heap = saved;

    // Re-open for cleanup, then unmap.
    (void)dm_heap_snapshot_resize(page);
    CHECK(munmap(base, cap) == 0);
    if (failures) return 1;
    puts("PASS heap snapshot lease: range/overflow/shrink/lock/protection/fallback");
    return 0;
}
