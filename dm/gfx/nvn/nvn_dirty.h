// 客体内存写保护脏页跟踪（nvn_dirty.c）
// Write-protect dirty-page tracking for guest memory (nvn_dirty.c)
#pragma once
#include <stddef.h>
#include <stdint.h>

void nvn_dirty_add_range(const void *mem, size_t size);           // 登记一个内存池为跟踪区间（首次调用时注册缺页钩子与宿主写前钩子） | Register a memory pool as a tracked range (the first call registers the page-fault hook and the host pre-write hook)
void nvn_dirty_stat(int *live, int *peak, int *cap);                // 在册的内存池数、峰值、跟踪表容量（内存读数行用） | number of registered memory pools, peak, tracking table capacity (for the memory readout line)
void nvn_dirty_remove_range(const void *mem);                     // 池销毁：受保护页还原可写并标失效 | Pool destruction: restore protected pages to writable and mark invalid
int nvn_dirty_tracked(const void *addr, size_t len);
void nvn_dirty_protect(const void *addr, size_t len);              // 覆盖页设只读 | Set the covered pages read-only
uint64_t nvn_dirty_now(void);                                      // 当前代号：上传前取，作为该份内容的戳 | Current generation: taken before upload, as the stamp of that content
int nvn_dirty_since(const void *addr, size_t len, uint64_t stamp); // 覆盖页在戳之后被写过 → 1 | Covered pages were written after the stamp -> 1
void nvn_dirty_stats(uint64_t *faults, uint64_t *preps);           // 累计：游戏写触发的缺页次数、宿主写前解保护的页数 | Cumulative: number of page faults triggered by game writes, number of pages unprotected before host writes
void nvn_dirty_mp_stats(uint64_t *calls, uint64_t *pages, uint64_t *ns);   // 累计：本模块 mprotect 调用次数、覆盖页数、耗时 | Cumulative: this module's mprotect call count, pages covered, time spent
void nvn_dirty_slot_stats(int *live, int *used_slots, uint64_t *adds_total); // 当前存活槽、槽表高水位、登记总次数 | Currently live slots, slot table high-water mark, total number of registrations

// 参考 Android 后端的可选只读/发布 ABI。本实现不提供这些符号时，后端按弱引用
// 走安全全量读取和原发布路径；不得用空实现冒充成功。
// Optional read-only/publish ABI used by the imported Android backend. This
// implementation intentionally leaves them undefined; the backend's weak
// references then take its safe full-read / original publication path.
typedef enum { NVN_DIRTY_DIAG_INVALID = 0, NVN_DIRTY_DIAG_UNTRACKED = 1, NVN_DIRTY_DIAG_TRACKED = 2 } nvn_dirty_diag_status;
typedef struct {
    uint64_t page_size, total_pages;
    uint64_t write_marked_pages, write_marked_bytes, write_marked_runs;
    uint64_t none_pages, prot_pages, busy_pages;
    uint64_t epoch_start, epoch_end;
    nvn_dirty_diag_status status;
} nvn_dirty_write_marked_stats;
void nvn_dirty_diagnose_write_marks(const void *addr, size_t len, uint64_t old_stamp, nvn_dirty_write_marked_stats *out);

#define NVN_DIRTY_SNAPSHOT_MAX_RUNS 32
typedef struct { size_t offset, size; } nvn_dirty_snapshot_run;
enum {
    NVN_DIRTY_SNAPSHOT_OK, NVN_DIRTY_SNAPSHOT_INVALID, NVN_DIRTY_SNAPSHOT_UNTRACKED,
    NVN_DIRTY_SNAPSHOT_BUSY, NVN_DIRTY_SNAPSHOT_EPOCH, NVN_DIRTY_SNAPSHOT_RUN_LIMIT,
    NVN_DIRTY_SNAPSHOT_PROTECTION, NVN_DIRTY_SNAPSHOT_DISABLED, NVN_DIRTY_SNAPSHOT_REENTRANT,
    NVN_DIRTY_SNAPSHOT_STATE
};
typedef struct {
    uint64_t incarnation, stamp;
    size_t size;
    unsigned full, run_count;
    int reason, protection_errno;
    nvn_dirty_snapshot_run runs[NVN_DIRTY_SNAPSHOT_MAX_RUNS];
} nvn_dirty_snapshot_plan;
int nvn_dirty_snapshot_begin(const void *addr, size_t len, uint64_t old_stamp, uint64_t old_incarnation, nvn_dirty_snapshot_plan *plan);
int nvn_dirty_snapshot_end(nvn_dirty_snapshot_plan *plan);
void nvn_dirty_snapshot_abort(nvn_dirty_snapshot_plan *plan);
typedef struct {
    uint64_t calls, bytes, protected_pages, rw_calls, rw_ns, fallback_calls, rw_clock_unavailable;
} nvn_dirty_publish_copy_stats;
int nvn_dirty_publish_copy(void *destination, const void *owned_readable_source, size_t size);
void nvn_dirty_publish_copy_get_stats(nvn_dirty_publish_copy_stats *out);
