// 独占窗口离线核对边界；依据上游窗口替换方法，额外检查整个 text 的直接分支入口。
// Offline exclusive-window verification boundary; based on upstream substitution, additionally check direct branch entries across all text.
#pragma once
#include "../nso.h"
#include "../runtime/x18_window.h"
typedef struct{const dm_nso_image*image;uint32_t offset,count;}dm_window_source;
// 没有 x18 返回 ENOENT；特殊指令、SP 使用或窗口中部外部入口均返回 ENOTSUP。
// Return ENOENT without x18; special instructions, SP use or external interior entry return ENOTSUP.
int dm_decode_window(const dm_window_source*source,dm_window_request*out);
