// #143 自有 WSI 诊断控制器的宿主测试：真实状态机 + 注入真实失败/陈旧窗口/重建/暂停/停止。
// 依据 app/wsi_diagnostic.c 的契约；不含 Vulkan 或 Android 运行时，反向对照证明测试不是空转。
// #143 host tests for the owned WSI diagnostic controller: the real state machine with injected failures, stale
// windows, rebuilds, pause and stop. Based on the app/wsi_diagnostic.c contract; no Vulkan or Android runtime, and
// the negative controls prove the suite is not vacuous.
#include "../app/wsi_diagnostic.h"
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

enum { CALL_OPEN, CALL_CLOSE, CALL_FRAME };
typedef struct {
    int calls[128];
    int call_count;
    void *open_window;
    uint32_t open_width, open_height;
    int opens, closes, frames;
    int open_result;        // 0 或 DM_WSI_UNSUPPORTED/ERROR
    int close_queue[8];     // close 依次返回的结果 | queued close results
    int close_queue_count, close_queue_index;
    int frame_queue[16];    // frame 依次返回的状态 | queued frame statuses
    int frame_queue_count, frame_queue_index;
    int frame_result_default;
} fake_wsi;

static void record(fake_wsi *f, int call) {
    if (f->call_count < 128) f->calls[f->call_count] = call;
    f->call_count++;
}
static int fake_open(void *ctx, void *window, uint32_t width, uint32_t height) {
    fake_wsi *f = ctx;
    record(f, CALL_OPEN);
    f->opens++;
    f->open_window = window;
    f->open_width = width;
    f->open_height = height;
    return f->open_result;
}
static int fake_close(void *ctx) {
    fake_wsi *f = ctx;
    record(f, CALL_CLOSE);
    f->closes++;
    if (f->close_queue_index < f->close_queue_count)
        return f->close_queue[f->close_queue_index++];
    return 0;
}
static int fake_frame(void *ctx) {
    fake_wsi *f = ctx;
    record(f, CALL_FRAME);
    f->frames++;
    if (f->frame_queue_index < f->frame_queue_count)
        return f->frame_queue[f->frame_queue_index++];
    return f->frame_result_default;
}

static dm_wsi_ops ops_for(fake_wsi *f) {
    dm_wsi_ops ops = {.ctx = f, .open = fake_open, .close = fake_close, .frame = fake_frame};
    return ops;
}

static void test_disabled(void) {
    fake_wsi f = {0};
    dm_wsi_ops ops = ops_for(&f);
    dm_wsi wsi;
    dm_wsi_init(&wsi, &ops, 0);
    CHECK(dm_wsi_window_ready(&wsi, (void *)0x1000, 1440, 3200) == DM_WSI_IDLE);
    CHECK(dm_wsi_tick(&wsi) == DM_WSI_IDLE);
    CHECK(dm_wsi_stop(&wsi) == DM_WSI_IDLE);
    // 反向对照：默认模式下注入边界一次都不能被调用。| Negative control: the default mode never touches the boundary.
    CHECK(f.call_count == 0);
    CHECK(f.opens == 0 && f.frames == 0 && f.closes == 0);
}

static void test_frames_without_window_or_input(void) {
    fake_wsi f = {0};
    dm_wsi_ops ops = ops_for(&f);
    dm_wsi wsi;
    dm_wsi_init(&wsi, &ops, 1);
    // 无窗口：不打开、不绘制、不空转。| Windowless: no open, no frame, no spin.
    CHECK(dm_wsi_tick(&wsi) == DM_WSI_IDLE);
    CHECK(f.call_count == 0);
    // 窗口就绪后连续帧不依赖输入队列（测试里根本没有输入）。| Continuous frames need no input queue.
    CHECK(dm_wsi_window_ready(&wsi, (void *)0x1000, 1440, 3200) == DM_WSI_OK);
    CHECK(f.opens == 1 && f.open_window == (void *)0x1000);
    CHECK(f.open_width == 1440 && f.open_height == 3200);
    for (int i = 0; i < 5; i++) CHECK(dm_wsi_tick(&wsi) == DM_WSI_OK);
    CHECK(f.frames == 5 && wsi.frames == 5);
    // 同一窗口重绘不重建。| A redraw of the same window does not rebuild.
    CHECK(dm_wsi_window_ready(&wsi, (void *)0x1000, 1440, 3200) == DM_WSI_OK);
    CHECK(f.opens == 1 && f.closes == 0);
    // 暂停期间零调用；恢复后继续。| Pause performs zero calls; resume continues.
    const int before = f.frames;
    CHECK(dm_wsi_pause(&wsi) == DM_WSI_IDLE);
    CHECK(dm_wsi_tick(&wsi) == DM_WSI_IDLE);
    CHECK(f.frames == before);
    CHECK(dm_wsi_resume(&wsi) != DM_WSI_ERROR);
    CHECK(dm_wsi_tick(&wsi) == DM_WSI_OK);
    CHECK(f.frames == before + 1);
    CHECK(dm_wsi_stop(&wsi) == DM_WSI_OK);
    CHECK(f.closes == 1);
    // 停机后再次 tick/stop 不再触碰设备。| After stop, further ticks/stop do not touch the device.
    const int calls = f.call_count;
    CHECK(dm_wsi_tick(&wsi) == DM_WSI_IDLE);
    CHECK(dm_wsi_stop(&wsi) == DM_WSI_OK);
    CHECK(f.call_count == calls);
}

