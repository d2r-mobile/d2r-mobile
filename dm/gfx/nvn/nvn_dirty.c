// 客体内存写保护脏页跟踪：预排布贴图（CPU 写、GPU 读）的池页上传后设只读，游戏一写即缺页 → 标脏、解保护；
// 使用前查覆盖页的写代号，比上传时新就重传。NVN 语义：贴图描述符指向池内存，GPU 每次读的是当前内容
// （实测游戏会把流式素材写进仍被采样的贴图存储，不重新登记）。
// 信号处理约束：缺页路径只用原子操作与 mprotect，不分配、不加锁。
// Write-protect dirty-page tracking for guest memory: pool pages of packaged textures (CPU writes, GPU reads) are set read-only after upload, and as soon as the game writes it faults -> mark dirty, unprotect;
// before use, check the write generation of the covered pages and re-upload if newer than at upload time. NVN semantics: a texture descriptor points to pool memory, and the GPU reads the current content every time
// (measured: the game writes streamed assets into texture storage that is still being sampled, without re-registering).
// Signal handler constraint: the page-fault path uses only atomic operations and mprotect, no allocation, no locks.
#include "nvn_dirty.h"
#include "../../host/dm.h"
#include <stdatomic.h>
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#include <mach/mach_time.h>
#include <sched.h>

// 页状态与 mprotect 的次序（改保护的一方先把状态改成 BUSY，mprotect 落地后才改成终态；别的线程见 BUSY 就等）：
//   设只读：NONE → BUSY → mprotect(只读) → PROT；解保护：PROT → BUSY → mprotect(可写) → NONE。
// 不变式：状态是 NONE 的页一定可写、是 PROT 的页一定只读。原先设只读是「先记 PROT、攒一段再 mprotect」，这段时间里另一线程的写前解保护
// 会把状态改回 NONE，随后攒批的 mprotect 落地 ⇒ 页只读而状态 NONE：游戏线程写它时缺页处理只回「重执行」永远空转；
// 宿主用系统调用往里读文件时 prep_write 看状态 NONE 跳过，系统调用不走缺页处理，直接 EFAULT、数据读不全。
// BUSY 最长持续 = 一次 mprotect 的时间（设只读时再加上给这一段逐页改状态的几次原子写）。等待有上限 BUSY_WAIT_MS，超限报红：
// 缺页处理里超限按「不是我们的页」返回，交给崩溃处理如实报崩，不静默空转。等待只读原子量、不拿任何锁。
// Order of page state and mprotect (the side changing protection first sets the state to BUSY and sets the final state only after mprotect has landed; other threads wait when they see BUSY):
//   set read-only: NONE -> BUSY -> mprotect(read-only) -> PROT; unprotect: PROT -> BUSY -> mprotect(writable) -> NONE.
// Invariant: a page whose state is NONE is always writable, and one whose state is PROT is always read-only. Setting read-only used to be "record PROT first, batch up a run, then mprotect"; during that time another thread's unprotect-before-write
// could set the state back to NONE, and then the batched mprotect landed => the page read-only with state NONE: when the game thread wrote to it the page-fault handler only returned "re-execute" and spun forever;
// when the host read a file into it with a system call, prep_write saw state NONE and skipped, the system call did not go through the page-fault handler and failed with EFAULT, leaving the data incomplete.
// BUSY lasts at most one mprotect (plus, when setting read-only, the few atomic writes that change the state of the pages in the run). The wait has an upper limit BUSY_WAIT_MS and reports red beyond it:
// in the page-fault handler, exceeding it returns "not our page" and lets the crash handler report the crash truthfully instead of spinning silently. The wait only reads atomics and takes no lock.
enum { PG_NONE = 0, PG_PROT = 1, PG_BUSY = 2 };
enum { BUSY_WAIT_MS = 2000 };
// 跟踪区间 = 各内存池（MemoryPoolInitialize 登记）。池销毁后复用 live=0 的槽，信号处理里无锁扫描。
// Tracked ranges = the memory pools (registered by MemoryPoolInitialize). After a pool is destroyed its live=0 slot is reused; the signal handler scans without locks.
#define MAX_RANGE 4096   // 原为 256：内存池多于它时超出的池不跟踪，里面的贴图每次取用都当成脏的重传（暗黑实测峰值见内存读数行的「内存池」；塞尔达约 300 个，踩过）。扫描只到已用的槽数，容量大不增加开销 | was 256: with more memory pools than that the excess pools are untracked and their textures are treated as dirty and re-uploaded on every use (for Diablo's measured peak see "memory pools" in the memory readout line; Zelda has about 300 and hit this). The scan only covers slots in use, so a larger capacity costs nothing
typedef struct { uintptr_t base, end; _Atomic uint8_t *state; _Atomic uint64_t *gen; size_t cap; _Atomic int live; uint64_t incarnation; int snapshot_disabled; } range_t;
static range_t g_r[MAX_RANGE];
static _Atomic int g_nr;
static uintptr_t g_pg, g_shift;
static _Atomic uint64_t g_now = 1;
static _Atomic uint64_t g_faults, g_preps;
static _Atomic uint64_t g_adds_total;
static _Atomic int g_live, g_live_peak;   // 在册的池数与峰值（内存读数行用） | number of registered pools and peak (for the memory readout line)
void nvn_dirty_stat(int *live, int *peak, int *cap) { *live = atomic_load(&g_live); *peak = atomic_load(&g_live_peak); *cap = MAX_RANGE; }
static _Atomic int g_overflow_logged;
static uint64_t g_range_incarnation;
static pthread_mutex_t g_slot_mu = PTHREAD_MUTEX_INITIALIZER;   // 建池 / 销池（客体线程，可能并发）互斥选槽；缺页处理路径不碰它 | Pool creation / destruction (guest threads, possibly concurrent) pick slots under a mutex; the page-fault handling path does not touch it
static _Atomic uint64_t g_mp_calls, g_mp_pages, g_mp_ticks;   // 本模块的 mprotect：调用次数、覆盖页数、耗时（mach 时基，缺页路径里也安全） | This module's mprotect: call count, pages covered, time spent (mach timebase, safe in the page-fault path too)
static int mp(uintptr_t a, size_t len, int prot) {
    uint64_t t = mach_absolute_time(); int result=mprotect((void *)a, len, prot),error=errno;
    atomic_fetch_add(&g_mp_ticks, mach_absolute_time() - t); atomic_fetch_add(&g_mp_calls, 1); atomic_fetch_add(&g_mp_pages, len >> g_shift);
    errno=error;return result;
}

