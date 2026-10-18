// MOD0 与重定位表按小端逐字段读取；所有元数据与写目标限定在已解码 NSO 的真实段内。
// Read MOD0 / relocation fields explicitly as little endian; constrain metadata and write targets to actual decoded NSO segments.
#include "module.h"
#include <stdlib.h>
#include <string.h>

static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint64_t u64(const uint8_t *p) { return u32(p) | (uint64_t)u32(p + 4) << 32; }
static int span(const dm_nso_image *im, uint64_t off, uint64_t size, int data) {
    if (off > im->header.image_size || size > im->header.image_size - off) return 0;
    for (unsigned i = data ? 2 : 0; i < 3; i++) {
        uint64_t start = im->header.segments[i].memory_offset;
        uint64_t length = im->header.segments[i].size;
        if (data) length += im->header.bss_size;
        if (off >= start && off - start <= length && size <= length - (off - start)) return 1;
    }
    return 0;
}
static const uint8_t *symbol(const dm_module *m, uint64_t index) {
    return m->image->bytes + m->symtab + index * 24;
}
static const char *name_of(const dm_module *m, const uint8_t *s) {
    return (const char *)m->image->bytes + m->strtab + u32(s);
}

dm_module_result dm_module_parse(dm_nso_image *image, uint64_t base, dm_module *out) {
    if (!image || !image->bytes || !out || !image->header.image_size ||
        image->header.image_size > DM_NSO_MAX_IMAGE || base > UINT64_MAX - image->header.image_size)
        return DM_MODULE_BOUNDS;
    if (!span(image, 0, 8, 0)) return DM_MODULE_BOUNDS;
    uint64_t mod = u32(image->bytes + 4);
    if (!span(image, mod, 8, 0)) return DM_MODULE_BOUNDS;
    if (memcmp(image->bytes + mod, "MOD0", 4)) return DM_MODULE_FORMAT;
    int64_t dyn = (int64_t)mod + (int32_t)u32(image->bytes + mod + 4);
    if (dyn < 0 || dyn % 8) return DM_MODULE_FORMAT;
    dm_module m = {.image = image, .base = base};
    uint64_t syment = 0, relaent = 0, pltrel = 0;
    uint64_t seen = 0;
    for (uint64_t off = (uint64_t)dyn;; off += 16) {
        if (!span(image, off, 16, 0)) return DM_MODULE_BOUNDS;
        uint64_t tag = u64(image->bytes + off), value = u64(image->bytes + off + 8);
        if (!tag) break;
        uint64_t *target = NULL;
        unsigned bit = 0;
        switch (tag) {
        case 4: target = &m.hash; bit = 1; break;
        case 5: target = &m.strtab; bit = 2; break;
        case 6: target = &m.symtab; bit = 3; break;
        case 10: target = &m.strsz; bit = 4; break;
        case 11: target = &syment; bit = 5; break;
        case 7: target = &m.rela[0]; bit = 6; break;
        case 8: target = &m.relasz[0]; bit = 7; break;
        case 9: target = &relaent; bit = 8; break;
        case 23: target = &m.rela[1]; bit = 9; break;
        case 2: target = &m.relasz[1]; bit = 10; break;
        case 20: target = &pltrel; bit = 11; break;
        case 12: target = &m.init; bit = 12; break;
        case 25: target = &m.init_array; bit = 13; break;
        case 27: target = &m.init_arraysz; bit = 14; break;
        case 17: case 18: case 19: case 35: case 36: case 37:
            if (value) return DM_MODULE_UNSUPPORTED;
            break;
        default: break;
        }
        if (target) {
            if (seen & (1ull << bit)) return DM_MODULE_FORMAT;
            seen |= 1ull << bit;
            *target = value;
        }
    }
    if ((seen & 0x3e) != 0x3e || syment != 24 || !m.strsz) return DM_MODULE_FORMAT;
    if (!span(image, m.hash, 8, 0)) return DM_MODULE_BOUNDS;
    uint64_t buckets = u32(image->bytes + m.hash);
    m.nsym = u32(image->bytes + m.hash + 4);
    if (!buckets || !m.nsym) return DM_MODULE_FORMAT;
    if (!span(image, m.hash, 8 + (buckets + m.nsym) * 4, 0) ||
        !span(image, m.symtab, m.nsym * 24, 0) || !span(image, m.strtab, m.strsz, 0))
        return DM_MODULE_BOUNDS;
    for (uint64_t i = 0; i < buckets + m.nsym; i++)
        if (u32(image->bytes + m.hash + 8 + i * 4) >= m.nsym) return DM_MODULE_FORMAT;
    for (uint64_t i = 0; i < m.nsym; i++) {
        const uint8_t *s = symbol(&m, i);
        uint64_t name = u32(s);
        if (name >= m.strsz || !memchr(image->bytes + m.strtab + name, 0, (size_t)(m.strsz - name)))
            return DM_MODULE_FORMAT;
        unsigned shndx = s[6] | (unsigned)s[7] << 8;
        if (shndx && shndx != 0xfff1 &&
            !span(image, u64(s + 8), u64(s + 16), 1) && !span(image, u64(s + 8), u64(s + 16), 0))
            return DM_MODULE_BOUNDS;
    }
    for (unsigned i = 0; i < 2; i++) {
        if (!m.relasz[i]) continue;
        if (!m.rela[i] || m.relasz[i] % 24 || (i ? pltrel != 7 : relaent != 24)) return DM_MODULE_FORMAT;
        if (!span(image, m.rela[i], m.relasz[i], 0)) return DM_MODULE_BOUNDS;
    }
    if (m.init && (!span(image, m.init, 4, 0) || m.init % 4)) return DM_MODULE_BOUNDS;
    if (m.init_arraysz && (m.init_arraysz % 8 || !span(image, m.init_array, m.init_arraysz, 0)))
        return DM_MODULE_BOUNDS;
    *out = m;
    return DM_MODULE_OK;
}

