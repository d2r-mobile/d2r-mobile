#!/usr/bin/env python3
"""#205 narrow actual-source WSI HUD caller fixture.

Extracts the actual wsi_record_hud/wsi_acquire/wsi_close text from native.c and
runs it against narrow Vulkan/presenter boundaries. The lower-level presenter
and real geometry are covered by the #199 tests; this fixture proves the App
call order, no submit after record failure, and close ownership ordering.
"""
from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from native_callbacks_host import extract_function

FUNCTIONS = ["wsi_open", "wsi_record_hud", "wsi_acquire", "wsi_submit", "wsi_present", "wsi_frame", "wsi_close"]

PRELUDE = r'''
#include "touch_hud.h"
#include "touch_hud_swapchain.h"
#include "wsi_report.h"
#include "wsi_frame_result.h"
#include "view_policy.h"
#include "vk_host.h"
#include "vk_swapchain.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    dm_vk wsi_vk;
    dm_vk_swapchain wsi_chain;
    dm_touch_hud_swapchain wsi_hud;
    dm_wsi_report report;
    dm_android_touch_observation hud_obs;
    dm_view_policy view;
    float density;
    char hud_vertex[4096], hud_fragment[4096];
    void *activity;
    int hud_obs_valid, wsi_chain_live, wsi_hud_live, wsi_hud_failed;
} app_state;

static int acquire_calls, record_calls, submit_calls, present_calls, presenter_record_result = 1;
static int copy_fail, presenter_create_fail, presenter_create_calls, swapchain_create_calls;
static int record_fail, chain_destroy_fail, chain_destroys, presenter_destroys, device_destroys;
static int destroy_order;

static void touch_refresh_hud(app_state *s) { (void)s; }
static int copy_asset(void *activity, const char *asset, char *out) {
    (void)activity; (void)asset; (void)out; return copy_fail;
}
VkResult dm_vk_create_present(const dm_vk_present_options *options, dm_vk *out) {
    (void)options; memset(out, 0, sizeof *out); out->swapchain_maintenance1 = 1;
    out->device = (VkDevice)(uintptr_t)2; return VK_SUCCESS;
}
VkResult dm_vk_swapchain_create(dm_vk *vk, const dm_vk_swapchain_options *options, dm_vk_swapchain *out) {
    (void)vk; swapchain_create_calls++; memset(out, 0, sizeof *out);
    out->extent.width = options->width; out->extent.height = options->height; out->image_count = 3;
    return VK_SUCCESS;
}
VkResult dm_touch_hud_swapchain_create(dm_vk *vk, const dm_vk_swapchain *chain,
    const char *vertex, const char *fragment, dm_touch_hud_swapchain *out) {
    (void)vk; (void)chain; (void)vertex; (void)fragment; memset(out, 0, sizeof *out);
    presenter_create_calls++; return presenter_create_fail ? VK_ERROR_INITIALIZATION_FAILED : VK_SUCCESS;
}
void dm_wsi_report_negotiated(dm_wsi_report *r, uint32_t w, uint32_t h, int f, int pm, uint32_t n, int m) {
    (void)r; (void)w; (void)h; (void)f; (void)pm; (void)n; (void)m;
}
size_t dm_wsi_report_config_line(const dm_wsi_report *r, char *out, size_t cap) {
    (void)r; if (out && cap) out[0] = 0; return 0;
}
int dm_touch_hud_snapshot_from_observation(const dm_android_touch_observation *o,
    uint32_t w, uint32_t h, dm_touch_hud_snapshot *out) {
    (void)o; if (!out || !w || !h) return 0; memset(out, 0, sizeof *out);
    out->pixel_width = w; out->pixel_height = h; out->width_pt = (float)w;
    out->height_pt = (float)h; out->density = 1.0f; return 1;
}
VkResult dm_touch_hud_swapchain_record(dm_vk *vk, const dm_vk_swapchain *chain,
    dm_touch_hud_swapchain *p, const dm_touch_hud_snapshot *s, int overlay) {
    (void)vk; (void)chain; (void)p; (void)s; (void)overlay;
    record_calls++; return presenter_record_result ? VK_SUCCESS : VK_ERROR_INITIALIZATION_FAILED;
}
VkResult dm_vk_swapchain_acquire(dm_vk *vk, dm_vk_swapchain *chain, int *suboptimal) {
    (void)vk; (void)chain; if (suboptimal) *suboptimal = 0; acquire_calls++; return VK_SUCCESS;
}
VkResult dm_vk_swapchain_submit(dm_vk *vk, dm_vk_swapchain *chain) {
    (void)vk; (void)chain; submit_calls++; return VK_SUCCESS;
}
VkResult dm_vk_swapchain_present(dm_vk *vk, dm_vk_swapchain *chain, int *suboptimal) {
    (void)vk; (void)chain; if (suboptimal) *suboptimal = 0; present_calls++; return VK_SUCCESS;
}
VkResult dm_vk_swapchain_destroy(dm_vk *vk, dm_vk_swapchain *chain) {
    (void)vk; (void)chain; chain_destroys++; destroy_order = destroy_order * 10 + 1;
    return chain_destroy_fail ? VK_ERROR_DEVICE_LOST : VK_SUCCESS;
}
void dm_touch_hud_swapchain_destroy(dm_vk *vk, dm_touch_hud_swapchain *p) {
    (void)vk; (void)p; presenter_destroys++; destroy_order = destroy_order * 10 + 2;
}
void dm_vk_destroy(dm_vk *vk) { (void)vk; device_destroys++; destroy_order = destroy_order * 10 + 3; }
void dm_wsi_report_note_stage(dm_wsi_report *r, dm_wsi_stage stage, int code) {
    (void)r; (void)stage; (void)code;
}
void dm_wsi_report_result(dm_wsi_report *r, dm_wsi_status status) { (void)r; (void)status; }
size_t dm_wsi_report_failure_line(const dm_wsi_report *r, char *out, size_t cap) {
    (void)r; if (out && cap) out[0] = 0; return 0;
}
static void wsi_report_snapshot(app_state *s, const char *reason) { (void)s; (void)reason; }
static void wsi_report_fail(app_state *s, int stage, int code, int unsupported, int open_failure) {
    (void)s; (void)stage; (void)code; (void)unsupported; (void)open_failure;
}
#define LOG(...) do { } while (0)
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); return 1; } } while (0)

static void reset_state(app_state *s) {
    memset(s, 0, sizeof *s);
    s->density = 1.0f;
    s->wsi_chain.extent.width = 640; s->wsi_chain.extent.height = 480;
    s->wsi_chain.image_count = 3;
    s->wsi_vk.device = (VkDevice)(uintptr_t)1;
    s->wsi_chain_live = 1; s->wsi_hud_live = 1;
    acquire_calls = record_calls = submit_calls = present_calls = chain_destroys = presenter_destroys = device_destroys = 0;
    copy_fail = presenter_create_fail = 0; presenter_create_calls = swapchain_create_calls = 0;
    chain_destroy_fail = 0; presenter_record_result = 1; record_fail = 0;
    destroy_order = 0;
}
'''

