#include "../Sources/dm_metal_cache_guard.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void expect(int condition, const char *label) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", label);
        exit(1);
    }
}

static void make_directory(const char *path) {
    expect(!mkdir(path, 0755) || errno == EEXIST, path);
}

int main(void) {
    char root_template[] = "./dm-metal-guard-XXXXXX";
    char *root = mkdtemp(root_template);
    char absolute_root[PATH_MAX];
    char path[PATH_MAX];
    expect(root != NULL, "mkdtemp");
    expect(realpath(root, absolute_root) != NULL, "realpath fixture");
    root = absolute_root;

    snprintf(path, sizeof path, "%s/Library", root); make_directory(path);
    snprintf(path, sizeof path, "%s/Library/Caches", root); make_directory(path);
    snprintf(path, sizeof path, "%s/Library/Caches/com.apple.metal", root); make_directory(path);
    snprintf(path, sizeof path, "%s/tmp", root); make_directory(path);
    snprintf(path, sizeof path, "%s/Documents", root); make_directory(path);
    snprintf(path, sizeof path, "%s/Documents/romfs", root); make_directory(path);
    snprintf(path, sizeof path, "%s/Documents/romfs/file", root);
    FILE *file = fopen(path, "w"); expect(file != NULL, "fixture file"); fclose(file);

    char metal[PATH_MAX], documents[PATH_MAX], romfs[PATH_MAX], parent[PATH_MAX], link_path[PATH_MAX];
    snprintf(metal, sizeof metal, "%s/Library/Caches/com.apple.metal", root);
    snprintf(documents, sizeof documents, "%s/Documents", root);
    snprintf(romfs, sizeof romfs, "%s/Documents/romfs", root);
    snprintf(parent, sizeof parent, "%s/..", root);
    snprintf(link_path, sizeof link_path, "%s/Library/Caches/metal-link", root);
    expect(!symlink(documents, link_path), "symlink fixture");

    expect(dm_metal_cache_path_allowed(root, metal), "Caches/com.apple.metal allowed");
    expect(!dm_metal_cache_path_allowed(root, romfs), "Documents/romfs rejected");
    expect(!dm_metal_cache_path_allowed(root, documents), "Documents rejected");
    expect(!dm_metal_cache_path_allowed(root, root), "container root rejected");
    expect(!dm_metal_cache_path_allowed(root, parent), "parent component rejected");
    expect(!dm_metal_cache_path_allowed(root, link_path), "symlink to Documents rejected");
    char appsup[PATH_MAX];   // iOS RomFS 基底在 Library/Application Support/romfs | the iOS RomFS base is at Library/Application Support/romfs
    snprintf(appsup, sizeof appsup, "%s/Library/Application Support", root); make_directory(appsup);
    snprintf(appsup, sizeof appsup, "%s/Library/Application Support/romfs", root); make_directory(appsup);
    expect(!dm_metal_cache_path_allowed(root, appsup), "Application Support/romfs rejected");
    puts("metal cache guard: PASS (7 cases)");
    expect(!unlink(link_path), "remove fixture symlink");
    expect(!unlink(path), "remove fixture file");
    expect(!rmdir(romfs), "remove fixture romfs");
    expect(!rmdir(documents), "remove fixture Documents");
    expect(!rmdir(metal), "remove fixture metal");
    expect(!rmdir(appsup), "remove fixture appsup romfs");
    snprintf(path, sizeof path, "%s/Library/Application Support", root); expect(!rmdir(path), "remove fixture Application Support");
    snprintf(path, sizeof path, "%s/Library/Caches", root); expect(!rmdir(path), "remove fixture Caches");
    snprintf(path, sizeof path, "%s/Library", root); expect(!rmdir(path), "remove fixture Library");
    snprintf(path, sizeof path, "%s/tmp", root); expect(!rmdir(path), "remove fixture tmp");
    expect(!rmdir(root), "remove fixture container");
    return 0;
}
