// 内存账：DM_MEMSTAT=<tsv 路径> 时每秒记一行（Mac / iOS 通用）。
//   总量：phys_footprint（task_info TASK_VM_INFO，iOS 内存上限按它算）、os_proc_available_memory（仅 iOS）、Metal currentAllocatedSize
//   拆账：遍历本进程 VM 区（vm_region_recurse_64，VM_REGION_SUBMAP_INFO_64），按区归类，每类记
//         虚拟 = 地址空间大小（只预留也算）、常驻 = pages_resident、计费 = 脏页 + 被压缩/换出页，按 VM 对象去重（≈ 进 phys_footprint 的部分）
//         去重键 = (对象号, 对象内偏移)；去重原因：Metal 以「不复制」包住客体内存池，同一对象被多处映射（share_mode TRUESHARED、ref_count>1），逐区相加会重复计；
// Mac 实测按对象去重后 Σ计费 2,139 MiB 对 phys_footprint 2,111 MiB（+1.3%）
//   类别：guest（客体地址空间整块，含 SetHeapSize 的堆、alias、栈；RomFS 经 pread 读进的数据也落在这里）、code（内嵌 / 装载的模块映像）、
//         malloc（宿主堆）、gpu（IOAccelerator / IOSurface / Metal 标签区）、stack、file（文件映射）、other
//   计费 ≠ phys_footprint 的部分（IOKit 记账的 GPU 私有内存不在任何 VM 区里）单列为「未归属」= footprint − Σ计费。
// Memory accounting: when DM_MEMSTAT=<tsv path> is set, one line is logged per second (common to Mac / iOS).
//   Totals: phys_footprint (task_info TASK_VM_INFO, which the iOS memory limit is based on), os_proc_available_memory (iOS only), Metal currentAllocatedSize
//   Breakdown: walk this process's VM regions (vm_region_recurse_64, VM_REGION_SUBMAP_INFO_64), classify by region, and for each class record
//         virtual = address space size (reserved-only counts too), resident = pages_resident, charged = dirty pages + compressed/swapped-out pages, deduplicated by VM object (≈ the part that goes into phys_footprint)
//         dedup key = (object id, offset within the object); reason for dedup: Metal wraps the guest memory pool with "no copy", so the same object is mapped in several places (share_mode TRUESHARED, ref_count>1), and summing region by region would double count;
// measured on Mac, after per-object dedup Σcharged is 2,139 MiB against phys_footprint 2,111 MiB (+1.3%)
//   Classes: guest (the whole guest address space block, including the SetHeapSize heap, alias, stacks; data read from RomFS through pread also lands here), code (embedded / loaded module images),
//         malloc (host heap), gpu (IOAccelerator / IOSurface / Metal tagged regions), stack, file (file mappings), other
//   The part where charged ≠ phys_footprint (GPU private memory accounted by IOKit is not in any VM region) is listed separately as 「未归属」(unattributed) = footprint − Σcharged.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <TargetConditionals.h>
#include <mach/mach.h>
#include <mach/vm_statistics.h>
#if TARGET_OS_IPHONE
#include <os/proc.h>
#endif
#include "dm.h"

void svc_guest_range(uint8_t **base, size_t *size);
enum { C_GUEST, C_CODE, C_MALLOC, C_GPU, C_STACK, C_FILE, C_OTHER, NC };
static const char *k_cls[NC] = {"guest", "code", "malloc", "gpu", "stack", "file", "other"};

static int classify(vm_address_t a, const vm_region_submap_info_data_64_t *in) {
    uint8_t *gb; size_t gs; svc_guest_range(&gb, &gs);
    if (gb && a >= (vm_address_t)gb && a < (vm_address_t)gb + gs) return C_GUEST;
    for (int i = 0; i < g_nmod; i++)
        if (a >= (vm_address_t)g_mod[i].base && a < (vm_address_t)g_mod[i].base + g_mod[i].size) return C_CODE;
    switch (in->user_tag) {
        case VM_MEMORY_MALLOC: case VM_MEMORY_MALLOC_SMALL: case VM_MEMORY_MALLOC_LARGE: case VM_MEMORY_MALLOC_HUGE:
        case VM_MEMORY_MALLOC_TINY: case VM_MEMORY_MALLOC_LARGE_REUSABLE: case VM_MEMORY_MALLOC_LARGE_REUSED:
        case VM_MEMORY_MALLOC_NANO: case VM_MEMORY_MALLOC_MEDIUM: return C_MALLOC;
        case VM_MEMORY_IOACCELERATOR: case VM_MEMORY_IOSURFACE: case VM_MEMORY_IOKIT: return C_GPU;
        case VM_MEMORY_STACK: return C_STACK;
    }
    if (in->external_pager) return C_FILE;
    return C_OTHER;
}

