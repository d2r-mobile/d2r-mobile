// 原生映像映射与 W^X 保护；输入必须来自 nso.h 解包边界，调用方负责改写与链接。
// Native image mapping and W^X protection; input must come from the nso.h decode boundary, caller owns rewriting / linking.
#pragma once
#include "nso.h"

typedef struct { uint8_t *base; size_t size; } dm_image_mapping;
typedef struct { size_t offset, size; const uint8_t *bytes; } dm_image_code;
typedef enum { DM_IMAGE_OK, DM_IMAGE_LAYOUT, DM_IMAGE_WX_CONFLICT, DM_IMAGE_MEMORY, DM_IMAGE_OS } dm_image_result;

// 只映射，不跳入代码；失败保持输出不变，成功后由 dm_image_unmap 释放。
// Map only, never enter code; failure leaves output untouched, release a successful mapping with dm_image_unmap.
dm_image_result dm_image_map(const dm_nso_image *image, dm_image_mapping *out);
// 两阶段装载：先保留不可访问地址，按实际基址链接解码缓冲区，再提交并封页；失败后仍由 unmap 释放。
// Two-stage loading: reserve inaccessible addresses, link decoded buffers at real bases, then commit / protect; unmap also releases failures.
dm_image_result dm_image_reserve(const dm_nso_image *image, dm_image_mapping *out);
dm_image_result dm_image_commit(const dm_nso_image *image, dm_image_mapping *reserved);
// 附加跳板区必须按宿主页对齐且位于原映像所有页面之后；reserve 忽略 bytes，commit 要求有效缓冲区。
// Appended stubs must be host-page aligned and beyond every original image page; reserve ignores bytes, commit requires a valid buffer.
dm_image_result dm_image_reserve_with_code(const dm_nso_image *image, const dm_image_code *code, dm_image_mapping *out);
dm_image_result dm_image_commit_with_code(const dm_nso_image *image, const dm_image_code *code, dm_image_mapping *reserved);
int dm_image_unmap(dm_image_mapping *mapping);

// 只计算实际宿主页布局；成功返回每页 POSIX 权限数组，调用方须 free，不映射或修改输入。
// Compute the exact host-page layout only; success returns per-page POSIX permissions for caller free, without mapping or modifying input.
dm_image_result dm_image_layout(const dm_nso_image *image,const dm_image_code *code,
    size_t *mapped_size,size_t *page_size,uint8_t **permissions);

// 重置仍由调用方拥有的完整预留子区，绝不先 munmap；失败时调用方必须保留所有权或终止。
// Reset a fully owned reserved subrange without first unmapping; on failure the caller must retain ownership or terminate.
int dm_image_reset_reserved(const dm_image_mapping *mapping);
