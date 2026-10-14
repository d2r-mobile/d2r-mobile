// NVN 空设备单测：照游戏的用法从 nvnBootstrapLoader 取址，一路走到出图；期望值全部手算，不调被测实现。
// 编译运行见 dm/gfx/tests/Makefile（make -C dm/gfx/tests）。
// NVN null device unit tests: look up addresses from nvnBootstrapLoader the way the game does and go all the way to present; all expected values are hand-calculated, without calling the implementation under test.
// For build and run see dm/gfx/tests/Makefile (make -C dm/gfx/tests).
#include "../nvn/hle_nvn.c"
uint64_t g_t0_ns;   // 宿主 main.c 的进程起点（hle_nvn 逐帧 @t_s 用），单测替身 | process start time from host main.c (used by hle_nvn's per-frame @t_s); unit test stub
#include "../tex/nvn_deswizzle.h"
#include <time.h>
void *front_metal_layer(void) { return NULL; }   // 单测无窗口：出图只计数 | no window in unit tests: present only counts
module_t *module_of(const void *p) { (void)p; return NULL; }   // 单测无客体模块 | no guest modules in unit tests
module_t g_mod[MAX_MOD]; int g_nmod;                            // crash.c 依赖的宿主符号 | host symbols that crash.c depends on
void svc_thread_dump(void) {}
void crash_install(void);
void nvn_present_config(int hz, int display_mode);
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
uint64_t now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000ull + t.tv_nsec; }

static int g_fail, g_n;
#define EXPECT(name, want, got) do { long long w_ = (long long)(want), g_ = (long long)(got); g_n++;                 \
    printf("%-44s 期望 %-12lld → 真结果 %-12lld %s\n", name, w_, g_, w_ == g_ ? "✓" : "✗"); if (w_ != g_) g_fail++; } while (0)

#define P(name) ((NVNT_##name)proc(#name))
static PFNNVNDEVICEGETPROCADDRESSPROC gpa; static NVNdevice dev;
static void *proc(const char *n) { void *p = (void *)gpa(&dev, n); if (!p) { printf("取址失败 %s\n", n); exit(1); } return p; }

// 命令内存回调：每次给一段 256 字节的新内存（强制多段 JUMP 链）
// Command memory callback: hands out a new 256-byte memory segment each time (forces a multi-segment JUMP chain)
static uint8_t g_cmdmem[64 * 1024]; static size_t g_cmdoff; static int g_cb_calls; static NVNmemoryPool g_cmdpool;
static void mem_cb(NVNcommandBuffer *c, NVNcommandBufferMemoryEvent ev, size_t minsz, void *data) {
    (void)data; g_cb_calls++;
    if (ev != NVN_COMMAND_BUFFER_MEMORY_EVENT_OUT_OF_COMMAND_MEMORY) return;
    size_t n = minsz > 256 ? minsz : 256;
    P(nvnCommandBufferAddCommandMemory)(c, &g_cmdpool, (ptrdiff_t)g_cmdoff, n); g_cmdoff += n;
}

// 挂起单测：另一线程里做提交
// Suspend unit test: do the submit on another thread
static _Atomic int g_sstarted, g_sdone; static pthread_mutex_t g_smu = PTHREAD_MUTEX_INITIALIZER; static pthread_cond_t g_scv = PTHREAD_COND_INITIALIZER;
static NVNqueue *g_sq; static NVNcommandHandle g_sh; static NVNT_nvnQueueSubmitCommands g_sub;
static void *susp_thr(void *a) { (void)a; pthread_mutex_lock(&g_smu); atomic_store(&g_sstarted, 1); pthread_cond_broadcast(&g_scv); pthread_mutex_unlock(&g_smu); g_sub(g_sq, 1, &g_sh); atomic_store(&g_sdone, 1); return NULL; }
static int wait_started(void) { struct timespec dl; clock_gettime(CLOCK_REALTIME, &dl); dl.tv_sec += 10; pthread_mutex_lock(&g_smu); while (!atomic_load(&g_sstarted) && pthread_cond_timedwait(&g_scv, &g_smu, &dl) == 0) {} int ok = atomic_load(&g_sstarted); pthread_mutex_unlock(&g_smu); return ok; }

static size_t tex_size(int fmt, int w, int h, int flags, size_t *align) {
    NVNtextureBuilder tb;
    P(nvnTextureBuilderSetDevice)(&tb, &dev); P(nvnTextureBuilderSetDefaults)(&tb);
    P(nvnTextureBuilderSetFlags)(&tb, flags); P(nvnTextureBuilderSetTarget)(&tb, NVN_TEXTURE_TARGET_2D);
    P(nvnTextureBuilderSetWidth)(&tb, w); P(nvnTextureBuilderSetHeight)(&tb, h); P(nvnTextureBuilderSetFormat)(&tb, fmt);
    if (align) *align = P(nvnTextureBuilderGetStorageAlignment)(&tb);
    return P(nvnTextureBuilderGetStorageSize)(&tb);
}

