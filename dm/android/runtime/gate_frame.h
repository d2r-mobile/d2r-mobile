// 与 host/entry.S 的 800 字节门帧逐字段一致；外层 fp/lr 由系统指令跳板另存。
// Field-for-field match to host/entry.S's 800-byte gate frame; the system-instruction stub separately saves outer fp/lr.
#pragma once
#include <stdalign.h>
#include <stdint.h>
typedef struct {
    uint64_t registers[31], reserved;
    alignas(16) uint8_t vectors[32][16];
    uint64_t nzcv, fpsr, fpcr, padding;
} dm_gate_frame;
_Static_assert(sizeof(dm_gate_frame) == 800, "host/entry.S gate frame ABI");