MAIN = r'''
int main(void) {
    app_state s;
    reset_state(&s);
    s.wsi_chain_live = s.wsi_hud_live = 0;
    CHECK(wsi_open(&s, NULL, 640, 480) == 0);
    CHECK(s.wsi_chain_live && s.wsi_hud_live && presenter_create_calls == 1);

    reset_state(&s);
    s.wsi_chain_live = s.wsi_hud_live = 0;
    copy_fail = 1;
    CHECK(wsi_open(&s, NULL, 640, 480) == DM_WSI_ERROR);
    CHECK(chain_destroys == 1 && device_destroys == 1 && presenter_create_calls == 0);
    CHECK(!s.wsi_chain_live && !s.wsi_hud_live);

    reset_state(&s);
    s.wsi_chain_live = s.wsi_hud_live = 0;
    presenter_create_fail = 1;
    CHECK(wsi_open(&s, NULL, 640, 480) == DM_WSI_ERROR);
    CHECK(chain_destroys == 1 && device_destroys == 1 && presenter_create_calls == 1);
    CHECK(!s.wsi_chain_live && !s.wsi_hud_live);

    reset_state(&s);
    CHECK(wsi_frame(&s) == DM_WSI_OK);
    CHECK(acquire_calls == 1 && record_calls == 1 && submit_calls == 1 && present_calls == 1);
    CHECK(!s.wsi_hud_failed);

    reset_state(&s);
    CHECK(wsi_acquire(&s, NULL) == VK_SUCCESS);
    CHECK(acquire_calls == 1 && record_calls == 1);
    CHECK(!s.wsi_hud_failed);

    reset_state(&s);
    presenter_record_result = 0;
    CHECK(wsi_frame(&s) == DM_WSI_ERROR);
    CHECK(s.wsi_hud_failed && record_calls == 1);
    CHECK(acquire_calls == 1 && submit_calls == 0 && present_calls == 0);  // no submit/present after record failure
    CHECK(wsi_acquire(&s, NULL) != VK_SUCCESS);
    CHECK(acquire_calls == 1 && record_calls == 1);  // failed state does not re-acquire

    reset_state(&s);
    chain_destroy_fail = 1;
    CHECK(wsi_close(&s) != 0);
    CHECK(chain_destroys == 1 && presenter_destroys == 0 && device_destroys == 0);
    CHECK(s.wsi_chain_live && s.wsi_hud_live);
    chain_destroy_fail = 0;
    destroy_order = 0;
    CHECK(wsi_close(&s) == 0);
    CHECK(chain_destroys == 2 && presenter_destroys == 1 && device_destroys == 1);
    CHECK(destroy_order == 123);
    CHECK(!s.wsi_chain_live && !s.wsi_hud_live);
    puts("PASS wsi hud caller");
    return 0;
}
'''


