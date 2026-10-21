// APK-asset metadata preparation shared by the Android activity boot path.
// The callback copies exactly one named asset; it must not synthesize a marker
// or substitute a marker from another module.
#pragma once
#include <stdio.h>
#include <errno.h>

typedef int (*dm_metadata_copy_fn)(void *context, const char *directory, const char *name);

static inline int dm_copy_module_metadata(const char *directory, const char *module,
                                          dm_metadata_copy_fn copy, void *context) {
    static const char *parts[] = {"fixups.tsv", "gates.tsv", "target"};
    for (unsigned i = 0; i < sizeof parts / sizeof parts[0]; ++i) {
        char name[96], path[1024];
        int name_len = snprintf(name, sizeof name, "%s.%s", module, parts[i]);
        int path_len = snprintf(path, sizeof path, "%s/%s", directory, name);
        if (name_len <= 0 || (size_t)name_len >= sizeof name ||
            path_len <= 0 || (size_t)path_len >= sizeof path) return -1;
        // Remove any destination leftover first: a missing source asset must
        // fail instead of silently reusing a marker from an older package.
        if (remove(path) != 0 && errno != ENOENT) return -1;
        if (copy(context, directory, name) != 0) return -1;
    }
    return 0;
}
