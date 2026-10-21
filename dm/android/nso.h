// 原生 NSO 读取边界；依据 tools/nso.py 与 NSO0 公开头格式，不执行客体代码。
// Native NSO read boundary; based on tools/nso.py and the public NSO0 header format, never executes guest code.
#pragma once
#include <stddef.h>
#include <stdint.h>

#define DM_NSO_HEADER_SIZE 0x100u
#define DM_NSO_MAX_IMAGE (512u * 1024u * 1024u)

typedef enum {
    DM_NSO_OK, DM_NSO_IO, DM_NSO_HEADER, DM_NSO_LAYOUT,
    DM_NSO_LIMIT, DM_NSO_MEMORY, DM_NSO_LZ4, DM_NSO_HASH
} dm_nso_result;

typedef struct {
    uint32_t file_offset, memory_offset, size, stored_size;
    uint8_t hash[32];
} dm_nso_segment;

typedef struct {
    dm_nso_segment segments[3];
    uint32_t flags, bss_size;
    uint8_t build_id[32];
    size_t image_size;
} dm_nso_header;

typedef struct {
    dm_nso_header header;
    uint8_t *bytes;
} dm_nso_image;

// 失败不改输出；成功的 image 由调用方用 dm_nso_free 释放；不得覆盖未释放的 image。
// Failure leaves output untouched; caller releases a successful image with dm_nso_free; never overwrite a live image.
dm_nso_result dm_nso_parse(const void *file, size_t length, dm_nso_header *out);
dm_nso_result dm_nso_decode(const void *file, size_t length, dm_nso_image *out);
dm_nso_result dm_nso_read(const char *path, dm_nso_image *out);
void dm_nso_free(dm_nso_image *image);
const char *dm_nso_error(dm_nso_result result);
