// 已核实的字节／半字索引跳转表跳板；依据上游 site_br，不使用红区，运行时检查目标范围。
// Stubs for verified byte/halfword-indexed jump tables; based on upstream site_br, without a red zone and with runtime target bounds checks.
#pragma once
#include "host.h"
typedef struct {
    uint32_t original_offset, stub_offset, table_offset, entries;
    uintptr_t host_gate;
} dm_jump_request;
typedef struct { uint32_t replacement; uint8_t *stub; size_t size; dm_gate_site site; } dm_jump_patch;
// 只接受 256 / 65536 项；调用方须验证 ADR→无符号索引加载→ADD lsl2→BR x18 数据流与所有目标范围。
// Accept only 256 / 65536 entries; caller must verify ADR -> unsigned index load -> ADD lsl2 -> BR x18 dataflow and all target ranges.
// 失败不改输出；成功后由 free 释放，不能覆盖尚未释放的 patch。
// Failure leaves output unchanged; release success with dm_jump_patch_free and never overwrite a live patch.
int dm_patch_x18_jump(const dm_jump_request *request, dm_jump_patch *out);
void dm_jump_patch_free(dm_jump_patch *patch);
