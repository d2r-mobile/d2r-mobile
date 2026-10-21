// 独占状态存活路径检查；依据 AArch64 直接分支与 CLREX／STXR 语义，拒绝路径中的宿主改写与未知控制流。
// Check live-exclusive paths; based on AArch64 direct branches and CLREX / STXR semantics, reject host rewrites or unknown control flow.
#pragma once
#include "decode.h"
#include "../nso.h"
// 只证明从该加载到清除／存储／后续独占加载／清除监视器的 SVC 边界前无需插入宿主调用，不改写指令、不证明整个程序控制流。
// Prove only that no host call is inserted before clear/store/next-exclusive-load/monitor-clearing SVC boundary; never rewrite instructions or certify whole-program control flow.
int dm_exclusive_untouched(const dm_nso_image *image,uint32_t offset,dm_decoder *decoder);
