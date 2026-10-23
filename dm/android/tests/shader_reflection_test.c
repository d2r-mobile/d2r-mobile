#include "nvn_vulkan_reflection.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    uint32_t code[] = {0x07230203, 0x10000, 0, 2, 0, (5u << 16) | 15, 0, 1, 0x6e69616d, 0};
    assert(dm_nvn_shader_entry_valid(code, sizeof code, 0));
    assert(!dm_nvn_shader_entry_valid(code, sizeof code, 1));
    assert(!dm_nvn_shader_entry_valid(code, sizeof code, 5));
    for (size_t n = 0; n < sizeof code; n++) assert(!dm_nvn_shader_entry_valid(code, n, 0));
    for (unsigned i = 0; i < sizeof code / sizeof *code; i++) {
        uint32_t old = code[i]; code[i] = UINT32_MAX;
        if (i != 2 && i != 3) assert(!dm_nvn_shader_entry_valid(code, sizeof code, 0));
        code[i] = old;
    }
    code[6] = 4; assert(dm_nvn_shader_entry_valid(code, sizeof code, 1));
    code[6] = 5; assert(dm_nvn_shader_entry_valid(code, sizeof code, 5));
    code[6] = 0;
    size_t capacity = 28 + 256 * 40 + 65536 + 128;
    uint32_t *meta = calloc(1, capacity);
    assert(meta);
    memcpy(meta, "DMVK", 4);
    meta[1] = 1; meta[4] = 2; meta[6] = 32;
    meta[7 + 7] = meta[17 + 7] = 1;
    meta[17 + 1] = 1;
    size_t bytes = 28 + 2 * 40 + 128;
    const uint32_t stages[] = {0, 1, 5};
    for (unsigned i = 0; i < 3; i++) {
        meta[2] = stages[i]; meta[3] = stages[i] == 1 ? 1 : 0;
        assert(dm_nvn_shader_reflection_valid(meta, bytes));
        meta[3] ^= 1;
        assert(!dm_nvn_shader_reflection_valid(meta, bytes));
    }
    meta[3] = 0;
    const uint32_t rejected[] = {2, 3, 4, 6, UINT32_MAX};
    for (unsigned i = 0; i < sizeof rejected / sizeof *rejected; i++) {
        meta[2] = rejected[i];
        assert(!dm_nvn_shader_reflection_valid(meta, bytes));
    }
    meta[2] = 0;
    const unsigned header_fields[] = {0, 1, 6};
    for (unsigned i = 0; i < 3; i++) {
        unsigned position = header_fields[i];
        uint32_t old = meta[position]; meta[position] ^= 0xff;
        assert(!dm_nvn_shader_reflection_valid(meta, bytes));
        meta[position] = old;
    }
    assert(!dm_nvn_shader_reflection_valid(NULL, bytes));
    for (size_t n = 0; n < bytes; n++) assert(!dm_nvn_shader_reflection_valid(meta, n));
    assert(!dm_nvn_shader_reflection_valid(meta, bytes + 1));
    meta[17 + 1] = 0;
    assert(!dm_nvn_shader_reflection_valid(meta, bytes));
    meta[17 + 1] = 1;
    for (unsigned field = 0; field < 3; field++) {
        unsigned position = field == 0 ? 7 : 14;
        uint32_t old = meta[position];
        meta[position] = field == 0 ? 5 : field == 1 ? 0 : 129;
        assert(!dm_nvn_shader_reflection_valid(meta, bytes));
        meta[position] = old;
    }
    meta[4] = 256; meta[5] = 65536;
    for (unsigned i = 0; i < 256; i++) {
        meta[7 + i * 10 + 1] = i;
        meta[7 + i * 10 + 7] = 128;
    }
    bytes = 28 + 256 * 40 + 65536 + 128;
    assert(dm_nvn_shader_reflection_valid(meta, bytes));
    meta[4] = UINT32_MAX;
    assert(!dm_nvn_shader_reflection_valid(meta, bytes));
    meta[4] = 256; meta[5] = 65537;
    assert(!dm_nvn_shader_reflection_valid(meta, bytes));
    for (int i = 1; i < argc; i++) {
        FILE *file = fopen(argv[i], "rb"); assert(file);
        size_t length = fread(meta, 1, capacity, file);
        assert(!ferror(file) && fgetc(file) == EOF && !fclose(file));
        assert(dm_nvn_shader_reflection_valid(meta, length));
    }
    free(meta);
    puts("PASS production reflection validation: stages, sets, lengths, bindings and capacity bounds");
}
