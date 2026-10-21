// x18 的 ADR/ADRP 与 CBZ/CBNZ/TBZ/TBNZ 原生跳板；依据 AArch64 PC 相对编码，不使用红区。
// Native x18 ADR/ADRP and CBZ/CBNZ/TBZ/TBNZ stubs; based on AArch64 PC-relative encodings, with no red zone.
#pragma once
#include "host.h"
typedef struct { uint32_t instruction, original_offset, stub_offset; uintptr_t host_gate; } dm_control_request;
typedef struct { uint32_t replacement; uint8_t stub[96]; dm_gate_site site; } dm_control_patch;
// 基址必须按 4KB 对齐；位移以同一模块的映像偏移计算，输出不包含文件内容或绝对客体地址。
// Base must be 4KB aligned; calculate displacements as offsets in one module, without file contents or absolute guest addresses in the output.
int dm_patch_x18_control(const dm_control_request *request, dm_control_patch *out);
