// svc 0x1C（WaitProcessWideKeyAtomic）与 SDK 用户态快路径之间的可见性窗口，确定性单测（资源表读写锁丢唤醒卡死）。
// 场景：W 持互斥锁后调 0x1C 等条件变量；钩子落在 0x1C「放锁之后、挂链之前」，此刻让 R 按 SDK 用户态写法动作：
//   CAS 0→R 抢锁 → 读条件键，非 0 才发 svc 0x1D(键, 1)（sdk InternalConditionVariable::Signal 0x351140 同此）→ 放锁：锁字仍是 R 的标签则 CAS→0，带等待位则 svc 0x1B。
// 期望（Horizon 内核语义：等待者先发布「有等待」再放锁 ⇒ 任何在其放锁后改条件的线程必能看到键非 0 并唤醒它）：
//   W 的 0x1C 返回 0；返回时锁字 = W 的标签（被唤醒者持锁）；条件键 = 0（无剩余等待者）。
// 构建两份：t_condvar（现行顺序，须全过）与 t_condvar_old（-DDM_TEST_CV_OLD_ORDER 还原旧顺序，须判出丢唤醒 = 负对照有效）。
// Deterministic unit test of the visibility window between svc 0x1C (WaitProcessWideKeyAtomic) and the SDK's user-mode fast path (the lost wakeup hang of the resource table read-write lock).
// Scenario: W holds the mutex and calls 0x1C to wait on the condition variable; the hook sits in 0x1C "after releasing the lock, before linking into the wait list", and at this moment R acts the way the SDK's user-mode code does:
//   CAS 0->R to grab the lock -> read the condition key, send svc 0x1D(key, 1) only if non-zero (same as sdk InternalConditionVariable::Signal 0x351140) -> release the lock: CAS->0 if the lock word is still R's tag, svc 0x1B if the waiter bit is set.
// Expected (Horizon kernel semantics: a waiter publishes "there is a waiter" before releasing the lock => any thread that changes the condition after it releases the lock is bound to see a non-zero key and wake it):
//   W's 0x1C returns 0; on return the lock word = W's tag (the woken thread holds the lock); condition key = 0 (no remaining waiters).
// Two builds: t_condvar (current order, must fully pass) and t_condvar_old (-DDM_TEST_CV_OLD_ORDER restores the old order, must detect the lost wakeup = the negative control is valid).
#include "../dm.h"
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>
int svc_thread(int num, uint64_t *regs);
uint32_t svc_thread_main_init(void);
void (*dm_test_cv_after_release)(void);

static _Atomic uint32_t g_mutex, g_key;            // 互斥锁字、条件键（对应 SDK InternalCriticalSection / InternalConditionVariable 的一个字） | mutex word, condition key (corresponding to one word of the SDK's InternalCriticalSection / InternalConditionVariable)
static uint32_t g_wtag, g_rtag = 0x2002;
static _Atomic int g_rstate;                       // 0 未动 1 已抢到锁 2 将发 0x1D 3 完成 | 0 not started 1 lock acquired 2 about to send 0x1D 3 done
static pthread_mutex_t g_m = PTHREAD_MUTEX_INITIALIZER; static pthread_cond_t g_c = PTHREAD_COND_INITIALIZER;
static int g_go, g_wdone; static int64_t g_wret = -1; static uint32_t g_wlock_after;
static void state_set(int state) {
    atomic_store(&g_rstate, state);
    pthread_mutex_lock(&g_m); pthread_cond_broadcast(&g_c); pthread_mutex_unlock(&g_m);
}

