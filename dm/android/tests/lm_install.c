// Concurrent first-install and identity regression for the lm backend.
#include "../runtime/host.h"
#include "../runtime/ipc.h"
#include "../runtime/lm.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#define WORKERS 8
#define UNIT (2u * 1024u * 1024u)
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); exit(1); } } while (0)

static dm_host *host;
typedef struct { pthread_mutex_t lock; pthread_cond_t cond; int count, generation; } barrier_t;
static barrier_t barrier;
static void barrier_init(barrier_t *b, int n) { (void)n; pthread_mutex_init(&b->lock,NULL); pthread_cond_init(&b->cond,NULL); b->count=0; b->generation=0; }
static void barrier_wait(barrier_t *b) {
    pthread_mutex_lock(&b->lock);
    int generation = b->generation;
    if (++b->count == WORKERS) { b->count = 0; b->generation++; pthread_cond_broadcast(&b->cond); }
    else while (generation == b->generation) pthread_cond_wait(&b->cond, &b->lock);
    pthread_mutex_unlock(&b->lock);
}
static int results[WORKERS];
static int dummy_handler(void *u, uint64_t c, unsigned f, uint32_t cmd, const uint8_t *r,
                         size_t n, uint8_t *o, size_t cap, size_t *sz, uint32_t *h) {
    (void)u;(void)c;(void)f;(void)cmd;(void)r;(void)n;(void)o;(void)cap;(void)sz;(void)h; return -1;
}
static void *worker(void *arg) {
    int i = (int)(intptr_t)arg;
    barrier_wait(&barrier);
    results[i] = dm_lm_install(host);
    return NULL;
}
int main(void) {
    host = dm_host_create(16 * UNIT);
    CHECK(host);
    barrier_init(&barrier, WORKERS);
    pthread_t threads[WORKERS];
    for (int i = 0; i < WORKERS; i++) CHECK(pthread_create(&threads[i], NULL, worker, (void *)(intptr_t)i) == 0);
    for (int i = 0; i < WORKERS; i++) pthread_join(threads[i], NULL);
    for (int i = 0; i < WORKERS; i++) CHECK(results[i] == 0);
    CHECK(dm_host_ipc_unregister_port(host, "lm") == 0);
    CHECK(dm_lm_install(host) == 0);                  // re-register after unregister
    CHECK(dm_host_ipc_unregister_port(host, "lm") == 0);
    CHECK(dm_host_destroy(host) == 0);
    pthread_mutex_destroy(&barrier.lock); pthread_cond_destroy(&barrier.cond);

    // Foreign same-name port must not be replaced.
    host = dm_host_create(16 * UNIT);
    CHECK(host);
    CHECK(dm_host_ipc_register_port(host, "lm", dummy_handler, NULL) == 0);
    CHECK(dm_lm_install(host) == EEXIST);
    CHECK(dm_host_ipc_unregister_port(host, "lm") == 0);
    CHECK(dm_lm_install(host) == 0);
    CHECK(dm_host_ipc_unregister_port(host, "lm") == 0);
    CHECK(dm_host_destroy(host) == 0);
    puts("lm install test: all checks passed");
    return 0;
}