static mach_timebase_info_data_t g_tb;   // 首次登记池时取好（缺页路径里只做乘除） | Obtained when a pool is first registered (the fault path only multiplies and divides)
static _Atomic int g_busy_red;
// 等这一页离开 BUSY。返回 1 = 已离开；0 = 等过了上限（报红一次）。信号处理里可用：只有原子读、mach_absolute_time、sched_yield
// Wait for this page to leave BUSY. Returns 1 = it has left; 0 = waited past the limit (reported red once). Usable in a signal handler: only atomic reads, mach_absolute_time and sched_yield
static int busy_wait(range_t *r, uintptr_t p) {
    uint64_t t0 = 0;
    for (unsigned n = 1; atomic_load(&r->state[p]) == PG_BUSY; n++) {
        if (n & 0x3ff) continue;
        uint64_t now = mach_absolute_time(); if (!t0) t0 = now;
        if ((now - t0) * g_tb.numer / g_tb.denom > (uint64_t)BUSY_WAIT_MS * 1000000ull) {
            if (!atomic_exchange(&g_busy_red, 1)) LOG("【红】脏页跟踪：页 %p 停在「忙」超过 %d 毫秒（改保护的线程没走完），不再等", (void *)(r->base + (p << g_shift)), BUSY_WAIT_MS);
            return 0; }
        sched_yield();
    }
    return 1;
}
static range_t *find(uintptr_t a) {
    int n = atomic_load(&g_nr);
    for (int i = 0; i < n; i++) if (atomic_load_explicit(&g_r[i].live, memory_order_acquire) && a >= g_r[i].base && a < g_r[i].end) return &g_r[i];
    return NULL;
}
static void unprotect_page(range_t *r, uintptr_t p) {   // 调用方已把状态从 PROT 改成 BUSY | The caller has already changed the state from PROT to BUSY
    mp(r->base + (p << g_shift), g_pg, PROT_READ | PROT_WRITE);
    atomic_store(&r->gen[p], atomic_fetch_add(&g_now, 1) + 1);
    atomic_store(&r->state[p], PG_NONE);
}
static int on_fault(void *addr, int is_write, void *uc) {
    (void)uc;
    range_t *r = is_write ? find((uintptr_t)addr) : NULL;
    if (!r) return 0;
    uintptr_t p = ((uintptr_t)addr - r->base) >> g_shift;
    uint8_t st = PG_PROT;
    if (atomic_compare_exchange_strong(&r->state[p], &st, PG_BUSY)) { unprotect_page(r, p); atomic_fetch_add(&g_faults, 1); return 1; }
    if (st == PG_BUSY) return busy_wait(r, p);   // 别的线程正在改这页的保护：等它改完重执行（改成了只读就再缺页一次、走上面那一支） | Another thread is changing this page's protection: wait for it to finish and re-execute (if it became read-only, it faults once more and takes the branch above)
    return atomic_load(&r->gen[p]) != 0;   // 刚被别的线程解掉（本次缺页发生在解之前）：重执行；从未保护过的页不是我们的 | Just unprotected by another thread (this fault happened before the unprotect): re-execute; pages that were never protected are not ours
}
static void unprotect_write_run(range_t *r,uintptr_t first,uintptr_t end) {
    // 所有页均已独占 BUSY；先逐页发布新代数，才允许整个连续段变为可写。
    // Every page is exclusively BUSY. Publish each new generation before any page in the run becomes writable.
    for(uintptr_t p=first;p<end;p++)atomic_store(&r->gen[p],atomic_fetch_add(&g_now,1)+1);
    if(mp(r->base+(first<<g_shift),(end-first)<<g_shift,PROT_READ|PROT_WRITE))
        die("dirty host write unprotect failed: address=%p pages=%zu errno=%d",(void*)(r->base+(first<<g_shift)),(size_t)(end-first),errno);
    for(uintptr_t p=first;p<end;p++)atomic_store(&r->state[p],PG_NONE);
    atomic_fetch_add(&g_preps,end-first);
}
static void prep_write(void *addr, size_t len) {
    uintptr_t a = (uintptr_t)addr, e = a + len;
    while (len && a < e) {
        range_t *r = find(a);
        if (!r) { a = (a + g_pg) & ~(g_pg - 1); continue; }
        uintptr_t stop = e < r->end ? e : r->end;
        uintptr_t first=0;int open=0;
        uintptr_t p=(a-r->base)>>g_shift,q=(stop-1-r->base)>>g_shift;
        for (;p<=q;p++) {
            for (;;) { uint8_t st = PG_PROT;   // 见 BUSY 等完要重看：对方可能是在设只读，等完这页是 PROT，照样得解 | After waiting out BUSY, look again: the other side may have been setting read-only, in which case the page is PROT after the wait and still has to be unprotected
                if (atomic_compare_exchange_strong(&r->state[p], &st, PG_BUSY)) {if(!open){first=p;open=1;}break;}
                // 等别人的 BUSY 前先释放已认领的前缀，避免跨线程互等。
                // Flush the claimed prefix before waiting on another BUSY page, avoiding cross-thread hold-and-wait.
                if(open){unprotect_write_run(r,first,p);open=0;}
                if (st != PG_BUSY || !busy_wait(r, p)) break; }
        }
        if(open)unprotect_write_run(r,first,q+1);
        a = stop;
    }
}

