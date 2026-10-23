// OHOS（musl）线程键布局探针：找 pthread_setspecific 的值相对 tpidr_el0 的取法，供改写器 DM_TARGET=ohos 用；
// 并测匿名页能否改成可执行（决定执行映像是否必须进签名 .so）。只读探测，不改系统。
// OHOS (musl) thread-key layout probe: find how a pthread_setspecific value is reached from tpidr_el0, for the rewriter's
// DM_TARGET=ohos; also test whether anonymous pages can be made executable (decides whether the execution image must live
// in a signed .so). Read-only probing; changes nothing on the system.
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static int pfd[2];

static uintptr_t tp(void) { uintptr_t t; __asm__ volatile("mrs %0, tpidr_el0" : "=r"(t)); return t; }

static void *probe(void *arg) {
    (void)arg;
    pthread_key_t k1, k2;
    pthread_key_create(&k1, NULL); pthread_key_create(&k2, NULL);
    void *v1 = (void *)0x5a5a1234abcd0001ull, *v2 = (void *)0x5a5a1234abcd0002ull;
    pthread_setspecific(k1, v1); pthread_setspecific(k2, v2);
    uintptr_t t = tp();
    printf("keys k1=%u k2=%u tp=%#lx\n", (unsigned)k1, (unsigned)k2, (unsigned long)t);
    // 一级：tp+off 处直接是键数组（slot = tp + off + 8k）| level 1: the key array sits at tp+off
    for (long off = -4096; off <= 4096; off += 8) {
        void **a = (void **)(t + off);
        unsigned hi = (k1 > k2 ? k1 : k2) + 1;
        if (write(pfd[1], a, hi * 8) != (ssize_t)(hi * 8)) continue;
        { char sink[4096]; read(pfd[0], sink, hi * 8 > sizeof sink ? sizeof sink : hi * 8); }
        if (a[k1] == v1 && a[k2] == v2) printf("DIRECT off=%ld (slot = tp%+ld + 8*key)\n", off, off);
    }
    // 二级：tp+off 处是指针 tsd，tsd[k] 为值（musl struct pthread.tsd）| level 2: tp+off holds a pointer tsd, tsd[k] is the value
    for (long off = -4096; off <= 4096; off += 8) {
        void **p = *(void ***)(t + off);
        if ((uintptr_t)p < 4096 || ((uintptr_t)p & 7)) continue;
        // 读之前用 write 到管道验证可读（不可读返回 EFAULT，不会崩）| validate readability via a pipe write (EFAULT, no crash)
        unsigned hi = (k1 > k2 ? k1 : k2) + 1;
        if (write(pfd[1], p, hi * 8) != (ssize_t)(hi * 8)) continue;
        { char sink[4096]; read(pfd[0], sink, hi * 8 > sizeof sink ? sizeof sink : hi * 8); }
        if (p[k1] == v1 && p[k2] == v2) printf("INDIRECT off=%ld (slot = *(tp%+ld) + 8*key) tsd=%p\n", off, off, (void *)p);
    }
    return NULL;
}

int main(void) {
    if (pipe(pfd)) return 1;
    pthread_t th; pthread_create(&th, NULL, probe, NULL); pthread_join(th, NULL);
    printf("main thread:\n"); probe(NULL);
    // 匿名页 RW → RX | anonymous page RW → RX
    void *m = mmap(NULL, 16384, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    int r = m == MAP_FAILED ? -2 : mprotect(m, 16384, PROT_READ | PROT_EXEC);
    printf("anon mprotect RX result=%d\n", r);
    return 0;
}
