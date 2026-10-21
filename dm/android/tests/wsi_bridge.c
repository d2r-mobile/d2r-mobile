// #143 WSI↔生命周期桥的宿主测试：使用真实 dm_app_lifecycle 与真实 dm_wsi 控制器，验证排空/释放顺序与失败保留。
// 依据 app/wsi_bridge.c 与 app/lifecycle.c 的引用契约；反向对照证明顺序断言能抓住错误实现。
// #143 host tests for the WSI/lifecycle bridge: the real dm_app_lifecycle and the real dm_wsi controller, checking
// drain/release ordering and failure retention. Based on app/wsi_bridge.c and app/lifecycle.c ownership contracts;
// the negative controls show the ordering assertions catch a wrong implementation.
#include "../app/lifecycle.h"
#include "../app/wsi_bridge.h"
#include <stdio.h>
#include <string.h>

static int checks, failures;
#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

typedef struct {
    dm_app_state states[8];
} fake_host_state;

static int host_load(void *ctx) { (void)ctx; return 0; }
static int host_stop(void *ctx) { (void)ctx; return 0; }
static int host_pause(void *ctx) { (void)ctx; return 0; }
static int host_resume(void *ctx) { (void)ctx; return 0; }

typedef struct {
    int acquires, releases, lifecycles;
    void *acquired[8], *released[8];
    int events;                 // 事件序列 | sequence of events
    int seq[64];
    int seq_len;
    int close_result;           // 排空结果注入 | injected drain result
    int opens, closes, frames;
    int frame_queue[8], frame_queue_count, frame_queue_index;
    int call_count;
    uint32_t open_width, open_height;
    void *open_window;
    dm_app_lifecycle *lifecycle;
} fixture;

enum { EV_REF_ACQUIRE, EV_REF_RELEASE, EV_DRAIN_OLD, EV_LIFECYCLE_ACQUIRE, EV_WSI_OPEN, EV_DRAIN_OK, EV_LIFECYCLE_RELEASE, EV_RESIZE, EV_OPEN_FAILED };

static void push(fixture *f, int ev) {
    if (f->seq_len < 64) f->seq[f->seq_len] = ev;
    f->seq_len++;
}
static int index_of(const fixture *f, int ev) {
    for (int i = 0; i < f->seq_len && i < 64; i++) if (f->seq[i] == ev) return i;
    return -1;
}

static int bridge_created(void *ctx, void *window) { return dm_app_lifecycle_surface_created((dm_app_lifecycle *)ctx, window); }
static int bridge_destroyed(void *ctx, void *window) { return dm_app_lifecycle_surface_destroyed((dm_app_lifecycle *)ctx, window); }

static int fake_acquire(void *ctx, void *surface) {
    fixture *f = ctx;
    f->acquires++;
    if (f->acquires <= 8) f->acquired[f->acquires - 1] = surface;
    push(f, EV_REF_ACQUIRE);
    return 0;
}
static void fake_release(void *ctx, void *surface) {
    fixture *f = ctx;
    f->releases++;
    if (f->releases <= 8) f->released[f->releases - 1] = surface;
    push(f, EV_REF_RELEASE);
}
static int wsi_open(void *ctx, void *window, uint32_t width, uint32_t height) {
    fixture *f = ctx;
    f->opens++; f->call_count++;
    f->open_window = window; f->open_width = width; f->open_height = height;
    push(f, EV_WSI_OPEN);
    return 0;
}
static int wsi_close(void *ctx) {
    fixture *f = ctx;
    f->closes++; f->call_count++;
    push(f, EV_DRAIN_OK);
    return f->close_result;
}
static int wsi_frame(void *ctx) {
    fixture *f = ctx;
    f->frames++; f->call_count++;
    if (f->frame_queue_index < f->frame_queue_count) return f->frame_queue[f->frame_queue_index++];
    return DM_WSI_OK;
}
static fixture *trace_fixture = NULL;
static void wsi_trace(void *ctx, const char *event) {
    // 桥的 lifecycle.ctx 是真实控制器，追踪用单独的 fixture 指针。
    // The bridge's lifecycle.ctx is the real controller, so tracing uses a separate fixture pointer.
    fixture *f = trace_fixture;
    (void)ctx;
    if (!f) return;
    if (!strcmp(event, "drain-old")) push(f, EV_DRAIN_OLD);
    else if (!strcmp(event, "wsi-open")) push(f, EV_WSI_OPEN);
    else if (!strcmp(event, "drain-ok")) push(f, EV_DRAIN_OK);
    else if (!strcmp(event, "lifecycle-acquire")) push(f, EV_LIFECYCLE_ACQUIRE);
    else if (!strcmp(event, "lifecycle-release")) push(f, EV_LIFECYCLE_RELEASE);
    else if (!strcmp(event, "wsi-resize")) push(f, EV_RESIZE);
    else if (!strcmp(event, "wsi-open-failed")) push(f, EV_OPEN_FAILED);
}

