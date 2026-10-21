// #243 owned asset/metadata fixture: exercise the actual APK-asset preparation
// helper and the real loader target guard. No game module is loaded.
#include "../native/module_metadata.h"
#include "../native/target_guard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)
#define TARGET "android-context-v1\n"
#define FIXUPS "# TEXT_END 0\n# PATCH_OFF 0x8000\n# KEY_PAGE_OFF 0x4000\n"

static int write_file(const char *path, const char *data) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t n = strlen(data);
    if (fwrite(data, 1, n, f) != n) { fclose(f); return -1; }
    return fclose(f);
}

static int read_file(const char *path, char *out, size_t cap) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(out, 1, cap - 1, f);
    int extra = fgetc(f);
    fclose(f);
    if (extra != EOF) return -1;
    out[n] = 0;
    return 0;
}

static int copy_fixture(void *context, const char *directory, const char *name) {
    const char *source = (const char *)context;
    char from[512], to[512], data[1024];
    if (snprintf(from, sizeof from, "%s/%s", source, name) <= 0 ||
        snprintf(to, sizeof to, "%s/%s", directory, name) <= 0) return -1;
    if (read_file(from, data, sizeof data) != 0) return -1;
    return write_file(to, data);
}

static int copied(const char *directory, const char *module) {
    static const char *parts[] = {"fixups.tsv", "gates.tsv", "target"};
    char path[512], data[1024], expected[1024];
    for (unsigned i = 0; i < 3; ++i) {
        if (snprintf(path, sizeof path, "%s/%s.%s", directory, module, parts[i]) <= 0) return 0;
        if (read_file(path, data, sizeof data) != 0) return 0;
        if (i == 0) snprintf(expected, sizeof expected, "%s", FIXUPS);
        else if (i == 1) snprintf(expected, sizeof expected, "%s", "gates\n");
        else snprintf(expected, sizeof expected, "%s", TARGET);
        if (strcmp(data, expected)) return 0;
    }
    return 1;
}

int main(void) {
    char root[] = "/tmp/module-metadata-XXXXXX";
    CHECK(mkdtemp(root) != NULL);
    char src[512], out[512];
    snprintf(src, sizeof src, "%s/src", root);
    snprintf(out, sizeof out, "%s/out", root);
    CHECK(mkdir(src, 0700) == 0 && mkdir(out, 0700) == 0);

    const char *modules[] = {"main", "sdk", "subsdk0"};
    for (unsigned m = 0; m < 3; ++m) {
        char path[512];
        snprintf(path, sizeof path, "%s/%s.fixups.tsv", src, modules[m]);
        CHECK(write_file(path, FIXUPS) == 0);
        snprintf(path, sizeof path, "%s/%s.gates.tsv", src, modules[m]);
        CHECK(write_file(path, "gates\n") == 0);
        snprintf(path, sizeof path, "%s/%s.target", src, modules[m]);
        CHECK(write_file(path, TARGET) == 0);
        CHECK(dm_copy_module_metadata(out, modules[m], copy_fixture, src) == 0);
        CHECK(copied(out, modules[m]));
        snprintf(path, sizeof path, "%s/%s.fixups.tsv", out, modules[m]);
        char error[160];
        CHECK(dm_android_target_validate(path, error, sizeof error) == 0);
    }

    // Missing per-module marker: the actual preparation helper fails before any
    // guest load, even though another module's marker is present.
    char path[512];
    snprintf(path, sizeof path, "%s/sdk.target", src);
    CHECK(unlink(path) == 0);
    CHECK(dm_copy_module_metadata(out, "sdk", copy_fixture, src) != 0);
    snprintf(path, sizeof path, "%s/sdk.fixups.tsv", out);
    char error[160];
    CHECK(dm_android_target_validate(path, error, sizeof error) != 0);
    CHECK(strstr(error, "missing marker") != NULL);

    // Wrong marker must also fail at the real target guard; it must not fall
    // back to main.target or a stale marker.
    snprintf(path, sizeof path, "%s/subsdk0.target", src);
    CHECK(write_file(path, "apple-context-v1\n") == 0);
    snprintf(path, sizeof path, "%s/subsdk0.target", out);
    CHECK(unlink(path) == 0);
    CHECK(dm_copy_module_metadata(out, "subsdk0", copy_fixture, src) == 0);
    snprintf(path, sizeof path, "%s/subsdk0.fixups.tsv", out);
    CHECK(dm_android_target_validate(path, error, sizeof error) != 0);
    CHECK(strstr(error, "wrong marker") != NULL);

    puts("PASS: per-module fixups/gates/target copied; missing and wrong markers fail before guest load");
    return 0;
}
