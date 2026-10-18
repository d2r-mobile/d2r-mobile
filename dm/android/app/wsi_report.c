// #147 WSI 证据报告实现：只统计实际结果，未知不编造，速率只按 queued-present 计。
// 依据 #143 的 dm_wsi_status 与 vk_swapchain 的协商字段；本文件不创建定时器，也不持有任何 GPU 对象。
// #147 evidence report implementation: counts only real results, never invents unknown values and rates only
// queued presents. Based on the #143 dm_wsi_status values and the vk_swapchain negotiation fields; this file
// creates no timer and owns no GPU object.
#include "wsi_report.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define DM_WSI_REPORT_INTERVAL_NS 1000000000ull

void dm_wsi_report_init(dm_wsi_report *report, int wsi_mode, uint64_t now_ns) {
    if (!report) return;
    memset(report, 0, sizeof *report);
    report->config.wsi_mode = wsi_mode ? 1 : 0;
    report->config.format = -1;
    report->config.present_mode = -1;
    report->config.maintenance1 = -1;
    report->last_stage = DM_WSI_STAGE_UNKNOWN;
    report->last_code = 0;
    report->started_ns = now_ns;
    report->last_rate_ns = now_ns;
    report->last_status = DM_WSI_IDLE;
}

// 失败后先前的成功协商不再是当前事实：清除 extent/format/present/images/maintenance，并置 stale。
// After a failure an earlier success is no longer the current fact: clear the negotiated fields and mark stale.
static void clear_negotiated(dm_wsi_report *report) {
    report->config.extent_width = 0;
    report->config.extent_height = 0;
    report->config.format = -1;
    report->config.present_mode = -1;
    report->config.image_count = 0;
    report->config.maintenance1 = -1;
    report->config.stale = 1;
}

void dm_wsi_report_attempt(dm_wsi_report *report, uint32_t width, uint32_t height) {
    if (!report) return;
    // 观察者保证每次真实打开只调用一次；因此这里总是计一次尝试，并更新请求尺寸。
    // The observer guarantees one call per real open, so every call counts one attempt and updates the request.
    report->open_attempts++;
    if (report->open_retry_pending) report->retries++;   // 失败后的再次尝试是明确的 retry | retry after a failure
    report->config.requested_width = width;
    report->config.requested_height = height;
    report->attempt_open = 1;
    report->attempt_failed = 0;
    report->dirty = 1;
}

void dm_wsi_report_mark_terminal(dm_wsi_report *report) {
    if (!report) return;
    report->snapshot_pending = 1;
    report->dirty = 1;
}

void dm_wsi_report_negotiated(dm_wsi_report *report, uint32_t width, uint32_t height,
                              int format, int present_mode, uint32_t image_count, int maintenance1) {
    if (!report) return;
    report->config.extent_width = width;
    report->config.extent_height = height;
    report->config.format = format;
    report->config.present_mode = present_mode;
    report->config.image_count = image_count;
    report->config.maintenance1 = maintenance1;
    report->config.stale = 0;
    report->open_retry_pending = 0;
    report->attempt_open = 0;
    report->attempt_failed = 0;
    report->dirty = 1;
}

