// 整段独占窗口原生跳板；依据上游 site_window，宿主门只位于首次独占加载前。
// Native whole-exclusive-window stubs; based on upstream site_window, with the host gate only before the first exclusive load.
#pragma once
#include "host.h"
#define DM_WINDOW_MAX 17u
typedef struct{
    uint32_t original_offset,stub_offset,count;
    uint32_t original[DM_WINDOW_MAX],translated[DM_WINDOW_MAX];
    unsigned scratch[3];uintptr_t host_gate;
}dm_window_request;
typedef struct{uint32_t replacement;uint8_t stub[512];size_t size;dm_gate_site site;}dm_window_patch;
// 调用方须核对替换、禁止 SP 访问及 PC 相对非分支，并排除窗口外进入内部的分支。
// Caller must verify substitutions, reject SP access / PC-relative nonbranches and exclude external branches into the interior.
int dm_patch_x18_window(const dm_window_request *request,dm_window_patch *out);
