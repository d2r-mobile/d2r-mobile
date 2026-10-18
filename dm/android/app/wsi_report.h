// #147 显式 WSI 诊断的有界证据报告：尝试/配置/计数/生命周期与至多 1 Hz 的进度行。
// 依据 #143 的 WSI 控制器、翻译器与桥接语义；纯 C（无 Vulkan/Android 头），宿主可用注入时间戳确定性测试。
// #147 bounded evidence report for the explicit WSI diagnostic: attempts, configuration, counters, lifecycle and at
// most one progress line per second. Based on the #143 WSI controller/translator/bridge semantics; plain C (no
// Vulkan/Android headers) so host tests can drive it with injected timestamps.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "wsi_diagnostic.h"

// “原始码未知”的显式哨兵：Vulkan 错误码本身是负值，不能用 -1 表示未知。
// Explicit sentinel for "raw code unknown": Vulkan error codes are themselves negative, so -1 cannot mean unknown.
#define DM_WSI_CODE_UNKNOWN INT32_MIN

typedef enum {
    DM_WSI_EVENT_PAUSE = 0,
    DM_WSI_EVENT_RESUME,
    DM_WSI_EVENT_STOP
} dm_wsi_event;

// 真实失败阶段。不可观察就用 UNKNOWN，绝不推断。| Real failure stage; UNKNOWN when it is not observable.
typedef enum {
    DM_WSI_STAGE_UNKNOWN = 0,
    DM_WSI_STAGE_LIFECYCLE,   // 生命周期借用/释放 | lifecycle borrow/release
    DM_WSI_STAGE_OPEN,        // 打开请求经桥进入控制器 | open request through the bridge
    DM_WSI_STAGE_INIT,        // present 设备/能力探测 | present device or capability init
    DM_WSI_STAGE_SWAPCHAIN,   // swapchain 创建 | swapchain creation
    DM_WSI_STAGE_ACQUIRE,
    DM_WSI_STAGE_SUBMIT,
    DM_WSI_STAGE_PRESENT,
    DM_WSI_STAGE_DRAIN,       // 排空/销毁 | drain or destroy
    DM_WSI_STAGE_TIMER        // 帧定时器读/启动 | frame timer read or arm
} dm_wsi_stage;

// 协商事实：请求尺寸与实际 extent 分开；未知保持 unknown（format/present=-1，尺寸 0，images 0，maintenance1 -1）。
// Negotiated facts: requested and actual extents are separate; unknown stays unknown (format/present -1, extent 0,
// images 0, maintenance1 -1).
typedef struct {
    int wsi_mode;              // 1 = wsi-clear-swapchain，0 = 默认 cpu-copy-indexed | renderer mode label
    uint32_t requested_width, requested_height;
    uint32_t extent_width, extent_height;
    int format;
    int present_mode;
    uint32_t image_count;
    int maintenance1;          // -1 未知 / 0 未启用 / 1 已启用 | unknown / disabled / enabled
    int stale;                 // 1 = 先前的成功协商已被失败清除，不再当作当前值 | earlier success cleared, not current
} dm_wsi_report_config;

typedef struct {
    dm_wsi_report_config config;
    uint64_t started_ns;
    uint64_t last_report_ns;
    uint64_t last_rate_ns;
    uint64_t last_rate_presents;
    uint64_t queued_presents;   // 成功排队的 present，绝不代表物理显示完成 | queued presents, never display completion
    uint64_t suboptimal;
    uint64_t rebuild_requests;  // OUT_OF_DATE 等重建请求 | rebuild requests such as OUT_OF_DATE
    uint64_t surface_lost;
    uint64_t errors;            // 失败路径，绝不累加 queued_presents | failure paths, never counted as presents
    uint64_t unsupported;
    uint64_t paused, resumed, stops;   // 真实状态转换，重复回调不重复计数 | real transitions only
    uint64_t open_attempts, open_failures, retries;
    uint64_t snapshots;         // 已输出的最终快照数 | final snapshots emitted
    int last_status;
    int last_stage;             // dm_wsi_stage
    int last_code;              // 原始 VkResult/系统码；has_code=0 时未知（DM_WSI_CODE_UNKNOWN），不推断
                                // raw code; unknown (DM_WSI_CODE_UNKNOWN) when has_code=0, never inferred
    int has_code;
    int open_retry_pending;     // only failed opens make the next open a retry; success clears it
    int attempt_open;           // 一次尝试正在进行 | an attempt is in flight
    int attempt_failed;         // 本次尝试已记过失败，避免重复计数 | failure already recorded for this attempt
    int paused_active;
    int stopped_active;
    int snapshot_pending;       // 终态/失败后欠一个快照 | one snapshot owed after a terminal transition
    int started;
    int dirty;
} dm_wsi_report;

