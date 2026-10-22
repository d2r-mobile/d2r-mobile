#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

#include "../hle_thread.c"
void (*dm_test_cv_after_release)(void);
static _Atomic uint32_t word;
static unsigned total;
static _Atomic int inside;
#define CHECK(x) do { if (!(x)) abort(); } while (0)
static void acquire(uint32_t tag) {
    for (;;) {
        uint32_t current = atomic_load(&word);
        if (!current) { if (atomic_compare_exchange_weak(&word, &current, tag)) return; continue; }
        if ((current & ~0x40000000u) == tag) return;
        if (!(current & 0x40000000u) && !atomic_compare_exchange_weak(&word, &current, current | 0x40000000u)) continue;
        uint64_t registers[8] = {current & ~0x40000000u, (uintptr_t)&word, tag};
        CHECK(svc_thread(0x1A, registers) && !registers[0]);
    }
}
static void release(uint32_t tag) {
    uint32_t expected = tag;
    if (!atomic_compare_exchange_strong(&word, &expected, 0)) {
        CHECK((expected & ~0x40000000u) == tag);
        uint64_t registers[8] = {(uintptr_t)&word};
        CHECK(svc_thread(0x1B, registers) && !registers[0]);
    }
}
static void *worker(void *unused) {
    (void)unused;
    thr_t *thread = calloc(1, sizeof *thread); CHECK(thread);
    CHECK(!pthread_cond_init(&thread->cv, NULL)); thread->prio = 44;
    pthread_mutex_lock(&G); thread->handle = h_new(O_THREAD, thread); pthread_mutex_unlock(&G);
    CHECK(!pthread_setspecific(g_keythr, thread)); uint32_t tag = thread->handle;
    for (unsigned n = 0; n < 3000; n++) {
        acquire(tag); CHECK(atomic_fetch_add(&inside, 1) == 0);
        total++; if (!(n % 300)) usleep(2000);
        CHECK(atomic_fetch_sub(&inside, 1) == 1); release(tag);
    }
    pthread_mutex_lock(&G); g_h[tag - 0x1000].type = 0; g_h[tag - 0x1000].obj = NULL; pthread_mutex_unlock(&G);
    CHECK(!pthread_setspecific(g_keythr, NULL)); CHECK(!pthread_cond_destroy(&thread->cv)); free(thread);
    return NULL;
}
int main(int argc, char **argv) {
    CHECK(argc == 2); CHECK(!setenv("DM_LOCK_SPIN_US", argv[1], 1)); alarm(30);
    uint32_t tag = svc_thread_main_init();
    uint64_t registers[8] = {tag, (uintptr_t)&word, tag};
    CHECK(svc_thread(0x1A, registers) && !registers[0]);
    pthread_t threads[4];
    for (unsigned n = 0; n < 4; n++) CHECK(!pthread_create(&threads[n], NULL, worker, NULL));
    for (unsigned n = 0; n < 4; n++) CHECK(!pthread_join(threads[n], NULL));
    CHECK(total == 12000 && !atomic_load(&inside) && !atomic_load(&word));
    puts("PASS: actual guest lock fast return, concurrent mutual exclusion, handoff and spin timeout");
    return 0;
}
