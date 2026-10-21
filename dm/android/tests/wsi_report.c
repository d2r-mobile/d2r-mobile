// #147 WSI 证据报告宿主测试：真实统计/格式化边界，确定性时间戳；失败不计为有效帧。
// 依据 app/wsi_report.h 契约；负向对照覆盖"失败被当成成功"与"未知被编造"两类回归。
// #147 host tests for the WSI evidence report: real accounting/formatting with deterministic timestamps; a failure is
// never a valid frame. Based on the app/wsi_report.h contract; negative controls cover "failure counted as success"
// and "unknown invented".
#include "../app/wsi_report.h"
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

static void test_config_lines(void) {
    dm_wsi_report r;
    dm_wsi_report_init(&r, 1, 1000);
    char line[512];
    // 协商前：extent/format/present/images/maintenance 全是 unknown，不编造。
    // Before negotiation every negotiated field stays unknown, never invented.
    dm_wsi_report_config_line(&r, line, sizeof line);
    CHECK(strstr(line, "mode=wsi-clear-swapchain") != NULL);
    CHECK(strstr(line, "extent=unknown") != NULL);
    CHECK(strstr(line, "format=unknown") != NULL);
    CHECK(strstr(line, "present_mode=unknown") != NULL);
    CHECK(strstr(line, "images=unknown") != NULL);
    CHECK(strstr(line, "maintenance1=unknown") != NULL);
    CHECK(strstr(line, "requested=unknown") != NULL);

    dm_wsi_report_attempt(&r, 1440, 3200);
    dm_wsi_report_negotiated(&r, 1440, 3200, 37, 2, 4, 1);
    dm_wsi_report_config_line(&r, line, sizeof line);
    CHECK(strstr(line, "requested=1440x3200") != NULL);
    CHECK(strstr(line, "extent=1440x3200") != NULL);
    CHECK(strstr(line, "format=37") != NULL);
    CHECK(strstr(line, "present_mode=2") != NULL);
    CHECK(strstr(line, "images=4") != NULL);
    CHECK(strstr(line, "maintenance1=enabled") != NULL);

    // 请求与实际不同时两者都必须出现。| Requested and actual extents both appear when they differ.
    dm_wsi_report_negotiated(&r, 1280, 720, 37, 2, 3, 0);
    dm_wsi_report_config_line(&r, line, sizeof line);
    CHECK(strstr(line, "requested=1440x3200") != NULL);
    CHECK(strstr(line, "extent=1280x720") != NULL);
    CHECK(strstr(line, "maintenance1=disabled") != NULL);

    // 默认 CPU 模式标签。| Default CPU-mode label.
    dm_wsi_report cpu;
    dm_wsi_report_init(&cpu, 0, 0);
    dm_wsi_report_config_line(&cpu, line, sizeof line);
    CHECK(strstr(line, "mode=cpu-copy-indexed") != NULL);
}

static void test_counters_never_count_failure(void) {
    dm_wsi_report r;
    dm_wsi_report_init(&r, 1, 0);
    dm_wsi_report_result(&r, DM_WSI_OK);
    dm_wsi_report_result(&r, DM_WSI_OK);
    dm_wsi_report_result(&r, DM_WSI_SUBOPTIMAL);
    CHECK(r.queued_presents == 3);
    CHECK(r.suboptimal == 1);

    // 负向对照：失败绝不增加 queued_presents。| Negative control: a failure never increments queued presents.
    dm_wsi_report_result(&r, DM_WSI_ERROR);
    dm_wsi_report_result(&r, DM_WSI_UNSUPPORTED);
    CHECK(r.queued_presents == 3);
    CHECK(r.errors == 1);
    CHECK(r.unsupported == 1);

    dm_wsi_report_result(&r, DM_WSI_REBUILD);
    dm_wsi_report_result(&r, DM_WSI_SURFACE_LOST);
    dm_wsi_report_result(&r, DM_WSI_IDLE);
    CHECK(r.rebuild_requests == 1);
    CHECK(r.surface_lost == 1);
    CHECK(r.queued_presents == 3);

    dm_wsi_report_event(&r, DM_WSI_EVENT_PAUSE);
    dm_wsi_report_event(&r, DM_WSI_EVENT_RESUME);
    dm_wsi_report_event(&r, DM_WSI_EVENT_STOP);
    CHECK(r.paused == 1 && r.resumed == 1 && r.stops == 1);

    // 进度行把失败与成功分开，并把速率标成 queued-present。| The progress line separates failures and labels the rate.
    char line[512];
    dm_wsi_report_progress_line(&r, 2000000000ull, line, sizeof line);
    CHECK(strstr(line, "queued_presents=3") != NULL);
    CHECK(strstr(line, "errors=1") != NULL);
    CHECK(strstr(line, "surface_lost=1") != NULL);
    CHECK(strstr(line, "queued-present throughput") != NULL);
    CHECK(strstr(line, "NOT display fps/latency/vsync") != NULL);
    CHECK(strstr(line, "display_fps") == NULL);
}

