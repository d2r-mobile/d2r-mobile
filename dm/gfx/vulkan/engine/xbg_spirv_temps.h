// 翻译器的单写临时变量直接转发给读取处（见 xbg_spirv_temps.cpp）
// The translator's single-store temporaries forwarded to their loads (see xbg_spirv_temps.cpp)
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// A Function variable without an initializer, stored exactly once by a whole OpStore and otherwise only loaded, with the store
// and every load in one block and the store first, is removed: each load's uses read the stored value instead, and the
// variable, its store and its loads are dropped. Variables with any other use, or named or decorated, stay.
// Returns the number of variables removed (0: out is empty and the module is to be used unchanged).
size_t xbg_spirv_forward_temps(const uint32_t *words, size_t count, std::vector<uint32_t> &out);
