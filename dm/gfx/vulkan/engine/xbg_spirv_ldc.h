// 常量缓冲区按字寻址时可由低位推出的分量下标换成常量（见 xbg_spirv_ldc.cpp）
// Component indexes of constant-buffer word addressing that follow from known low bits become constants (see xbg_spirv_ldc.cpp)
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// Every use of an OpBitwiseAnd of a 32-bit integer with a constant mask below 16, whose value follows from the known low bits
// of its operand, is replaced by that value as a constant. Low bits are tracked through IMul, IAdd, ISub, shifts by a constant,
// bitcasts, integer conversions, copies, and loads of Function variables stored exactly once. The AND stays, as dead code.
// Returns the number of ANDs folded (0: out is empty and the module is to be used unchanged).
size_t xbg_spirv_ldc_fold(const uint32_t *words, size_t count, std::vector<uint32_t> &out);
