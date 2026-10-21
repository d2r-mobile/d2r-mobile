// NSO 文件适配器：先限大小再读取，随后交给统一解析边界；依据 nso.h 的所有权约定。
// NSO file adapter: bound file size before reading, then use the shared parse boundary; follows nso.h ownership rules.
#include "nso.h"
#include <stdio.h>
#include <stdlib.h>

dm_nso_result dm_nso_read(const char *path, dm_nso_image *out) {
    if (!path || !out) return DM_NSO_HEADER;
    FILE *f = fopen(path, "rb");
    if (!f) return DM_NSO_IO;
    dm_nso_result result = DM_NSO_IO;
    uint8_t *file = NULL;
    if (fseek(f, 0, SEEK_END)) goto done;
    long size = ftell(f);
    if (size < 0) goto done;
    if ((unsigned long)size > DM_NSO_MAX_IMAGE) { result = DM_NSO_LIMIT; goto done; }
    if (size < DM_NSO_HEADER_SIZE) { result = DM_NSO_HEADER; goto done; }
    if (fseek(f, 0, SEEK_SET)) goto done;
    file = malloc((size_t)size);
    if (!file) { result = DM_NSO_MEMORY; goto done; }
    if (fread(file, 1, (size_t)size, f) != (size_t)size) goto done;
    result = dm_nso_decode(file, (size_t)size, out);
done:
    free(file);
    fclose(f);
    return result;
}
