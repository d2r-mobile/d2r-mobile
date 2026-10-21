// 设备上：64 个同时存活的线程各自登记上下文，经真实汇编入口 dm_android_context_entry 取回 x16，
// 必须等于本线程上下文且 NZCV 不变；统计登记进快路的线程数和每次入口耗时。
// On device: 64 concurrently live threads register their contexts and fetch x16 through the real assembly entry
// dm_android_context_entry; it must equal the thread's own context with NZCV unchanged. Reports how many threads
// registered into the fast path and the per-call entry cost.
// Build (NDK, from dm/android; BUILD = an APK build dir holding gpu-driver/include):
//   python3 tools/generate_entry.py /tmp/entry_elf.S
//   CC=aarch64-linux-android29-clang; $CC -O1 -I native -I $BUILD -I $BUILD/gpu-driver/include -c native/probe.c -Dmain=probe_main -o /tmp/probe.o
//   $CC -O2 -I native -I $BUILD -I $BUILD/gpu-driver/include native/runtime.c /tmp/probe.o /tmp/entry_elf.S tests/context_cache_device_test.c -ldl -o ctxtest
//   adb push ctxtest /data/local/tmp/ && adb shell /data/local/tmp/ctxtest
#include "runtime.h"
#include "runtime_context_cache.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <time.h>

uintptr_t ctx_probe(uint64_t nzcv, uint64_t *nzcv_out);
__asm__(".text\n.globl ctx_probe\n.type ctx_probe,%function\nctx_probe:\n"
        "stp x29, x30, [sp, #-16]!\nmsr nzcv, x0\nbl dm_android_context_entry\n"
        "mrs x2, nzcv\nstr x2, [x1]\nmov x0, x16\nldp x29, x30, [sp], #16\nret\n");

enum { THREADS = 64, CALLS = 200000 };
static pthread_barrier_t start, done;
static _Atomic int registered, wrong, flags_lost;
static _Atomic uint64_t cached_ns, cached_calls;

static uint64_t now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000000000ull + t.tv_nsec; }
static void *worker(void *arg) {
    (void)arg;
    dm_android_context *self = dm_android_thread_init(NULL);
    pthread_barrier_wait(&start);   // 全部存活后再统计：最大化撞桶 | count only once all are alive: maximize bucket collisions
    int hit = dm_android_context_cache_lookup((uintptr_t)__builtin_thread_pointer()) == self;
    registered += hit;
    uint64_t t0 = now();
    for (unsigned i = 0; i < CALLS; i++) {
        uint64_t in = (uint64_t)(i & 15) << 28, out = 0;
        if (ctx_probe(in, &out) != (uintptr_t)self) { wrong++; break; }
        if (out != in) { flags_lost++; break; }
    }
    if (hit) { cached_ns += now() - t0; cached_calls += CALLS; }
    pthread_barrier_wait(&done);    // 保持存活直到全部测完 | stay alive until every thread has finished
    return NULL;
}
int main(void) {
    pthread_t t[THREADS];
    pthread_barrier_init(&start, NULL, THREADS); pthread_barrier_init(&done, NULL, THREADS);
    for (int i = 0; i < THREADS; i++) if (pthread_create(&t[i], NULL, worker, NULL)) { puts("FAIL: pthread_create"); return 1; }
    for (int i = 0; i < THREADS; i++) pthread_join(t[i], NULL);
    int ok = !wrong && !flags_lost && registered == THREADS;
    printf("%s context cache: registered %d/%d, wrong %d, nzcv lost %d, cached entry %.2f ns/call\n", ok ? "PASS" : "FAIL",
           (int)registered, THREADS, (int)wrong, (int)flags_lost, cached_calls ? (double)cached_ns / cached_calls : 0.0);
    return ok ? 0 : 1;
}
