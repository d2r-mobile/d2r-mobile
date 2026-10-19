// 装载真实模块并枚举跨模块符号和重定位需求；不调用缺少 HLE 的游戏入口。
// Load real modules and enumerate cross-module symbols and relocation requirements; do not call an entry missing HLE.
#include "runtime.h"
#include <elf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <android/log.h>

typedef struct {
    dm_android_module module;
    const char *name;
    Elf64_Sym *symbols;
    const char *strings;
    size_t symbol_count, string_size;
    Elf64_Rela *rela, *jmprel;
    size_t rela_count, jmprel_count;
} guest_module;

static void *at(guest_module *m, uint64_t offset, size_t bytes) {
    if (offset > m->module.size || bytes > m->module.size - offset) return NULL;
    return m->module.image + offset;
}
static int parse(guest_module *m) {
    if (m->module.size < 8) return -1;
    uint32_t mod0; memcpy(&mod0, m->module.image + 4, sizeof mod0);
    uint8_t *header = at(m, mod0, 28);
    if (!header || memcmp(header, "MOD0", 4)) return -1;
    int32_t displacement; memcpy(&displacement, header + 4, sizeof displacement);
    int64_t dynamic_offset = (int64_t)mod0 + displacement;
    if (dynamic_offset < 0) return -1;
    uint64_t symbol_offset = 0, string_offset = 0, rela_offset = 0, jmprel_offset = 0;
    size_t rela_size = 0, jmprel_size = 0;
    int ended = 0;
    for (unsigned i = 0; i < 4096; ++i) {
        Elf64_Dyn *d = at(m, dynamic_offset + i * sizeof *d, sizeof *d);
        if (!d) return -1;
        if (d->d_tag == DT_NULL) { ended = 1; break; }
        switch (d->d_tag) {
            case DT_SYMTAB: symbol_offset = d->d_un.d_ptr; break;
            case DT_STRTAB: string_offset = d->d_un.d_ptr; break;
            case DT_STRSZ: m->string_size = d->d_un.d_val; break;
            case DT_SYMENT: if (d->d_un.d_val != sizeof(Elf64_Sym)) return -1; break;
            case DT_RELA: rela_offset = d->d_un.d_ptr; break;
            case DT_RELASZ: rela_size = d->d_un.d_val; break;
            case DT_JMPREL: jmprel_offset = d->d_un.d_ptr; break;
            case DT_PLTRELSZ: jmprel_size = d->d_un.d_val; break;
        }
    }
    if (!ended || !symbol_offset || string_offset < symbol_offset ||
        (string_offset - symbol_offset) % sizeof(Elf64_Sym) || rela_size % sizeof(Elf64_Rela) || jmprel_size % sizeof(Elf64_Rela)) return -1;
    m->symbol_count = (string_offset - symbol_offset) / sizeof(Elf64_Sym);
    m->symbols = at(m, symbol_offset, m->symbol_count * sizeof(Elf64_Sym));
    m->strings = at(m, string_offset, m->string_size);
    m->rela = at(m, rela_offset, rela_size); m->rela_count = rela_size / sizeof(Elf64_Rela);
    m->jmprel = at(m, jmprel_offset, jmprel_size); m->jmprel_count = jmprel_size / sizeof(Elf64_Rela);
    if (!m->symbols || !m->strings || !m->rela || !m->jmprel) return -1;
    for (unsigned i = 0; i < m->symbol_count; ++i) {
        uint32_t offset = m->symbols[i].st_name;
        if (offset >= m->string_size || !memchr(m->strings + offset, 0, m->string_size - offset)) return -1;
    }
    return 0;
}

static int defined(guest_module *modules, unsigned count, const char *name) {
    for (unsigned i = 0; i < count; ++i) for (unsigned j = 1; j < modules[i].symbol_count; ++j)
        if (modules[i].symbols[j].st_shndx && !strcmp(name, modules[i].strings + modules[i].symbols[j].st_name)) return 1;
    return 0;
}

int dm_android_game_modules(const char *libraries, const char *data, char *report, size_t capacity) {
    guest_module modules[3] = {{.name = "main"}, {.name = "sdk"}, {.name = "subsdk0"}};
    unsigned loaded = 0; size_t symbols = 0, relocations = 0, imports = 0, missing = 0;
    char library[1024], layout[1024]; int result = -1;
    for (unsigned i = 0; i < 3; ++i) {
        snprintf(library, sizeof library, "%s/libdmguest_%s.so", libraries, modules[i].name);
        snprintf(layout, sizeof layout, "%s/%s.fixups.tsv", data, modules[i].name);
        if (dm_android_load(&modules[i].module, library, layout, report, capacity)) goto done;
        loaded++;
        if (parse(&modules[i])) { snprintf(report, capacity, "Invalid dynamic metadata: %s", modules[i].name); goto done; }
        symbols += modules[i].symbol_count;
        relocations += modules[i].rela_count + modules[i].jmprel_count;
        __android_log_print(ANDROID_LOG_INFO, "D2RAndroid", "Game module %s loaded: %zu bytes, %zu symbols, %zu relocations",
            modules[i].name, modules[i].module.size, modules[i].symbol_count, modules[i].rela_count + modules[i].jmprel_count);
    }
    for (unsigned i = 0; i < 3; ++i) for (unsigned j = 1; j < modules[i].symbol_count; ++j) {
        const Elf64_Sym *s = &modules[i].symbols[j];
        if (!s->st_shndx) {
            imports++;
            if (!defined(modules, 3, modules[i].strings + s->st_name)) missing++;
        }
    }
    snprintf(report, capacity, "Game modules: LOADED (3 modules; %zu symbols, %zu relocations, %zu imports, %zu unresolved). SDK platform services still need host replacements. Game entry remains pending in this diagnostic APK.",
             symbols, relocations, imports, missing);
    result = 0;
done:
    for (unsigned i = 0; i < loaded; ++i) dm_android_unload(&modules[i].module);
    return result;
}
