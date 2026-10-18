// 按宿主实际页大小合并段权限，拒绝同页可写且可执行；依据 POSIX mmap / mprotect 与 NSO 段布局。
// Merge segment permissions at the host's actual page size, reject writable-executable pages; based on POSIX mmap / mprotect and NSO segment layout.
#include "image_map.h"
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

dm_image_result dm_image_layout(const dm_nso_image *image, const dm_image_code *code,
                              size_t *mapped_size, size_t *page_size, uint8_t **modes_out) {
    if (!mapped_size || !page_size || !modes_out || !image || !image->bytes || !image->header.image_size ||
        image->header.image_size > DM_NSO_MAX_IMAGE) return DM_IMAGE_LAYOUT;
    long host_page = sysconf(_SC_PAGESIZE);
    if (host_page <= 0) return DM_IMAGE_OS;
    size_t page = (size_t)host_page;
    if ((page & (page - 1)) || page > DM_NSO_MAX_IMAGE) return DM_IMAGE_OS;
    size_t size = (image->header.image_size + page - 1) & ~(page - 1);
    if (code) {
        if (!code->size || code->offset % page || code->offset < size ||
            code->offset > DM_NSO_MAX_IMAGE || code->size > DM_NSO_MAX_IMAGE - code->offset)
            return DM_IMAGE_LAYOUT;
        size = (code->offset + code->size + page - 1) & ~(page - 1);
    }
    size_t count = size / page;
    uint8_t *permissions = calloc(count, 1);
    if (!permissions) return DM_IMAGE_MEMORY;
    const int modes[] = {PROT_READ | PROT_EXEC, PROT_READ, PROT_READ | PROT_WRITE};
    dm_image_result result = DM_IMAGE_OK;
    for (unsigned i = 0; i < 3; i++) {
        const dm_nso_segment *s = &image->header.segments[i];
        uint64_t length = (uint64_t)s->size + (i == 2 ? image->header.bss_size : 0);
        uint64_t end = (uint64_t)s->memory_offset + length;
        if (end > image->header.image_size) { result = DM_IMAGE_LAYOUT; break; }
        if (!length) continue;
        for (size_t p = s->memory_offset / page; p <= (end - 1) / page; p++) {
            permissions[p] |= (uint8_t)modes[i];
            if ((permissions[p] & (PROT_WRITE | PROT_EXEC)) == (PROT_WRITE | PROT_EXEC)) {
                result = DM_IMAGE_WX_CONFLICT; break;
            }
        }
        if (result != DM_IMAGE_OK) break;
    }
    if (result != DM_IMAGE_OK) { free(permissions); return result; }
    if (code) for (size_t p = code->offset / page; p < count; p++) permissions[p] = PROT_READ | PROT_EXEC;
    *mapped_size = size; *page_size = page; *modes_out = permissions;
    return DM_IMAGE_OK;
}

dm_image_result dm_image_reserve(const dm_nso_image *image, dm_image_mapping *out) {
    return dm_image_reserve_with_code(image, NULL, out);
}

dm_image_result dm_image_reserve_with_code(const dm_nso_image *image, const dm_image_code *code, dm_image_mapping *out) {
    if (!out) return DM_IMAGE_LAYOUT;
    size_t size, page;
    uint8_t *permissions = NULL;
    dm_image_result result = dm_image_layout(image, code, &size, &page, &permissions);
    if (result != DM_IMAGE_OK) return result;
    free(permissions);
    void *base = mmap(NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (base == MAP_FAILED) return DM_IMAGE_OS;
    *out = (dm_image_mapping){.base = base, .size = size};
    return DM_IMAGE_OK;
}

dm_image_result dm_image_commit(const dm_nso_image *image, dm_image_mapping *reserved) {
    return dm_image_commit_with_code(image, NULL, reserved);
}

dm_image_result dm_image_commit_with_code(const dm_nso_image *image, const dm_image_code *code, dm_image_mapping *reserved) {
    if (!reserved || !reserved->base || (code && !code->bytes)) return DM_IMAGE_LAYOUT;
    size_t size, page;
    uint8_t *permissions = NULL;
    dm_image_result result = dm_image_layout(image, code, &size, &page, &permissions);
    if (result != DM_IMAGE_OK) return result;
    if (size != reserved->size || (uintptr_t)reserved->base % page) { free(permissions); return DM_IMAGE_LAYOUT; }
    uint8_t *base = reserved->base;
    if (mprotect(base, size, PROT_READ | PROT_WRITE)) { free(permissions); return DM_IMAGE_OS; }
    memset(base, 0, size);
    memcpy(base, image->bytes, image->header.image_size);
    if (code) memcpy(base + code->offset, code->bytes, code->size);
    __builtin___clear_cache((char *)base, (char *)base + size);
    if (mprotect(base, size, PROT_NONE)) result = DM_IMAGE_OS;
    for (size_t first = 0, count = size / page; first < count && result == DM_IMAGE_OK;) {
        size_t last = first + 1;
        while (last < count && permissions[last] == permissions[first]) last++;
        if (permissions[first] && mprotect(base + first * page, (last - first) * page, permissions[first]))
            result = DM_IMAGE_OS;
        first = last;
    }
    free(permissions);
    if (result != DM_IMAGE_OK) (void)mprotect(base, size, PROT_NONE);
    return result;
}

dm_image_result dm_image_map(const dm_nso_image *image, dm_image_mapping *out) {
    if (!out) return DM_IMAGE_LAYOUT;
    dm_image_mapping mapping = {0};
    dm_image_result result = dm_image_reserve(image, &mapping);
    if (result != DM_IMAGE_OK) return result;
    result = dm_image_commit(image, &mapping);
    if (result != DM_IMAGE_OK) { dm_image_unmap(&mapping); return result; }
    *out = mapping;
    return DM_IMAGE_OK;
}

int dm_image_unmap(dm_image_mapping *mapping) {
    if (mapping->base && munmap(mapping->base, mapping->size)) return -1;
    memset(mapping, 0, sizeof *mapping);
    return 0;
}

int dm_image_reset_reserved(const dm_image_mapping *mapping){
    long page=sysconf(_SC_PAGESIZE);
    if(!mapping||!mapping->base||!mapping->size||page<=0||(uintptr_t)mapping->base%(size_t)page||
       mapping->size%(size_t)page||mapping->size>UINTPTR_MAX-(uintptr_t)mapping->base){errno=EINVAL;return -1;}
    return mmap(mapping->base,mapping->size,PROT_NONE,MAP_PRIVATE|MAP_ANON|MAP_FIXED,-1,0)==MAP_FAILED?-1:0;
}