static void test_bounded_rate(void) {
    dm_wsi_report r;
    dm_wsi_report_init(&r, 1, 1000000000ull);
    // 首次调用给出初始快照。| The first call provides the initial snapshot.
    CHECK(dm_wsi_report_due(&r, 1000000000ull) == 1);
    // 无新进展：即使过了一秒也不重复。| No new progress: no repeat even after a second.
    CHECK(dm_wsi_report_due(&r, 2000000000ull) == 0);
    // 有新进展但距上次报告不足 1 秒：不触发。| New progress under one second since the last report: not due.
    dm_wsi_report_result(&r, DM_WSI_OK);
    CHECK(dm_wsi_report_due(&r, 1200000000ull) == 0);
    // 满 1 秒：触发一次并清 dirty。| At one second: one report, dirty cleared.
    CHECK(dm_wsi_report_due(&r, 2000000000ull) == 1);
    CHECK(dm_wsi_report_due(&r, 2000000001ull) == 0);
    // 时钟回退不触发。| Clock regression does not report.
    dm_wsi_report_result(&r, DM_WSI_OK);
    CHECK(dm_wsi_report_due(&r, 1900000000ull) == 0);
    // 继续进展后再次到期。| Later progress becomes due again.
    CHECK(dm_wsi_report_due(&r, 3000000000ull) == 1);
    // 强制快照不受限制，并清 dirty。| Forced snapshots are not rate limited and clear dirty.
    dm_wsi_report_force(&r, 3000000001ull);
    CHECK(dm_wsi_report_due(&r, 3000000002ull) == 0);
}

static void test_inactive_periods(void) {
    dm_wsi_report r;
    dm_wsi_report_init(&r, 1, 0);
    CHECK(dm_wsi_report_due(&r, 0) == 1);
    // 暂停/停顿期间没有任何结果：10 秒内不产生任何报告（没有空转输出）。
    // No results while paused/idle: ten seconds produce no report (no idle spam).
    for (uint64_t t = 1000000000ull; t <= 10000000000ull; t += 1000000000ull)
        CHECK(dm_wsi_report_due(&r, t) == 0);
    // 停顿后的第一个真实进展立即产生一行，随后仍限制在每秒一行。
    // The first real progress after the pause reports immediately, then stays at most one line per second.
    dm_wsi_report_result(&r, DM_WSI_OK);
    CHECK(dm_wsi_report_due(&r, 10500000000ull) == 1);
    CHECK(dm_wsi_report_due(&r, 10600000000ull) == 0);
    dm_wsi_report_result(&r, DM_WSI_OK);
    CHECK(dm_wsi_report_due(&r, 11400000000ull) == 0);
    CHECK(dm_wsi_report_due(&r, 11600000000ull) == 1);
}

static void test_capacity_truncation(void) {
    dm_wsi_report r;
    dm_wsi_report_init(&r, 1, 0);
    dm_wsi_report_result(&r, DM_WSI_OK);
    char small[48];
    size_t used = dm_wsi_report_progress_line(&r, 1000000000ull, small, sizeof small);
    CHECK(used < sizeof small);
    CHECK(small[sizeof small - 1] == '\0');
}

