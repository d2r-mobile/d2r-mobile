// 无游戏数据的系统指令改写器：只支持明确列出的 SVC/TLS/计时指令，其余返回 ENOTSUP。
// Game-free system-instruction rewriter: only explicitly supported SVC / TLS / timer instructions; others return ENOTSUP.
#pragma once
#include "host.h"
typedef struct { uint32_t instruction, original_offset, stub_offset; uintptr_t host_gate; } dm_patch_request;
typedef struct { uint32_t replacement; uint8_t stub[32]; dm_gate_site site; } dm_patch_output;
// site.return_address 是映像内偏移，映射后由调用方加实际基址再登记；映射前写入 replacement/stub。
// site.return_address is image-relative; caller adds mapped base before registration, writes replacement / stub before mapping.
int dm_patch_system(const dm_patch_request *request, dm_patch_output *out);