static uint32_t elf_hash(const char *name) {
    uint32_t h = 0;
    while (*name) {
        h = (h << 4) + (uint8_t)*name++;
        uint32_t g = h & 0xf0000000u;
        h ^= g >> 24;
        h &= ~g;
    }
    return h;
}
static dm_module_result symbol_value(const dm_module *m, const uint8_t *s, uint64_t *value) {
    unsigned type = s[4] & 15, section = s[6] | (unsigned)s[7] << 8;
    if (type != 0 && type != 1 && type != 2) return DM_MODULE_UNSUPPORTED;
    if (section >= 0xff00 && section != 0xfff1) return DM_MODULE_UNSUPPORTED;
    uint64_t offset = u64(s + 8);
    if (section == 0xfff1) *value = offset;
    else {
        if (offset > UINT64_MAX - m->base) return DM_MODULE_BOUNDS;
        *value = m->base + offset;
    }
    return DM_MODULE_OK;
}
static dm_module_result lookup(const dm_module *m, const char *name, uint64_t *value, int *binding) {
    const uint8_t *hash = m->image->bytes + m->hash;
    uint32_t buckets = u32(hash), index = u32(hash + 8 + elf_hash(name) % buckets * 4);
    *binding = 0;
    for (uint64_t step = 0; index; step++) {
        if (step >= m->nsym) return DM_MODULE_FORMAT;
        const uint8_t *s = symbol(m, index);
        unsigned bind = s[4] >> 4, visibility = s[5] & 3;
        if ((bind == 1 || bind == 2) && (visibility == 0 || visibility == 3) &&
            (s[6] || s[7]) && !strcmp(name_of(m, s), name)) {
            *binding = (int)bind;
            return symbol_value(m, s, value);
        }
        index = u32(hash + 8 + (buckets + (uint64_t)index) * 4);
    }
    return DM_MODULE_OK;
}
static int order_writes(const void *left, const void *right) {
    const dm_link_write *a = left, *b = right;
    if (a->module != b->module) return a->module < b->module ? -1 : 1;
    return a->offset < b->offset ? -1 : a->offset != b->offset;
}

