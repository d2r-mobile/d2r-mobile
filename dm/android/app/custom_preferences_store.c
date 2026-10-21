#include "custom_preferences_store.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

struct dm_cp_store {
    int dirfd;
};

#ifdef DM_CP_STORE_TEST_FAULT
extern int dm_cp_store_test_fail_after_fsync(void);
#endif

static int valid_store(const dm_cp_store *store) {
    return store && store->dirfd >= 0;
}

dm_cp_store *dm_cp_store_create(int directory_fd) {
    if (directory_fd < 0) return NULL;
    struct stat st;
    if (fstat(directory_fd, &st) != 0 || !S_ISDIR(st.st_mode)) return NULL;
    int copy = fcntl(directory_fd, F_DUPFD_CLOEXEC, 0);
    if (copy < 0) return NULL;
    dm_cp_store *store = malloc(sizeof *store);
    if (!store) {
        close(copy);
        return NULL;
    }
    store->dirfd = copy;
    return store;
}

void dm_cp_store_destroy(dm_cp_store *store) {
    if (!store) return;
    if (store->dirfd >= 0) close(store->dirfd);
    free(store);
}

int dm_cp_store_load(void *ctx, char *out, size_t capacity, size_t *len) {
    dm_cp_store *store = ctx;
    if (!valid_store(store) || !out || capacity == 0 || !len) return 1;
    int fd = openat(store->dirfd, DM_CP_STORE_FILE,
                    O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        if (errno == ENOENT) {
            *len = 0;
            return 0;
        }
        return 1;
    }
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0) {
        close(fd);
        return 1;
    }
    if ((uintmax_t)st.st_size > DM_CP_MAX_BYTES) {
        close(fd);
        *len = DM_CP_MAX_BYTES + 1u;
        return 0;
    }
    size_t size = (size_t)st.st_size;
    if (size > capacity) {
        close(fd);
        return 1;
    }
    size_t used = 0;
    while (used < size) {
        ssize_t got = read(fd, out + used, size - used);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) {
            close(fd);
            return 1;
        }
        used += (size_t)got;
    }
    if (close(fd) != 0) return 1;
    *len = used;
    return 0;
}

static void remove_own_temp(dm_cp_store *store) {
    (void)unlinkat(store->dirfd, DM_CP_STORE_TEMP, 0);
}

int dm_cp_store_save(void *ctx, const char *data, size_t len) {
    dm_cp_store *store = ctx;
    if (!valid_store(store) || len > DM_CP_MAX_BYTES || (len && !data)) return 1;

    /* A private directory is serialized by its single owning control thread.
       Refuse an existing destination symlink/non-regular object before creating
       a temp; this is not an adversarial concurrent-writer framework. */
    struct stat destination;
    if (fstatat(store->dirfd, DM_CP_STORE_FILE, &destination, AT_SYMLINK_NOFOLLOW) == 0) {
        if (!S_ISREG(destination.st_mode)) return 1;
    } else if (errno != ENOENT) {
        return 1;
    }

    int fd = openat(store->dirfd, DM_CP_STORE_TEMP,
                    O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) return 1;   /* a stale/symlink temp is an unknown collision */

    size_t written = 0;
    while (written < len) {
        ssize_t put = write(fd, data + written, len - written);
        if (put < 0 && errno == EINTR) continue;
        if (put <= 0) {
            close(fd);
            remove_own_temp(store);
            return 1;
        }
        written += (size_t)put;
    }
    if (fsync(fd) != 0) {
        close(fd);
        remove_own_temp(store);
        return 1;
    }
#ifdef DM_CP_STORE_TEST_FAULT
    if (dm_cp_store_test_fail_after_fsync()) {
        close(fd);
        remove_own_temp(store);
        return 1;
    }
#endif
    if (close(fd) != 0) {
        remove_own_temp(store);
        return 1;
    }
    /* Atomic commit point. No failure is reported after this rename. */
    if (renameat(store->dirfd, DM_CP_STORE_TEMP, store->dirfd, DM_CP_STORE_FILE) != 0) {
        remove_own_temp(store);
        return 1;
    }
    return 0;
}
