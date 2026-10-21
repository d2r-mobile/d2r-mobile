// #167 host tests for the real custom-preferences file adapter and the accepted
// dm_cp_apply/dm_cp_load transaction module.
#include "../app/custom_preferences_store.h"
#include "../app/custom_preferences.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int checks;
#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        return 1; \
    } \
} while (0)

#ifdef DM_CP_STORE_TEST_FAULT
static int fail_after_fsync;
int dm_cp_store_test_fail_after_fsync(void) { return fail_after_fsync; }
#endif

static int make_dir(char path[]) {
    return mkdtemp(path) != NULL;
}

static int path_exists(int dirfd, const char *name) {
    struct stat st;
    return fstatat(dirfd, name, &st, AT_SYMLINK_NOFOLLOW) == 0;
}

static int read_file(int dirfd, const char *name, char *out, size_t capacity, size_t *len) {
    int fd = openat(dirfd, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return 0;
    ssize_t n = read(fd, out, capacity);
    close(fd);
    if (n < 0) return 0;
    *len = (size_t)n;
    return 1;
}

static int write_file(int dirfd, const char *name, const char *data, size_t len) {
    int fd = openat(dirfd, name, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) return 0;
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, data + off, len - off);
        if (n <= 0) { close(fd); return 0; }
        off += (size_t)n;
    }
    close(fd);
    return 1;
}