static void test_attempts_and_failures(void) {
    dm_wsi_report r;
    dm_wsi_report_init(&r, 1, 0);
    dm_wsi_report_attempt(&r, 1440, 3200);
    dm_wsi_report_negotiated(&r, 1440, 3200, 37, 2, 4, 1);
    CHECK(r.open_attempts == 1);
    CHECK(r.open_failures == 0);
    CHECK(r.config.stale == 0);
    CHECK(r.config.extent_width == 1440);

    // 一次失败：保留阶段与原始码，清除先前的成功协商，并欠一个快照。
    // A failure keeps the stage/code, clears the earlier success and owes one snapshot.
    dm_wsi_report_attempt(&r, 1280, 720);
    CHECK(dm_wsi_report_failure(&r, DM_WSI_STAGE_SWAPCHAIN, -9, 0, 1) == 1);
    CHECK(r.open_failures == 1);
    CHECK(r.errors == 1);
    CHECK(r.unsupported == 0);
    CHECK(r.queued_presents == 0);              // 打开失败不是帧 | a failed open is never a frame
    CHECK(r.last_stage == DM_WSI_STAGE_SWAPCHAIN);
    CHECK(r.has_code == 1 && r.last_code == -9);
    CHECK(r.config.stale == 1);
    CHECK(r.config.extent_width == 0 && r.config.format == -1 && r.config.maintenance1 == -1);
    CHECK(r.config.requested_width == 1280 && r.config.requested_height == 720);
    // 同一尝试重复到达的失败不重复计数。| A repeated failure for the same attempt is not counted twice.
    CHECK(dm_wsi_report_failure(&r, DM_WSI_STAGE_OPEN, DM_WSI_CODE_UNKNOWN, 0, 0) == 0);
    CHECK(r.open_failures == 1 && r.errors == 1);
    CHECK(r.last_stage == DM_WSI_STAGE_SWAPCHAIN);   // 保持第一次真实阶段 | the first real stage is kept

    // 能力缺失：没有 VkResult 可报，保持 unknown 而不是编造。| Missing capability: unknown, never invented.
    dm_wsi_report_attempt(&r, 800, 600);
    CHECK(dm_wsi_report_failure(&r, DM_WSI_STAGE_INIT, DM_WSI_CODE_UNKNOWN, 1, 1) == 1);
    CHECK(r.unsupported == 1);
    CHECK(r.has_code == 0 && r.last_code == 0);
    CHECK(r.retries == 1);                       // 第二次尝试明确记为 retry | the second attempt is a named retry
    char line[512];
    dm_wsi_report_failure_line(&r, line, sizeof line);
    CHECK(strstr(line, "stage=init") != NULL);
    CHECK(strstr(line, "code=unknown") != NULL);
    CHECK(strstr(line, "category=unsupported") != NULL);
    CHECK(strstr(line, "retries=1") != NULL);

    // 成功重新协商清除 stale。| A successful renegotiation clears stale.
    dm_wsi_report_attempt(&r, 800, 600);
    dm_wsi_report_negotiated(&r, 800, 600, 37, 2, 3, 1);
    CHECK(r.config.stale == 0);
    CHECK(r.open_failures == 2 && r.open_attempts == 4);
    CHECK(r.retries == 2);
}

static void test_exactly_once_snapshots(void) {
    dm_wsi_report r;
    dm_wsi_report_init(&r, 1, 0);
    // 失败只欠一次快照，重复输出不再产生。| A failure owes one snapshot; repeats emit nothing.
    dm_wsi_report_attempt(&r, 640, 480);
    dm_wsi_report_failure(&r, DM_WSI_STAGE_INIT, DM_WSI_CODE_UNKNOWN, 1, 1);
    CHECK(dm_wsi_report_take_snapshot(&r, 1000) == 1);
    CHECK(dm_wsi_report_take_snapshot(&r, 2000) == 0);
    CHECK(r.snapshots == 1);
    // 窗口丢失/停止同样是终态，各一次。| Window loss and stop are terminal, each exactly once.
    dm_wsi_report_mark_terminal(&r);
    CHECK(dm_wsi_report_take_snapshot(&r, 3000) == 1);
    CHECK(dm_wsi_report_event(&r, DM_WSI_EVENT_STOP) == 1);
    CHECK(dm_wsi_report_take_snapshot(&r, 4000) == 1);
    CHECK(r.snapshots == 3);
}