// DM_MEMSTAT_DUMP=秒：到该秒时把每个 ≥1 MiB 的 VM 区逐行写到 <tsv>.regions（诊断记账口径用）
// DM_MEMSTAT_DUMP=seconds: at that second, write every VM region ≥1 MiB line by line to <tsv>.regions (for diagnosing the accounting method)
static void dump_regions(const char *path) {
    FILE *f = fopen(path, "w"); if (!f) return;
    fprintf(f, "起址	大小MiB	类别	user_tag	share_mode	obj_id	ref_count	常驻MiB	脏MiB	换出MiB	文件\n");
    vm_address_t a = 0; vm_size_t sz; natural_t depth = 0; vm_size_t pg = vm_page_size;
    for (;;) {
        vm_region_submap_info_data_64_t in; mach_msg_type_number_t cnt = VM_REGION_SUBMAP_INFO_COUNT_64;
        if (vm_region_recurse_64(mach_task_self(), &a, &sz, &depth, (vm_region_recurse_info_t)&in, &cnt) != KERN_SUCCESS) break;
        if (in.is_submap) { depth++; continue; }
        if (((uint64_t)in.pages_resident * pg >> 20) >= 1 || sz >= (64ull << 20))
            fprintf(f, "%#lx\t%lu\t%s\t%u\t%u\t%#x\t%u\t%llu\t%llu\t%llu\t%d\n", a, sz >> 20, k_cls[classify(a, &in)], in.user_tag, in.share_mode,
                    in.object_id, in.ref_count, (uint64_t)in.pages_resident * pg >> 20, (uint64_t)in.pages_dirtied * pg >> 20, (uint64_t)in.pages_swapped_out * pg >> 20, in.external_pager);
        a += sz;
    }
    fclose(f);
}

static void sample(FILE *f, double t, id<MTLDevice> dev) {
    task_vm_info_data_t vi; mach_msg_type_number_t n = TASK_VM_INFO_COUNT;
    uint64_t fp = task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&vi, &n) == KERN_SUCCESS ? vi.phys_footprint : 0;
    uint64_t avail = 0;
#if TARGET_OS_IPHONE
    avail = os_proc_available_memory();
