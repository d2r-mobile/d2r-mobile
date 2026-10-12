// 宿主堆增长定位（诊断，DM_MALLOCDIAG=起始秒,结束秒 时生效；零点 = 本构造函数）：
//   起始秒挂 libmalloc 的 malloc_logger 回调，逐次记分配 → 调用点（帧指针回溯 10 帧）、释放即销账；结束秒摘钩，
//   按「窗口内分配、到结束仍活着」的字节数给调用点排序，写 Documents/mallocdiag.txt（前 40 个，模块+偏移）。
// 为什么不用 xctrace Allocations：它的记录库跑在当前栈上，HLE 在客体线程的小栈上调 malloc 时探栈越过保护页（真机两次 sig=10 ___chkstk_darwin）。
// 约束：回调里不分配内存、栈帧小（可能跑在客体栈上）；表满即停记并在报告里标红。
// Locating host heap growth (diagnostic; active when DM_MALLOCDIAG=start_seconds,end_seconds; zero point = this constructor):
//   at the start second, hook libmalloc's malloc_logger callback and record each allocation → call site (frame-pointer backtrace of 10 frames), cancelling the entry on free; at the end second, unhook,
//   sort call sites by the bytes "allocated inside the window and still live at the end", and write Documents/mallocdiag.txt (top 40, module+offset).
// Why not xctrace Allocations: its recording library runs on the current stack, and when HLE calls malloc on a guest thread's small stack the stack probe crosses the guard page (twice on a real device: sig=10 ___chkstk_darwin).
// Constraints: no memory allocation inside the callback, small stack frame (it may run on a guest stack); when a table fills up, stop recording and mark it as failed in the report.
#import <Foundation/Foundation.h>
#include <dlfcn.h>
#include <os/lock.h>
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>
#include "dm.h"

typedef void(malloc_logger_t)(uint32_t type, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t result, uint32_t skip);
extern malloc_logger_t *malloc_logger;   // libsystem_malloc 导出（MallocStackLogging / Instruments 用的同一挂点） | Exported by libsystem_malloc (the same hook point used by MallocStackLogging / Instruments)
enum { T_ALLOC = 2, T_DEALLOC = 4, T_ZONE = 8 };

#define NF 10
#define LIVE_BITS 22                     // 活块表 4M 项 × 24 B = 96 MiB（只记窗口内分配） | Live-block table 4M entries × 24 B = 96 MiB (records only allocations inside the window)
#define SITE_BITS 16
typedef struct { uintptr_t p; uint32_t size, site; } live_t;
typedef struct { uintptr_t f[NF]; uint64_t bytes, n, total_n; } site_t;
static live_t *g_live; static site_t *g_site;
static os_unfair_lock g_lk = OS_UNFAIR_LOCK_INIT;
static int g_full;

static uint32_t hsite(const uintptr_t *f) { uint64_t h = 1469598103934665603ull; for (int i = 0; i < NF; i++) h = (h ^ f[i]) * 1099511628211ull; return (uint32_t)(h ^ h >> 32); }
static uint32_t hptr(uintptr_t p) { return (uint32_t)((p >> 4) * 0x9E3779B97F4A7C15ull >> (64 - LIVE_BITS)); }

static void on_alloc(uintptr_t p, uintptr_t size) {
    uintptr_t f[NF] = {0}; uintptr_t *fp = __builtin_frame_address(0);
    for (int i = 0, k = 0; fp && k < NF && i < NF + 1; i++) {   // 跳过本函数自己的返回地址 | Skip this function's own return address
        uintptr_t *nx = (uintptr_t *)fp[0]; uintptr_t lr = fp[1] & 0x0000ffffffffffffull;
        if (i >= 1) f[k++] = lr;
        if (!nx || nx <= fp || (uintptr_t)nx - (uintptr_t)fp > (1u << 20) || ((uintptr_t)nx & 7)) break;
        fp = nx;
    }
    uint32_t s = hsite(f) & ((1u << SITE_BITS) - 1);
    os_unfair_lock_lock(&g_lk);
    for (uint32_t i = 0; i < (1u << SITE_BITS); i++, s = (s + 1) & ((1u << SITE_BITS) - 1)) {
        site_t *e = &g_site[s];
        if (!e->total_n) { memcpy(e->f, f, sizeof f); break; }
        if (!memcmp(e->f, f, sizeof f)) break;
        if (i == (1u << SITE_BITS) - 1) { g_full = 1; os_unfair_lock_unlock(&g_lk); return; }
    }
    g_site[s].bytes += size; g_site[s].n++; g_site[s].total_n++;
    uint32_t h = hptr(p);
    for (uint32_t i = 0; i < (1u << LIVE_BITS); i++, h = (h + 1) & ((1u << LIVE_BITS) - 1))
        if (!g_live[h].p || g_live[h].p == 1) { g_live[h] = (live_t){p, (uint32_t)size, s}; os_unfair_lock_unlock(&g_lk); return; }
    g_full = 1; os_unfair_lock_unlock(&g_lk);
}
static void on_free(uintptr_t p) {
    uint32_t h = hptr(p);
    os_unfair_lock_lock(&g_lk);
    for (uint32_t i = 0; i < (1u << LIVE_BITS) && g_live[h].p; i++, h = (h + 1) & ((1u << LIVE_BITS) - 1))
        if (g_live[h].p == p) { site_t *e = &g_site[g_live[h].site]; e->bytes -= g_live[h].size; e->n--; g_live[h].p = 1; break; }   // 1 = 墓碑 | 1 = tombstone
    os_unfair_lock_unlock(&g_lk);
}
static void logger(uint32_t type, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t result, uint32_t skip) {
    (void)a1; (void)skip;
    if ((type & (T_ALLOC | T_DEALLOC)) == (T_ALLOC | T_DEALLOC)) {   // realloc：a2 = 旧块，a3 = 新长，result = 新块 | realloc: a2 = old block, a3 = new length, result = new block
        if (a2) on_free(a2);
        if (result) on_alloc(result, a3);
    } else if (type & T_ALLOC) { if (result) on_alloc(result, a2); }
    else if (type & T_DEALLOC) on_free(a2);
}

