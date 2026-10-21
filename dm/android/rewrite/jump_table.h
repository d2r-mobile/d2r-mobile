// 宿主跳转表识别边界；依据上游 patch_nso.py 的最近八条指令模式，拒绝被覆盖的寄存器数据流。
// Host jump-table recognition boundary; based on upstream patch_nso.py's eight-instruction pattern, reject overwritten register dataflow.
#pragma once
#include "../nso.h"
typedef struct{uint32_t table_offset,entries;} dm_jump_table;
int dm_recognize_jump(const dm_nso_image *image,uint32_t branch_offset,dm_jump_table *out);
