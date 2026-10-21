#define _GNU_SOURCE
// #234 owned caller fixture: dm_android_load must reject the target marker before dlopen/map.
#include "../native/runtime.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

static int write_file(const char *path, const char *data, size_t size) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    if (size && fwrite(data, 1, size, f) != size) { fclose(f); return -1; }
    return fclose(f);
}
void dm_android_context_entry(void) {}

int main(void) {
    char root[128];
    snprintf(root, sizeof root, "/tmp/target-caller-%ld", (long)getpid());
    CHECK(mkdir(root, 0700) == 0);
    char layout[512], target[512], error[256];
    snprintf(layout, sizeof layout, "%s/main.fixups.tsv", root);
    snprintf(target, sizeof target, "%s/main.target", root);
    CHECK(write_file(layout, "# TEXT_END 0\n# PATCH_OFF 0x8000\n# KEY_PAGE_OFF 0x4000\n", 57) == 0);

    dm_android_module m;
    CHECK(dm_android_load(&m, "/nonexistent/lib.so", layout, error, sizeof error) != 0);
    CHECK(strstr(error, "target: missing marker") != NULL);

    CHECK(write_file(target, "apple-context-v1\n", 17) == 0);
    CHECK(dm_android_load(&m, "/nonexistent/lib.so", layout, error, sizeof error) != 0);
    CHECK(strstr(error, "target: wrong marker") != NULL);

    CHECK(write_file(target, "android-context-v1\n", 19) == 0);
    CHECK(dm_android_load(&m, "/nonexistent/lib.so", layout, error, sizeof error) != 0);
    CHECK(strstr(error, "dlopen") != NULL);   // target gate passed; reached the next boundary

    // Mixed module outputs: a valid main.target must not authorize sdk.fixups.tsv.
    char sdk_layout[512], sdk_target[512];
    snprintf(sdk_layout, sizeof sdk_layout, "%s/sdk.fixups.tsv", root);
    snprintf(sdk_target, sizeof sdk_target, "%s/sdk.target", root);
    CHECK(write_file(sdk_layout, "# TEXT_END 0\n# PATCH_OFF 0x8000\n# KEY_PAGE_OFF 0x4000\n", 57) == 0);
    CHECK(write_file(sdk_target, "apple-context-v1\n", 17) == 0);
    CHECK(dm_android_load(&m, "/nonexistent/lib.so", sdk_layout, error, sizeof error) != 0);
    CHECK(strstr(error, "target: wrong marker") != NULL);
    puts("PASS: module-specific target markers; mixed Android/Apple module is rejected");
    return 0;
}