dm_module_result dm_link_plan_build(const dm_module *modules, size_t count, dm_link_plan *out) {
    return dm_link_plan_build_imports(modules,count,NULL,0,out);
}
dm_module_result dm_link_plan_build_imports(const dm_module *modules,size_t count,
    const dm_symbol_override *imports,size_t import_count,dm_link_plan *out) {
    if (!modules || !count || !out || import_count>4096 || (import_count&&!imports)) return DM_MODULE_FORMAT;
    for(size_t i=0;i<import_count;i++){
        if(!imports[i].name||!imports[i].name[0]||!imports[i].address)return DM_MODULE_FORMAT;
        for(size_t j=0;j<i;j++)if(!strcmp(imports[i].name,imports[j].name))return DM_MODULE_FORMAT;
    }
    dm_link_plan plan = {0};
    size_t capacity = 0;
    for (size_t i = 0; i < count; i++) {
        for (unsigned t = 0; t < 2; t++) {
            uint64_t n = modules[i].relasz[t] / 24;
            if (n > SIZE_MAX / sizeof(dm_link_write) - capacity) return DM_MODULE_BOUNDS;
            capacity += (size_t)n;
        }
    }
    if (capacity) {
        plan.writes = calloc(capacity, sizeof(*plan.writes));
        if (!plan.writes) return DM_MODULE_MEMORY;
    }
    dm_module_result result = DM_MODULE_OK;
    for (size_t i = 0; i < count; i++) {
        const dm_module *m = &modules[i];
        for (unsigned t = 0; t < 2; t++) for (uint64_t off = 0; off < m->relasz[t]; off += 24) {
            const uint8_t *r = m->image->bytes + m->rela[t] + off;
            uint64_t dest = u64(r), info = u64(r + 8), addend = u64(r + 16), value = 0;
            uint32_t type = (uint32_t)info, si = (uint32_t)(info >> 32);
            if (!type) continue;
            if (type != 257 && type != 1025 && type != 1026 && type != 1027) {
                result = DM_MODULE_UNSUPPORTED; goto fail;
            }
            if (!span(m->image, dest, 8, 1)) { result = DM_MODULE_BOUNDS; goto fail; }
            if (type == 1027) {
                if (si) { result = DM_MODULE_FORMAT; goto fail; }
                value = m->base;
                plan.relative++;
            } else {
                if (!si || si >= m->nsym) { result = DM_MODULE_BOUNDS; goto fail; }
                const uint8_t *s = symbol(m, si);
                if ((s[4] & 15) > 2) { result = DM_MODULE_UNSUPPORTED; goto fail; }
                if (s[6] || s[7]) {
                    result = symbol_value(m, s, &value);
                    if (result != DM_MODULE_OK) goto fail;
                } else {
                    unsigned bind = s[4] >> 4;
                    if ((bind != 1 && bind != 2) || (s[5] & 3)) { result = DM_MODULE_UNSUPPORTED; goto fail; }
                    int best = 0;
                    for(size_t j=0;j<import_count;j++)if(!strcmp(imports[j].name,name_of(m,s))){value=imports[j].address;best=1;plan.host_imports++;break;}
                    int host_import=best;
                    for (size_t j = 0; j < count&&!host_import; j++) {
                        uint64_t candidate = 0;
                        int binding = 0;
                        result = lookup(&modules[j], name_of(m, s), &candidate, &binding);
                        if (result != DM_MODULE_OK) goto fail;
                        if (binding && (!best || binding == 1)) { best = binding; value = candidate; }
                        if (best == 1) break;
                    }
                    if (!best) {
                        if (bind == 2) plan.weak_zero++;
                        else plan.unresolved++;
                    }
                }
                if (type == 1026) plan.jump++; else plan.absolute++;
            }
            if (!plan.writes || plan.count >= capacity) { result = DM_MODULE_BOUNDS; goto fail; }
            plan.writes[plan.count++] = (dm_link_write){i, dest, u64(m->image->bytes + dest), value + addend};
        }
    }
    if (plan.count) qsort(plan.writes, plan.count, sizeof(*plan.writes), order_writes);
    for (size_t i = 1; i < plan.count; i++) if (plan.writes[i - 1].module == plan.writes[i].module &&
            plan.writes[i].offset - plan.writes[i - 1].offset < 8) {
        result = DM_MODULE_FORMAT; goto fail;
    }
    *out = plan;
    return DM_MODULE_OK;
fail:
    free(plan.writes);
    return result;
}

dm_module_result dm_link_apply(const dm_module *modules, size_t count, const dm_link_plan *plan) {
    if (!modules || !count || !plan || (plan->count && !plan->writes)) return DM_MODULE_FORMAT;
    if (plan->unresolved) return DM_MODULE_UNRESOLVED;
    for (size_t i = 0; i < plan->count; i++) {
        const dm_link_write *w = &plan->writes[i];
        if (w->module >= count || !span(modules[w->module].image, w->offset, 8, 1))
            return DM_MODULE_BOUNDS;
        if (i && order_writes(w - 1, w) >= 0) return DM_MODULE_FORMAT;
        if (i && (w - 1)->module == w->module && w->offset - (w - 1)->offset < 8)
            return DM_MODULE_FORMAT;
        if (u64(modules[w->module].image->bytes + w->offset) != w->before) return DM_MODULE_STALE;
    }
    for (size_t i = 0; i < plan->count; i++) {
        const dm_link_write *w = &plan->writes[i];
        uint8_t *dest = modules[w->module].image->bytes + w->offset;
        for (unsigned b = 0; b < 8; b++) dest[b] = (uint8_t)(w->value >> (b * 8));
    }
    return DM_MODULE_OK;
}
void dm_link_plan_free(dm_link_plan *plan) {
    if (plan) { free(plan->writes); memset(plan, 0, sizeof(*plan)); }
}
const char *dm_module_error(dm_module_result result) {
    switch (result) {
    case DM_MODULE_OK: return "ok";
    case DM_MODULE_FORMAT: return "invalid MOD0 / ELF metadata";
    case DM_MODULE_BOUNDS: return "module range / address out of bounds";
    case DM_MODULE_UNSUPPORTED: return "unsupported ELF feature";
    case DM_MODULE_MEMORY: return "allocation failed";
    case DM_MODULE_UNRESOLVED: return "unresolved strong imports";
    case DM_MODULE_STALE: return "relocation target changed since planning";
    case DM_MODULE_MAPPING: return "address reservation / segment protection failed";
    }
    return "unknown module error";
}
