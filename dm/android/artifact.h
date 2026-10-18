// ARM64 改写产物与装载边界；依据 prepare / module_map ABI，不携带可复用的宿主地址，不自动执行初始化器。
// ARM64 rewrite artifact / loading boundary; based on prepare / module_map ABI, without reusable host addresses or automatic initializer execution.
#pragma once
#include "module_map.h"
#include "runtime/host.h"
typedef struct {
    dm_nso_image image;
    dm_image_code code;
    dm_gate_site *sites;
    uint32_t *gate_literals;
    size_t count;
} dm_rewrite_output;
void dm_rewrite_free(dm_rewrite_output *output);
// 只编码未链接产物；小端版本化格式，SHA-256 检查损坏而非签名，仅接受可信程序。门字面量保存为零，装载时才绑定。
// Encode unlinked artifacts only; versioned little-endian format, SHA-256 detects corruption rather than authenticity, trusted programs only. Zero gate literals until load.
int dm_artifact_encode(const dm_rewrite_output *input,uint8_t **bytes,size_t *size);
int dm_artifact_decode(const void *bytes,size_t size,dm_rewrite_output *output);
// 读取上限 512 MiB；写入使用独占创建，不覆盖已有路径。失败不改输出，成功的缓冲区由调用方释放。
// Read at most 512 MiB; write exclusively creates a new path without overwriting. Failure preserves output; caller releases successful buffers.
int dm_artifact_read(const char *path,dm_rewrite_output *output);
int dm_artifact_write(const char *path,const dm_rewrite_output *input);
typedef struct {
    dm_module_mappings modules;
    dm_gate_site *sites;
    dm_gate_table gates;
} dm_artifact_mappings;
// 复制／绑定跳板，按实际基址链接，合并并排序门表；输入保持不变。不得在任何附着线程仍使用门表时释放。
// Copy/bind stubs, link at actual bases and combine/sort gates; preserve inputs. Never release while attached threads still use the table.
int dm_artifacts_map(const dm_rewrite_output *inputs,size_t count,uintptr_t host_gate,dm_artifact_mappings *output);
int dm_artifacts_map_imports(const dm_rewrite_output *inputs,size_t count,uintptr_t host_gate,
    const dm_symbol_override *imports,size_t import_count,dm_artifact_mappings *output);
int dm_artifacts_unmap(dm_artifact_mappings *mapped);