static void setup(fixture *f, dm_app_lifecycle **lifecycle, dm_wsi *wsi, dm_wsi_bridge *bridge) {
    memset(f, 0, sizeof *f);
    trace_fixture = f;
    dm_app_host_ops ops = {.ctx = f, .load = host_load, .stop = host_stop, .pause = host_pause,
        .resume = host_resume, .surface_acquire = fake_acquire, .surface_release = fake_release};
    *lifecycle = dm_app_lifecycle_create(&ops, NULL, NULL);
    dm_app_lifecycle_start(*lifecycle);
    dm_wsi_ops wops = {.ctx = f, .open = wsi_open, .close = wsi_close, .frame = wsi_frame};
    dm_wsi_init(wsi, &wops, 1);
    dm_wsi_lifecycle_ops lops = {.ctx = *lifecycle, .surface_created = bridge_created,
        .surface_destroyed = bridge_destroyed, .trace = wsi_trace};
    dm_wsi_bridge_init(bridge, wsi, &lops);
}

static void test_replacement_order(void) {
    fixture f; dm_app_lifecycle *lc; dm_wsi wsi; dm_wsi_bridge bridge;
    setup(&f, &lc, &wsi, &bridge);
    CHECK(dm_wsi_bridge_window_ready(&bridge, (void *)0x1000, 640, 360) == 1);
    CHECK(f.acquires == 1 && f.opens == 1);
    CHECK(index_of(&f, EV_LIFECYCLE_ACQUIRE) < index_of(&f, EV_WSI_OPEN));
    // 替换：必须先排空旧设备，然后生命周期才 acquire 新引用（其内部释放旧引用）。
    // Replacement: drain the old device first; only then does the lifecycle acquire the new reference (releasing the old).
    CHECK(dm_wsi_bridge_window_ready(&bridge, (void *)0x2000, 640, 360) == 1);
    CHECK(f.opens == 2 && f.open_window == (void *)0x2000);
    const int drain = index_of(&f, EV_DRAIN_OLD);
    int acquire_new = -1;
    for (int i = drain + 1; i >= 0 && i < f.seq_len && i < 64; i++)
        if (f.seq[i] == EV_LIFECYCLE_ACQUIRE) { acquire_new = i; break; }
    CHECK(drain >= 0 && acquire_new > drain);
    // 旧引用的释放发生在第二个 acquire 之后，而不是排空之前。
    // The old reference release happens after the second acquire, never before the drain.
    int releases = 0;
    for (int i = 0; i < f.seq_len && i < 64; i++) if (f.seq[i] == EV_REF_RELEASE) releases++;
    CHECK(releases == 1);
    CHECK(f.released[0] == (void *)0x1000);
    CHECK(f.closes == 1);
    dm_app_lifecycle_stop(lc); dm_app_lifecycle_destroy(lc);
}

static void test_destroy_order_and_failure(void) {
    fixture f; dm_app_lifecycle *lc; dm_wsi wsi; dm_wsi_bridge bridge;
    setup(&f, &lc, &wsi, &bridge);
    CHECK(dm_wsi_bridge_window_ready(&bridge, (void *)0x1000, 640, 360) == 1);
    // 排空失败：不得让生命周期释放真实窗口引用。
    // Failed drain: the lifecycle must not release the real window reference.
    f.close_result = 1;
    CHECK(dm_wsi_bridge_window_destroyed(&bridge, (void *)0x1000) == 0);
    CHECK(f.releases == 0);
    CHECK(dm_app_lifecycle_surface(lc) == (void *)0x1000);
    CHECK(bridge.window == (void *)0x1000);
    // 重试成功后才释放，并且顺序是 drain → release。
    // A successful retry releases only then, in the order drain -> release.
    f.close_result = 0;
    CHECK(dm_wsi_bridge_window_destroyed(&bridge, (void *)0x1000) == 1);
    CHECK(f.releases == 1 && f.released[0] == (void *)0x1000);
    CHECK(dm_app_lifecycle_surface(lc) == NULL);
    CHECK(index_of(&f, EV_DRAIN_OK) < index_of(&f, EV_LIFECYCLE_RELEASE));
    // 陈旧/重复 destroy 不再释放。| Stale/duplicate destroy releases nothing more.
    CHECK(dm_wsi_bridge_window_destroyed(&bridge, (void *)0x1000) == 0);
    CHECK(f.releases == 1);
    dm_app_lifecycle_stop(lc); dm_app_lifecycle_destroy(lc);
}