void dm_wsi_report_init(dm_wsi_report *report, int wsi_mode, uint64_t now_ns);
// 一次打开尝试（记录真实请求尺寸；失败后的再次尝试计为 retry）。
// One open attempt: records the real requested size; a further attempt after a failure counts as a retry.
void dm_wsi_report_attempt(dm_wsi_report *report, uint32_t width, uint32_t height);
// 实际协商结果（来自 swapchain 与设备能力）；成功后清除 stale。
// Actual negotiation result from the swapchain and device capability; success clears stale.
void dm_wsi_report_negotiated(dm_wsi_report *report, uint32_t width, uint32_t height,
                              int format, int present_mode, uint32_t image_count, int maintenance1);
// 标记一个终态（窗口丢失/排空）欠一次最终快照。| Mark a terminal transition (window loss/drain) as owing one snapshot.
void dm_wsi_report_mark_terminal(dm_wsi_report *report);
// 真实失败：保留阶段与原始码（未知传 DM_WSI_CODE_UNKNOWN），同一次失败在多个回调层只记一次；只有真实打开
// （open_failure=1）才计入打开尝试失败，排空/定时器/帧失败不会伪造打开尝试。先前的成功协商会被清除。
// Real failure: keeps the stage and original code (DM_WSI_CODE_UNKNOWN when unknown); one failure observed through
// several callback layers is recorded once; only a real open (open_failure=1) counts as an open failure, while
// drain/timer/frame failures never fabricate an open attempt. An earlier successful negotiation is cleared.
int dm_wsi_report_failure(dm_wsi_report *report, dm_wsi_stage stage, int code, int unsupported, int open_failure);
// 帧内失败阶段/码（不计数）：由 acquire/submit/present 包装器在真实调用点登记，避免从控制器枚举推断。
// Frame-internal failure stage/code without counting, noted by the acquire/submit/present wrappers at the real call
// site so no Vulkan code is inferred from a controller enum.
void dm_wsi_report_note_stage(dm_wsi_report *report, dm_wsi_stage stage, int code);
// 每次真实帧结果；ERROR/UNSUPPORTED 只加错误计数并欠一个终态快照，绝不成为有效帧。
// Every real frame result; ERROR/UNSUPPORTED only raise their counters and owe one terminal snapshot, never a frame.
void dm_wsi_report_result(dm_wsi_report *report, dm_wsi_status status);
// 生命周期转换：仅真实变化计数，返回 1 表示本次确实计数。重复 pause/stop 回调不重复计数。
// Lifecycle transition: counted only on a real change, returning 1 when counted. Duplicate pause/stop callbacks do
// not double count.
int dm_wsi_report_event(dm_wsi_report *report, dm_wsi_event event);

// 至多每秒一次且只在实际有新进展时触发；首次调用返回 1（初始快照）。不创建定时器。
// At most once per second and only when real progress exists since the last report; the first call reports the
// initial snapshot. Creates no timer.
int dm_wsi_report_due(dm_wsi_report *report, uint64_t now_ns);
// 输出欠下的最终快照：每个终态恰好一次，返回 1 表示本次输出。
// Emit the owed final snapshot: exactly once per terminal transition, returning 1 when this call emitted it.
int dm_wsi_report_take_snapshot(dm_wsi_report *report, uint64_t now_ns);
// 无条件快照（测试/显式强制），不受 1 Hz 限制。| Unconditional snapshot for tests or explicit forcing.
void dm_wsi_report_force(dm_wsi_report *report, uint64_t now_ns);

// 格式化：未知值写 unknown；吞吐只标 queued-present；失败行保留阶段与原始码。
// Formatting: unknown values print as unknown; any rate is labelled queued-present; the failure line keeps the stage
// and the original code.
size_t dm_wsi_report_config_line(const dm_wsi_report *report, char *out, size_t capacity);
size_t dm_wsi_report_failure_line(const dm_wsi_report *report, char *out, size_t capacity);
size_t dm_wsi_report_progress_line(dm_wsi_report *report, uint64_t now_ns, char *out, size_t capacity);