static void *reader(void *a) {
    (void)a;
    pthread_mutex_lock(&g_m); while (!g_go) pthread_cond_wait(&g_c, &g_m); pthread_mutex_unlock(&g_m);
    uint32_t z = 0; while (!atomic_compare_exchange_strong(&g_mutex, &z, g_rtag)) { z = 0; sched_yield(); }   // 用户态抢锁 | grab the lock in user mode
    state_set(1);
    // （此处 R 改条件：例如把读者计数减到 0）
    // (here R changes the condition: for example decrementing the reader count to 0)
    if (atomic_load(&g_key)) { state_set(2); uint64_t r[8] = {(uint64_t)&g_key, 1}; svc_thread(0x1D, r); }   // Signal 快路径：键 0 不发 svc | Signal fast path: key 0 sends no svc
    uint32_t v = g_rtag;
    if (!atomic_compare_exchange_strong(&g_mutex, &v, 0)) { uint64_t r[8] = {(uint64_t)&g_mutex}; svc_thread(0x1B, r); }      // 带等待位 ⇒ ArbitrateUnlock | waiter bit set => ArbitrateUnlock
    state_set(3);
    return NULL;
}
static void hook(void) {   // 在 W 的 0x1C 里、持宿主全局锁 G、已放锁未挂链：放 R 去跑，等它「完成」或「将发 0x1D（会卡在 G 上）」 | inside W's 0x1C, holding the host global lock G, lock released but not yet linked into the wait list: let R run and wait until it is "done" or "about to send 0x1D (will block on G)"
    dm_test_cv_after_release = NULL;
    pthread_mutex_lock(&g_m); g_go = 1; pthread_cond_broadcast(&g_c); pthread_mutex_unlock(&g_m);
    pthread_mutex_lock(&g_m); while (atomic_load(&g_rstate) != 2 && atomic_load(&g_rstate) != 3) pthread_cond_wait(&g_c, &g_m); pthread_mutex_unlock(&g_m);
}
static void *waiter(void *a) {
    (void)a;
    g_wtag = svc_thread_main_init();
    uint32_t z = 0; atomic_compare_exchange_strong(&g_mutex, &z, g_wtag);                 // W 持锁 | W holds the lock
    dm_test_cv_after_release = hook;
    uint64_t r[8] = {(uint64_t)&g_mutex, (uint64_t)&g_key, g_wtag, (uint64_t)-1};
    svc_thread(0x1C, r);
    pthread_mutex_lock(&g_m); g_wret = (int64_t)r[0]; g_wlock_after = atomic_load(&g_mutex); g_wdone = 1; pthread_cond_broadcast(&g_c); pthread_mutex_unlock(&g_m);
    return NULL;
}
int main(void) {
    pthread_t w, rd; pthread_create(&rd, NULL, reader, NULL); pthread_create(&w, NULL, waiter, NULL);
    struct timespec dl; clock_gettime(CLOCK_REALTIME, &dl); dl.tv_sec += 10;
    pthread_mutex_lock(&g_m); while (!g_wdone) if (pthread_cond_timedwait(&g_c, &g_m, &dl)) break; int done = g_wdone; pthread_mutex_unlock(&g_m);
#ifdef DM_TEST_CV_OLD_ORDER
    // 负对照：旧顺序必丢唤醒（R 读到键 0 不发 0x1D，W 挂上去后无人唤醒）
    // Negative control: the old order always loses the wakeup (R reads key 0 and does not send 0x1D, and nobody wakes W after it is queued)
    int ok = !done && atomic_load(&g_rstate) == 3;
    printf("%s 旧顺序负对照：W %s，R 状态 %d（期望 W 不醒、R 未发 0x1D 即完成）\n", ok ? "✓" : "✗", done ? "被唤醒" : "10 秒未醒", atomic_load(&g_rstate));
    printf(ok ? "全过：1/1\n" : "失败：负对照没判出丢唤醒（判据无效）\n");
#else
    int ok = done && g_wret == 0 && g_wlock_after == g_wtag && atomic_load(&g_key) == 0;
    printf("%s 现行顺序：W %s 返回 %lld，锁字 %#x（期望 W 标签 %#x），条件键 %u（期望 0），R 状态 %d\n", ok ? "✓" : "✗",
           done ? "被唤醒" : "2 秒未醒", (long long)g_wret, g_wlock_after, g_wtag, atomic_load(&g_key), atomic_load(&g_rstate));
    printf(ok ? "全过：1/1\n" : "失败：1/1\n");
#endif
    fflush(stdout); _exit(ok ? 0 : 1);
}