void nvn_dirty_add_range(const void *mem, size_t size) {
    static int inited;
    if (!inited) { inited = 1; g_pg = (uintptr_t)getpagesize(); g_shift = __builtin_ctzl(g_pg); mach_timebase_info(&g_tb); dm_fault_hook_register(on_fault); dm_guest_write_prep_register(prep_write); }
    atomic_fetch_add(&g_adds_total, 1);
    pthread_mutex_lock(&g_slot_mu);
    int n = atomic_load(&g_nr);
    int slot = -1;
    for (int i = 0; i < n; i++) if (!atomic_load_explicit(&g_r[i].live, memory_order_acquire)) { slot = i; break; }
    if (slot < 0) {
        if (n >= MAX_RANGE) {
            int expected = 0;
            if (atomic_compare_exchange_strong(&g_overflow_logged, &expected, 1)) LOG("【红】脏页跟踪区间已满（%d），池 %p 不跟踪", MAX_RANGE, mem);
            pthread_mutex_unlock(&g_slot_mu); return;
        }
        slot = n;
    }
    { int lv = atomic_fetch_add(&g_live, 1) + 1, pk = atomic_load(&g_live_peak); while (lv > pk && !atomic_compare_exchange_weak(&g_live_peak, &pk, lv)) {} }
    range_t *r = &g_r[slot];
    uintptr_t base = (uintptr_t)mem & ~(g_pg - 1), end = ((uintptr_t)mem + size + g_pg - 1) & ~(g_pg - 1);
    size_t np = (end - base) >> g_shift;
    _Atomic uint8_t *state = r->state;
    _Atomic uint64_t *gen = r->gen;
    if (r->cap < np) {
        state = calloc(np, 1); gen = calloc(np, sizeof *gen);
        if (!state || !gen) die("脏页表分配失败（%zu 页）", np);
        // 旧数组不 free：信号处理里可能仍有极短暂的陈旧读者，泄漏量 = 每页 9 字节。
        // The old array is not freed: the signal handler may still have a very brief stale reader; leak = 9 bytes per page.
        r->cap = np;
    } else {
        memset((void *)state, 0, np); memset((void *)gen, 0, np * sizeof *gen);
    }
    r->base = base; r->end = end; r->state = state; r->gen = gen;
    if (!++g_range_incarnation) die("dirty range incarnation overflow");
    r->incarnation = g_range_incarnation; r->snapshot_disabled = 0;
    if (slot == n) atomic_store(&g_nr, n + 1);
    atomic_store_explicit(&r->live, 1, memory_order_release);
    pthread_mutex_unlock(&g_slot_mu);
}
// 池销毁：先把仍受保护的页还原成可写（池内存归还游戏后可能另作他用），再标失效
// Pool destruction: first restore still-protected pages to writable (the pool memory may be reused for something else once returned to the game), then mark invalid
void nvn_dirty_remove_range(const void *mem) {
    pthread_mutex_lock(&g_slot_mu);
    range_t *r = find((uintptr_t)mem);
    if (r) { prep_write((void *)r->base, r->end - r->base); atomic_store_explicit(&r->live, 0, memory_order_release); atomic_fetch_sub(&g_live, 1); }
    pthread_mutex_unlock(&g_slot_mu);
}
int nvn_dirty_tracked(const void *addr, size_t len) {
    range_t *r = find((uintptr_t)addr); return r && (uintptr_t)addr + len <= r->end;
}
// 设只读：先把页标成 BUSY，mprotect 落地后才标 PROT（次序与不变式见文件头的状态说明）
// Set read-only: mark the page BUSY first, and mark it PROT only after mprotect has landed (for the order and the invariant see the state notes at the top of the file)
void nvn_dirty_protect(const void *addr, size_t len) {
    if (!len || !nvn_dirty_tracked(addr, len)) return;
    range_t *r = find((uintptr_t)addr);
    uintptr_t a = (uintptr_t)addr, p = (a - r->base) >> g_shift, q = (a + len - 1 - r->base) >> g_shift;
    // 连续置成 PROT 的页合成一次 mprotect（逐页调用在首传大贴图时一帧上千次；选角页启动帧 5：4888 次）。
    // 已是 PROT / BUSY 的页断开连续段：不重复保护，也不碰别的线程正在改的页
    // Consecutive pages set to PROT are merged into one mprotect (per-page calls ran to over a thousand per frame when large textures were first uploaded; character select, startup frame 5: 4888 calls).
    // Pages already PROT / BUSY break a run: they are not protected again, and pages another thread is changing are left alone
    uintptr_t run = 0; int open = 0;
    for (uintptr_t i = p; i <= q + 1; i++) {
        uint8_t st = PG_NONE;
        int took = i <= q && atomic_compare_exchange_strong(&r->state[i], &st, PG_BUSY);
        if (took && !open) { run = i; open = 1; }
        if (!took && open) { mp(r->base + (run << g_shift), (i - run) << g_shift, PROT_READ); for (uintptr_t k = run; k < i; k++) atomic_store(&r->state[k], PG_PROT); open = 0; }
    }
}
uint64_t nvn_dirty_now(void) { return atomic_load(&g_now); }
int nvn_dirty_since(const void *addr, size_t len, uint64_t stamp) {
    if (!len) return 0;
    if (!nvn_dirty_tracked(addr, len)) return 1;   // 未跟踪时保守视为脏，宁可多传也不显示旧内容 | When untracked, conservatively treat as dirty: better to upload more than to show stale content
    range_t *r = find((uintptr_t)addr);
    uintptr_t a = (uintptr_t)addr, p = (a - r->base) >> g_shift, q = (a + len - 1 - r->base) >> g_shift;
    for (uintptr_t i = p; i <= q; i++) if (atomic_load(&r->gen[i]) > stamp) return 1;   // 上传前最后一次写的代号 ≤ 戳；之后的写必 > 戳 | The generation of the last write before upload is <= the stamp; any later write must be > the stamp
    return 0;
}
// 新 GPU-only 谱系单独持普通元数据事务；信号路径不读取 token，也不取得它的锁。
// A separate normal-thread metadata transaction serves the new GPU-only lineage; signal paths neither inspect its token nor acquire its lock.
static _Thread_local struct { nvn_dirty_snapshot_plan *plan; range_t *range; uintptr_t first, last; } g_snapshot_tx;
#ifdef DM_NVN_DIRTY_TEST
// Owned-test only: inject exactly one PROT_READ failure and let the real RW
// rollback succeed, so the permanent-disable path can be exercised without
// unmapping the guest range.
static int g_test_fail_protect_read;
void nvn_dirty_test_fail_next_protect(void) { g_test_fail_protect_read = 1; }
#endif
static int snapshot_mp_checked(uintptr_t address, size_t size, int prot) {
    uint64_t started = mach_absolute_time();
#ifdef DM_NVN_DIRTY_TEST
    if (g_test_fail_protect_read && prot == PROT_READ) { g_test_fail_protect_read = 0; errno = EACCES; return -1; }
#endif
    int result = mprotect((void *)address, size, prot), error = errno;
    atomic_fetch_add(&g_mp_ticks, mach_absolute_time() - started);
    atomic_fetch_add(&g_mp_calls, 1); atomic_fetch_add(&g_mp_pages, size >> g_shift);
    errno = error; return result;
}
static int snapshot_protect_run(range_t *r, uintptr_t first, uintptr_t end, nvn_dirty_snapshot_plan *plan) {
    if (first == end) return 1;
    uintptr_t address = r->base + (first << g_shift); size_t bytes = (end - first) << g_shift;
    if (snapshot_mp_checked(address, bytes, PROT_READ)) {
        plan->protection_errno = errno; plan->reason = NVN_DIRTY_SNAPSHOT_PROTECTION; r->snapshot_disabled = 1;
        // mprotect 可能部分生效；先真实恢复 RW，再结束 BUSY。恢复失败不能伪造任何可读写状态。
        // mprotect may have partially applied: actually restore RW before releasing BUSY. Failed rollback cannot certify any readable/writable state.
        if (snapshot_mp_checked(address, bytes, PROT_READ | PROT_WRITE)) die("dirty snapshot protection rollback failed: address=%p size=%zu errno=%d", (void *)address, bytes, errno);
        // 回滚也解除过原 PROT 页；发布新代数使原 CPU 缓存拒绝旧戳，不能只禁新 GPU 谱系。
        // Rollback also unprotects formerly PROT pages: publish a fresh generation so old CPU caches reject their stamps, not just the new GPU lineage.
        uint64_t generation = atomic_fetch_add(&g_now, 1) + 1;
        for (uintptr_t p = first; p < end; p++) { atomic_store(&r->gen[p], generation); atomic_store(&r->state[p], PG_NONE); }
        return 0;
    }
    for (uintptr_t p = first; p < end; p++) atomic_store(&r->state[p], PG_PROT);
    return 1;
}
static int snapshot_validate_locked(nvn_dirty_snapshot_plan *plan) {
    range_t *r = g_snapshot_tx.range;
    if (r->snapshot_disabled || r->incarnation != plan->incarnation || !atomic_load_explicit(&r->live, memory_order_acquire)) {
        plan->reason = NVN_DIRTY_SNAPSHOT_DISABLED; return 0;
    }
    if (atomic_load(&g_now) != plan->stamp) { plan->reason = NVN_DIRTY_SNAPSHOT_EPOCH; return 0; }
    for (uintptr_t p = g_snapshot_tx.first; p <= g_snapshot_tx.last; p++) {
        uint64_t gen = atomic_load(&r->gen[p]); uint8_t state = atomic_load(&r->state[p]);
        if (gen > plan->stamp) { plan->reason = NVN_DIRTY_SNAPSHOT_EPOCH; return 0; }
        if (state != PG_PROT) { plan->reason = state == PG_BUSY ? NVN_DIRTY_SNAPSHOT_BUSY : NVN_DIRTY_SNAPSHOT_STATE; return 0; }
    }
    if (atomic_load(&g_now) != plan->stamp) { plan->reason = NVN_DIRTY_SNAPSHOT_EPOCH; return 0; }
    return 1;
}
int nvn_dirty_snapshot_begin(const void *addr, size_t len, uint64_t old_stamp, uint64_t old_incarnation, nvn_dirty_snapshot_plan *plan) {
    int saved_errno = errno, accepted = 0; uintptr_t a = (uintptr_t)addr;
    if (!plan) { errno = saved_errno; return 0; }
    if (g_snapshot_tx.plan) {
        if (plan != g_snapshot_tx.plan) { memset(plan, 0, sizeof *plan); plan->reason = NVN_DIRTY_SNAPSHOT_REENTRANT; }
        errno = saved_errno; return 0;
    }
    memset(plan, 0, sizeof *plan); plan->size = len;
    if (!len || len > UINTPTR_MAX - a) { plan->reason = NVN_DIRTY_SNAPSHOT_INVALID; goto done; }
    pthread_mutex_lock(&g_slot_mu);
    range_t *r = find(a);
    if (!r || a + len > r->end) { plan->reason = NVN_DIRTY_SNAPSHOT_UNTRACKED; goto unlock; }
    plan->incarnation = r->incarnation;
    if (r->snapshot_disabled) { plan->reason = NVN_DIRTY_SNAPSHOT_DISABLED; goto unlock; }
    plan->stamp = atomic_load(&g_now); // 先取戳再扫描 / 保护 / 读，不能吃掉这一窗口中的写标记。 | Stamp before scan/protect/read, never absorb write marks from that window.
    plan->full = !old_incarnation || old_incarnation != r->incarnation;
    if (!plan->full && old_stamp > plan->stamp) { plan->reason = NVN_DIRTY_SNAPSHOT_INVALID; goto unlock; }
    uintptr_t first = (a - r->base) >> g_shift, last = (a + len - 1 - r->base) >> g_shift;
    for (uintptr_t p = first; p <= last; p++) {
        uint64_t gen = atomic_load(&r->gen[p]); uint8_t state = atomic_load(&r->state[p]);
        if (state == PG_BUSY) { plan->reason = NVN_DIRTY_SNAPSHOT_BUSY; goto unlock; }
        if (state != PG_NONE && state != PG_PROT) { plan->reason = NVN_DIRTY_SNAPSHOT_STATE; goto unlock; }
        if (!plan->full && gen <= old_stamp && state == PG_PROT) continue;
        uintptr_t page_start = r->base + (p << g_shift), start = page_start < a ? a : page_start;
        uintptr_t end = a + len - page_start < g_pg ? a + len : page_start + g_pg;
        size_t offset = start - a, bytes = end - start;
        if (plan->run_count && plan->runs[plan->run_count - 1].offset + plan->runs[plan->run_count - 1].size == offset) plan->runs[plan->run_count - 1].size += bytes;
        else {
            if (plan->run_count == NVN_DIRTY_SNAPSHOT_MAX_RUNS) { plan->reason = NVN_DIRTY_SNAPSHOT_RUN_LIMIT; goto unlock; }
            plan->runs[plan->run_count++] = (nvn_dirty_snapshot_run){offset, bytes};
        }
    }
    if (atomic_load(&g_now) != plan->stamp) { plan->reason = NVN_DIRTY_SNAPSHOT_EPOCH; goto unlock; }
    for (unsigned i = 0; i < plan->run_count; i++) {
        uintptr_t begin = (a + plan->runs[i].offset - r->base) >> g_shift;
        uintptr_t end = (a + plan->runs[i].offset + plan->runs[i].size - 1 - r->base) >> g_shift;
        uintptr_t owned_end = begin;
        for (uintptr_t p = begin; p <= end; p++) {
            uint8_t expected = atomic_load(&r->state[p]);
            if ((expected != PG_NONE && expected != PG_PROT) || !atomic_compare_exchange_strong(&r->state[p], &expected, PG_BUSY)) {
                if (!snapshot_protect_run(r, begin, owned_end, plan)) goto unlock;
                plan->reason = expected == PG_BUSY ? NVN_DIRTY_SNAPSHOT_BUSY : NVN_DIRTY_SNAPSHOT_STATE; goto unlock;
            }
            owned_end = p + 1;
        }
        if (!snapshot_protect_run(r, begin, owned_end, plan)) goto unlock;
    }
    g_snapshot_tx.plan = plan; g_snapshot_tx.range = r; g_snapshot_tx.first = first; g_snapshot_tx.last = last;
    if (!snapshot_validate_locked(plan)) { memset(&g_snapshot_tx, 0, sizeof g_snapshot_tx); goto unlock; }
    accepted = 1; goto done;
unlock:
    pthread_mutex_unlock(&g_slot_mu);
    if (plan->reason == NVN_DIRTY_SNAPSHOT_PROTECTION) LOG("dirty snapshot caching disabled after protection failure: address=%p size=%zu incarnation=%llu errno=%d", addr, len, (unsigned long long)plan->incarnation, plan->protection_errno);
done:
    errno = saved_errno; return accepted;
}
int nvn_dirty_snapshot_end(nvn_dirty_snapshot_plan *plan) {
    int saved_errno = errno, accepted = 0;
    if (plan && g_snapshot_tx.plan == plan) {
        accepted = snapshot_validate_locked(plan); memset(&g_snapshot_tx, 0, sizeof g_snapshot_tx); pthread_mutex_unlock(&g_slot_mu);
    }
    errno = saved_errno; return accepted;
}
void nvn_dirty_snapshot_abort(nvn_dirty_snapshot_plan *plan) {
    int saved_errno = errno;
    if (plan && g_snapshot_tx.plan == plan) { memset(&g_snapshot_tx, 0, sizeof g_snapshot_tx); pthread_mutex_unlock(&g_slot_mu); }
    errno = saved_errno;
}
void nvn_dirty_slot_stats(int *live, int *used_slots, uint64_t *adds_total) {
    int n = atomic_load(&g_nr), active = 0;
    for (int i = 0; i < n; i++) active += atomic_load_explicit(&g_r[i].live, memory_order_acquire) != 0;
    *live = active; *used_slots = n; *adds_total = atomic_load(&g_adds_total);
}
void nvn_dirty_stats(uint64_t *faults, uint64_t *preps) { *faults = atomic_load(&g_faults); *preps = atomic_load(&g_preps); }
void nvn_dirty_mp_stats(uint64_t *calls, uint64_t *pages, uint64_t *ns) {
    static mach_timebase_info_data_t tb; if (!tb.denom) mach_timebase_info(&tb);
    *calls = atomic_load(&g_mp_calls); *pages = atomic_load(&g_mp_pages); *ns = atomic_load(&g_mp_ticks) * tb.numer / tb.denom;
}