int dm_wsi_report_failure(dm_wsi_report *report, dm_wsi_stage stage, int code, int unsupported, int open_failure) {
    if (!report) return 0;
    // Generic callback fallbacks and repeated observations do not count twice.
    // A different concrete stage/code (e.g. drain after a failed present) remains observable.
    if (report->attempt_failed) {
        if (stage == DM_WSI_STAGE_UNKNOWN ||
            (stage == DM_WSI_STAGE_OPEN && code == DM_WSI_CODE_UNKNOWN)) return 0;
        if ((int)stage == report->last_stage) {
            if (code == DM_WSI_CODE_UNKNOWN) return 0;
            if (!report->has_code) {
                report->last_code = code;
                report->has_code = 1;
                report->dirty = 1;
                return 0;
            }
            if (code == report->last_code) return 0;
        }
    }
    report->attempt_failed = 1;
    if (open_failure) {
        report->attempt_open = 0;
        report->open_failures++;
        report->open_retry_pending = 1;
        clear_negotiated(report);
    }
    if (unsupported) report->unsupported++;
    else report->errors++;
    // 首次观察到的阶段/原始码保留；通用兜底不能覆盖更精确的观察（此处已由 episode 保护）。
    // The first observed stage/code is kept; a generic fallback cannot overwrite a more precise observation
    // (the episode guard above already guarantees that).
    if (stage != DM_WSI_STAGE_UNKNOWN || !report->has_code) {
        report->last_stage = (int)stage;
        report->has_code = code != DM_WSI_CODE_UNKNOWN;
        report->last_code = report->has_code ? code : 0;
    }
    report->last_status = unsupported ? DM_WSI_UNSUPPORTED : DM_WSI_ERROR;
    report->snapshot_pending = 1;
    report->dirty = 1;
    return 1;
}

void dm_wsi_report_note_stage(dm_wsi_report *report, dm_wsi_stage stage, int code) {
    if (!report) return;
    report->last_stage = (int)stage;
    report->has_code = code != DM_WSI_CODE_UNKNOWN;
    report->last_code = report->has_code ? code : 0;
}

void dm_wsi_report_result(dm_wsi_report *report, dm_wsi_status status) {
    if (!report) return;
    report->last_status = (int)status;
    switch (status) {
    case DM_WSI_OK:
        report->queued_presents++;   // 只有真实排队的 present 才计数 | only a real queued present counts
        report->attempt_failed = 0;  // 成功呈现结束上一段失败 | a presented frame ends the previous failure episode
        report->has_code = 0;
        report->last_code = 0;
        report->last_stage = DM_WSI_STAGE_UNKNOWN;
        report->dirty = 1;
        break;
    case DM_WSI_SUBOPTIMAL:
        report->queued_presents++;
        report->suboptimal++;
        report->attempt_failed = 0;
        report->has_code = 0;
        report->last_code = 0;
        report->last_stage = DM_WSI_STAGE_UNKNOWN;
        report->dirty = 1;
        break;
    case DM_WSI_REBUILD:
        report->rebuild_requests++;
        report->attempt_failed = 0;  // 重建是新一次尝试的开始 | a rebuild starts the next attempt
        report->dirty = 1;
        break;
    case DM_WSI_SURFACE_LOST:
        report->surface_lost++;
        report->attempt_failed = 1;  // 已是一个终态观察 | already a terminal observation
        report->snapshot_pending = 1;
        report->dirty = 1;
        break;
    case DM_WSI_ERROR:
        report->errors++;            // 失败绝不成为有效帧 | a failure never becomes a valid frame
        report->attempt_failed = 1;  // 精确的帧内观察优先于后续通用兜底 | precise frame observation wins
        report->snapshot_pending = 1;
        report->dirty = 1;
        break;
    case DM_WSI_UNSUPPORTED:
        report->unsupported++;
        report->attempt_failed = 1;
        report->snapshot_pending = 1;
        report->dirty = 1;
        break;
    case DM_WSI_IDLE:
    default:
        break;
    }
}

int dm_wsi_report_event(dm_wsi_report *report, dm_wsi_event event) {
    if (!report) return 0;
    switch (event) {
    case DM_WSI_EVENT_PAUSE:
        // 重复 pause（多回调/重试）不重复计数。| A duplicate pause callback does not count twice.
        if (report->paused_active) return 0;
        report->paused_active = 1;
        report->stopped_active = 0;
        report->paused++;
        break;
    case DM_WSI_EVENT_RESUME:
        // 只有真实暂停之后才存在恢复转换。| A resume only exists after a real pause.
        if (!report->paused_active) return 0;
        report->paused_active = 0;
        report->resumed++;
        break;
    case DM_WSI_EVENT_STOP:
        // destroy 与 lifecycle→host_stop 是同一个停机：只计一次。| destroy and lifecycle->host_stop are one stop.
        if (report->stopped_active) return 0;
        report->stopped_active = 1;
        report->paused_active = 0;
        report->stops++;
        break;
    }
    report->snapshot_pending = 1;
    report->dirty = 1;
    return 1;
}

