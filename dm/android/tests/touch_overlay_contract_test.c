#include "touch_overlay_contract.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;
#define CHECK(x) do { if (!(x)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

static void write_file(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    if (f) { fputs(text, f); fclose(f); }
}
static void make_valid(const char *root) {
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/data", root); mkdir(path, 0700);
    snprintf(path, sizeof path, "%s/ctrl_touch", root); write_file(path, "marker\n");
    snprintf(path, sizeof path, "%s/touch_strings.tsv", root); write_file(path, "A\tABCDEF12\tB\nHiddenHint\t1234abcd\t\n");
    snprintf(path, sizeof path, "%s/data/.build.config", root); write_file(path, "config\n");
    snprintf(path, sizeof path, "%s/data/00-00000007.data", root); write_file(path, "archive\n");
}
static void remove_tree(const char *root) {
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/data/00-00000007.data", root); remove(path);
    snprintf(path, sizeof path, "%s/data/.build.config", root); remove(path);
    snprintf(path, sizeof path, "%s/ctrl_touch", root); remove(path);
    snprintf(path, sizeof path, "%s/touch_strings.tsv", root); remove(path);
    snprintf(path, sizeof path, "%s/data", root); rmdir(path);
    rmdir(root);
}

int main(void) {
    char root[] = "/tmp/dm-overlay-contract-XXXXXX";
    CHECK(mkdtemp(root) != NULL);
    char why[256] = {0};

    char missing[PATH_MAX];
    snprintf(missing, sizeof missing, "%s/missing", root);
    setenv("DM_ROMFS_OVERLAY", "/stale/should/not/survive", 1);
    CHECK(dm_touch_overlay_apply(missing, why, sizeof why) == 0); // absent path
    CHECK(getenv("DM_ROMFS_OVERLAY") == NULL);
    CHECK(why[0] == 0);

    make_valid(root);
    setenv("DM_ROMFS_OVERLAY", "/stale/should/not/survive", 1);
    CHECK(dm_touch_overlay_apply(root, why, sizeof why) == 1);
    CHECK(getenv("DM_ROMFS_OVERLAY") && !strcmp(getenv("DM_ROMFS_OVERLAY"), root));

    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/android-update.properties", root);
    write_file(path, "format=1\n");
    CHECK(dm_touch_overlay_apply(root, why, sizeof why) == 1);
    remove(path);
    snprintf(path, sizeof path, "%s/unrecognized.txt", root);
    write_file(path, "not part of the contract");
    CHECK(dm_touch_overlay_apply(root, why, sizeof why) == -1);
    CHECK(getenv("DM_ROMFS_OVERLAY") == NULL);
    remove(path);
    snprintf(path, sizeof path, "%s/touch_strings.tsv", root);
    write_file(path, "A\tNOTHEX\tB\n");
    CHECK(dm_touch_overlay_apply(root, why, sizeof why) == -1);
    CHECK(getenv("DM_ROMFS_OVERLAY") == NULL);
    CHECK(strstr(why, "touch_strings.tsv") != NULL);
    write_file(path, "A\tABCDEF12\n");
    CHECK(dm_touch_overlay_apply(root, why, sizeof why) == -1);
    CHECK(getenv("DM_ROMFS_OVERLAY") == NULL);
    write_file(path, "A\tABCDEF12\tB\textra\n");
    CHECK(dm_touch_overlay_apply(root, why, sizeof why) == -1);
    CHECK(getenv("DM_ROMFS_OVERLAY") == NULL);

    remove_tree(root);
    puts("PASS: overlay contract accepts four-file fixture and disables absent/malformed env");
    return failures ? 1 : 0;
}