def build(text: str) -> str:
    return PRELUDE + "\n".join(extract_function(text, name) for name in FUNCTIONS) + MAIN


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--native", default="dm/android/app/native.c")
    ap.add_argument("--cc", default="cc")
    ap.add_argument("--mutant", action="store_true")
    ap.add_argument("--vulkan-include", action="append", default=[],
                    help="CMake-resolved Vulkan include directory (repeatable)")
    args = ap.parse_args()
    text = Path(args.native).read_text()
    if args.mutant:
        text = text.replace("&&s->wsi_hud_live&&!wsi_record_hud(s)", "&&0")
    root = Path(__file__).resolve().parents[3]
    vulkan_inc = [p for p in ("/opt/homebrew/include", "/usr/local/include")
                  if (Path(p) / "vulkan/vulkan.h").exists()]
    for entry in args.vulkan_include:
        for p in entry.split(";"):
            if p and p not in vulkan_inc:
                vulkan_inc.insert(0, p)
    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        c = work / "wsi_hud.c"
        exe = work / "wsi_hud"
        c.write_text(build(text))
        cmd = [args.cc, "-std=gnu11", "-g", "-Wall", "-Wextra", "-Werror",
               "-Wno-unused-parameter", "-I", str(root / "dm/android/app"),
               "-I", str(root / "dm/android/runtime"), "-I", str(root / "dm/android/vulkan"),
               "-I", str(root / "dm/ios/Sources")]
        for p in vulkan_inc:
            cmd += ["-I", p]
        cmd += ["-o", str(exe), str(c), str(root / "dm/android/app/wsi_frame_result.c")]
        if args.mutant:
            cmd.insert(4, "-Wno-unused-function")
        subprocess.run(cmd, check=True)
        rc = subprocess.run([str(exe)], check=False).returncode
        if args.mutant:
            if rc == 0:
                print("FAIL mutant no-HUD-record passed")
                return 1
            print("private wsi HUD mutant no-record failed as required")
            return 0
        return rc


if __name__ == "__main__":
    raise SystemExit(main())
