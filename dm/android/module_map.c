// 装载过程中所有代码保持不可执行，重定位只写副本，全部成功后才依次提交段权限。
// Keep all code nonexecutable during linking, relocate copies only, and commit segment permissions after complete link success.
#include "module_map.h"
#include <stdlib.h>
#include <errno.h>
#include <string.h>

int dm_modules_unmap(dm_module_mappings *mapped) {
    if (!mapped||mapped->borrowed) {errno=EINVAL;return -1;}
    int result = 0;
    for (size_t i = 0; i < mapped->count; i++)
        if (dm_image_unmap(&mapped->mappings[i])) result = -1;
    if (!result) { free(mapped->mappings); memset(mapped, 0, sizeof(*mapped)); }
    return result;
}

dm_module_result dm_modules_map(const dm_nso_image *images, size_t count, dm_module_mappings *out) {
    return dm_modules_map_with_code(images, NULL, count, out);
}

static const dm_image_code *code_at(const dm_image_code *codes, size_t i) {
    return codes && (codes[i].offset || codes[i].size || codes[i].bytes) ? &codes[i] : NULL;
}

dm_module_result dm_modules_map_with_code(const dm_nso_image *images, const dm_image_code *codes,
                                         size_t count, dm_module_mappings *out) {
    return dm_modules_map_imports(images,codes,count,NULL,0,out);
}
static dm_module_result map_imports(const dm_nso_image *images,const dm_image_code *codes,size_t count,
    const dm_symbol_override *imports,size_t import_count,const dm_image_mapping *reserved,dm_module_mappings *out) {
    if (!images || !out || !count || count > 16) return DM_MODULE_FORMAT;
    dm_module_mappings mapped = {.count = count,.borrowed=reserved!=NULL};
    dm_nso_image *copies = calloc(count, sizeof(*copies));
    dm_module *modules = calloc(count, sizeof(*modules));
    mapped.mappings = calloc(count, sizeof(*mapped.mappings));
    dm_link_plan plan = {0};
    dm_module_result result = DM_MODULE_MEMORY;
    if (!copies || !modules || !mapped.mappings) goto done;
    for (size_t i = 0; i < count; i++) {
        if(reserved){
            size_t size,page;uint8_t *permissions=NULL;
            dm_image_result layout=dm_image_layout(&images[i],code_at(codes,i),&size,&page,&permissions);free(permissions);
            if(layout!=DM_IMAGE_OK||!reserved[i].base||(uintptr_t)reserved[i].base%page||reserved[i].size!=size||size>UINTPTR_MAX-(uintptr_t)reserved[i].base){result=DM_MODULE_MAPPING;goto done;}
            for(size_t j=0;j<i;j++)if((uintptr_t)reserved[i].base<(uintptr_t)reserved[j].base+reserved[j].size&&
                (uintptr_t)reserved[j].base<(uintptr_t)reserved[i].base+size){result=DM_MODULE_MAPPING;goto done;}
            mapped.mappings[i]=reserved[i];
        }else if (dm_image_reserve_with_code(&images[i], code_at(codes,i), &mapped.mappings[i]) != DM_IMAGE_OK) { result = DM_MODULE_MAPPING; goto done; }
        copies[i].header = images[i].header;
        copies[i].bytes = malloc(images[i].header.image_size);
        if (!copies[i].bytes) { result = DM_MODULE_MEMORY; goto done; }
        memcpy(copies[i].bytes, images[i].bytes, images[i].header.image_size);
        result = dm_module_parse(&copies[i], (uintptr_t)mapped.mappings[i].base, &modules[i]);
        if (result != DM_MODULE_OK) goto done;
    }
    result = dm_link_plan_build_imports(modules, count, imports, import_count, &plan);
    if (result != DM_MODULE_OK) goto done;
    result = dm_link_apply(modules, count, &plan);
    if (result != DM_MODULE_OK) goto done;
    for (size_t i = 0; i < count; i++) {
        if (dm_image_commit_with_code(&copies[i], code_at(codes,i), &mapped.mappings[i]) != DM_IMAGE_OK) { result = DM_MODULE_MAPPING; goto done; }
    }
    mapped.relocations = plan.count; mapped.weak_zero = plan.weak_zero; mapped.host_imports=plan.host_imports;
    *out = mapped;
    memset(&mapped, 0, sizeof(mapped));
done:
    dm_link_plan_free(&plan);
    if (copies) for (size_t i = 0; i < count; i++) dm_nso_free(&copies[i]);
    free(copies); free(modules);
    if(mapped.mappings){
        if(mapped.borrowed){
            for(size_t i=0;i<mapped.count;i++)if(mapped.mappings[i].base&&dm_image_reset_reserved(&mapped.mappings[i]))abort();
            free(mapped.mappings);
        }else dm_modules_unmap(&mapped);
    }
    return result;
}

dm_module_result dm_modules_map_imports(const dm_nso_image *images,const dm_image_code *codes,size_t count,
    const dm_symbol_override *imports,size_t import_count,dm_module_mappings *out){
    return map_imports(images,codes,count,imports,import_count,NULL,out);
}
dm_module_result dm_modules_commit_imports(const dm_nso_image *images,const dm_image_code *codes,size_t count,
    const dm_symbol_override *imports,size_t import_count,const dm_image_mapping *reserved,dm_module_mappings *out){
    if(!reserved)return DM_MODULE_FORMAT;
    return map_imports(images,codes,count,imports,import_count,reserved,out);
}
