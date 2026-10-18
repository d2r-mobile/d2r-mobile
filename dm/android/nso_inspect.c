// 原生 NSO 检查入口；只输出元信息，不执行、导出或修改游戏程序，依据 nso.h。
// Native NSO inspection entry; print metadata only, never execute, export or modify game programs; based on nso.h.
#include "nso.h"
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "Usage: %s <ExeFS/main> [ExeFS/sdk ExeFS/subsdk0]\n", argv[0]); return 2; }
    int failed = 0;
    const char *names[] = {"text", "rodata", "data"};
    for (int i = 1; i < argc; i++) {
        dm_nso_image image = {0};
        dm_nso_result result = dm_nso_read(argv[i], &image);
        if (result != DM_NSO_OK) {
            fprintf(stderr, "%s: %s\n", argv[i], dm_nso_error(result)); failed = 1; continue;
        }
        printf("%s: NSO0 image=%zu bss=%u flags=0x%x build_id=", argv[i],
            image.header.image_size, image.header.bss_size, image.header.flags);
        for (unsigned b = 0; b < 32; b++) printf("%02x", image.header.build_id[b]);
        putchar('\n');
        for (unsigned s = 0; s < 3; s++) {
            const dm_nso_segment *seg = &image.header.segments[s];
            printf("  %s offset=0x%x size=%u stored=%u sha256=%s\n", names[s],
                seg->memory_offset, seg->size, seg->stored_size,
                image.header.flags & (1u << (s + 3)) ? "verified" : "not supplied");
        }
        puts("  Decoded as data only; no guest code executed.");
        dm_nso_free(&image);
    }
    return failed;
}