#endif
    uint64_t virt[NC] = {0}, res[NC] = {0}, bill[NC] = {0};
    static uint32_t seen[1 << 16]; static uint32_t gen; gen++; if (!gen) { memset(seen, 0, sizeof seen); gen = 1; }   // 本次采样已计对象（开放寻址） | Objects already counted in this sample (open addressing)
    vm_address_t a = 0; vm_size_t sz; natural_t depth = 0; vm_size_t pg = vm_page_size;
    for (;;) {
        vm_region_submap_info_data_64_t in; mach_msg_type_number_t cnt = VM_REGION_SUBMAP_INFO_COUNT_64;
        if (vm_region_recurse_64(mach_task_self(), &a, &sz, &depth, (vm_region_recurse_info_t)&in, &cnt) != KERN_SUCCESS) break;
        if (in.is_submap) { depth++; continue; }
        int c = classify(a, &in);
        virt[c] += sz; res[c] += (uint64_t)in.pages_resident * pg;
        if (!in.external_pager) {                             // 文件页可回收，不进 footprint | File pages are reclaimable and do not count toward footprint
            static uint64_t key[1 << 16];                     // 键 = (对象号, 对象内偏移)：同对象被 mprotect 切开的各段覆盖不同页，不能并 | Key = (object id, offset within the object): the pieces of one object split apart by mprotect cover different pages and must not be merged
            uint64_t k = (uint64_t)in.object_id << 32 ^ (in.offset >> 14);
            uint32_t h = (uint32_t)((k * 0x9E3779B97F4A7C15ull) >> 48), dup = 0;
            while (seen[h] == gen) { if (key[h] == k) { dup = 1; break; } h = (h + 1) & 0xFFFF; }
            if (!dup) { seen[h] = gen; key[h] = k; bill[c] += ((uint64_t)in.pages_dirtied + in.pages_swapped_out) * pg; }
        }
        a += sz; continue;
    }
    uint64_t sum = 0; for (int c = 0; c < NC; c++) sum += bill[c];
    fprintf(f, "%.0f\t%llu\t%llu\t%llu\t%llu\t%llu\t%lld\t%lld", t, fp >> 20, avail >> 20, (unsigned long long)(dev.currentAllocatedSize >> 20),
            vi.internal >> 20, vi.compressed >> 20, (vi.ledger_tag_graphics_footprint + vi.ledger_tag_graphics_footprint_compressed) >> 20,
            (long long)(vi.limit_bytes_remaining >> 20));
    for (int c = 0; c < NC; c++) fprintf(f, "\t%llu/%llu/%llu", virt[c] >> 20, res[c] >> 20, bill[c] >> 20);
    fprintf(f, "\t%llu\t%lld\n", sum >> 20, ((int64_t)fp - (int64_t)sum) >> 20);
    fflush(f);
}

static void *memstat_thread(void *p) {
    FILE *f = fopen((const char *)p, "w");
    if (!f) { LOG("memstat：打不开 %s", (const char *)p); return NULL; }
    fprintf(f, "# 单位 MiB；内核记账 internal / compressed / 图形 ledger / 距上限；类别列 = 虚拟/常驻/计费（按对象去重）；计费合计 = Σ私有计费；未归属 = phys_footprint − 计费合计\n秒\tphys_footprint\t可用\tmetal_alloc\tinternal\tcompressed\tgraphics\t距上限");
    for (int c = 0; c < NC; c++) fprintf(f, "\t%s", k_cls[c]);
    fprintf(f, "\t计费合计\t未归属\n");
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice();   // 与图形后端同一设备对象（系统缺省设备单例） | The same device object as the graphics backend (the system default device singleton)
    uint64_t t0 = now_ns();
    const char *ds = getenv("DM_MEMSTAT_DUMP"); int dump_at = ds ? atoi(ds) : -1;
    char dp[1100]; snprintf(dp, sizeof dp, "%s.regions", (const char *)p);
    for (int sec = 0;; sec++) { sample(f, (now_ns() - t0) / 1e9, dev); if (sec == dump_at) dump_regions(dp); sleep(1); }
    return NULL;
}

__attribute__((constructor)) static void memstat_start(void) {
    const char *p = getenv("DM_MEMSTAT");
    if (!p || !*p) return;
    char *path = strdup(p);
#if TARGET_OS_IPHONE
    if (*p != '/') { size_t n = strlen(getenv("HOME")) + strlen(p) + 16; path = malloc(n); snprintf(path, n, "%s/Documents/%s", getenv("HOME"), p); }   // 相对名 → 容器 Documents（启动前不知容器路径） | Relative name → the container's Documents (the container path is not known before launch)
#endif
    pthread_t th; pthread_create(&th, NULL, memstat_thread, path); pthread_detach(th);
}