static void sym(char *o, size_t n, uintptr_t a) {
    for (int i = 0; i < g_nmod; i++)
        if (a >= (uintptr_t)g_mod[i].base && a < (uintptr_t)g_mod[i].base + g_mod[i].size) { snprintf(o, n, "%s+%#lx", g_mod[i].name, a - (uintptr_t)g_mod[i].base); return; }
    Dl_info d; if (dladdr((void *)a, &d) && d.dli_sname) { snprintf(o, n, "%s+%#lx", d.dli_sname, a - (uintptr_t)d.dli_saddr); return; }
    if (dladdr((void *)a, &d) && d.dli_fname) { const char *b = strrchr(d.dli_fname, '/'); snprintf(o, n, "%s+%#lx", b ? b + 1 : d.dli_fname, a - (uintptr_t)d.dli_fbase); return; }
    snprintf(o, n, "%#lx", a);
}
static int by_bytes(const void *a, const void *b) { uint64_t x = (*(site_t *const *)a)->bytes, y = (*(site_t *const *)b)->bytes; return (x < y) - (x > y); }

static void *diag(void *arg) {
    int *w = arg; sleep((unsigned)w[0]);
    malloc_logger = logger;
    sleep((unsigned)(w[1] - w[0]));
    malloc_logger = NULL; usleep(200000);   // 摘钩后等在途回调走完 | After unhooking, wait for in-flight callbacks to finish
    static site_t *top[1u << SITE_BITS]; int nt = 0; uint64_t sum = 0, sumn = 0;
    for (uint32_t i = 0; i < (1u << SITE_BITS); i++) if (g_site[i].total_n && g_site[i].bytes) { top[nt++] = &g_site[i]; sum += g_site[i].bytes; sumn += g_site[i].n; }
    qsort(top, nt, sizeof *top, by_bytes);
    NSString *doc = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES)[0];
    FILE *f = fopen([doc stringByAppendingPathComponent:@"mallocdiag.txt"].fileSystemRepresentation, "w"); if (!f) return NULL;
    fprintf(f, "窗口 %d–%d 秒：窗口内分配、到结束仍活着 %.2f MiB / %llu 块，调用点 %d 个%s\n", w[0], w[1], sum / 1048576.0, sumn, nt, g_full ? "（【红】表满，有漏记）" : "");
    for (int i = 0; i < nt && i < 40; i++) {
        fprintf(f, "%8.1f KiB %7llu 块（窗口内共分配 %llu 次）", top[i]->bytes / 1024.0, top[i]->n, top[i]->total_n);
        for (int k = 0; k < NF && top[i]->f[k]; k++) { char s[160]; sym(s, sizeof s, top[i]->f[k]); fprintf(f, " ← %s", s); }
        fputc('\n', f);
    }
    fclose(f); return NULL;
}

__attribute__((constructor)) static void mallocdiag_ios(void) {
    const char *e = getenv("DM_MALLOCDIAG"); if (!e) return;
    static int w[2]; if (sscanf(e, "%d,%d", &w[0], &w[1]) != 2 || w[1] <= w[0]) return;
    g_live = calloc(1u << LIVE_BITS, sizeof *g_live); g_site = calloc(1u << SITE_BITS, sizeof *g_site);
    pthread_t th; pthread_create(&th, NULL, diag, w); pthread_detach(th);
}
