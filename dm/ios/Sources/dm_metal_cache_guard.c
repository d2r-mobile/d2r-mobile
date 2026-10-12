#include "dm_metal_cache_guard.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int has_parent_component(const char *path) {
    const char *cursor = path;
    while (*cursor) {
        while (*cursor == '/') cursor++;
        const char *start = cursor;
        while (*cursor && *cursor != '/') cursor++;
        if ((size_t)(cursor - start) == 2 && !strncmp(start, "..", 2)) return 1;
    }
    return 0;
}

static int under(const char *path, const char *directory) {
    size_t length = strlen(directory);
    return strlen(path) > length && !strncmp(path, directory, length) && path[length] == '/';
}

int dm_metal_cache_path_allowed(const char *container_root, const char *target_path) {
    char root[PATH_MAX], target[PATH_MAX], caches[PATH_MAX], temporary[PATH_MAX];
    if (!container_root || !target_path || container_root[0] != '/' || target_path[0] != '/' ||
        has_parent_component(container_root) || has_parent_component(target_path)) return 0;
    if (!realpath(container_root, root) || !realpath(target_path, target)) return 0;
    if (!strcmp(root, target)) return 0;
    size_t prefix_length = strlen(container_root);
    while (prefix_length > 1 && container_root[prefix_length - 1] == '/') prefix_length--;
    if (strlen(target_path) <= prefix_length || strncmp(target_path, container_root, prefix_length)) return 0;
    const char *relative = target_path + prefix_length;
    if (strncmp(relative, "/Library/Caches/", strlen("/Library/Caches/")) &&
        strncmp(relative, "/tmp/", strlen("/tmp/"))) return 0;
    int caches_length = snprintf(caches, sizeof caches, "%s/Library/Caches", root);
    int temporary_length = snprintf(temporary, sizeof temporary, "%s/tmp", root);
    if (caches_length < 0 || (size_t)caches_length >= sizeof caches ||
        temporary_length < 0 || (size_t)temporary_length >= sizeof temporary) return 0;
    return under(target, caches) || under(target, temporary);
}
