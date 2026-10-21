// 合成 NSO 回归：依据 NSO0 字段布局，覆盖 LZ4、哈希、截断和边界，不使用游戏内容。
// Synthetic NSO regression: based on the NSO0 field layout; covers LZ4, hashes, truncation and bounds without game content.
#include "nso.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lz4.h>
#include <mbedtls/sha256.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
typedef struct { uint8_t bytes[4096]; size_t size; } fixture;
static const uint32_t sizes[] = {257, 97, 83};

static void put32(uint8_t *p, uint32_t value) {
    for (unsigned i = 0; i < 4; i++) p[i] = (uint8_t)(value >> (i * 8));
}

static fixture make_fixture(int compressed) {
    fixture f = {.size = DM_NSO_HEADER_SIZE};
    memcpy(f.bytes, "NSO0", 4); put32(f.bytes + 12, compressed ? 0x3f : 0x38);
    put32(f.bytes + 0x3c, 128);
    for (unsigned i = 0; i < 3; i++) {
        uint8_t plain[257];
        for (unsigned j = 0; j < sizes[i]; j++) plain[j] = (uint8_t)(j * 3 + i);
        uint8_t *row = f.bytes + 0x10 + i * 0x10;
        put32(row, (uint32_t)f.size); put32(row + 4, i * 0x1000); put32(row + 8, sizes[i]);
        CHECK(!mbedtls_sha256(plain, sizes[i], f.bytes + 0xa0 + i * 32, 0));
        int stored = (int)sizes[i];
        if (compressed) stored = LZ4_compress_default((const char *)plain,
            (char *)f.bytes + f.size, (int)sizes[i], (int)(sizeof f.bytes - f.size));
        else memcpy(f.bytes + f.size, plain, sizes[i]);
        CHECK(stored > 0); put32(f.bytes + 0x60 + i * 4, (uint32_t)stored);
        f.size += (size_t)stored;
    }
    return f;
}

static void decodes_segments_when_valid(int compressed) {
    // Given：带真实摘要的自造 NSO。 | Given: a synthetic NSO with real digests.
    fixture f = make_fixture(compressed);
    dm_nso_image image = {0};
    // When：完整解包。 | When: decode the complete image.
    CHECK(dm_nso_decode(f.bytes, f.size, &image) == DM_NSO_OK);
    // Then：段内容、间隙和 BSS 均精确匹配。 | Then: segment contents, gaps and BSS match exactly.
    CHECK(image.header.image_size == 0x2000 + sizes[2] + 128);
    for (unsigned i = 0; i < 3; i++) {
        for (unsigned j = 0; j < sizes[i]; j++) CHECK(image.bytes[i * 0x1000 + j] == (uint8_t)(j * 3 + i));
        size_t end = i < 2 ? (i + 1) * 0x1000 : image.header.image_size;
        for (size_t p = i * 0x1000 + sizes[i]; p < end; p++) CHECK(image.bytes[p] == 0);
    }
    dm_nso_free(&image);
    CHECK(!image.bytes && !image.header.image_size);
}

static void preserves_output_when_rejected(fixture f, dm_nso_result expected) {
    // Given：损坏输入与原有输出哨兵。 | Given: corrupt input and an existing output sentinel.
    dm_nso_image image = {.header.flags = 0xdead};
    // When：尝试解包。 | When: attempt decoding.
    dm_nso_result result = dm_nso_decode(f.bytes, f.size, &image);
    // Then：返回指定错误且不改输出。 | Then: return the expected error without changing the output.
    CHECK(result == expected);
    CHECK(!image.bytes && image.header.flags == 0xdead);
}

