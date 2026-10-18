// 宿主内存改写产物；依据现有解码器与原生跳板 ABI，共享审计路径，失败不发布部分产物。
// Host in-memory rewrite artifacts; based on existing decoders / native stub ABI, sharing audit logic without publishing partial failures.
#pragma once
#include "../artifact.h"
typedef struct {
    size_t systems, plain, addresses, conditionals, jumps, windows, rewritten_windows, rewritten_sites;
    size_t window_sites, unmatched, pcrel, branches, other, undecodable, emitter_errors, unsupported_system;
    size_t stub_bytes, sites, native_exclusive, metadata_words, metadata_entries, retained_traps, literal_words, literal_conflicts;
} dm_rewrite_report;
// out=NULL 仅审计；否则生成独立映像副本、跳板和相对门表，调用方负责映射后加基址并排序。
// out=NULL audits only; otherwise generate a private image, stubs and relative gate table; caller adds mapped bases and sorts.
// stub_offset 至少为 image_size，按 8 字节对齐；映射时还须宿主页对齐。阻塞返回 ENOTSUP，输出不变。
// stub_offset is at least image_size and 8-byte aligned; mapping additionally requires host-page alignment. Blockers return ENOTSUP, output unchanged.
// 成功不证明代码／数据和间接入口分类、服务覆盖或初始化安全；内存门地址仅对当前进程有效，跨进程须使用 artifact.h 格式。
// Success does not certify code/data or indirect-entry classification, service coverage or initialization safety; memory gate addresses are process-local, use artifact.h across processes.
int dm_rewrite_prepare(const dm_nso_image *image, uint32_t stub_offset, uintptr_t host_gate,
                       dm_rewrite_report *report, dm_rewrite_output *out);
void dm_rewrite_free(dm_rewrite_output *output);
