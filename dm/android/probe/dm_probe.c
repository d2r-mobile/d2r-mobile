// 安卓真机探针：宿主方案在这台机器上成不成立，一次跑完、逐项报 OK / FAIL。
// 用法：NDK clang 编成可执行文件，adb push 到 /data/local/tmp 运行（shell 身份；应用身份的内存上限另在 App 里测）。
//   ① 页大小、CPU 特性；② bionic 键表（改写器 DM_TARGET=android 的线程槽取法）并用一段运行时生成的补丁同款代码实读；
//   ③ 匿名页写入后改只读+执行能否执行（装载器 map_image 的做法）；④ x18：宿主线程里是什么、跨调度会不会被改；
//   ⑤ 新线程有没有备用信号栈（补丁块借用 SP 下方 16–24 字节，Linux 没有红区保证，信号必须走备用栈）；⑥ 大块地址保留。
// Android device probe: whether the host's approach holds on this machine, run all at once and report OK / FAIL item by item.
// Usage: build as an executable with NDK clang, adb push to /data/local/tmp and run (shell identity; the memory limit under the app identity is measured separately in the App).
//   ① page size, CPU features; ② the bionic key table (the thread slot access of rewriter DM_TARGET=android), read for real through run-time generated code identical to the patch code;
//   ③ whether anonymous pages can execute after being written and switched to read-only+execute (what the loader's map_image does); ④ x18: what it holds in host threads and whether scheduling changes it;
//   ⑤ whether new threads have an alternate signal stack (patch blocks borrow 16–24 bytes below SP, Linux gives no red zone guarantee, so signals must use the alternate stack); ⑥ large address reservations.
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <unistd.h>

static int g_fail;
#define OK(...) do { printf("OK    " __VA_ARGS__); putchar('\n'); } while (0)
#define FAIL(...) do { printf("FAIL  " __VA_ARGS__); putchar('\n'); g_fail++; } while (0)
#define INFO(...) do { printf("INFO  " __VA_ARGS__); putchar('\n'); } while (0)

static uint8_t *tsd_base(void) { uint64_t tp; __asm__ volatile("mrs %0, tpidr_el0" : "=r"(tp)); return ((uint8_t **)tp)[-1]; }
static uint64_t g_tsd_off;
static uint64_t kidx(pthread_key_t k) { return (uint32_t)k & 0x7fffffffu; }   // bionic 键号带第 31 位有效标记 | bionic key numbers carry a validity flag in bit 31
static pthread_key_t g_k1, g_k2;

static void probe_tsd(void) {
    void *const m1 = (void *)0x6d4d7d5d4d3d2d1dull, *const m2 = (void *)0x1d2d3d4d5d7d4d6dull;
    // 先占掉几个键，让被测键号不是 0（装载器实际拿到的键号也不会是 0）
    // Use up a few keys first so the tested key number is not 0 (the key numbers the loader actually gets are not 0 either)
    pthread_key_t pad[5]; for (int i = 0; i < 5; i++) pthread_key_create(&pad[i], NULL);
    if (pthread_key_create(&g_k1, NULL) || pthread_key_create(&g_k2, NULL)) { FAIL("pthread_key_create"); return; }
    pthread_setspecific(g_k1, m1);
    uint8_t *b = tsd_base(); uint64_t at = 0;
    for (uint64_t o = 8; o < 0x4000 && !at; o += 8) if (*(void **)(b + o) == m1) at = o;
    if (!at) { FAIL("bionic 键表：键 %d 的值不在 bionic_tls 前 16 KB", (int)g_k1); return; }
    g_tsd_off = at - kidx(g_k1) * 16 - 8;
    pthread_setspecific(g_k2, m2);
    if (*(void **)(b + g_tsd_off + kidx(g_k2) * 16 + 8) == m2) OK("bionic 键表偏移 %#llx（键 %#x / %#x 都对得上）", (unsigned long long)g_tsd_off, (unsigned)g_k1, (unsigned)g_k2);
    else FAIL("bionic 键表：键 %d 推出的偏移 %#llx 对键 %d 不成立", (int)g_k1, (unsigned long long)g_tsd_off, (int)g_k2);
}