int main(void) {
    crash_install();   // 生产同一套 SIGSEGV / SIGBUS 处理（缺页钩子在其中分发） | the same SIGSEGV / SIGBUS handling as production (the page-fault hook is dispatched from within it)
    // 1. 引导取址：bootstrap 取 GetProcAddress，再用它取全部名字
    // 1. Bootstrap address lookup: bootstrap gets GetProcAddress, then uses it to get all the names
    PFNNVNBOOTSTRAPLOADERPROC boot = (PFNNVNBOOTSTRAPLOADERPROC)h_nvnBootstrapLoader;
    gpa = (PFNNVNDEVICEGETPROCADDRESSPROC)boot("nvnDeviceGetProcAddress");
    EXPECT("bootstrap 取 GetProcAddress 非空", 1, gpa != NULL);
    int nonnull = 0; for (unsigned i = 0; i < NVN_NFUNC; i++) nonnull += gpa(NULL, nvn_names[i]) != NULL;
    EXPECT("523 个名字全部取到地址", 523, nonnull);
    EXPECT("未知名字取址返回空", 0, gpa(NULL, "nvnNoSuchFunction") != NULL);

    // 2. 设备
    // 2. Device
    NVNdeviceBuilder db; P(nvnDeviceBuilderSetDefaults)(&db); P(nvnDeviceBuilderSetFlags)(&db, 0);
    EXPECT("DeviceInitialize 成功", 1, P(nvnDeviceInitialize)(&dev, &db));
    int v; P(nvnDeviceGetInteger)(&dev, NVN_DEVICE_INFO_UNIFORM_BUFFER_ALIGNMENT, &v);
    EXPECT("uniform 缓冲对齐（X1 实测 256）", 256, v);
    P(nvnDeviceGetInteger)(&dev, NVN_DEVICE_INFO_TEXTURE_DESCRIPTOR_SIZE, &v); EXPECT("贴图描述符大小（32）", 32, v);
    P(nvnDeviceGetInteger)(&dev, NVN_DEVICE_INFO_QUEUE_COMMAND_MEMORY_MIN_SIZE, &v); EXPECT("队列命令内存下限（65536）", 65536, v);

    // 3. 内存池：GPU 地址 = 游戏给的存储地址
    // 3. Memory pool: GPU address = the storage address given by the game
    static uint8_t poolmem[1 << 20] __attribute__((aligned(4096)));
    NVNmemoryPoolBuilder pb; NVNmemoryPool pool;
    P(nvnMemoryPoolBuilderSetDevice)(&pb, &dev); P(nvnMemoryPoolBuilderSetDefaults)(&pb);
    P(nvnMemoryPoolBuilderSetStorage)(&pb, poolmem, sizeof poolmem); P(nvnMemoryPoolBuilderSetFlags)(&pb, 0x24);
    EXPECT("MemoryPoolInitialize 成功", 1, P(nvnMemoryPoolInitialize)(&pool, &pb));
    EXPECT("池 GPU 地址 = 存储地址", (long long)(uintptr_t)poolmem, P(nvnMemoryPoolGetBufferAddress)(&pool));

    // 4. 贴图存储尺寸（块线性：GOB = 64 B × 8 行；块高按「高×1.5」取 GOB 数 ≥16→16、≥8→8 …）
    //    256×256 RGBA8：每行 1024 B = 16 GOB；高 256 行 = 32 GOB 行，块高 16 GOB → 2 块；16×2×(512×16) = 262144
    // 4. Texture storage size (block-linear: GOB = 64 B × 8 rows; block height from the GOB count of "height × 1.5": ≥16→16, ≥8→8 ...)
    //    256×256 RGBA8: 1024 B per row = 16 GOBs; height 256 rows = 32 GOB rows, block height 16 GOBs → 2 blocks; 16×2×(512×16) = 262144
    size_t al; EXPECT("256x256 RGBA8 尺寸", 262144, tex_size(NVN_FORMAT_RGBA8, 256, 256, 0, &al));
    EXPECT("块线性对齐", 512, al);
    //    100×100 RGBA8：400 B → 7 GOB 宽；高 100 → 13 GOB 行，1.5 倍 = 150 行 = 19 GOB → 块高 16 → 1 块；7×1×8192 = 57344
    //    100×100 RGBA8: 400 B → 7 GOBs wide; height 100 → 13 GOB rows, ×1.5 = 150 rows = 19 GOBs → block height 16 → 1 block; 7×1×8192 = 57344
    EXPECT("100x100 RGBA8 尺寸", 57344, tex_size(NVN_FORMAT_RGBA8, 100, 100, 0, NULL));
    //    128×128 DXT1：32×32 块、8 B/块 → 256 B = 4 GOB 宽；32 行 → 1.5 倍 48 行 = 6 GOB → 块高 4 → 4 GOB 行 = 1 块；4×1×2048 = 8192
    //    128×128 DXT1: 32×32 blocks, 8 B/block → 256 B = 4 GOBs wide; 32 rows → ×1.5 = 48 rows = 6 GOBs → block height 4 → 4 GOB rows = 1 block; 4×1×2048 = 8192
    EXPECT("128x128 DXT1 尺寸", 8192, tex_size(NVN_FORMAT_RGBA_DXT1, 128, 128, 0, NULL));
    //    LINEAR 100×100 RGBA8：行距 400 按 32 对齐 = 416；416×100 = 41600，对齐 32
    //    LINEAR 100×100 RGBA8: row pitch 400 aligned to 32 = 416; 416×100 = 41600, aligned to 32
    EXPECT("线性 100x100 RGBA8 尺寸", 41600, tex_size(NVN_FORMAT_RGBA8, 100, 100, NVN_TEXTURE_FLAGS_LINEAR, &al));
    EXPECT("线性对齐", 32, al);

    // 5. 命令缓冲：录 uniform 更新 / 事件 / 计数器 / 缓冲拷贝 / 嵌套调用，内存 256 B 一段逼出多段链接
    // 5. Command buffer: record uniform updates / events / counters / buffer copies / nested calls, with memory in 256 B segments to force multi-segment linking
    NVNcommandBuffer cb; P(nvnCommandBufferInitialize)(&cb, &dev);
    NVNmemoryPoolBuilder cpb; P(nvnMemoryPoolBuilderSetDefaults)(&cpb); P(nvnMemoryPoolBuilderSetStorage)(&cpb, g_cmdmem, sizeof g_cmdmem);
    P(nvnMemoryPoolInitialize)(&g_cmdpool, &cpb);
    P(nvnCommandBufferSetMemoryCallback)(&cb, mem_cb); P(nvnCommandBufferSetMemoryCallbackData)(&cb, NULL);
    NVNeventBuilder eb; NVNevent ev; P(nvnEventBuilderSetDefaults)(&eb); P(nvnEventBuilderSetStorage)(&eb, &pool, 0x1000);
    P(nvnEventInitialize)(&ev, &eb);
    uint8_t *ub = poolmem + 0x2000, *cnt = poolmem + 0x3000, *src = poolmem + 0x4000, *dst = poolmem + 0x5000;
    for (int i = 0; i < 64; i++) src[i] = (uint8_t)(i * 3 + 1);
    uint32_t data[16];

    P(nvnCommandBufferBeginRecording)(&cb);
    EXPECT("录制中", 1, P(nvnCommandBufferIsRecording)(&cb));
    for (int k = 0; k < 20; k++) {                        // 20 次更新同一缓冲不同偏移，每次 64 B 数据 | 20 updates to different offsets of the same buffer, 64 B of data each
        for (int i = 0; i < 16; i++) data[i] = k * 100 + i;
        P(nvnCommandBufferUpdateUniformBuffer)(&cb, (NVNbufferAddress)ub, 4096, k * 64, 64, data);
        memset(data, 0xEE, sizeof data);                  // 录完改原数据：提交时必须用录制时拷进去的值 | modify the original data after recording: the submit must use the values copied in at record time
    }
    P(nvnCommandBufferSignalEvent)(&cb, &ev, NVN_EVENT_SIGNAL_MODE_WRITE, NVN_EVENT_SIGNAL_LOCATION_BOTTOM, 0, 7);
    P(nvnCommandBufferSignalEvent)(&cb, &ev, NVN_EVENT_SIGNAL_MODE_ADD, NVN_EVENT_SIGNAL_LOCATION_BOTTOM, 0, 5);
    P(nvnCommandBufferReportCounter)(&cb, NVN_COUNTER_TYPE_TIMESTAMP, (NVNbufferAddress)cnt);
    NVNcommandHandle h1 = P(nvnCommandBufferEndRecording)(&cb);
    EXPECT("录完不在录制", 0, P(nvnCommandBufferIsRecording)(&cb));
    P(nvnCommandBufferBeginRecording)(&cb);
    P(nvnCommandBufferCopyBufferToBuffer)(&cb, (NVNbufferAddress)src, (NVNbufferAddress)dst, 64, 0);
    P(nvnCommandBufferCopyCommands)(&cb, 1, &h1);         // 嵌套：再执行一遍 h1（事件 7 → +5 → 再写 7 → +5） | nested: execute h1 once more (event 7 → +5 → write 7 again → +5)
    NVNcommandHandle h2 = P(nvnCommandBufferEndRecording)(&cb);
    printf("（命令内存回调 %d 次，用掉 %zu B）\n", g_cb_calls, g_cmdoff);
    EXPECT("命令内存回调至少 2 次（多段）", 1, g_cb_calls >= 2);
    EXPECT("提交前事件值仍为 0", 0, P(nvnEventGetValue)(&ev));

    NVNqueueBuilder qb; NVNqueue q; static uint8_t qmem[1 << 19];
    P(nvnQueueBuilderSetDevice)(&qb, &dev); P(nvnQueueBuilderSetDefaults)(&qb);
    size_t qsz = P(nvnQueueBuilderGetQueueMemorySize)(&qb);
    EXPECT("队列内存 = 64K+256K+16K 取整 4K", 65536 + 262144 + 16384, qsz);
    P(nvnQueueBuilderSetQueueMemory)(&qb, qmem, qsz);
    EXPECT("QueueInitialize 成功", 1, P(nvnQueueInitialize)(&q, &qb));
    NVNcommandHandle hs[2] = {h1, h2};
    P(nvnQueueSubmitCommands)(&q, 2, hs);
    NVNsync sf; P(nvnSyncInitialize)(&sf, &dev); P(nvnQueueFenceSync)(&q, &sf, NVN_SYNC_CONDITION_ALL_GPU_COMMANDS_COMPLETE, 0);
    int wr = P(nvnSyncWait)(&sf, 5000000000ull);
    EXPECT("提交后围栏等待成功（0 或 1）", 1, wr == 0 || wr == 1);
    int ub_ok = 1;
    for (int k = 0; k < 20; k++) for (int i = 0; i < 16; i++) { uint32_t w; memcpy(&w, ub + k * 64 + i * 4, 4); ub_ok &= w == (uint32_t)(k * 100 + i); }
    EXPECT("uniform 缓冲 20×16 个字全等录制值", 1, ub_ok);
    EXPECT("事件值：h1(7,+5) h2 拷贝后再跑 h1(7,+5) = 12", 12, P(nvnEventGetValue)(&ev));
    EXPECT("缓冲拷贝 64 B 全等", 0, memcmp(src, dst, 64));
    uint64_t ts[2]; memcpy(ts, cnt, 16);
    EXPECT("计数器时间戳已写（非 0）", 1, ts[0] != 0 && ts[0] == ts[1]);

    // 5b. 同步必须挡住 CPU：先排 24 次 64 MiB 拷贝把 GPU 占住，最后一条把 src 拷到 dst；
    //     等围栏返回后 CPU 才改写 src。若等待提前放行，最后那条拷贝会读到改写后的 src。
    // 5b. Sync must hold the CPU back: first queue 24 copies of 64 MiB to keep the GPU busy, with the last one copying src to dst;
    //     the CPU rewrites src only after the fence wait returns. If the wait is released early, that last copy reads the rewritten src.
    {
        size_t big = 64u << 20, pgsz = 16384;
        uint8_t *heap; posix_memalign((void **)&heap, pgsz, 4 * big);
        uint8_t *busy_a = heap, *busy_b = heap + big, *bsrc = heap + 2 * big, *bdst = heap + 3 * big;
        memset(heap, 0, 4 * big); memset(bsrc, 0x5A, big);
        NVNmemoryPoolBuilder hb; NVNmemoryPool hp; P(nvnMemoryPoolBuilderSetDefaults)(&hb); P(nvnMemoryPoolBuilderSetStorage)(&hb, heap, 4 * big);
        P(nvnMemoryPoolInitialize)(&hp, &hb);
        P(nvnCommandBufferBeginRecording)(&cb);
        for (int i = 0; i < 24; i++) P(nvnCommandBufferCopyBufferToBuffer)(&cb, (NVNbufferAddress)busy_a, (NVNbufferAddress)busy_b, big, 0);
        P(nvnCommandBufferCopyBufferToBuffer)(&cb, (NVNbufferAddress)bsrc, (NVNbufferAddress)bdst, big, 0);
        NVNcommandHandle hb2 = P(nvnCommandBufferEndRecording)(&cb);
        NVNsync sb; P(nvnSyncInitialize)(&sb, &dev);
        uint64_t t0 = now_ns();
        P(nvnQueueSubmitCommands)(&q, 1, &hb2); P(nvnQueueFenceSync)(&q, &sb, NVN_SYNC_CONDITION_ALL_GPU_COMMANDS_COMPLETE, 0);
        int w0 = P(nvnSyncWait)(&sb, 0);
        int w1 = P(nvnSyncWait)(&sb, 10000000000ull);
        double ms = (now_ns() - t0) / 1e6;
        memset(bsrc, 0xC3, big);                                       // 等待放行后才改写 | rewrite only after the wait is released
        size_t bad = 0; for (size_t i = 0; i < big; i += 4096) bad += bdst[i] != 0x5A;
        printf("（GPU 占用约 %.1f ms）\n", ms);
        EXPECT("刚提交时零超时等待 = 超时(2)", 2, w0);
        EXPECT("长等待 = 满足(1)", 1, w1);
        EXPECT("dst 全为改写前的值（抽 16384 处）", 0, bad);
        P(nvnMemoryPoolFinalize)(&hp); free(heap);
    }

    // 6. 同步 / 窗口 / 出图
    // 6. Sync / window / present
    NVNsync s1, s2; P(nvnSyncInitialize)(&s1, &dev); P(nvnSyncInitialize)(&s2, &dev);
    EXPECT("未围栏的同步等待 = 超时(2)", 2, P(nvnSyncWait)(&s1, 0));
    P(nvnQueueFenceSync)(&q, &s1, NVN_SYNC_CONDITION_ALL_GPU_COMMANDS_COMPLETE, 0);
    EXPECT("围栏后等待 = 已就绪(0)", 0, P(nvnSyncWait)(&s1, 0));
    NVNtexture sc[2]; NVNtextureBuilder tb; P(nvnTextureBuilderSetDevice)(&tb, &dev); P(nvnTextureBuilderSetDefaults)(&tb);
    P(nvnTextureBuilderSetWidth)(&tb, 1280); P(nvnTextureBuilderSetHeight)(&tb, 720); P(nvnTextureBuilderSetFormat)(&tb, NVN_FORMAT_RGBA8);
    P(nvnTextureBuilderSetStorage)(&tb, &pool, 0); P(nvnTextureInitialize)(&sc[0], &tb); P(nvnTextureInitialize)(&sc[1], &tb);
    EXPECT("贴图宽", 1280, P(nvnTextureGetWidth)(&sc[0]));
    NVNtexture *scp[2] = {&sc[0], &sc[1]};
    NVNwindowBuilder wb; NVNwindow win; P(nvnWindowBuilderSetDevice)(&wb, &dev); P(nvnWindowBuilderSetDefaults)(&wb);
    P(nvnWindowBuilderSetTextures)(&wb, 2, scp); EXPECT("WindowInitialize 成功", 1, P(nvnWindowInitialize)(&win, &wb));
    int idx[4];
    for (int f = 0; f < 4; f++) {
        P(nvnWindowAcquireTexture)(&win, &s2, &idx[f]);
        for (int b = 0; b < 5; b++) P(nvnCommandBufferBarrier)(&cb, 0);
        P(nvnQueuePresentTexture)(&q, &win, idx[f]);
    }
    EXPECT("交换链轮转 0,1,0,1", 0x0101, idx[0] << 12 | idx[1] << 8 | idx[2] << 4 | idx[3]);
    EXPECT("取图后同步已就绪(0)", 0, P(nvnSyncWait)(&s2, 0));
    unsigned bi = 0; while (strcmp(nvn_names[bi], "nvnCommandBufferBarrier")) bi++;
    EXPECT("Barrier 计数 = 4 帧 × 5", 20, nvn_slot[bi].cnt);

    // 6b. 读回：像素 (x,y) = {x*16+y, y*16+x, 0x55, 0xAA}（期望按公式直接算）
    // 6b. Readback: pixel (x,y) = {x*16+y, y*16+x, 0x55, 0xAA} (expected values computed directly from the formula)
    {
        #define PX(x, y, c) ((uint8_t)((c) == 0 ? (x) * 16 + (y) : (c) == 1 ? (y) * 16 + (x) : (c) == 2 ? 0x55 : 0xAA))
        static uint8_t rb[256 << 10] __attribute__((aligned(16384)));
        NVNmemoryPoolBuilder rpb; NVNmemoryPool rp; P(nvnMemoryPoolBuilderSetDefaults)(&rpb); P(nvnMemoryPoolBuilderSetStorage)(&rpb, rb, sizeof rb);
        P(nvnMemoryPoolInitialize)(&rp, &rpb);
        uint8_t *stage = rb + 0x8000, *outb = rb + 0xC000;
        for (int y = 0; y < 4; y++) for (int x = 0; x < 8; x++) for (int c = 0; c < 4; c++) stage[(y * 8 + x) * 4 + c] = PX(x, y, c);
        NVNtextureBuilder b; NVNtexture ta; P(nvnTextureBuilderSetDevice)(&b, &dev); P(nvnTextureBuilderSetDefaults)(&b);
        P(nvnTextureBuilderSetWidth)(&b, 8); P(nvnTextureBuilderSetHeight)(&b, 4); P(nvnTextureBuilderSetFormat)(&b, NVN_FORMAT_RGBA8);
        P(nvnTextureBuilderSetStorage)(&b, &rp, 0); P(nvnTextureInitialize)(&ta, &b);
        NVNcopyRegion all = {0, 0, 0, 8, 4, 1}, sub = {2, 1, 0, 5, 3, 1}, cp = {1, 0, 0, 4, 2, 1};
        P(nvnCommandBufferBeginRecording)(&cb);
        P(nvnCommandBufferSetCopyRowStride)(&cb, 0);
        P(nvnCommandBufferCopyBufferToTexture)(&cb, (NVNbufferAddress)stage, &ta, NULL, &all, 0);
        P(nvnCommandBufferSetCopyRowStride)(&cb, 64);
        P(nvnCommandBufferCopyTextureToBuffer)(&cb, &ta, NULL, &cp, (NVNbufferAddress)outb, 0);
        NVNcommandHandle hr = P(nvnCommandBufferEndRecording)(&cb);
        memset(outb, 0xEE, 256);
        P(nvnQueueSubmitCommands)(&q, 1, &hr); P(nvnQueueFinish)(&q);
        uint8_t got[5 * 3 * 4]; P(nvnTextureReadTexels)(&ta, NULL, &sub, got);
        int bad = 0; for (int y = 0; y < 3; y++) for (int x = 0; x < 5; x++) for (int c = 0; c < 4; c++) bad += got[(y * 5 + x) * 4 + c] != PX(x + 2, y + 1, c);
        EXPECT("ReadTexels 子区域 5x3 逐字节（GPU 写入的贴图）", 0, bad);
        uint8_t gs[4 * 40]; memset(gs, 0xEE, sizeof gs); P(nvnTextureReadTexelsStrided)(&ta, NULL, &all, gs, 40, 0);
        bad = 0; for (int y = 0; y < 4; y++) { for (int x = 0; x < 8; x++) for (int c = 0; c < 4; c++) bad += gs[y * 40 + x * 4 + c] != PX(x, y, c); for (int k = 32; k < 40; k++) bad += gs[y * 40 + k] != 0xEE; }
        EXPECT("ReadTexelsStrided 行距 40（行尾填充不动）", 0, bad);
        bad = 0; for (int y = 0; y < 2; y++) { for (int x = 0; x < 4; x++) for (int c = 0; c < 4; c++) bad += outb[y * 64 + x * 4 + c] != PX(x + 1, y, c); for (int k = 16; k < 64; k++) bad += outb[y * 64 + k] != 0xEE; }
        EXPECT("CopyTextureToBuffer 行距 64（GPU 拷回）", 0, bad);
        // 预排布贴图：池内存就是真值（块线性），ReadTexels 解排布直读
        // Packaged texture: the pool memory is the ground truth (block-linear); ReadTexels deswizzles and reads it directly
        uint8_t lin[16 * 8 * 4]; for (int y = 0; y < 8; y++) for (int x = 0; x < 16; x++) for (int c = 0; c < 4; c++) lin[(y * 16 + x) * 4 + c] = PX(x, y, c);
        NVNtexture tb; NVNpackagedTextureLayout lay = {{1, 0, 0, 0, 7, 0, 1, 0}};
        P(nvnTextureBuilderSetDefaults)(&b); P(nvnTextureBuilderSetWidth)(&b, 16); P(nvnTextureBuilderSetHeight)(&b, 8); P(nvnTextureBuilderSetFormat)(&b, NVN_FORMAT_RGBA8);
        P(nvnTextureBuilderSetPackagedTextureLayout)(&b, &lay); P(nvnTextureBuilderSetStorage)(&b, &rp, 0x10000); P(nvnTextureInitialize)(&tb, &b);
        nvn_texdesc dd = ((tex_t *)&tb)->d;
        if (nvn_swizzle(&dd, 0, 0, lin, 64, rb + 0x10000)) { printf("nvn_swizzle 失败\n"); g_fail++; }
        NVNcopyRegion s2 = {3, 2, 0, 9, 5, 1}; uint8_t g2[9 * 5 * 4];
        P(nvnTextureReadTexels)(&tb, NULL, &s2, g2);
        bad = 0; for (int y = 0; y < 5; y++) for (int x = 0; x < 9; x++) for (int c = 0; c < 4; c++) bad += g2[(y * 9 + x) * 4 + c] != PX(x + 3, y + 2, c);
        EXPECT("ReadTexels 预排布贴图 9x5（池内存块线性直读）", 0, bad);
        P(nvnMemoryPoolFinalize)(&rp);
    }

    // 6c. 写保护脏页：预排布贴图上传后池页只读；游戏写 → 缺页 → 标脏 → 下次使用重传；宿主 read() 直写需先 prepare
    // 6c. Write-protected dirty pages: after a packaged texture is uploaded its pool pages are read-only; game write → page fault → mark dirty → re-upload on next use; a direct host read() write needs prepare first
    {
        static uint8_t wb_[256 << 10] __attribute__((aligned(16384)));
        NVNmemoryPoolBuilder wpb; NVNmemoryPool wp; P(nvnMemoryPoolBuilderSetDefaults)(&wpb); P(nvnMemoryPoolBuilderSetStorage)(&wpb, wb_, sizeof wb_);
        P(nvnMemoryPoolInitialize)(&wp, &wpb);
        NVNtextureBuilder b; NVNtexture tw; NVNpackagedTextureLayout lay = {{1, 0, 0, 0, 7, 0, 1, 0}};
        P(nvnTextureBuilderSetDevice)(&b, &dev); P(nvnTextureBuilderSetDefaults)(&b); P(nvnTextureBuilderSetWidth)(&b, 16); P(nvnTextureBuilderSetHeight)(&b, 8);
        P(nvnTextureBuilderSetFormat)(&b, NVN_FORMAT_RGBA8); P(nvnTextureBuilderSetPackagedTextureLayout)(&b, &lay);
        P(nvnTextureBuilderSetStorage)(&b, &wp, 0x10000); P(nvnTextureInitialize)(&tw, &b);
        nvn_texdesc dd = ((tex_t *)&tw)->d;
        uint8_t lin[16 * 8 * 4], *store = wb_ + 0x10000, *outb = wb_ + 0x20000;
        #define FILLPAT(k) for (int y = 0; y < 8; y++) for (int x = 0; x < 16; x++) for (int c = 0; c < 4; c++) lin[(y * 16 + x) * 4 + c] = (uint8_t)(x * 16 + y + (k) * 3 + c * 41)
        FILLPAT(0); nvn_swizzle(&dd, 0, 0, lin, 64, store);
        NVNcopyRegion all = {0, 0, 0, 16, 8, 1};
        // 用 GPU 拷回（CopyTextureToBuffer 读 MTLTexture）检验 GPU 侧实际内容：期望 = 第 k 版图案
        // Check the actual GPU-side contents with a GPU copy-back (CopyTextureToBuffer reading the MTLTexture): expected = pattern version k
        #define GPU_READ_EQ(k, name) do { P(nvnCommandBufferBeginRecording)(&cb); P(nvnCommandBufferSetCopyRowStride)(&cb, 0); \
            P(nvnCommandBufferCopyTextureToBuffer)(&cb, &tw, NULL, &all, (NVNbufferAddress)outb, 0); NVNcommandHandle hh = P(nvnCommandBufferEndRecording)(&cb); \
            P(nvnQueueSubmitCommands)(&q, 1, &hh); P(nvnQueueFinish)(&q); FILLPAT(k); EXPECT(name, 0, memcmp(outb, lin, sizeof lin)); } while (0)
        uint64_t up0, re0, fa0, pr0, up, re, fa, pr; mtl_pkg_stats(&up0, &re0); nvn_dirty_stats(&fa0, &pr0);
        GPU_READ_EQ(0, "首传：GPU 侧内容 = 第 0 版");
        mtl_pkg_stats(&up, &re); EXPECT("首传次数 +1", 1, up - up0);
        FILLPAT(1); nvn_swizzle(&dd, 0, 0, lin, 64, store);          // 游戏式 CPU 写：受保护页 → 缺页 → 钩子标脏 | game-style CPU write: protected page → page fault → hook marks it dirty
        nvn_dirty_stats(&fa, &pr); EXPECT("游戏写触发缺页 ≥ 1 次（已被钩子接住、未崩）", 1, fa - fa0 >= 1);
        GPU_READ_EQ(1, "重传后 GPU 侧内容 = 第 1 版");
        mtl_pkg_stats(&up, &re); EXPECT("池内容变化重传 +1", 1, re - re0);
        uint64_t fa1 = fa; GPU_READ_EQ(1, "内容不变再用：仍为第 1 版");
        nvn_dirty_stats(&fa, &pr); mtl_pkg_stats(&up, &re); EXPECT("内容不变：缺页 +0", 0, fa - fa1); EXPECT("内容不变：重传仍为 1", 1, re - re0);
        // 坑一：宿主 read() 直写受保护页。不 prepare 时内核返回 EFAULT（不缺页）；prepare 后读成功、标脏、下次重传
        // Pitfall 1: host read() writing directly into protected pages. Without prepare the kernel returns EFAULT (no page fault); after prepare the read succeeds, the pages are marked dirty and re-uploaded next time
        char path[1024]; snprintf(path, sizeof path, "%s/t_nvn_rd_XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "."); int fd = mkstemp(path);   // 每用户临时目录，不写 /tmp | per-user temp directory, do not write to /tmp
        FILLPAT(2); uint8_t tmpst[sizeof lin * 4]; memset(tmpst, 0, sizeof tmpst);
        static uint8_t full[0x10000]; memcpy(full, store, sizeof full);   // 现有存储内容（块线性），把第 2 版写进副本再存文件 | existing storage contents (block-linear); write version 2 into a copy and then save it to a file
        nvn_swizzle(&dd, 0, 0, lin, 64, full); if (write(fd, full, ((tex_t *)&tw)->L.size) < 0) g_fail++;
        ssize_t r0 = pread(fd, store, ((tex_t *)&tw)->L.size, 0); int e0 = errno;
        EXPECT("不 prepare：read() 进受保护页返回 -1 且 EFAULT", 1, r0 == -1 && e0 == EFAULT);
        dm_guest_write_prepare(store, ((tex_t *)&tw)->L.size);
        ssize_t r1 = pread(fd, store, ((tex_t *)&tw)->L.size, 0);
        EXPECT("prepare 后 read() 成功", (long long)((tex_t *)&tw)->L.size, r1);
        close(fd); unlink(path);
        GPU_READ_EQ(2, "宿主写后重传：GPU 侧内容 = 第 2 版");
        mtl_pkg_stats(&up, &re); EXPECT("宿主写触发重传，累计 2", 2, re - re0);
        P(nvnMemoryPoolFinalize)(&wp);
    }

    // 6d. 生命周期：挂起返回时在途 GPU 工作为 0；挂起期间提交被挡住，恢复后放行
    // 6d. Lifecycle: in-flight GPU work is 0 when suspend returns; submits are blocked during suspend and let through after resume
    {
        size_t big = 64u << 20; uint8_t *heap; posix_memalign((void **)&heap, 16384, 2 * big); memset(heap, 0, 2 * big);
        NVNmemoryPoolBuilder hb; NVNmemoryPool hp; P(nvnMemoryPoolBuilderSetDefaults)(&hb); P(nvnMemoryPoolBuilderSetStorage)(&hb, heap, 2 * big);
        P(nvnMemoryPoolInitialize)(&hp, &hb);
        P(nvnCommandBufferBeginRecording)(&cb);
        for (int i = 0; i < 24; i++) P(nvnCommandBufferCopyBufferToBuffer)(&cb, (NVNbufferAddress)heap, (NVNbufferAddress)(heap + big), big, 0);
        NVNcommandHandle hbusy = P(nvnCommandBufferEndRecording)(&cb);
        P(nvnQueueSubmitCommands)(&q, 1, &hbusy);
        uint64_t f = x_fence();   // 游戏经 FenceSync 拿到的栅栏值（翻译异步：可能还没翻译到） | Fence value the game gets from FenceSync (translation is asynchronous: it may not have been translated yet)
        int busy_before = !mtl_wait(f, 0);
        dm_gfx_suspend(1);
        EXPECT("挂起前 GPU 确实在忙（零超时等待未完成）", 1, busy_before);
        EXPECT("挂起返回时在途工作已完成", 1, mtl_wait(f, 0));
        P(nvnCommandBufferBeginRecording)(&cb); P(nvnCommandBufferBarrier)(&cb, 0); NVNcommandHandle hsmall = P(nvnCommandBufferEndRecording)(&cb);
        g_sq = &q; g_sh = hsmall; g_sub = P(nvnQueueSubmitCommands); atomic_store(&g_sstarted, 0); atomic_store(&g_sdone, 0);
        pthread_t th; pthread_create(&th, NULL, susp_thr, NULL);
        int started = wait_started();
        EXPECT("挂起期间另一线程已进入提交且尚未返回", 1, started && !atomic_load(&g_sdone));
        dm_gfx_suspend(0);
        pthread_join(th, NULL);
        EXPECT("恢复后该提交放行返回", 1, atomic_load(&g_sdone));
        P(nvnQueueFinish)(&q); P(nvnMemoryPoolFinalize)(&hp); free(heap);
    }

    // 6e. 出图节拍（真机语义）：出图立即返回；阻塞在取图——交换链 3 张、间隔 1、60 Hz，第 4 次取图等到第 0 张显示满一周期
    // 6e. Present pacing (real-device semantics): present returns immediately; blocking happens at acquire - swapchain of 3, interval 1, 60 Hz, the 4th acquire waits until texture 0 has been displayed for a full period
    {
        nvn_present_config(60, 0);
        NVNwindowBuilder wb2; NVNwindow w2; P(nvnWindowBuilderSetDevice)(&wb2, &dev); P(nvnWindowBuilderSetDefaults)(&wb2);
        NVNtexture *sc3[3] = {&sc[0], &sc[1], &sc[0]};
        P(nvnWindowBuilderSetTextures)(&wb2, 3, sc3); P(nvnWindowInitialize)(&w2, &wb2); P(nvnWindowSetPresentInterval)(&w2, 1);
        int ix;
        for (int k = 0; k < 3; k++) {
            P(nvnWindowAcquireTexture)(&w2, NULL, &ix);
            P(nvnQueuePresentTexture)(&q, &w2, ix);
        }
        P(nvnWindowAcquireTexture)(&w2, NULL, &ix);
        EXPECT("第 4 次取到的是第 0 张", 0, ix);
        P(nvnQueuePresentTexture)(&q, &w2, ix);
        P(nvnWindowAcquireTexture)(&w2, NULL, &ix);
        EXPECT("第 5 次取到的是第 1 张", 1, ix);
        nvn_present_config(0, 0);
    }

    // 6f. 出图线程：drawable 迟迟不可用（出图线程每次停 200 ms）时，出图调用仍立即返回；
    //     交换链转满一圈后，取回第 0 张要等它那次出图被处理完（第一次入队 + 200 ms）
    // 6f. Present thread: when the drawable stays unavailable for a long time (the present thread stalls 200 ms each time), the present call still returns immediately;
    //     after the swapchain has gone a full round, re-acquiring texture 0 has to wait until its present has been processed (first enqueue + 200 ms)
    {
        nvn_present_config(0, 0);   // 不按节拍等，只看出图线程归还 | do not wait on the display link; only check that the present thread hands it back
        const double S = 200;
        mtl_present_test_stall((uint64_t)(S * 1e6));
        NVNwindowBuilder wb3; NVNwindow w3; P(nvnWindowBuilderSetDevice)(&wb3, &dev); P(nvnWindowBuilderSetDefaults)(&wb3);
        NVNtexture *sc3[3] = {&sc[0], &sc[1], &sc[0]};
        P(nvnWindowBuilderSetTextures)(&wb3, 3, sc3); P(nvnWindowInitialize)(&w3, &wb3);
        int ix;
        for (int k = 0; k < 3; k++) {
            P(nvnWindowAcquireTexture)(&w3, NULL, &ix);
            P(nvnQueuePresentTexture)(&q, &w3, ix);
        }
        P(nvnWindowAcquireTexture)(&w3, NULL, &ix);
        EXPECT("取到的是第 0 张", 0, ix);
        mtl_present_test_stall(0); mtl_present_flush();
    }

    // 8. 对象副本：录完命令后游戏 Finalize 并在同址重建贴图 / 换程序着色器，已录命令里读到的仍是录制时的内容；退役副本过 SNAP_KEEP 次提交才回收
    // 8. Object snapshots: after commands are recorded the game Finalizes and rebuilds a texture at the same address / swaps a program's shaders; recorded commands still read what was there at record time; retired snapshots are reclaimed only after SNAP_KEEP submits
    {
        NVNtextureBuilder b; NVNtexture tx; P(nvnTextureBuilderSetDevice)(&b, &dev); P(nvnTextureBuilderSetDefaults)(&b);
        P(nvnTextureBuilderSetWidth)(&b, 8); P(nvnTextureBuilderSetHeight)(&b, 4); P(nvnTextureBuilderSetFormat)(&b, NVN_FORMAT_RGBA8); P(nvnTextureInitialize)(&tx, &b);
        NVNprogram pg; NVNshaderData sd[2]; memset(sd, 0, sizeof sd); P(nvnProgramInitialize)(&pg, &dev); P(nvnProgramSetShaders)(&pg, 1, sd);
        const NVNtexture *ct[1] = {&tx};
        P(nvnCommandBufferBeginRecording)(&cb);
        P(nvnCommandBufferSetRenderTargets)(&cb, 1, ct, NULL, NULL, NULL);
        P(nvnCommandBufferBindProgram)(&cb, &pg, NVN_SHADER_STAGE_VERTEX_BIT);
        NVNcommandHandle hs = P(nvnCommandBufferEndRecording)(&cb);
        const p_set_rt *rt = NULL; const p_bind_program *bp = NULL;
        for (const uint8_t *p = (const uint8_t *)hs; p;) {   // 按记录格式走：JUMP 跟到下一段，END 结束 | Walk the record format: follow JUMP to the next segment, stop at END
            op_hdr h; memcpy(&h, p, sizeof h);
            if (h.op == OP_END) break;
            if (h.op == OP_JUMP) { memcpy(&p, p + 8, 8); continue; }
            if (h.op == OP_SET_RT) rt = (const p_set_rt *)(p + 8);
            if (h.op == OP_BIND_PROGRAM) bp = (const p_bind_program *)(p + 8);
            p += h.size;
        }
        EXPECT("录到 SET_RT 与 BIND_PROGRAM", 2, (rt != NULL) + (bp != NULL));
        if (rt && bp) {
            const tex_t *s0 = rt->color[0];
            EXPECT("SET_RT 存的不是游戏对象地址", 1, (const void *)s0 != (const void *)&tx);
            EXPECT("副本宽 8", 8, s0->d.width);
            NVNtexture cp; memcpy(&cp, &tx, sizeof cp);
            EXPECT("按值拷来的同序号对象共用同一副本", 1, snap_tex(&cp) == s0);
            uint64_t ser0 = s0->serial;
            P(nvnTextureFinalize)(&tx); P(nvnTextureBuilderSetWidth)(&b, 16); P(nvnTextureBuilderSetHeight)(&b, 16); P(nvnTextureInitialize)(&tx, &b);
            EXPECT("同址重建后已录命令仍读到宽 8", 8, rt->color[0]->d.width);
            EXPECT("同址重建后已录命令仍是旧序号", 1, rt->color[0]->serial == ser0);
            const tex_t *s1 = snap_tex(&tx);
            EXPECT("重建后再录取到新副本、宽 16", 16, s1 != s0 ? (long long)s1->d.width : -1);
            P(nvnProgramSetShaders)(&pg, 2, sd);
            EXPECT("换着色器后已录命令的程序仍是 1 段", 1, bp->prog->nstage);
            x_drain(); uint64_t s_at = atomic_load(&g_x_subs);   // 退役盖戳 = 当时已入队的提交数；回收按已执行数 | Retire stamp = number of submits enqueued at that time; reclaim goes by the number executed
            for (int k = 1; k < SNAP_KEEP; k++) snap_tick(s_at + k);
            EXPECT("退役后 SNAP_KEEP−1 次提交：副本还在", 8, s0->d.width);
            snap_tick(s_at + SNAP_KEEP);
            EXPECT("退役后 SNAP_KEEP 次提交：副本已回收（序号清零）", 0, s0->serial);
            const tex_t *s2 = snap_tex(&cp);
            EXPECT("回收后旧拷贝对象再取副本：宽 8、序号是它自己的", 8, s2->serial == ((const tex_t *)&cp)->serial ? (long long)s2->d.width : -1);
        }
        P(nvnTextureFinalize)(&tx); P(nvnProgramFinalize)(&pg);
    }

    // 7. 未实现函数：返回 0、报红、只报一次
    // 7. Unimplemented functions: return 0, report a failure, report only once
    unsigned ui = 0; while (strcmp(nvn_names[ui], "nvnBufferBuilderSetDefaults")) ui++;
    ((void (*)(void *))proc("nvnBufferBuilderSetDefaults"))(NULL); ((void (*)(void *))proc("nvnBufferBuilderSetDefaults"))(NULL);
    EXPECT("未实现函数被调用后记为已报红", 1, g_unimpl_seen[ui] > 0);
    EXPECT("未实现函数计数 2", 2, nvn_slot[ui].cnt);

    printf("== %d 项，失败 %d ==\n", g_n, g_fail);
    return g_fail != 0;
}