// ---------- 按线程 CPU：DM_THRCPU=<tsv 路径>[,间隔秒（缺省 10）]（Mac / iOS 通用；iOS 相对名 → 容器 Documents）----------
// 每个间隔遍历本进程全部线程（task_threads），THREAD_EXTENDED_INFO 取线程名与用户 + 系统 CPU 时间（ns），按线程号差分：
// 每行「秒 线程号 线程名 CPU%（100 = 一个核）」；末行「秒 - 合计 CPU%」。发热归因用（哪条线程吃 CPU），与 DM_HEAT 同时开可对照温控。
// ---------- Per-thread CPU: DM_THRCPU=<tsv path>[,interval in seconds (default 10)] (common to Mac / iOS; on iOS a relative name → the container's Documents) ----------
// Every interval, walk all threads of this process (task_threads), take the thread name and user + system CPU time (ns) from THREAD_EXTENDED_INFO, and difference them by thread id:
// each line is "seconds, thread id, thread name, CPU% (100 = one core)"; the last line is "seconds, -, total, CPU%". Used for heat attribution (which thread eats CPU); enable together with DM_HEAT to compare against thermal state.
#include <mach/thread_info.h>
#define THR_MAX 512
static void *thrcpu_thread(void *p) {
    char path[1024]; int iv = 10; { const char *c = strchr((const char *)p, ','); snprintf(path, sizeof path, "%.*s", c ? (int)(c - (const char *)p) : (int)strlen(p), (const char *)p); if (c) iv = atoi(c + 1); }
    FILE *f = fopen(path, "w"); if (!f) return NULL;
    fprintf(f, "秒\t线程号\t线程名\tCPU%%\n"); fflush(f);
    static uint64_t ids[THR_MAX], last[THR_MAX]; int n = 0; uint64_t t0 = now_ns(), tl = t0;
    for (;;) {
        sleep(iv); uint64_t t = now_ns(); double dt = (t - tl) / 1e9, tot = 0; tl = t;
        thread_act_array_t th; mach_msg_type_number_t nt = 0;
        if (task_threads(mach_task_self(), &th, &nt) != KERN_SUCCESS) continue;
        for (mach_msg_type_number_t i = 0; i < nt; i++) {
            thread_extended_info_data_t ei; mach_msg_type_number_t c1 = THREAD_EXTENDED_INFO_COUNT;
            thread_identifier_info_data_t ii; mach_msg_type_number_t c2 = THREAD_IDENTIFIER_INFO_COUNT;
            if (thread_info(th[i], THREAD_EXTENDED_INFO, (thread_info_t)&ei, &c1) == KERN_SUCCESS && thread_info(th[i], THREAD_IDENTIFIER_INFO, (thread_info_t)&ii, &c2) == KERN_SUCCESS) {
                uint64_t cpu = ei.pth_user_time + ei.pth_system_time; int k = 0; while (k < n && ids[k] != ii.thread_id) k++;
                if (k == n && n < THR_MAX) { ids[n] = ii.thread_id; last[n] = cpu; n++; }   // 新线程：本轮只记基线 | New thread: only record the baseline this round
                else if (k < n) { double pct = (cpu - last[k]) / 1e9 / dt * 100; last[k] = cpu; tot += pct;
                    if (pct >= 0.5) { char gn[96]; int thr_name_of(pthread_t, char *, size_t); pthread_t pt = pthread_from_mach_thread_np(th[i]);
                        const char *nm = pt && thr_name_of(pt, gn, sizeof gn) ? gn : ei.pth_name[0] ? ei.pth_name : "-";   // 客体线程：句柄 + 客体名；宿主线程：pthread 名 | Guest thread: handle + guest name; host thread: pthread name
                        fprintf(f, "%.0f\t%llu\t%s\t%.1f\n", (t - t0) / 1e9, ii.thread_id, nm, pct); } }
            }
            mach_port_deallocate(mach_task_self(), th[i]);
        }
        vm_deallocate(mach_task_self(), (vm_address_t)th, nt * sizeof *th);
        fprintf(f, "%.0f\t-\t合计\t%.1f\n", (t - t0) / 1e9, tot); fflush(f);
    }
    return NULL;
}
__attribute__((constructor)) static void thrcpu_start(void) {
    const char *p = getenv("DM_THRCPU");
    if (!p || !*p) return;
    char *spec = strdup(p);
#if TARGET_OS_IPHONE
    if (*p != '/') { size_t n = strlen(getenv("HOME")) + strlen(p) + 16; spec = malloc(n); snprintf(spec, n, "%s/Documents/%s", getenv("HOME"), p); }
#endif
    pthread_t th; pthread_create(&th, NULL, thrcpu_thread, spec); pthread_detach(th);
}
