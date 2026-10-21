// #167 Minimal app-private file adapter for the accepted custom-preferences
// transaction module. It owns a duplicated private directory fd and one fixed
// file name; it does not read game saves or expose UI/JNI.
#pragma once

#include <stddef.h>
#include "custom_preferences.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DM_CP_STORE_FILE "custom_preferences.txt"
#define DM_CP_STORE_TEMP ".custom_preferences.tmp"

typedef struct dm_cp_store dm_cp_store;

// Duplicates directory_fd; the caller keeps ownership of its original fd.
// Returns NULL for an invalid descriptor or non-directory.
dm_cp_store *dm_cp_store_create(int directory_fd);
void dm_cp_store_destroy(dm_cp_store *store);

// Match dm_cp_load_fn/dm_cp_store_fn. Load returns 0 with *len == 0 for an
// absent file and resolves only a bounded regular file without following
// symlinks. A too-large file is reported through *len > DM_CP_MAX_BYTES so the
// existing transaction module returns DM_CP_LOAD_TOO_LARGE.
int dm_cp_store_load(void *ctx, char *out, size_t capacity, size_t *len);
// Save writes at most DM_CP_MAX_BYTES to a fixed same-directory temporary file,
// fsyncs it, then renameat() is the commit point. Failures before rename remove
// only the temporary file created by this call and leave the old file intact.
int dm_cp_store_save(void *ctx, const char *data, size_t len);

#ifdef __cplusplus
}
#endif
