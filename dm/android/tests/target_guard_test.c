// #234 owned target-marker fixture: exercises the actual header used by runtime.c and loader.c.
#include "../native/target_guard.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

static int write_file(const char *path, const char *data, size_t size) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    if (size && fwrite(data, 1, size, f) != size) { fclose(f); return -1; }
    return fclose(f);
}

int main(void) {
    char root[] = "/tmp/target-guard-XXXXXX";
    CHECK(mkdtemp(root) != NULL);
    char layout[512], target[512];
    snprintf(layout, sizeof layout, "%s/main.fixups.tsv", root);
    snprintf(target, sizeof target, "%s/main.target", root);
    CHECK(write_file(layout, "# PATCH_OFF 0\n", 13) == 0);
    char error[160];

    CHECK(write_file(target, "android-context-v1\n", 19) == 0);
    CHECK(dm_android_target_validate(layout, error, sizeof error) == 0);
    CHECK(write_file(target, "android-context-v1", 18) == 0);
    CHECK(dm_android_target_validate(layout, NULL, 0) == 0);

    CHECK(unlink(target) == 0);
    CHECK(dm_android_target_validate(layout, error, sizeof error) != 0);
    CHECK(strstr(error, "missing") != NULL);

    CHECK(write_file(target, "apple-context-v1\n", 17) == 0);
    CHECK(dm_android_target_validate(layout, error, sizeof error) != 0);
    CHECK(strstr(error, "wrong") != NULL);

    CHECK(write_file(target, "", 0) == 0);
    CHECK(dm_android_target_validate(layout, error, sizeof error) != 0);
    CHECK(strstr(error, "malformed") != NULL);

    CHECK(write_file(target, "android-context-v1\nextra\n", 26) == 0);
    CHECK(dm_android_target_validate(layout, error, sizeof error) != 0);
    CHECK(strstr(error, "malformed") != NULL);

    CHECK(write_file(target, "android-context-v1\n\0junk", 24) == 0);
    CHECK(dm_android_target_validate(layout, error, sizeof error) != 0);
    CHECK(strstr(error, "malformed") != NULL);

    CHECK(dm_android_target_validate("relative.fixups.tsv", error, sizeof error) != 0);
    CHECK(strstr(error, "malformed") != NULL);
    puts("PASS: valid Android target accepted; missing/wrong/malformed rejected before loading");
    return 0;
}
