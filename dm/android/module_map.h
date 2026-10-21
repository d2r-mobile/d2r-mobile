// 多模块地址预留、链接与封页；依据 module.h / image_map.h，不安装指令补丁，不提供执行入口。
// Multi-module reservation, linking and protection; based on module.h / image_map.h, installs no instruction patches and exposes no execution entry.
#pragma once
#include "module.h"
#include "image_map.h"
typedef struct {
    dm_image_mapping *mappings;
    size_t count, relocations, weak_zero, host_imports;
    int borrowed;
} dm_module_mappings;
// 复制输入后进行重定位，保留原始解码映像；缺少强符号或任何失败均回滚全部映射，输出保持不变。
// Relocate copies, preserving original decoded images; missing strong imports or any failure rolls back all mappings and leaves output unchanged.
dm_module_result dm_modules_map(const dm_nso_image *images, size_t count, dm_module_mappings *out);
// 每模块附加已生成的 RX 跳板；codes 为 count 项，全零项表示无跳板；与原映像一起回滚，不执行入口。
// Append generated RX stubs per module; codes has count entries, zero entries mean no stubs; roll back with original images, never execute entries.
dm_module_result dm_modules_map_with_code(const dm_nso_image *images, const dm_image_code *codes,
                                         size_t count, dm_module_mappings *out);
// 显式宿主导入仅在未定义符号的重定位中优先查找；其他生命周期及回滚约定相同。
// Explicit host imports take precedence for undefined-symbol relocations only; lifecycle / rollback contracts are unchanged.
dm_module_result dm_modules_map_imports(const dm_nso_image *images,const dm_image_code *codes,size_t count,
    const dm_symbol_override *imports,size_t import_count,dm_module_mappings *out);
int dm_modules_unmap(dm_module_mappings *mapped);

// 借用调用方持续拥有的 PROT_NONE 子区进行链接／提交；失败重置所有尝试区间，不解除预留。
// Link/commit into continuously owned PROT_NONE subranges; failure resets every attempted span without releasing reservations.
// 成功输出仅拥有元数据数组，不得传给 unmap；调用方在释放父预留后 free(mappings)。
// Successful output owns only its metadata array and must not be passed to unmap; caller frees mappings after releasing the parent reservation.
dm_module_result dm_modules_commit_imports(const dm_nso_image *images,const dm_image_code *codes,size_t count,
    const dm_symbol_override *imports,size_t import_count,const dm_image_mapping *reserved,dm_module_mappings *out);
