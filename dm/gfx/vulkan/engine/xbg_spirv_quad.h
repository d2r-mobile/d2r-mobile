// 片元四元组邻居的保守导数改写，基于PR14 a90826a及SPIR-V规范；不把未知流当浮点数。
// Conservative fragment quad-neighbour derivative rewrite, based on PR14 a90826a and SPIR-V; unknown flows are never presumed numeric.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

enum class xbg_quad_skip : uint32_t {
    malformed, module_contract, non_float, lane_index, control_flow,
    bit_pattern, integer_flow, unknown_source, unknown_use, memory_flow, count
};
// 辅助函数调用形式（Ryujinx 把 SHFL 译成辅助函数，见 xbg_spirv_quad.cpp）的跳过原因。
// Skip reasons of the helper-call form (the translator's SHFL helper functions; see xbg_spirv_quad.cpp).
enum class xbg_quad_call_skip : uint32_t { module_contract, helper, arguments, control_flow, source, use, count };
struct xbg_spirv_quad_stats {
    size_t candidates=0, rewritten=0, guarded_nonfinite=0;
    size_t skipped[static_cast<size_t>(xbg_quad_skip::count)]={};
    size_t calls=0, calls_rewritten=0;
    size_t call_skipped[static_cast<size_t>(xbg_quad_call_skip::count)]={};
};
const char *xbg_spirv_quad_skip_name(xbg_quad_skip reason);
const char *xbg_spirv_quad_call_skip_name(xbg_quad_call_skip reason);

// 返回改写数（shuffle 与辅助函数调用之和）；0时out为空，调用方使用原模块。stats每次调用重置，供每shader记录。
// 数值重建存在有限浮点舍入；非有限输入/中间值回退原shuffle，不声称位精确或真机改善。
// Return rewrite count (shuffles plus helper calls); when zero, out is empty and callers retain the original module. Reset stats per call for per-shader reporting.
// Numerical reconstruction can round finite values; nonfinite inputs/intermediates fall back to the original shuffle, with no bit-exact or device-benefit claim.
size_t xbg_spirv_quad_derivatives(const uint32_t *words,size_t count,std::vector<uint32_t> &out,
                                  xbg_spirv_quad_stats *stats);
size_t xbg_spirv_quad_derivatives(const uint32_t *words,size_t count,std::vector<uint32_t> &out);
