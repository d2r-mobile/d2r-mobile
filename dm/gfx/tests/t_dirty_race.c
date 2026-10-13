// 脏页跟踪「设只读」与「写前解保护」的次序竞态。
// 现场：线程甲 nvn_dirty_protect 已把页的状态记成「受保护」、攒批的 mprotect 还没落地；线程乙这时来 prep_write（宿主往客体内存写之前的解保护，
// 例如读盘把文件读进贴图存储）。旧实现：乙把状态改回「无」，随后甲的 mprotect 落地 ⇒ 页只读而状态「无」——
//   宿主再往里读文件：prep_write 看状态「无」跳过，系统调用不走缺页处理，直接 EFAULT；游戏线程直接写：缺页处理只回「重执行」，永远空转。
// 造法：把 nvn_dirty.c 里的 mprotect 换成这里的 t_mprotect——甲设只读的那一次调用落地之前，先放乙进来并等 100 毫秒，次序是确定的。
// 期望值不靠被测实现：乙的 prep_write 返回之后，这一页必须真的可写（往里 read 16 字节得 16；直接写一个字节不卡、读回是写进去的值），
// 且这次写要被记成脏（nvn_dirty_since 对设只读之前取的戳返回 1）。
// Ordering race between "set read-only" and "unprotect before write" in dirty-page tracking.
// Scene: thread A, in nvn_dirty_protect, has already recorded the page state as "protected" while the batched mprotect has not landed yet; thread B now arrives with prep_write (the unprotect done before the host writes into guest memory,
// for example a disk read putting a file into texture storage). Old implementation: B sets the state back to "none", then A's mprotect lands => the page is read-only while its state is "none" --
//   when the host reads a file into it again: prep_write sees state "none" and skips, the system call does not go through the page-fault handler and fails with EFAULT; when the game thread writes directly: the page-fault handler only returns "re-execute" and spins forever.
// How it is staged: the mprotect in nvn_dirty.c is replaced by t_mprotect here -- before the call with which A sets read-only lands, B is let in and given 100 ms, so the order is deterministic.
// Expected values do not depend on the implementation under test: after B's prep_write returns, this page must really be writable (reading 16 bytes into it gives 16; writing one byte directly does not hang and reads back as written),
// and this write must be recorded as dirty (nvn_dirty_since returns 1 for a stamp taken before the page was set read-only).
#define mprotect t_mprotect
#include "../nvn/nvn_dirty.c"
#undef mprotect
int mprotect(void *, size_t, int);
#include <fcntl.h>
#include <errno.h>

module_t g_mod[MAX_MOD];
int g_nmod;
module_t *module_of(const void *p) { (void)p; return NULL; }
void crash_install(void);

static int g_fail;
#define EXPECT(name, want, got) do { \
    long long want_ = (long long)(want), got_ = (long long)(got); \
    printf("%-58s 期望 %-4lld → 真结果 %-4lld %s\n", name, want_, got_, want_ == got_ ? "✓" : "✗"); \
    if (want_ != got_) g_fail++; \
} while (0)

static uint8_t *g_page; static _Atomic int g_armed, g_go, g_b_done;
static void *thread_b(void *arg) { (void)arg; while (!atomic_load(&g_go)) usleep(200); prep_write(g_page, 16); atomic_store(&g_b_done, 1); return NULL; }
int t_mprotect(void *a, size_t n, int prot) {
    if (prot == PROT_READ && atomic_exchange(&g_armed, 0)) { atomic_store(&g_go, 1); usleep(100000); }   // 甲的 mprotect 落地前：放乙进来，等它跑到头或卡在等待上 | Before A's mprotect lands: let B in and wait until it runs to the end or gets stuck waiting
    return mprotect(a, n, prot);
}

int main(void) {
    crash_install();
    size_t pg = (size_t)getpagesize();
    g_page = mmap(NULL, 2 * pg, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (g_page == MAP_FAILED) { perror("mmap"); return 2; }
    nvn_dirty_add_range(g_page, 2 * pg);
    uint64_t stamp = nvn_dirty_now();
    pthread_t tb; pthread_create(&tb, NULL, thread_b, NULL);
    atomic_store(&g_armed, 1);
    nvn_dirty_protect(g_page, 2 * pg);                       // 甲：置状态 → （乙插进来）→ mprotect 落地 | A: set the state -> (B cuts in) -> mprotect lands
    for (int i = 0; i < 2000 && !atomic_load(&g_b_done); i++) usleep(1000);
    EXPECT("乙的写前解保护在 2 秒内返回", 1, atomic_load(&g_b_done));
    if (!atomic_load(&g_b_done)) { printf("t_dirty_race：乙没返回，后面的项不测\n"); return 1; }
    pthread_join(tb, NULL);
    int fd = open("/dev/zero", O_RDONLY); g_page[pg] = 0;     // 第二页没人解过保护，这一写走缺页处理（对照：正常路径是通的） | Nobody has unprotected the second page, so this write goes through the page-fault handler (control: the normal path works)
    ssize_t r = read(fd, g_page, 16);                        // 乙接下来要做的那次宿主写：系统调用直接写进页里 | The host write that B is about to make: a system call writing straight into the page
    EXPECT("乙解完保护后往这页读文件：读到的字节数 = 请求的 16", 16, r);
    if (r == 16) { ((volatile uint8_t *)g_page)[1] = 0x5a; EXPECT("直接写这页不卡、读回是写进去的值", 0x5a, g_page[1]); }
    else { printf("  （read 失败 errno=%d；页只读而状态为「无」，直接写会在缺页处理里空转，这一项不执行）\n", errno); g_fail++; }
    EXPECT("这次写被记成脏（对设只读前的戳）", 1, nvn_dirty_since(g_page, 16, stamp));
    close(fd);
    printf("t_dirty_race：%s\n", g_fail ? "不通" : "全通"); return g_fail != 0;
}