static void test_transitions_idempotent(void) {
    dm_wsi_report r;
    dm_wsi_report_init(&r, 1, 0);
    // 没有暂停就没有恢复。| No resume without a real pause.
    CHECK(dm_wsi_report_event(&r, DM_WSI_EVENT_RESUME) == 0);
    CHECK(r.resumed == 0);
    CHECK(dm_wsi_report_event(&r, DM_WSI_EVENT_PAUSE) == 1);
    CHECK(dm_wsi_report_event(&r, DM_WSI_EVENT_PAUSE) == 0);   // 重复回调 | duplicate callback
    CHECK(r.paused == 1);
    CHECK(dm_wsi_report_event(&r, DM_WSI_EVENT_RESUME) == 1);
    CHECK(dm_wsi_report_event(&r, DM_WSI_EVENT_RESUME) == 0);
    CHECK(r.resumed == 1);
    // destroy 与 lifecycle→host_stop 是同一个停机：只计一次。
    // destroy and lifecycle->host_stop are one shutdown: counted once.
    CHECK(dm_wsi_report_event(&r, DM_WSI_EVENT_STOP) == 1);
    CHECK(dm_wsi_report_event(&r, DM_WSI_EVENT_STOP) == 0);
    CHECK(r.stops == 1);
    // 停止后重新暂停/恢复仍是新的真实转换。| A later pause/resume is a new real transition.
    CHECK(dm_wsi_report_event(&r, DM_WSI_EVENT_PAUSE) == 1);
    CHECK(r.paused == 2);
}

static void test_failure_order_preserved(void) {
    dm_wsi_report r;
    dm_wsi_report_init(&r, 1, 0);
    dm_wsi_report_attempt(&r, 1280, 720);
    dm_wsi_report_negotiated(&r, 1280, 720, 37, 2, 3, 0);
    uint64_t attempts = r.open_attempts;

    // 生产顺序：present 包装器 note(PRESENT, VK_ERROR_DEVICE_LOST) → 帧翻译 ERROR → 回调层通用兜底。
    // Production order: present wrapper note(PRESENT, DEVICE_LOST) -> translated frame ERROR -> generic callback fallback.
    dm_wsi_report_note_stage(&r, DM_WSI_STAGE_PRESENT, -4);
    dm_wsi_report_result(&r, DM_WSI_ERROR);
    CHECK(dm_wsi_report_failure(&r, DM_WSI_STAGE_UNKNOWN, DM_WSI_CODE_UNKNOWN, 0, 1) == 0);
    CHECK(r.errors == 1);                 // 一次失败，不是两次 | one failure, not two
    CHECK(r.open_failures == 0);          // 帧错误不是打开失败 | a frame error is not an open failure
    CHECK(r.open_attempts == attempts);   // 也没有伪造尝试 | and no fabricated attempt
    CHECK(r.last_stage == DM_WSI_STAGE_PRESENT);
    CHECK(r.has_code == 1 && r.last_code == -4);
    CHECK(dm_wsi_report_take_snapshot(&r, 1000) == 1);
    CHECK(dm_wsi_report_take_snapshot(&r, 2000) == 0);   // 终态只快照一次 | one snapshot only

    // 之后一次成功的帧开启新的一段；不同的排空失败必须照常计数，不被先前帧错误吞掉。
    // A later successful frame starts a new episode; a distinct drain failure must still count.
    dm_wsi_report_result(&r, DM_WSI_OK);
    CHECK(dm_wsi_report_failure(&r, DM_WSI_STAGE_DRAIN, -13, 0, 0) == 1);
    CHECK(r.errors == 2);
    CHECK(r.open_failures == 0);
    CHECK(r.last_stage == DM_WSI_STAGE_DRAIN);
    CHECK(r.has_code == 1 && r.last_code == -13);
}