// 运行时生成与 patch_nso.py（DM_TARGET=android）同款的取槽代码：mrs x0,tpidr_el0; ldur x0,[x0,#-8]; ldr x1,[keypage]; add x0,x0,x1; ldr x0,[x0]; ret
// Generate at run time slot-fetch code identical to patch_nso.py (DM_TARGET=android): mrs x0,tpidr_el0; ldur x0,[x0,#-8]; ldr x1,[keypage]; add x0,x0,x1; ldr x0,[x0]; ret
typedef uint64_t (*slotfn)(void);
static slotfn g_slotfn;
static void probe_exec(void) {
    long pg = sysconf(_SC_PAGESIZE);
    size_t sz = 0x8000;   // 两页 16 KB：代码一页、键页一页（与装载器的 16 KB 粒度一致） | Two 16 KB pages: one code, one key page (matching the loader's 16 KB granularity)
    uint8_t *p = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (p == MAP_FAILED) { FAIL("mmap 匿名页"); return; }
    uint64_t *key = (uint64_t *)(p + 0x4000);
    key[0] = g_tsd_off + kidx(g_k1) * 16 + 8;
    uint32_t *c = (uint32_t *)p; int n = 0;
    c[n++] = 0xD53BD040 | 0;                        // mrs x0, tpidr_el0
    c[n++] = 0xF8400000 | ((-8 & 0x1FF) << 12) | 0; // ldur x0, [x0, #-8]
    { int32_t imm = (int32_t)(0x4000 - 4 * n) >> 2; c[n++] = 0x58000000 | ((imm & 0x7FFFF) << 5) | 1; }   // ldr x1, =key[0]（字面量池寻址） | ldr x1, =key[0] (literal addressing)
    c[n++] = 0x8B010000;                            // add x0, x0, x1
    c[n++] = 0xF9400000;                            // ldr x0, [x0]
    c[n++] = 0xD65F03C0;                            // ret
    __builtin___clear_cache((char *)p, (char *)(p + 4 * n));
    if (mprotect(p, 0x4000, PROT_READ | PROT_EXEC)) { FAIL("mprotect 只读+执行被拒（页大小 %ld）", pg); return; }
    if (mprotect(p + 0x4000, 0x4000, PROT_READ)) { FAIL("mprotect 键页只读"); return; }
    g_slotfn = (slotfn)p;
    uint64_t v = g_slotfn();
    if (v == 0x6d4d7d5d4d3d2d1dull) OK("匿名页改只读+执行后可执行，补丁同款取槽代码读到键 %d 的值", (int)g_k1);
    else FAIL("补丁同款取槽代码读到 %#llx", (unsigned long long)v);
    // 写入后可执行页也可 RWX？（只记录，宿主不依赖） | Is RWX also allowed? (recorded only; the host does not depend on it)
    void *rwx = mmap(NULL, 0x4000, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0);
    INFO("RWX 匿名页：%s", rwx == MAP_FAILED ? "被拒" : "允许"); if (rwx != MAP_FAILED) munmap(rwx, 0x4000);
}

static uint64_t read_x18(void) { uint64_t v; __asm__ volatile("mov %0, x18" : "=r"(v)); return v; }

struct thr { uint64_t x18; uint64_t slot; int altstack; size_t altsize; int slot_ok; };
static void *thr_main(void *arg) {
    struct thr *t = arg;
    t->x18 = read_x18();
    stack_t ss; t->altstack = sigaltstack(NULL, &ss) == 0 && !(ss.ss_flags & SS_DISABLE); t->altsize = ss.ss_size;
    // 新线程里经同一段生成代码取槽：设值后读，验证「每线程各自一份」 | Fetch the slot via the same generated code in a new thread: set then read, verifying "one copy per thread"
    if (g_slotfn) { pthread_setspecific(g_k1, (void *)0x5151); t->slot = g_slotfn(); t->slot_ok = t->slot == 0x5151; }
    return NULL;
}

static void probe_threads(void) {
    INFO("主线程 x18 = %#llx", (unsigned long long)read_x18());
    struct thr t = {0}; pthread_t th;
    if (pthread_create(&th, NULL, thr_main, &t)) { FAIL("pthread_create"); return; }
    pthread_join(th, NULL);
    INFO("新线程 x18 = %#llx（非 0 多半是 bionic 给的影子调用栈指针：宿主代码与系统库要用 x18，客体 x18 必须走线程槽，与苹果相同）", (unsigned long long)t.x18);
    if (g_slotfn) { if (t.slot_ok) OK("新线程经生成代码取到本线程自己的槽值"); else FAIL("新线程取槽得 %#llx", (unsigned long long)t.slot); }
    if (t.altstack) OK("新线程有备用信号栈（%zu 字节）：SA_ONSTACK 的信号不压当前栈", t.altsize);
    else FAIL("新线程没有备用信号栈：客体线程要自己 sigaltstack，宿主信号处理一律 SA_ONSTACK");
    // x18 跨调度是否被改：在本线程把 x18 设成记号、让出 CPU 若干次，再读回（只在不调任何库函数的汇编循环里测）
    // Whether x18 is changed across scheduling: set x18 to a marker in this thread, yield the CPU a number of times, then read back (measured only inside an asm loop that calls no library functions)
    uint64_t back = 0, changed = 0;
    for (int i = 0; i < 2000; i++) {
        __asm__ volatile(
            "mov x9, x18\n"
            "movz x18, #0x5a5a\n"
            "mov x8, #124\n"          // __NR_sched_yield
            "svc #0\n"
            "mov %0, x18\n"
            "mov x18, x9\n" : "=r"(back) : : "x0", "x8", "x9", "memory");
        if (back != 0x5a5a) changed++;
    }
    if (!changed) OK("x18 跨 2000 次系统调用 / 让出调度不被内核改写");
    else FAIL("x18 跨系统调用被改 %llu / 2000 次", (unsigned long long)changed);
}

static void probe_vm(void) {
    for (int gb = 8; gb <= 64; gb *= 2) {
        size_t sz = (size_t)gb << 30;
        void *p = mmap(NULL, sz, PROT_NONE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
        if (p == MAP_FAILED) { INFO("保留 %d GiB 地址：失败", gb); break; }
        INFO("保留 %d GiB 地址：%p", gb, p); munmap(p, sz);
    }
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    INFO("页大小 %ld，CPU %ld 核", sysconf(_SC_PAGESIZE), sysconf(_SC_NPROCESSORS_CONF));
    unsigned long hw = getauxval(AT_HWCAP), hw2 = getauxval(AT_HWCAP2);
    INFO("HWCAP %#lx HWCAP2 %#lx（LSE 原子 %s）", hw, hw2, hw & (1ul << 8) ? "有" : "无");
    probe_tsd();
    probe_exec();
    probe_threads();
    probe_vm();
    printf(g_fail ? "RESULT FAIL %d\n" : "RESULT OK\n", g_fail);
    return g_fail != 0;
}
