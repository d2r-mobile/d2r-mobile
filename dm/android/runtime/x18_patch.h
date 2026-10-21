// 普通 x18 指令的原生跳板；依据 AArch64 编码与 gate_frame.h，输入须由离线解码器核对操作数替换。
// Native stubs for plain x18 instructions; based on AArch64 encodings / gate_frame.h, inputs require offline operand-substitution verification.
#pragma once
#include "host.h"
typedef struct {
    uint32_t original_offset, stub_offset, translated;
    unsigned scratch[3];
    int sp_delta;
    uintptr_t host_gate;
} dm_x18_request;
typedef struct { uint32_t replacement; uint8_t stub[64]; dm_gate_site site; } dm_x18_patch;
// 仅支持普通、非 PC 相对、非分支、非独占窗口内的指令；SP 写回只能为 [-128,128] 内的 16 字节整倍数。
// Only plain non-PC-relative, nonbranch instructions outside exclusive windows; SP writeback must be a multiple of 16 within [-128,128].
int dm_patch_x18(const dm_x18_request *request, dm_x18_patch *out);
