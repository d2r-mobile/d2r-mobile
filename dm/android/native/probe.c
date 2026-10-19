// 真正执行改写后的合成指令；这里的服务仅用于测试，不作为游戏 HLE。
// Execute actual rewritten synthetic instructions. These services are test-only, not game HLE.
#include "runtime.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void dm_gate(void);
typedef struct { uintptr_t ret; unsigned timer, arg; } gate;
static gate gates[32];
static unsigned ngates;

uint64_t gate_dispatch(uint64_t ret, uint64_t *regs) {
    for (unsigned i = 0; i < ngates; ++i) if (gates[i].ret == ret) {
        if (gates[i].timer) regs[gates[i].arg] = 123456;
        else if (gates[i].arg == 0x42) regs[0] = 0x55;
        else abort();
        return regs[0];
    }
    abort();
}
uint64_t unimpl_c(unsigned index, uint64_t *regs) { (void)index; (void)regs; abort(); }
void dm_thread_returned(void) { abort(); }

typedef struct { void (*entry)(uint64_t *, uint64_t); uint64_t seed; int ok; uint64_t failed[11]; } probe_test;
static void *run_probe(void *arg) {
    probe_test *t = arg;
    dm_android_context *c = dm_android_thread_init(dm_gate);
    uintptr_t tp = (uintptr_t)__builtin_thread_pointer();
    uint64_t x18;
    __asm__ volatile("mov %0, x18" : "=r"(x18));
    for (unsigned i = 0; i < 1000; ++i) {
        uint64_t output[8] = {0};
        t->entry(output, t->seed);
        uint64_t after;
        __asm__ volatile("mov %0, x18" : "=r"(after));
        if (output[0] != 0x55 || output[1] != t->seed + 7 || output[2] != t->seed + 7 ||
            output[3] != (uintptr_t)c->tlr || output[4] != 123456 || after != x18 ||
            output[5] != 0x4000000000000000ull || output[6] != output[7] ||
            (uintptr_t)__builtin_thread_pointer() != tp) {
            memcpy(t->failed, output, sizeof output);
            t->failed[8] = (uintptr_t)c->tlr; t->failed[9] = x18; t->failed[10] = after;
            return NULL;
        }
    }
    t->ok = 1;
    return NULL;
}

int dm_android_probe(const char *library_dir, const char *data_dir, char *report, size_t cap) {
    dm_android_module module;
    char library[1024], layout[1024], path[1024];
    snprintf(library, sizeof library, "%s/libdmguest_probe.so", library_dir);
    snprintf(layout, sizeof layout, "%s/probe.fixups.tsv", data_dir);
    if (dm_android_load(&module, library, layout, report, cap)) return -1;
    snprintf(path, sizeof path, "%s/probe.gates.tsv", data_dir);
    FILE *f = fopen(path, "r");
    if (!f) { snprintf(report, cap, "Missing probe gates"); dm_android_unload(&module); return -1; }
    char kind[16]; unsigned long long ret, orig; unsigned arg;
    ngates = 0;
    while (fscanf(f, "%llx %15s %u %llx", &ret, kind, &arg, &orig) == 4) {
        if (ngates == 32) abort();
        gates[ngates++] = (gate){(uintptr_t)module.image + ret, strcmp(kind, "svc") != 0, arg};
    }
    fclose(f);
    pthread_t threads[4]; probe_test tests[4] = {0}; unsigned created = 0;
    for (unsigned i = 0; i < 4; ++i) {
        tests[i].entry = (void *)module.image; tests[i].seed = 100 * i;
        if (pthread_create(&threads[i], NULL, run_probe, &tests[i])) break;
        created++;
    }
    for (unsigned i = 0; i < created; ++i) pthread_join(threads[i], NULL);
    int ok = created == 4;
    for (unsigned i = 0; i < created; ++i) ok &= tests[i].ok;
    snprintf(report, cap, "Rewritten ARM64: %s (4 threads x 1000; x18, TLS, TLR, svc, timer, host TP, FP, NZCV)", ok ? "PASS" : "FAIL");
    if (!ok) for (unsigned i = 0; i < created; ++i) if (!tests[i].ok) {
        size_t used = strlen(report);
        snprintf(report + used, cap - used, "\nseed=%llu values=%llx/%llx/%llx/%llx/%llx TLR=%llx x18=%llx/%llx",
                 (unsigned long long)tests[i].seed, (unsigned long long)tests[i].failed[0],
                 (unsigned long long)tests[i].failed[1], (unsigned long long)tests[i].failed[2],
                 (unsigned long long)tests[i].failed[3], (unsigned long long)tests[i].failed[4],
                 (unsigned long long)tests[i].failed[8], (unsigned long long)tests[i].failed[9], (unsigned long long)tests[i].failed[10]);
        break;
    }
    dm_android_unload(&module);
    return ok ? 0 : -1;
}

#ifndef DM_NATIVE_APP
int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "Usage: runtime-smoke <probe directory>\n"); return 2; }
    char report[2048];
    int a = dm_android_runtime_selftest(report, sizeof report); puts(report);
    int b = dm_android_probe(argv[1], argv[1], report, sizeof report); puts(report);
    return a || b;
}
#endif