static void rejects_malformed_headers(void) {
    fixture good = make_fixture(1), f;
    for (size_t n = 0; n < DM_NSO_HEADER_SIZE; n++) {
        f = good; f.size = n; preserves_output_when_rejected(f, DM_NSO_HEADER);
    }
    f = good; f.bytes[0] = 'X'; preserves_output_when_rejected(f, DM_NSO_HEADER);
    f = good; f.bytes[4] = 1; preserves_output_when_rejected(f, DM_NSO_HEADER);
    f = good; f.bytes[8] = 1; preserves_output_when_rejected(f, DM_NSO_HEADER);
    f = good; f.bytes[12] = 0xff; preserves_output_when_rejected(f, DM_NSO_HEADER);
    f = good; put32(f.bytes + 0x14, 0x1000); preserves_output_when_rejected(f, DM_NSO_LAYOUT);
    f = good; put32(f.bytes + 0x24, 0); preserves_output_when_rejected(f, DM_NSO_LAYOUT);
    f = good; put32(f.bytes + 0x24, 0x1001); preserves_output_when_rejected(f, DM_NSO_LAYOUT);
    f = good; put32(f.bytes + 0x20, 0x100); preserves_output_when_rejected(f, DM_NSO_LAYOUT);
    f = good; put32(f.bytes + 0x10, 0x80); preserves_output_when_rejected(f, DM_NSO_LAYOUT);
    f = good; put32(f.bytes + 0x60, 0); preserves_output_when_rejected(f, DM_NSO_LAYOUT);
    f = good; put32(f.bytes + 0x64, 0xffffffffu); preserves_output_when_rejected(f, DM_NSO_LIMIT);
    f = good; put32(f.bytes + 0x34, 0xfffff000u); preserves_output_when_rejected(f, DM_NSO_LIMIT);
    f = good; put32(f.bytes + 0x3c, 0xffffffffu); preserves_output_when_rejected(f, DM_NSO_LIMIT);
    f = good; f.size--; preserves_output_when_rejected(f, DM_NSO_LAYOUT);
    f = good; put32(f.bytes + 0x18, 258); preserves_output_when_rejected(f, DM_NSO_LZ4);
    f = good; memset(f.bytes + 0x100, 0xff, 20); preserves_output_when_rejected(f, DM_NSO_LZ4);
    for (unsigned i = 0; i < 3; i++) {
        f = good; f.bytes[0xa0 + i * 32] ^= 1; preserves_output_when_rejected(f, DM_NSO_HASH);
    }
}

static void hashes_known_vector_when_sha256_enabled(void) {
    // Given：NIST 的 abc 输入和 SHA-256 结果。 | Given: NIST's abc input and SHA-256 result.
    const uint8_t expected[32] = {0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
        0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
    uint8_t actual[32];
    // When：计算摘要。 | When: calculate the digest.
    CHECK(!mbedtls_sha256((const uint8_t *)"abc", 3, actual, 0));
    // Then：逐字节等于已知结果。 | Then: match the known result byte for byte.
    CHECK(!memcmp(actual, expected, 32));
}

static void verifies_hash_when_segment_empty(void) {
    // Given：空 rodata 段，仍带有 SHA-256 标志及正确空摘要。
    // Given: an empty rodata segment with the SHA-256 flag and correct empty digest.
    fixture f = make_fixture(1);
    put32(f.bytes + 0x28, 0); put32(f.bytes + 0x64, 0);
    CHECK(!mbedtls_sha256((const uint8_t *)"", 0, f.bytes + 0xc0, 0));
    dm_nso_image image = {0};
    // When：解包空段。 | When: decode the empty segment.
    CHECK(dm_nso_decode(f.bytes, f.size, &image) == DM_NSO_OK);
    // Then：空段有效，摘要损坏时仍须拒绝。 | Then: the empty segment is valid, but a corrupt digest must still be rejected.
    CHECK(image.header.segments[1].size == 0); dm_nso_free(&image);
    f.bytes[0xc0] ^= 1;
    preserves_output_when_rejected(f, DM_NSO_HASH);
}

int main(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "--fixture")) {
        fixture f = make_fixture(1);
        FILE *out = fopen(argv[2], "wb"); CHECK(out);
        CHECK(fwrite(f.bytes, 1, f.size, out) == f.size); CHECK(!fclose(out));
        return 0;
    }
    CHECK(argc == 1);
    hashes_known_vector_when_sha256_enabled();
    decodes_segments_when_valid(0); decodes_segments_when_valid(1);
    rejects_malformed_headers();
    verifies_hash_when_segment_empty();
    puts("PASS: NSO plain / LZ4, SHA-256, BSS, truncation, overlaps, limits, failure ownership");
    return 0;
}
