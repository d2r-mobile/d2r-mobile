// 只读 RomFS 宿主适配；依据上游 hle_fs.c 的 nn::fs ABI，仅支持普通原始资源，不支持 holes／overlay／存档。
// Read-only RomFS host adapter; based on upstream hle_fs.c nn::fs ABI, supports raw resources only, without holes / overlays / saves.
#pragma once
#include "host.h"
#include "../module.h"
// 必须在附着线程前配置一次；不跟随根及路径中的符号链接，禁止穿越与大小写替代。
// Configure once before attaching threads; never follow root/path symlinks, traversal or case substitutions.
int dm_host_mount_romfs(dm_host *host,const char *root);
// 原生查询允许未附着调用者及其有效缓冲区；导入表中的 ARM64 入口要求线程已附着。
// The native query accepts unattached callers with caller-valid buffers; ARM64 import-table entries require an attached thread.
uint32_t dm_romfs_query_cache_size(size_t *out);
const dm_symbol_override *dm_romfs_imports(size_t *count);