static void test_resize_rebuild(void) {
    fake_wsi f = {0};
    dm_wsi_ops ops = ops_for(&f);
    dm_wsi wsi;
    dm_wsi_init(&wsi, &ops, 1);
    CHECK(dm_wsi_window_ready(&wsi, (void *)0x1000, 1440, 3200) == DM_WSI_OK);
    CHECK(dm_wsi_resize(&wsi, 3200, 1440) == DM_WSI_REBUILD);
    CHECK(f.closes == 0);   // 只排程，不在 resize 回调里销毁 | schedule only, no teardown inside the callback
    CHECK(dm_wsi_tick(&wsi) == DM_WSI_OK);
    CHECK(f.closes == 1 && f.opens == 2);
    CHECK(f.open_width == 3200 && f.open_height == 1440);
    CHECK(dm_wsi_tick(&wsi) == DM_WSI_OK);
}

static void test_suboptimal_and_out_of_date(void) {
    fake_wsi f = {0};
    dm_wsi_ops ops = ops_for(&f);
    f.frame_queue[0] = DM_WSI_SUBOPTIMAL;
    f.frame_queue[1] = DM_WSI_OK;          // 重建后的第一帧 | first frame after the rebuild
    f.frame_queue[2] = DM_WSI_REBUILD;     // acquire 报告 OUT_OF_DATE | acquire reports OUT_OF_DATE
    f.frame_queue[3] = DM_WSI_OK;
    f.frame_queue_count = 4;
    f.frame_result_default = DM_WSI_OK;
    dm_wsi wsi;
    dm_wsi_init(&wsi, &ops, 1);
    CHECK(dm_wsi_window_ready(&wsi, (void *)0x1000, 800, 480) == DM_WSI_OK);
    // SUBOPTIMAL：本帧有效并排程重建。| SUBOPTIMAL: the frame counts and recreation is scheduled.
    CHECK(dm_wsi_tick(&wsi) == DM_WSI_SUBOPTIMAL);
    CHECK(wsi.frames == 1);
    CHECK(dm_wsi_tick(&wsi) == DM_WSI_OK);       // 先重建再画 | rebuild, then draw
    CHECK(f.closes == 1 && f.opens == 2);
    // OUT_OF_DATE：不呈现、要求重建；下一 tick 重建后恢复。| OUT_OF_DATE: nothing presented, rebuild required.
    CHECK(dm_wsi_tick(&wsi) == DM_WSI_REBUILD);
    CHECK(wsi.frames == 2);
    CHECK(dm_wsi_tick(&wsi) == DM_WSI_OK);
    CHECK(f.closes == 2 && f.opens == 3);
    CHECK(wsi.rebuilds >= 3);
}

static void test_surface_loss_and_replacement(void) {
    fake_wsi f = {0};
    dm_wsi_ops ops = ops_for(&f);
    f.frame_queue[0] = DM_WSI_SURFACE_LOST;
    f.frame_queue_count = 1;
    f.frame_result_default = DM_WSI_OK;
    dm_wsi wsi;
    dm_wsi_init(&wsi, &ops, 1);
    CHECK(dm_wsi_window_ready(&wsi, (void *)0x1000, 1440, 3200) == DM_WSI_OK);
    CHECK(dm_wsi_tick(&wsi) == DM_WSI_SURFACE_LOST);
    // 丢失后必须已排空关闭，并等待替换：tick 不再触碰设备。
    // After loss the device is drained/closed and ticks no longer touch it while awaiting a replacement.
    CHECK(f.closes == 1);
    CHECK(wsi.awaiting_surface == 1);
    const int calls = f.call_count;
    CHECK(dm_wsi_tick(&wsi) == DM_WSI_IDLE);
    CHECK(f.call_count == calls);
    // 陈旧窗口丢失回调不得影响状态。| A stale window-lost callback must not change state.
    CHECK(dm_wsi_window_lost(&wsi, (void *)0x2000) == DM_WSI_IDLE);
    CHECK(wsi.awaiting_surface == 1);
    // 新窗口到达：重新打开并继续连续帧。| A new window reopens and continuous frames continue.
    CHECK(dm_wsi_window_ready(&wsi, (void *)0x2000, 1440, 3200) == DM_WSI_OK);
    CHECK(f.opens == 2 && f.open_window == (void *)0x2000);
    CHECK(dm_wsi_tick(&wsi) == DM_WSI_OK);
    CHECK(wsi.losses == 1);
}

