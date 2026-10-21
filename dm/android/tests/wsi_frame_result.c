// #143 帧结果翻译的宿主测试：真实 dm_wsi_present_frame + 真实 VkResult 常量，断言 SUBOPTIMAL 仍会 submit/present。
// 依据 VK_KHR_swapchain 语义；旧实现把 SUBOPTIMAL 当 ERROR 且 acquire 后不再提交，这些断言即其反例。
// #143 host tests for the frame result translation: the real dm_wsi_present_frame with real VkResult constants,
// asserting SUBOPTIMAL still submits and presents. Based on VK_KHR_swapchain semantics; the old implementation
// treating SUBOPTIMAL as an error and skipping submit is exactly what these assertions reject.
#include "../app/wsi_frame_result.h"
#include <stdio.h>

static int checks, failures;
#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

typedef struct {
    VkResult acquire_result, submit_result, present_result;
    int acquire_suboptimal, present_suboptimal;
    int submits, presents;
} stub;

static VkResult do_acquire(void *ctx, int *suboptimal) {
    stub *s = ctx;
    *suboptimal = s->acquire_suboptimal;
    return s->acquire_result;
}
static VkResult do_submit(void *ctx) { stub *s = ctx; s->submits++; return s->submit_result; }
static VkResult do_present(void *ctx, int *suboptimal) {
    stub *s = ctx;
    s->presents++;
    *suboptimal = s->present_suboptimal;
    return s->present_result;
}

static dm_wsi_status run(stub *s) {
    dm_wsi_frame_ops ops = {.ctx = s, .acquire = do_acquire, .submit = do_submit, .present = do_present};
    return dm_wsi_present_frame(&ops);
}

int main(void) {
    // acquire suboptimal + present DEVICE_LOST：真实错误优先，不得被 suboptimal 标记掩盖成有效帧。
    // Acquire suboptimal with a present device loss: the real error wins; the flag must not mask it as a valid frame.
    stub sub_then_lost = {.acquire_result = VK_SUCCESS, .acquire_suboptimal = 1,
                          .submit_result = VK_SUCCESS, .present_result = VK_ERROR_DEVICE_LOST};
    CHECK(run(&sub_then_lost) == DM_WSI_ERROR);
    CHECK(sub_then_lost.submits == 1 && sub_then_lost.presents == 1);

    // acquire suboptimal + present OUT_OF_DATE：仍是重建，不是有效帧。| Still a rebuild, not a valid frame.
    stub sub_then_ood = {.acquire_result = VK_SUCCESS, .acquire_suboptimal = 1,
                         .submit_result = VK_SUCCESS, .present_result = VK_ERROR_OUT_OF_DATE_KHR};
    CHECK(run(&sub_then_ood) == DM_WSI_REBUILD);

    // 正常帧。| Normal frame.
    stub ok = {.acquire_result = VK_SUCCESS, .submit_result = VK_SUCCESS, .present_result = VK_SUCCESS};
    CHECK(run(&ok) == DM_WSI_OK);
    CHECK(ok.submits == 1 && ok.presents == 1);

    // acquire 报告 SUBOPTIMAL：仍必须提交并呈现，结果不是错误。
    // Suboptimal acquire: submit and present must still happen and the result is not an error.
    stub acquire_sub = {.acquire_result = VK_SUCCESS, .acquire_suboptimal = 1,
                        .submit_result = VK_SUCCESS, .present_result = VK_SUCCESS};
    CHECK(run(&acquire_sub) == DM_WSI_SUBOPTIMAL);
    CHECK(acquire_sub.submits == 1 && acquire_sub.presents == 1);

    // acquire 直接返回 VK_SUBOPTIMAL_KHR（规范允许）：同样继续。| acquire may return VK_SUBOPTIMAL_KHR directly.
    stub acquire_code = {.acquire_result = VK_SUBOPTIMAL_KHR, .submit_result = VK_SUCCESS, .present_result = VK_SUCCESS};
    CHECK(run(&acquire_code) == DM_WSI_SUBOPTIMAL);
    CHECK(acquire_code.submits == 1 && acquire_code.presents == 1);

    // present 返回 SUBOPTIMAL：帧已呈现，需重建。| present returns SUBOPTIMAL: presented, recreation required.
    stub present_sub = {.acquire_result = VK_SUCCESS, .submit_result = VK_SUCCESS, .present_result = VK_SUBOPTIMAL_KHR};
    CHECK(run(&present_sub) == DM_WSI_SUBOPTIMAL);
    CHECK(present_sub.submits == 1 && present_sub.presents == 1);

    // present 成功但带 suboptimal 标记。| present succeeds with the suboptimal flag set.
    stub present_flag = {.acquire_result = VK_SUCCESS, .submit_result = VK_SUCCESS,
                         .present_result = VK_SUCCESS, .present_suboptimal = 1};
    CHECK(run(&present_flag) == DM_WSI_SUBOPTIMAL);

    // OUT_OF_DATE 在 acquire 阶段：不提交、不呈现。| OUT_OF_DATE at acquire: no submit, no present.
    stub acquire_ood = {.acquire_result = VK_ERROR_OUT_OF_DATE_KHR};
    CHECK(run(&acquire_ood) == DM_WSI_REBUILD);
    CHECK(acquire_ood.submits == 0 && acquire_ood.presents == 0);

    // acquire 返回 DEVICE_LOST：错误不被 SUBOPTIMAL/成功标记掩盖，且不提交/呈现。
    // Acquire device loss: no submit/present and the error is not masked.
    stub acquire_device_lost = {.acquire_result = VK_ERROR_DEVICE_LOST};
    CHECK(run(&acquire_device_lost) == DM_WSI_ERROR);
    CHECK(acquire_device_lost.submits == 0 && acquire_device_lost.presents == 0);

    // OUT_OF_DATE 在 present 阶段：帧已提交，呈现被拒。| OUT_OF_DATE at present: submitted, present rejected.
    stub present_ood = {.acquire_result = VK_SUCCESS, .submit_result = VK_SUCCESS,
                        .present_result = VK_ERROR_OUT_OF_DATE_KHR};
    CHECK(run(&present_ood) == DM_WSI_REBUILD);
    CHECK(present_ood.submits == 1 && present_ood.presents == 1);

    // SURFACE_LOST 两个阶段。| SURFACE_LOST at both stages.
    stub acquire_lost = {.acquire_result = VK_ERROR_SURFACE_LOST_KHR};
    CHECK(run(&acquire_lost) == DM_WSI_SURFACE_LOST);
    stub present_lost = {.acquire_result = VK_SUCCESS, .submit_result = VK_SUCCESS,
                         .present_result = VK_ERROR_SURFACE_LOST_KHR};
    CHECK(run(&present_lost) == DM_WSI_SURFACE_LOST);

    // submit 失败：绝不调用 present。| submit failure: present is never called.
    stub submit_fail = {.acquire_result = VK_SUCCESS, .submit_result = VK_ERROR_DEVICE_LOST};
    CHECK(run(&submit_fail) == DM_WSI_ERROR);
    CHECK(submit_fail.submits == 1 && submit_fail.presents == 0);

    // 其他 present 错误。| Any other present error.
    stub present_fail = {.acquire_result = VK_SUCCESS, .submit_result = VK_SUCCESS,
                         .present_result = VK_ERROR_DEVICE_LOST};
    CHECK(run(&present_fail) == DM_WSI_ERROR);

    printf("wsi_frame_result: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
