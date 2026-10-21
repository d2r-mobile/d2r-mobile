// NSO 头、LZ4 段与 SHA-256 校验；依据 tools/nso.py / NSO0，保持原程序只作为数据。
// NSO headers, LZ4 segments and SHA-256 checks; based on tools/nso.py / NSO0, keep the original program as data only.
#include "nso.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <lz4.h>
#include <mbedtls/sha256.h>

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

dm_nso_result dm_nso_parse(const void *file, size_t length, dm_nso_header *out) {
    if (!file || !out || length < DM_NSO_HEADER_SIZE) return DM_NSO_HEADER;
    const uint8_t *p = file;
    if (memcmp(p, "NSO0", 4) || le32(p + 4) != 0 || le32(p + 8) != 0) return DM_NSO_HEADER;
    dm_nso_header h = {.flags = le32(p + 12), .bss_size = le32(p + 0x3c)};
    if (h.flags & ~0x3fu) return DM_NSO_HEADER;
    memcpy(h.build_id, p + 0x40, 32);
    uint64_t end = 0;
    for (unsigned i = 0; i < 3; i++) {
        dm_nso_segment *s = &h.segments[i];
        const uint8_t *row = p + 0x10 + i * 0x10;
        s->file_offset = le32(row); s->memory_offset = le32(row + 4); s->size = le32(row + 8);
        s->stored_size = h.flags & (1u << i) ? le32(p + 0x60 + i * 4) : s->size;
        memcpy(s->hash, p + 0xa0 + i * 32, 32);
        if (s->size > INT_MAX || s->stored_size > INT_MAX) return DM_NSO_LIMIT;
        if ((i == 0 && (s->memory_offset != 0 || s->size < 8)) ||
            s->memory_offset % 0x1000 || s->memory_offset < end) return DM_NSO_LAYOUT;
        if (s->size && (s->file_offset < DM_NSO_HEADER_SIZE ||
            s->file_offset > length || s->stored_size == 0 ||
            s->stored_size > length - s->file_offset)) return DM_NSO_LAYOUT;
        if (!s->size && s->stored_size) return DM_NSO_LAYOUT;
        for (unsigned j = 0; j < i; j++) {
            const dm_nso_segment *prev = &h.segments[j];
            if (s->stored_size && prev->stored_size &&
                (uint64_t)s->file_offset < (uint64_t)prev->file_offset + prev->stored_size &&
                (uint64_t)prev->file_offset < (uint64_t)s->file_offset + s->stored_size)
                return DM_NSO_LAYOUT;
        }
        end = (uint64_t)s->memory_offset + s->size;
        if (end > DM_NSO_MAX_IMAGE) return DM_NSO_LIMIT;
    }
    end += h.bss_size;
    if (end > DM_NSO_MAX_IMAGE) return DM_NSO_LIMIT;
    h.image_size = (size_t)end;
    *out = h;
    return DM_NSO_OK;
}

dm_nso_result dm_nso_decode(const void *file, size_t length, dm_nso_image *out) {
    if (!out) return DM_NSO_HEADER;
    dm_nso_image image = {0};
    dm_nso_result result = dm_nso_parse(file, length, &image.header);
    if (result != DM_NSO_OK) return result;
    image.bytes = calloc(1, image.header.image_size);
    if (!image.bytes) return DM_NSO_MEMORY;
    for (unsigned i = 0; i < 3; i++) {
        const dm_nso_segment *s = &image.header.segments[i];
        uint8_t *dest = image.bytes + s->memory_offset;
        if (s->size) {
            const uint8_t *src = (const uint8_t *)file + s->file_offset;
            if (image.header.flags & (1u << i)) {
                int size = LZ4_decompress_safe((const char *)src, (char *)dest,
                    (int)s->stored_size, (int)s->size);
                if (size != (int)s->size) { result = DM_NSO_LZ4; break; }
            } else memcpy(dest, src, s->size);
        }
        if (image.header.flags & (1u << (i + 3))) {
            uint8_t hash[32];
            if (mbedtls_sha256(dest, s->size, hash, 0) || memcmp(hash, s->hash, 32)) {
                result = DM_NSO_HASH; break;
            }
        }
    }
    if (result != DM_NSO_OK) { dm_nso_free(&image); return result; }
    *out = image;
    return DM_NSO_OK;
}

void dm_nso_free(dm_nso_image *image) {
    free(image->bytes);
    memset(image, 0, sizeof *image);
}

const char *dm_nso_error(dm_nso_result result) {
    switch (result) {
    case DM_NSO_OK: return "ok";
    case DM_NSO_IO: return "cannot read input file";
    case DM_NSO_HEADER: return "invalid or unsupported NSO0 header";
    case DM_NSO_LAYOUT: return "invalid, overlapping or truncated segment layout";
    case DM_NSO_LIMIT: return "NSO exceeds the 512 MiB image / file limit";
    case DM_NSO_MEMORY: return "memory allocation failed";
    case DM_NSO_LZ4: return "invalid LZ4 segment or decompressed length mismatch";
    case DM_NSO_HASH: return "segment SHA-256 mismatch";
    }
    return "unknown NSO error";
}
