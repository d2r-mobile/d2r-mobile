// 严格进程的模块所有权；复用真实装载器，输出仅供借用检查。
// Module ownership for strict processes; reuse the real loader, returning inspection-only borrowed output.
#pragma once
#include "host.h"
#include "../module_map.h"
// 只可在首次附着前成功调用一次；宿主负责释放，调用方不得 unmap／改变保护，输出在销毁时失效。
// Succeeds once before first attachment; host owns release, callers must not unmap/reprotect, and output expires at destruction.
// 自动登记真实页面权限；失败保持宿主和输出不变，返回 errno 风格错误。
// Registers actual page permissions automatically; failure leaves host/output unchanged and returns errno-style errors.
int dm_host_map_modules(dm_host *host,const dm_nso_image *images,const dm_image_code *codes,size_t count,
    const dm_symbol_override *imports,size_t import_count,const dm_module_mappings **out);