static void test_resize_path(void) {
    fixture f; dm_app_lifecycle *lc; dm_wsi wsi; dm_wsi_bridge bridge;
    setup(&f, &lc, &wsi, &bridge);
    CHECK(dm_wsi_bridge_window_ready(&bridge, (void *)0x1000, 640, 360) == 1);
    CHECK(f.open_width == 640 && f.open_height == 360);
    // 真实 resize 回调路径：返回 REBUILD 并在下一 tick 用新尺寸重建。
    // Real resize callback path: REBUILD, then the next tick recreates with the new extent.
    CHECK(dm_wsi_bridge_window_resized(&bridge, (void *)0x1000, 1280, 720) == DM_WSI_REBUILD);
    CHECK(index_of(&f, EV_RESIZE) >= 0);
    CHECK(dm_wsi_tick(&wsi) == DM_WSI_OK);
    CHECK(f.opens == 2 && f.open_width == 1280 && f.open_height == 720);
    CHECK(f.closes == 1);
    // 只更新尺寸的旧快速路径不能通过：它不会产生第二次 open。
    // The old dimensions-only fast path fails this: it would never produce a second open.
    dm_app_lifecycle_stop(lc); dm_app_lifecycle_destroy(lc);
}

static void test_stale_and_open_failure(void) {
    fixture f; dm_app_lifecycle *lc; dm_wsi wsi; dm_wsi_bridge bridge;
    setup(&f, &lc, &wsi, &bridge);
    CHECK(dm_wsi_bridge_window_ready(&bridge, (void *)0x1000, 640, 360) == 1);
    // 陈旧窗口 destroy 不触碰当前状态。| A stale window destroy leaves the current state alone.
    CHECK(dm_wsi_bridge_window_destroyed(&bridge, (void *)0x9999) == 0);
    CHECK(f.closes == 0 && f.releases == 0);
    CHECK(bridge.window == (void *)0x1000);
    dm_app_lifecycle_stop(lc); dm_app_lifecycle_destroy(lc);
}

static void test_stale_identity_matrix(void) {
    fixture f; dm_app_lifecycle *lc; dm_wsi wsi; dm_wsi_bridge bridge;
    setup(&f, &lc, &wsi, &bridge);
    CHECK(dm_wsi_bridge_window_ready(&bridge, (void *)0x1000, 640, 360) == 1);
    CHECK(dm_wsi_bridge_window_ready(&bridge, (void *)0x2000, 640, 360) == 1);
    const int opens = f.opens, closes = f.closes, releases = f.releases;
    // 陈旧 destroy(A) 在 B 被接受后：不拥有、不关闭、不释放、不影响 B。
    // Stale destroy(A) after B is accepted: not owned, no close, no release, B untouched.
    CHECK(dm_wsi_bridge_owns_window(&bridge, (void *)0x1000) == 0);
    CHECK(dm_wsi_bridge_owns_window(&bridge, (void *)0x2000) == 1);
    CHECK(dm_wsi_bridge_window_destroyed(&bridge, (void *)0x1000) == 0);
    CHECK(f.closes == closes && f.releases == releases);
    CHECK(bridge.window == (void *)0x2000);
    // 陈旧 resize(A) 不得改到 B：返回 IDLE 且不排程重建、不重新打开。
    // Stale resize(A) must not touch B: IDLE, no rebuild scheduled, no reopen.
    CHECK(dm_wsi_bridge_window_resized(&bridge, (void *)0x1000, 999, 555) == DM_WSI_IDLE);
    CHECK(f.opens == opens && wsi.width == 640 && wsi.height == 360);
    CHECK(dm_wsi_bridge_window_resized(&bridge, (void *)0x2000, 1280, 720) == DM_WSI_REBUILD);
    CHECK(wsi.width == 1280 && wsi.height == 720);
    dm_app_lifecycle_stop(lc); dm_app_lifecycle_destroy(lc);
}

