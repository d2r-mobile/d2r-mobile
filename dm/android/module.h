// 有界 MOD0 / ELF64 动态链接；依据上游 host/loader.c 与 AArch64 ELF RELA ABI，不执行初始化器。
// Bounded MOD0 / ELF64 dynamic linking; based on host/loader.c and the AArch64 ELF RELA ABI, never runs initializers.
#pragma once
#include "nso.h"

typedef enum {
    DM_MODULE_OK, DM_MODULE_FORMAT, DM_MODULE_BOUNDS, DM_MODULE_UNSUPPORTED,
    DM_MODULE_MEMORY, DM_MODULE_UNRESOLVED, DM_MODULE_STALE, DM_MODULE_MAPPING
} dm_module_result;
typedef struct {
    dm_nso_image *image;
    uint64_t base, hash, symtab, strtab, strsz, nsym;
    uint64_t rela[2], relasz[2], init, init_array, init_arraysz;
} dm_module;
typedef struct {
    size_t module;
    uint64_t offset, before, value;
} dm_link_write;
typedef struct {
    dm_link_write *writes;
    size_t count, unresolved, relative, absolute, jump, weak_zero, host_imports;
} dm_link_plan;
typedef struct { const char *name; uint64_t address; } dm_symbol_override;

// base 是调用方预留的目标地址，也可供离线分析使用；image 在模块 / 计划使用期间必须保持有效且不变。
// base is the caller-reserved target address, also usable for offline analysis; keep image alive and unchanged while modules / plans use it.
dm_module_result dm_module_parse(dm_nso_image *image, uint64_t base, dm_module *out);
// 输入必须来自 dm_module_parse；模块顺序决定全局符号查找顺序。缺失强符号计入 unresolved，错误不返回半份计划。
// Inputs must come from dm_module_parse; module order determines global lookup order. Missing strong symbols increment unresolved; errors never return partial plans.
dm_module_result dm_link_plan_build(const dm_module *modules, size_t count, dm_link_plan *out);
// 显式宿主导入优先于模块查找，仅替换未定义符号；不改本模块定义或直接分支。
// Explicit host imports precede module lookup for undefined symbols only; never patch local definitions or direct branches.
dm_module_result dm_link_plan_build_imports(const dm_module *modules,size_t count,
    const dm_symbol_override *imports,size_t import_count,dm_link_plan *out);
// 先核对所有写入，再统一执行；缺失符号、重复目标或数据变化时完全不写。只修改解码缓冲区的 data/BSS。
// Validate every write before applying any; missing symbols, duplicate targets or changed data cause no writes. Only decoded data/BSS is modified.
dm_module_result dm_link_apply(const dm_module *modules, size_t count, const dm_link_plan *plan);
void dm_link_plan_free(dm_link_plan *plan);
const char *dm_module_error(dm_module_result result);