static void test_window_replacement_and_stale_destroy(void) {
    fake_wsi f = {0};
    dm_wsi_ops ops = ops_for(&f);
    dm_wsi wsi;
    dm_wsi_init(&wsi, &ops, 1);
    CHECK(dm_wsi_window_ready(&wsi, (void *)0x1000, 800, 480) == DM_WSI_OK);
    CHECK(dm_wsi_window_ready(&wsi, (void *)0x2000, 800, 480) == DM_WSI_OK);
    // 替换顺序：先关闭旧设备，再打开新窗口。| Replacement order: close the old device before opening the new one.
    CHECK(f.call_count == 3);
    CHECK(f.calls[0] == CALL_OPEN && f.calls[1] == CALL_CLOSE && f.calls[2] == CALL_OPEN);
    CHECK(f.opens == 2 && f.closes == 1);
    // 旧窗口的销毁回调不得关闭新设备。| The old window's destroy callback must not close the new device.
    CHECK(dm_wsi_window_lost(&wsi, (void *)0x1000) == DM_WSI_IDLE);
    CHECK(f.closes == 1);
    CHECK(wsi.window == (void *)0x2000);
    CHECK(dm_wsi_tick(&wsi) == DM_WSI_OK);
}

static void test_open_failures(void) {
    fake_wsi f = {0};
    dm_wsi_ops ops = ops_for(&f);
    f.open_result = DM_WSI_ERROR;
    dm_wsi wsi;
    dm_wsi_init(&wsi, &ops, 1);
    CHECK(dm_wsi_window_ready(&wsi, (void *)0x1000, 800, 480) == DM_WSI_ERROR);
    CHECK(f.opens == 1 && f.frames == 0);
    // 错误态不空转、不绘制。| Failed state neither spins nor draws.
    CHECK(dm_wsi_tick(&wsi) == DM_WSI_IDLE);
    CHECK(f.frames == 0 && f.opens == 1);

    // 缺少 maintenance 能力：显式 UNSUPPORTED，不回落。| Missing maintenance capability: explicit UNSUPPORTED, no fallback.
    fake_wsi u = {0};
    dm_wsi_ops uops = ops_for(&u);
    u.open_result = DM_WSI_UNSUPPORTED;
    dm_wsi uwsi;
    dm_wsi_init(&uwsi, &uops, 1);
    CHECK(dm_wsi_window_ready(&uwsi, (void *)0x1000, 800, 480) == DM_WSI_UNSUPPORTED);
    CHECK(dm_wsi_tick(&uwsi) == DM_WSI_UNSUPPORTED);
    CHECK(u.frames == 0);
    // 帧返回 UNSUPPORTED 也进入显式错误态。| A frame reporting UNSUPPORTED also becomes an explicit failure.
    fake_wsi g = {0};
    dm_wsi_ops gops = ops_for(&g);
    g.frame_result_default = DM_WSI_UNSUPPORTED;
    dm_wsi gwsi;
    dm_wsi_init(&gwsi, &gops, 1);
    CHECK(dm_wsi_window_ready(&gwsi, (void *)0x1000, 800, 480) == DM_WSI_OK);
    CHECK(dm_wsi_tick(&gwsi) == DM_WSI_UNSUPPORTED);
    const int calls = g.call_count;
    CHECK(dm_wsi_tick(&gwsi) == DM_WSI_UNSUPPORTED);
    CHECK(g.call_count == calls);
}

static void test_teardown_failure_retains_ownership(void) {
    fake_wsi f = {0};
    dm_wsi_ops ops = ops_for(&f);
    f.close_queue[0] = 1;   // 第一次 close 失败 | first close fails
    f.close_queue_count = 1;
    dm_wsi wsi;
    dm_wsi_init(&wsi, &ops, 1);
    CHECK(dm_wsi_window_ready(&wsi, (void *)0x1000, 800, 480) == DM_WSI_OK);
    CHECK(dm_wsi_tick(&wsi) == DM_WSI_OK);
    // 停机失败：ERROR 且窗口/设备所有权保留，可重试。| Stop failure: ERROR with retained ownership, retryable.
    CHECK(dm_wsi_stop(&wsi) == DM_WSI_ERROR);
    CHECK(wsi.open == 1);
    CHECK(wsi.window == (void *)0x1000);
    CHECK(dm_wsi_tick(&wsi) == DM_WSI_IDLE);   // 错误态不空转 | failed state does not spin
    CHECK(dm_wsi_stop(&wsi) == DM_WSI_OK);     // 重试成功 | retry succeeds
    CHECK(wsi.open == 0 && wsi.window == NULL);
    CHECK(f.closes == 2);
}

int main(void) {
    test_disabled();
    test_frames_without_window_or_input();
    test_resize_rebuild();
    test_suboptimal_and_out_of_date();
    test_surface_loss_and_replacement();
    test_window_replacement_and_stale_destroy();
    test_open_failures();
    test_teardown_failure_retains_ownership();
    printf("app_wsi_diagnostic: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