static int test_roundtrip_and_ownership(void) {
    char path_a[] = "/tmp/dm-cp-store-a-XXXXXX";
    CHECK(make_dir(path_a));
    int fd = open(path_a, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    CHECK(fd >= 0);
    dm_cp_store *store = dm_cp_store_create(fd);
    CHECK(store != NULL);
    close(fd);   // adapter owns its duplicate
    dm_cp_instance p, q;
    dm_cp_init(&p);
    dm_cp_init(&q);
    CHECK(dm_cp_set(&p, "eco_fps", 1) == DM_CP_OK);
    CHECK(dm_cp_set(&p, "auto_mana", 0) == DM_CP_OK);
    CHECK(dm_cp_apply(&p, dm_cp_store_save, store) == DM_CP_OK);
    char file[DM_CP_MAX_BYTES + 1];
    size_t file_len = 0;
    int dirfd = open(path_a, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    CHECK(dirfd >= 0);
    CHECK(read_file(dirfd, DM_CP_STORE_FILE, file, sizeof file, &file_len));
    CHECK(file_len > 0 && strstr(file, "eco_fps=1\n") != NULL);
    CHECK(!path_exists(dirfd, DM_CP_STORE_TEMP));
    CHECK(dm_cp_load(&q, dm_cp_store_load, store) == DM_CP_OK);
    CHECK(dm_cp_get(&q, "eco_fps") == 1 && dm_cp_get(&q, "auto_mana") == 0);
    close(dirfd);
    dm_cp_store_destroy(store);

    // A restarted adapter loads the committed file.
    dirfd = open(path_a, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    CHECK(dirfd >= 0);
    dm_cp_store *restart = dm_cp_store_create(dirfd);
    close(dirfd);
    CHECK(restart != NULL);
    dm_cp_instance r;
    dm_cp_init(&r);
    CHECK(dm_cp_load(&r, dm_cp_store_load, restart) == DM_CP_OK);
    CHECK(dm_cp_get(&r, "eco_fps") == 1 && dm_cp_get(&r, "auto_mana") == 0);
    dm_cp_store_destroy(restart);
    char path[512];
    snprintf(path, sizeof path, "%s/%s", path_a, DM_CP_STORE_FILE);
    unlink(path);
    snprintf(path, sizeof path, "%s/%s", path_a, DM_CP_STORE_TEMP);
    unlink(path);
    rmdir(path_a);
    return 0;
}

static int test_missing_and_invalid_inputs(void) {
    char path_a[] = "/tmp/dm-cp-store-b-XXXXXX";
    CHECK(make_dir(path_a));
    int fd = open(path_a, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    CHECK(fd >= 0);
    dm_cp_store *store = dm_cp_store_create(fd);
    CHECK(store != NULL);
    CHECK(dm_cp_store_create(-1) == NULL);
    int regular = openat(fd, "ordinary", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    CHECK(regular >= 0);
    CHECK(dm_cp_store_create(regular) == NULL);
    close(regular);

    dm_cp_instance p;
    dm_cp_init(&p);
    CHECK(dm_cp_load(&p, dm_cp_store_load, store) == DM_CP_OK);
    CHECK(dm_cp_get(&p, "eco_fps") == 0 && p.loaded == 1);   // missing file = defaults

    char data[DM_CP_MAX_BYTES + 1];
    memset(data, 'x', sizeof data);
    CHECK(write_file(fd, DM_CP_STORE_FILE, data, sizeof data));
    dm_cp_instance q;
    dm_cp_init(&q);
    CHECK(dm_cp_load(&q, dm_cp_store_load, store) == DM_CP_LOAD_TOO_LARGE);
    CHECK(dm_cp_get(&q, "eco_fps") == 0);
    unlinkat(fd, DM_CP_STORE_FILE, 0);

    // A FIFO at the data name must be rejected without blocking.
    char fifo_path[512];
    snprintf(fifo_path, sizeof fifo_path, "%s/%s", path_a, DM_CP_STORE_FILE);
    CHECK(mkfifo(fifo_path, 0600) == 0);
    dm_cp_instance fifo;
    dm_cp_init(&fifo);
    CHECK(dm_cp_load(&fifo, dm_cp_store_load, store) == DM_CP_LOAD_IO);
    CHECK(dm_cp_get(&fifo, "eco_fps") == 0);
    unlink(fifo_path);

    // Symlink at the data path is not followed.
    int target = openat(fd, "target", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    CHECK(target >= 0);
    close(target);
    CHECK(symlinkat("target", fd, DM_CP_STORE_FILE) == 0);
    dm_cp_instance s;
    dm_cp_init(&s);
    CHECK(dm_cp_load(&s, dm_cp_store_load, store) == DM_CP_LOAD_IO);
    CHECK(dm_cp_get(&s, "eco_fps") == 0);
    CHECK(unlinkat(fd, DM_CP_STORE_FILE, 0) == 0);
    CHECK(unlinkat(fd, "ordinary", 0) == 0);

    close(fd);
    dm_cp_store_destroy(store);
    char path[512];
    snprintf(path, sizeof path, "%s/target", path_a);
    unlink(path);
    rmdir(path_a);
    return 0;
}

static int test_pre_rename_failure_and_retry(void) {
    char path_a[] = "/tmp/dm-cp-store-c-XXXXXX";
    CHECK(make_dir(path_a));
    int fd = open(path_a, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    CHECK(fd >= 0);
    dm_cp_store *store = dm_cp_store_create(fd);
    CHECK(store != NULL);
    dm_cp_instance p;
    dm_cp_init(&p);
    CHECK(dm_cp_set(&p, "eco_fps", 1) == DM_CP_OK);
    CHECK(dm_cp_apply(&p, dm_cp_store_save, store) == DM_CP_OK);
    char before[DM_CP_MAX_BYTES + 1], after[DM_CP_MAX_BYTES + 1];
    size_t before_len = 0, after_len = 0;
    CHECK(read_file(fd, DM_CP_STORE_FILE, before, sizeof before, &before_len));

    // Unknown temp collision must fail without changing the committed file.
    CHECK(mkdirat(fd, DM_CP_STORE_TEMP, 0700) == 0);
    CHECK(dm_cp_set(&p, "q3d", 1) == DM_CP_OK);
    CHECK(dm_cp_apply(&p, dm_cp_store_save, store) == DM_CP_SAVE_FAILED);
    CHECK(dm_cp_pending(&p, "q3d") == 1);
    CHECK(read_file(fd, DM_CP_STORE_FILE, after, sizeof after, &after_len));
    CHECK(before_len == after_len && !memcmp(before, after, before_len));
    CHECK(unlinkat(fd, DM_CP_STORE_TEMP, AT_REMOVEDIR) == 0);

    // A symlink at the temp path is also an unknown collision.
    CHECK(symlinkat(DM_CP_STORE_FILE, fd, DM_CP_STORE_TEMP) == 0);
    CHECK(dm_cp_apply(&p, dm_cp_store_save, store) == DM_CP_SAVE_FAILED);
    CHECK(dm_cp_pending(&p, "q3d") == 1);
    CHECK(unlinkat(fd, DM_CP_STORE_TEMP, 0) == 0);

    // Retry commits exactly once; temp is gone.
    CHECK(dm_cp_apply(&p, dm_cp_store_save, store) == DM_CP_OK);
    CHECK(dm_cp_get(&p, "q3d") == 1);
    CHECK(read_file(fd, DM_CP_STORE_FILE, after, sizeof after, &after_len));
    CHECK(after_len > 0 && strstr(after, "q3d=1\n") != NULL);
    CHECK(!path_exists(fd, DM_CP_STORE_TEMP));

    // Existing destination symlink must be refused, leaving link and target.
    CHECK(unlinkat(fd, DM_CP_STORE_FILE, 0) == 0);
    CHECK(write_file(fd, "destination_target", "target-data", 11));
    CHECK(symlinkat("destination_target", fd, DM_CP_STORE_FILE) == 0);
    CHECK(dm_cp_set(&p, "auto_mana", 0) == DM_CP_OK);
    CHECK(dm_cp_apply(&p, dm_cp_store_save, store) == DM_CP_SAVE_FAILED);
    CHECK(dm_cp_pending(&p, "auto_mana") == 0);
    struct stat link_st;
    CHECK(fstatat(fd, DM_CP_STORE_FILE, &link_st, AT_SYMLINK_NOFOLLOW) == 0 && S_ISLNK(link_st.st_mode));
    char target_data[32];
    size_t target_len = 0;
    CHECK(read_file(fd, "destination_target", target_data, sizeof target_data, &target_len));
    CHECK(target_len == 11 && !memcmp(target_data, "target-data", 11));
    CHECK(!path_exists(fd, DM_CP_STORE_TEMP));
    CHECK(unlinkat(fd, DM_CP_STORE_FILE, 0) == 0);
    CHECK(dm_cp_apply(&p, dm_cp_store_save, store) == DM_CP_OK);
    CHECK(dm_cp_get(&p, "auto_mana") == 0);
    CHECK(unlinkat(fd, "destination_target", 0) == 0);

    char big[DM_CP_MAX_BYTES + 1];
    memset(big, 'y', sizeof big);
    CHECK(dm_cp_store_save(store, big, sizeof big) != 0);
    CHECK(dm_cp_store_save(store, NULL, 1) != 0);

    close(fd);
    dm_cp_store_destroy(store);
    char path[512];
    snprintf(path, sizeof path, "%s/%s", path_a, DM_CP_STORE_FILE);
    unlink(path);
    rmdir(path_a);
    return 0;
}

static int test_independent_directories(void) {
    char path_a[] = "/tmp/dm-cp-store-d-XXXXXX";
    char path_b[] = "/tmp/dm-cp-store-e-XXXXXX";
    CHECK(make_dir(path_a));
    CHECK(make_dir(path_b));
    int a = open(path_a, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    int b = open(path_b, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    CHECK(a >= 0 && b >= 0);
    dm_cp_store *sa = dm_cp_store_create(a);
    dm_cp_store *sb = dm_cp_store_create(b);
    CHECK(sa && sb);
    dm_cp_instance pa, pb;
    dm_cp_init(&pa);
    dm_cp_init(&pb);
    CHECK(dm_cp_set(&pa, "eco_fps", 1) == DM_CP_OK);
    CHECK(dm_cp_set(&pb, "q3d", 1) == DM_CP_OK);
    CHECK(dm_cp_apply(&pa, dm_cp_store_save, sa) == DM_CP_OK);
    CHECK(dm_cp_apply(&pb, dm_cp_store_save, sb) == DM_CP_OK);
    dm_cp_instance ra, rb;
    dm_cp_init(&ra);
    dm_cp_init(&rb);
    CHECK(dm_cp_load(&ra, dm_cp_store_load, sa) == DM_CP_OK);
    CHECK(dm_cp_load(&rb, dm_cp_store_load, sb) == DM_CP_OK);
    CHECK(dm_cp_get(&ra, "eco_fps") == 1 && dm_cp_get(&ra, "q3d") == 0);
    CHECK(dm_cp_get(&rb, "eco_fps") == 0 && dm_cp_get(&rb, "q3d") == 1);
    close(a);
    close(b);
    dm_cp_store_destroy(sa);
    dm_cp_store_destroy(sb);
    char path[512];
    snprintf(path, sizeof path, "%s/%s", path_a, DM_CP_STORE_FILE);
    unlink(path);
    snprintf(path, sizeof path, "%s/%s", path_b, DM_CP_STORE_FILE);
    unlink(path);
    rmdir(path_a);
    rmdir(path_b);
    return 0;
}

#ifdef DM_CP_STORE_TEST_FAULT
static int test_post_fsync_failure(void) {
    char path_a[] = "/tmp/dm-cp-store-f-XXXXXX";
    CHECK(make_dir(path_a));
    int fd = open(path_a, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    CHECK(fd >= 0);
    dm_cp_store *store = dm_cp_store_create(fd);
    CHECK(store != NULL);
    dm_cp_instance p;
    dm_cp_init(&p);
    CHECK(dm_cp_set(&p, "eco_fps", 1) == DM_CP_OK);
    CHECK(dm_cp_apply(&p, dm_cp_store_save, store) == DM_CP_OK);
    char old_doc[DM_CP_MAX_BYTES + 1];
    size_t old_len = 0;
    CHECK(read_file(fd, DM_CP_STORE_FILE, old_doc, sizeof old_doc, &old_len));

    CHECK(dm_cp_set(&p, "q3d", 1) == DM_CP_OK);
    fail_after_fsync = 1;
    CHECK(dm_cp_apply(&p, dm_cp_store_save, store) == DM_CP_SAVE_FAILED);
    fail_after_fsync = 0;
    CHECK(dm_cp_pending(&p, "q3d") == 1);
    char current[DM_CP_MAX_BYTES + 1];
    size_t current_len = 0;
    CHECK(read_file(fd, DM_CP_STORE_FILE, current, sizeof current, &current_len));
    CHECK(old_len == current_len && !memcmp(old_doc, current, old_len));
    CHECK(!path_exists(fd, DM_CP_STORE_TEMP));
    CHECK(dm_cp_apply(&p, dm_cp_store_save, store) == DM_CP_OK);
    CHECK(dm_cp_get(&p, "q3d") == 1);
    CHECK(read_file(fd, DM_CP_STORE_FILE, current, sizeof current, &current_len));
    CHECK(current_len > 0 && strstr(current, "q3d=1\n") != NULL);

    close(fd);
    dm_cp_store_destroy(store);
    char path[512];
    snprintf(path, sizeof path, "%s/%s", path_a, DM_CP_STORE_FILE);
    unlink(path);
    rmdir(path_a);
    return 0;
}
#endif

int main(void) {
    if (test_roundtrip_and_ownership()) return 1;
    if (test_missing_and_invalid_inputs()) return 1;
    if (test_pre_rename_failure_and_retry()) return 1;
    if (test_independent_directories()) return 1;
#ifdef DM_CP_STORE_TEST_FAULT
    if (test_post_fsync_failure()) return 1;
#endif
    printf("PASS custom preferences store: %d checks\n", checks);
    return 0;
}
