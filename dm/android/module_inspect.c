// 真实模块离线链接检查；依据 module.h，不执行客体、不写游戏文件、不把分析地址当作可运行映像。
// Offline link inspection of real modules; based on module.h, never runs guests or writes game files; analysis addresses are not runnable mappings.
#include "module.h"
#include "module_map.h"
#include "runtime/romfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    const char *program = argv[0];
    int map=0,romfs=0;
    while(argc>1&&(!strcmp(argv[1],"--map")||!strcmp(argv[1],"--romfs-imports"))){
        if(!strcmp(argv[1],"--map"))map=1;else romfs=1;argc--;argv++;
    }
    if (argc < 2 || argc > 17) { fprintf(stderr, "Usage: %s [--map] [--romfs-imports] <ExeFS/module> [more modules, max 16]\n", program); return 2; }
    size_t count = (size_t)argc - 1;
    dm_nso_image images[16] = {0};
    dm_module modules[16] = {0};
    dm_link_plan plan = {0};
    dm_module_mappings mapped = {0};
    int status = 1;
    for (size_t i = 0; i < count; i++) {
        dm_nso_result read = dm_nso_read(argv[i + 1], &images[i]);
        if (read != DM_NSO_OK) { fprintf(stderr, "%s: %s\n", argv[i + 1], dm_nso_error(read)); goto done; }
        dm_module_result parsed = dm_module_parse(&images[i], 0x100000000ull + i * 0x40000000ull, &modules[i]);
        if (parsed != DM_MODULE_OK) { fprintf(stderr, "%s: %s\n", argv[i + 1], dm_module_error(parsed)); goto done; }
        printf("%s: symbols=%llu rela=%llu plt=%llu init=0x%llx init_array=%llu\n", argv[i + 1],
               (unsigned long long)modules[i].nsym, (unsigned long long)(modules[i].relasz[0] / 24),
               (unsigned long long)(modules[i].relasz[1] / 24), (unsigned long long)modules[i].init,
               (unsigned long long)(modules[i].init_arraysz / 8));
    }
    size_t import_count=0;const dm_symbol_override*imports=romfs?dm_romfs_imports(&import_count):NULL;
    dm_module_result linked = dm_link_plan_build_imports(modules, count, imports, import_count, &plan);
    if (linked != DM_MODULE_OK) { fprintf(stderr, "Link planning: %s\n", dm_module_error(linked)); goto done; }
    printf("Plan: writes=%zu relative=%zu abs/glob=%zu jump=%zu weak_zero=%zu unresolved_strong=%zu host_import_writes=%zu\n",
           plan.count, plan.relative, plan.absolute, plan.jump, plan.weak_zero, plan.unresolved,plan.host_imports);
    if (map && !plan.unresolved) {
        linked = dm_modules_map_imports(images,NULL,count,imports,import_count,&mapped);
        if (linked != DM_MODULE_OK) { fprintf(stderr, "Mapping: %s\n", dm_module_error(linked)); goto done; }
        printf("Mapped %zu modules at actual reserved addresses; applied %zu relocations; protected RX/R/RW segments.\n",
               mapped.count, mapped.relocations);
    }
    puts("Inspection only: no file writes, initializers or guest instructions executed.");
    status = plan.unresolved ? 3 : 0;
done:
    if (dm_modules_unmap(&mapped)) { fprintf(stderr, "Module unmap failed\n"); status = 1; }
    dm_link_plan_free(&plan);
    for (size_t i = 0; i < count; i++) dm_nso_free(&images[i]);
    return status;
}