int dm_wsi_report_due(dm_wsi_report *report, uint64_t now_ns) {
    if (!report) return 0;
    if (!report->started) {
        report->started = 1;
        report->last_report_ns = now_ns;
        report->dirty = 0;
        return 1;
    }
    if (!report->dirty) return 0;
    if (now_ns < report->last_report_ns) return 0;   // 时钟回退不触发 | no report on clock regression
    if (now_ns - report->last_report_ns < DM_WSI_REPORT_INTERVAL_NS) return 0;
    report->last_report_ns = now_ns;
    report->dirty = 0;
    return 1;
}

int dm_wsi_report_take_snapshot(dm_wsi_report *report, uint64_t now_ns) {
    if (!report || !report->snapshot_pending) return 0;
    report->snapshot_pending = 0;
    report->started = 1;
    report->last_report_ns = now_ns;
    report->dirty = 0;
    report->snapshots++;
    return 1;
}

void dm_wsi_report_force(dm_wsi_report *report, uint64_t now_ns) {
    if (!report) return;
    report->snapshot_pending = 0;
    report->started = 1;
    report->last_report_ns = now_ns;
    report->dirty = 0;
    report->snapshots++;
}

static const char *mode_name(const dm_wsi_report *report) {
    return report->config.wsi_mode ? "wsi-clear-swapchain" : "cpu-copy-indexed";
}

static const char *stage_name(int stage) {
    switch ((dm_wsi_stage)stage) {
    case DM_WSI_STAGE_LIFECYCLE: return "lifecycle";
    case DM_WSI_STAGE_OPEN: return "open";
    case DM_WSI_STAGE_INIT: return "init";
    case DM_WSI_STAGE_SWAPCHAIN: return "swapchain";
    case DM_WSI_STAGE_ACQUIRE: return "acquire";
    case DM_WSI_STAGE_SUBMIT: return "submit";
    case DM_WSI_STAGE_PRESENT: return "present";
    case DM_WSI_STAGE_DRAIN: return "drain";
    case DM_WSI_STAGE_TIMER: return "timer";
    case DM_WSI_STAGE_UNKNOWN:
    default: return "unknown";
    }
}

static size_t append(char *out, size_t capacity, size_t used, const char *format, ...) __attribute__((format(printf, 4, 5)));
static size_t append(char *out, size_t capacity, size_t used, const char *format, ...) {
    if (used >= capacity) return used;
    va_list args;
    va_start(args, format);
    int written = vsnprintf(out + used, capacity - used, format, args);
    va_end(args);
    if (written < 0) return used;
    size_t next = used + (size_t)written;
    return next < capacity ? next : capacity - 1;
}

size_t dm_wsi_report_config_line(const dm_wsi_report *report, char *out, size_t capacity) {
    if (!out || capacity == 0) return 0;
    out[0] = '\0';
    if (!report) return 0;
    size_t used = 0;
    used = append(out, capacity, used, "wsi config: mode=%s requested=", mode_name(report));
    if (report->config.requested_width && report->config.requested_height)
        used = append(out, capacity, used, "%ux%u", report->config.requested_width, report->config.requested_height);
    else used = append(out, capacity, used, "unknown");
    used = append(out, capacity, used, " extent=");
    if (report->config.extent_width && report->config.extent_height)
        used = append(out, capacity, used, "%ux%u", report->config.extent_width, report->config.extent_height);
    else used = append(out, capacity, used, "unknown");
    if (report->config.format >= 0) used = append(out, capacity, used, " format=%d", report->config.format);
    else used = append(out, capacity, used, " format=unknown");
    if (report->config.present_mode >= 0) used = append(out, capacity, used, " present_mode=%d", report->config.present_mode);
    else used = append(out, capacity, used, " present_mode=unknown");
    if (report->config.image_count) used = append(out, capacity, used, " images=%u", report->config.image_count);
    else used = append(out, capacity, used, " images=unknown");
    if (report->config.maintenance1 == 1) used = append(out, capacity, used, " maintenance1=enabled");
    else if (report->config.maintenance1 == 0) used = append(out, capacity, used, " maintenance1=disabled");
    else used = append(out, capacity, used, " maintenance1=unknown");
    if (report->config.stale) used = append(out, capacity, used, " stale=yes(earlier-success-cleared)");
    return used;
}