static void test_dispatch_eligibility(void) {
    fixture f; dm_app_lifecycle *lc; dm_wsi wsi; dm_wsi_bridge bridge;
    setup(&f, &lc, &wsi, &bridge);
    dm_wsi_timer_action action = DM_WSI_TIMER_KEEP;
    // 未就绪/未恢复：不 tick、STOP。| Windowless and pre-resume: no tick, STOP.
    CHECK(dm_wsi_bridge_dispatch(&bridge, 1, &action) == DM_WSI_IDLE && action == DM_WSI_TIMER_STOP);
    CHECK(f.frames == 0);
    CHECK(dm_wsi_bridge_window_ready(&bridge, (void *)0x1000, 640, 360) == 1);
    action = DM_WSI_TIMER_KEEP;
    CHECK(dm_wsi_bridge_dispatch(&bridge, 0, &action) == DM_WSI_IDLE && action == DM_WSI_TIMER_STOP);
    CHECK(f.frames == 0);
    // 恢复后正常出帧并要求保持定时器。| After resume it frames and keeps the timer.
    action = DM_WSI_TIMER_STOP;
    CHECK(dm_wsi_bridge_dispatch(&bridge, 1, &action) == DM_WSI_OK && action == DM_WSI_TIMER_KEEP);
    CHECK(f.frames == 1);
    // 暂停：STOP 且不 tick。| Paused: STOP and no tick.
    dm_wsi_pause(&wsi);
    action = DM_WSI_TIMER_KEEP;
    CHECK(dm_wsi_bridge_dispatch(&bridge, 1, &action) == DM_WSI_IDLE && action == DM_WSI_TIMER_STOP);
    CHECK(f.frames == 1);
    dm_wsi_resume(&wsi);
    // Surface 丢失：tick 后进入等待态并 STOP；后续 tick 不再进设备。
    // Surface lost: the tick enters the awaiting state and STOPs; later ticks do no device work.
    f.frame_queue[0] = DM_WSI_SURFACE_LOST;
    f.frame_queue_count = 1;
    action = DM_WSI_TIMER_KEEP;
    CHECK(dm_wsi_bridge_dispatch(&bridge, 1, &action) == DM_WSI_SURFACE_LOST && action == DM_WSI_TIMER_STOP);
    const int calls = f.call_count;
    action = DM_WSI_TIMER_KEEP;
    CHECK(dm_wsi_bridge_dispatch(&bridge, 1, &action) == DM_WSI_IDLE && action == DM_WSI_TIMER_STOP);
    CHECK(f.call_count == calls);
    // 粘性错误：永不 tick、永远 STOP。| Sticky error: never ticks, always STOP.
    fixture g; dm_app_lifecycle *lc2; dm_wsi wsi2; dm_wsi_bridge bridge2;
    setup(&g, &lc2, &wsi2, &bridge2);
    CHECK(dm_wsi_bridge_window_ready(&bridge2, (void *)0x1000, 640, 360) == 1);
    g.frame_queue[0] = DM_WSI_ERROR;
    g.frame_queue_count = 1;
    action = DM_WSI_TIMER_KEEP;
    CHECK(dm_wsi_bridge_dispatch(&bridge2, 1, &action) == DM_WSI_ERROR && action == DM_WSI_TIMER_STOP);
    const int g_calls = g.call_count;
    for (int i = 0; i < 3; i++) {
        action = DM_WSI_TIMER_KEEP;
        CHECK(dm_wsi_bridge_dispatch(&bridge2, 1, &action) == DM_WSI_ERROR && action == DM_WSI_TIMER_STOP);
    }
    CHECK(g.call_count == g_calls);   // 错误后零设备调用 | zero device calls after the error
    dm_app_lifecycle_stop(lc); dm_app_lifecycle_destroy(lc);
    dm_app_lifecycle_stop(lc2); dm_app_lifecycle_destroy(lc2);
}

int main(void) {
    test_replacement_order();
    test_destroy_order_and_failure();
    test_resize_path();
    test_stale_and_open_failure();
    test_stale_identity_matrix();
    test_dispatch_eligibility();
    printf("wsi_bridge: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
