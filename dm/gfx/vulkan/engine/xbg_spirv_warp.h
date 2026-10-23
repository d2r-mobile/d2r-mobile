// 按 Maxwell 32 道线程束写的计算着色器在更宽的子组上运行（见 xbg_spirv_warp.cpp）
// Compute shaders written for Maxwell's 32-lane warps, run on wider subgroups (see xbg_spirv_warp.cpp)
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// 将按32道翻译的compute模块改为64/128子组的每32道独立warp：lane取id&31、ballot/mask选id>>5字、投票按该字、shuffle仅本warp。
// 返回改写指令数量；0时out为空、调用者使用原模块，包括32道、不支持宽度或未知子组用途。
// Rewrites a compute module translated for 32-lane subgroups so that each 32-lane half (or quarter) of a subgroup of
// subgroup_size lanes (64 or 128) acts as its own warp: the lane id becomes id & 31, word 0 of a ballot or subgroup mask
// becomes word id >> 5, a vote is answered from the ballot word of the lane's warp, and a shuffle reads from the same warp
// ((id & 0x60) | index).
// Returns the number of instructions rewritten (0: out is empty and the module is to be used unchanged, including when it
// uses a subgroup feature this does not know).
size_t xbg_spirv_warp32(const uint32_t *words, size_t count, uint32_t subgroup_size, std::vector<uint32_t> &out);