size_t dm_wsi_report_failure_line(const dm_wsi_report *report, char *out, size_t capacity) {
    if (!out || capacity == 0) return 0;
    out[0] = '\0';
    if (!report) return 0;
    size_t used = 0;
    used = append(out, capacity, used, "wsi failure: attempts=%llu failures=%llu retries=%llu stage=%s",
                  (unsigned long long)report->open_attempts, (unsigned long long)report->open_failures,
                  (unsigned long long)report->retries, stage_name(report->last_stage));
    if (report->has_code) used = append(out, capacity, used, " code=%d", report->last_code);
    else used = append(out, capacity, used, " code=unknown");
    used = append(out, capacity, used, " category=%s",
                  report->last_status == DM_WSI_UNSUPPORTED ? "unsupported" : "error");
    return used;
}

size_t dm_wsi_report_progress_line(dm_wsi_report *report, uint64_t now_ns, char *out, size_t capacity) {
    if (!out || capacity == 0) return 0;
    out[0] = '\0';
    if (!report) return 0;
    const uint64_t elapsed_ms = now_ns >= report->started_ns ? (now_ns - report->started_ns) / 1000000ull : 0;
    const uint64_t presents_delta = report->queued_presents - report->last_rate_presents;
    const uint64_t ns_delta = now_ns >= report->last_rate_ns ? now_ns - report->last_rate_ns : 0;
    double rate = ns_delta ? (double)presents_delta * 1e9 / (double)ns_delta : 0.0;
    report->last_rate_ns = now_ns;
    report->last_rate_presents = report->queued_presents;
    size_t used = 0;
    used = append(out, capacity, used, "wsi progress: elapsed_ms=%llu", (unsigned long long)elapsed_ms);
    used = append(out, capacity, used, " queued_presents=%llu", (unsigned long long)report->queued_presents);
    used = append(out, capacity, used, " rate=%.1f/s queued-present throughput (NOT display fps/latency/vsync)", rate);
    used = append(out, capacity, used, " suboptimal=%llu", (unsigned long long)report->suboptimal);
    used = append(out, capacity, used, " rebuild=%llu", (unsigned long long)report->rebuild_requests);
    used = append(out, capacity, used, " surface_lost=%llu", (unsigned long long)report->surface_lost);
    used = append(out, capacity, used, " errors=%llu", (unsigned long long)report->errors);
    used = append(out, capacity, used, " unsupported=%llu", (unsigned long long)report->unsupported);
    used = append(out, capacity, used, " open_attempts=%llu open_failures=%llu retries=%llu",
                  (unsigned long long)report->open_attempts, (unsigned long long)report->open_failures,
                  (unsigned long long)report->retries);
    used = append(out, capacity, used, " paused=%llu resumed=%llu stops=%llu",
                  (unsigned long long)report->paused, (unsigned long long)report->resumed,
                  (unsigned long long)report->stops);
    used = append(out, capacity, used, " last_status=%d last_stage=%s code=", report->last_status,
                  stage_name(report->last_stage));
    if (report->has_code) used = append(out, capacity, used, "%d", report->last_code);
    else used = append(out, capacity, used, "unknown");
    return used;
}