static void test_attempts_only_at_real_open(void) {
    dm_wsi_report r;
    dm_wsi_report_init(&r, 1, 0);
    // 三次真实打开 = 三次尝试；redraw 不经过这里，因此不会增加。
    // Three real opens are three attempts; a redraw never reaches this, so it adds nothing.
    dm_wsi_report_attempt(&r, 800, 600);
    dm_wsi_report_negotiated(&r, 800, 600, 37, 2, 4, 1);
    CHECK(r.open_attempts == 1);
    dm_wsi_report_attempt(&r, 801, 601);          // 尺寸变化的真实重建打开 | real rebuild open with a new extent
    dm_wsi_report_negotiated(&r, 801, 601, 37, 2, 3, 1);
    CHECK(r.open_attempts == 2);
    CHECK(r.config.requested_width == 801);
    CHECK(r.open_failures == 0 && r.retries == 0);

    // 定时器/排空失败不伪造打开尝试。| A timer or drain failure never fabricates an open attempt.
    dm_wsi_report_note_stage(&r, DM_WSI_STAGE_TIMER, -11);
    dm_wsi_report_result(&r, DM_WSI_ERROR);
    CHECK(dm_wsi_report_failure(&r, DM_WSI_STAGE_TIMER, -11, 0, 0) == 0);   // 同一段失败 | same episode
    CHECK(r.open_attempts == 2 && r.open_failures == 0);
    dm_wsi_report_result(&r, DM_WSI_OK);
    CHECK(dm_wsi_report_failure(&r, DM_WSI_STAGE_TIMER, -11, 0, 0) == 1);
    CHECK(r.open_attempts == 2 && r.open_failures == 0);
    CHECK(r.last_stage == DM_WSI_STAGE_TIMER && r.last_code == -11);

    // 真实打开失败才计入打开失败与 retry。| Only a real open failure counts as an open failure and retry.
    dm_wsi_report_result(&r, DM_WSI_OK);
    dm_wsi_report_attempt(&r, 1024, 768);
    CHECK(dm_wsi_report_failure(&r, DM_WSI_STAGE_SWAPCHAIN, -9, 0, 1) == 1);
    CHECK(r.open_failures == 1);
    CHECK(r.open_attempts == 3);
    dm_wsi_report_attempt(&r, 1024, 768);
    CHECK(r.retries == 1);
    CHECK(r.open_attempts == 4);
}

static void test_distinct_failures_without_recovery(void) {
    dm_wsi_report r;
    dm_wsi_report_init(&r, 1, 0);
    dm_wsi_report_note_stage(&r, DM_WSI_STAGE_PRESENT, -4);
    dm_wsi_report_result(&r, DM_WSI_ERROR);
    CHECK(dm_wsi_report_failure(&r, DM_WSI_STAGE_UNKNOWN, DM_WSI_CODE_UNKNOWN, 0, 0) == 0);
    CHECK(dm_wsi_report_failure(&r, DM_WSI_STAGE_DRAIN, -13, 0, 0) == 1);
    CHECK(r.errors == 2 && r.last_stage == DM_WSI_STAGE_DRAIN && r.last_code == -13);
    CHECK(dm_wsi_report_failure(&r, DM_WSI_STAGE_DRAIN, DM_WSI_CODE_UNKNOWN, 0, 0) == 0);
    CHECK(r.has_code && r.last_code == -13);
    CHECK(dm_wsi_report_failure(&r, DM_WSI_STAGE_TIMER, 5, 0, 0) == 1);
    CHECK(r.errors == 3 && r.last_stage == DM_WSI_STAGE_TIMER && r.last_code == 5);
    CHECK(dm_wsi_report_failure(&r, DM_WSI_STAGE_TIMER, 5, 0, 0) == 0);
    CHECK(r.open_attempts == 0 && r.open_failures == 0);
}

static void test_recovered_open_is_not_retry(void) {
    dm_wsi_report r;
    dm_wsi_report_init(&r, 1, 0);
    dm_wsi_report_attempt(&r, 640, 480);
    dm_wsi_report_failure(&r, DM_WSI_STAGE_INIT, -3, 0, 1);
    dm_wsi_report_attempt(&r, 640, 480);
    dm_wsi_report_negotiated(&r, 640, 480, 37, 2, 3, 1);
    CHECK(r.retries == 1);
    dm_wsi_report_attempt(&r, 1280, 720);
    CHECK(r.retries == 1 && r.open_attempts == 3);
}

int main(void) {
    test_distinct_failures_without_recovery();
    test_recovered_open_is_not_retry();
    test_config_lines();
    test_counters_never_count_failure();
    test_bounded_rate();
    test_inactive_periods();
    test_capacity_truncation();
    test_attempts_and_failures();
    test_exactly_once_snapshots();
    test_transitions_idempotent();
    test_failure_order_preserved();
    test_attempts_only_at_real_open();
    printf("wsi_report: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
