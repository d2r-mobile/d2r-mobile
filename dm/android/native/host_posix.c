// 复制存档目录并用 renameat2 原子交换，保留旧提交、时间戳和用户扩展属性。
// Copy save directories and atomically exchange with renameat2, preserving old commits, timestamps, and user xattrs.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "host_posix.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/xattr.h>
#include <sys/uio.h>
#include <unistd.h>

uint64_t dm_android_clock_ns(clockid_t clock) {
    struct timespec t;
    if (clock_gettime(clock, &t)) return 0;
    return (uint64_t)t.tv_sec * 1000000000 + t.tv_nsec;
}
uint64_t dm_android_process_start_ns(void) {
    uint64_t now = dm_android_clock_ns(CLOCK_MONOTONIC);
    FILE *file = fopen("/proc/self/stat", "r");
    if (!file) return now;
    char line[4096]; char *got = fgets(line, sizeof line, file); fclose(file);
    char *end = got ? strrchr(line, ')') : NULL;
    if (!end) return now;
    char *save = NULL, *token = strtok_r(end + 1, " ", &save);
    for (unsigned field = 3; token && field < 22; ++field) token = strtok_r(NULL, " ", &save);
    long rate = sysconf(_SC_CLK_TCK);
    if (!token || rate <= 0) return now;
    uint64_t start = strtoull(token, NULL, 10) * 1000000000 / rate;
    uint64_t boot = dm_android_clock_ns(CLOCK_BOOTTIME);
    uint64_t age = boot >= start ? boot - start : 0;
    return age < now ? now - age : 0;
}
void *dm_android_stack_top(pthread_t thread) {
    pthread_attr_t attributes; void *base; size_t size;
    if (pthread_getattr_np(thread, &attributes)) return NULL;
    int error = pthread_attr_getstack(&attributes, &base, &size);
    pthread_attr_destroy(&attributes);
    return error ? NULL : (char *)base + size;
}
size_t dm_android_stack_size(pthread_t thread) {
    pthread_attr_t attributes; void *base; size_t size;
    if (pthread_getattr_np(thread, &attributes)) return 0;
    int error = pthread_attr_getstack(&attributes, &base, &size);
    pthread_attr_destroy(&attributes); return error ? 0 : size;
}
int dm_android_fd_limit(void) {
    struct rlimit limit;
    if (getrlimit(RLIMIT_NOFILE, &limit)) return -1;
    return limit.rlim_cur > INT_MAX ? INT_MAX : (int)limit.rlim_cur;
}
int dm_android_fd_path(int fd, char *path, size_t capacity) {
    if (!capacity) { errno = EINVAL; return -1; }
    char link[64]; snprintf(link, sizeof link, "/proc/self/fd/%d", fd);
    ssize_t n = readlink(link, path, capacity - 1);
    if (n < 0) return -1;
    if ((size_t)n == capacity - 1) { errno = ENAMETOOLONG; return -1; }
    path[n] = 0; return 0;
}
static _Thread_local dm_android_read_stats current_thread_read_stats;
void dm_android_current_thread_read_stats(dm_android_read_stats *out) {
    if (!out) return;
    int saved_errno = errno;
    *out = current_thread_read_stats; out->tid = (uint32_t)gettid();
    errno = saved_errno;
}
int dm_android_read_memory(uintptr_t address, void *destination, size_t size) {
    struct iovec local = {destination, size}, remote = {(void *)address, size};
    // 两次真实 wall 读取；保留进入 syscall 前与 syscall 返回后的 errno，不更改安全读取结果。
    // Take two actual wall readings; preserve errno before/after the syscall without changing the safe-read result.
    struct timespec before, after; int incoming_errno = errno;
    int before_ok = !clock_gettime(CLOCK_MONOTONIC, &before);
    errno = incoming_errno;
    ssize_t n = process_vm_readv(getpid(), &local, 1, &remote, 1, 0);
    int read_errno = errno;
    int after_ok = !clock_gettime(CLOCK_MONOTONIC, &after);
    dm_android_read_stats *stats = &current_thread_read_stats;
    stats->calls++; stats->requested_bytes += size;
    if (n >= 0) {
        stats->returned_bytes += (uint64_t)n;
        if ((size_t)n == size) stats->full++; else stats->partial++;
    } else stats->errors++;
    unsigned bucket = !size ? 0 : size <= 64 ? 1 : size <= 256 ? 2 : size <= 1024 ? 3 : size <= 4096 ? 4 : size <= 65536 ? 5 : 6;
    stats->size_buckets[bucket]++;
    stats->bucket_requested_bytes[bucket] += size;
    if (n >= 0) stats->bucket_returned_bytes[bucket] += (uint64_t)n;
    if (before_ok && after_ok) {
        uint64_t start = (uint64_t)before.tv_sec * 1000000000ull + before.tv_nsec;
        uint64_t finish = (uint64_t)after.tv_sec * 1000000000ull + after.tv_nsec;
        if (finish >= start) {
            uint64_t elapsed = finish - start;
            stats->wall_ns += elapsed; stats->bucket_wall_ns[bucket] += elapsed;
        } else { stats->wall_unavailable++; stats->bucket_wall_unavailable[bucket]++; }
    } else { stats->wall_unavailable++; stats->bucket_wall_unavailable[bucket]++; }
    errno = read_errno;
    return n >= 0 && (size_t)n == size ? 0 : -1;
}

static int remove_created(const char *path) {
    struct stat st;
    if (lstat(path, &st)) return -1;
    if (!S_ISDIR(st.st_mode)) return unlink(path);
    DIR *directory = opendir(path); if (!directory) return -1;
    struct dirent *entry; int error = 0;
    while ((entry = readdir(directory))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        char child[PATH_MAX];
        if (snprintf(child, sizeof child, "%s/%s", path, entry->d_name) >= (int)sizeof child || remove_created(child)) { error = -1; break; }
    }
    closedir(directory);
    return error ? error : rmdir(path);
}
static int copy_xattrs(const char *source, const char *destination) {
    ssize_t size = llistxattr(source, NULL, 0);
    if (size < 0) return errno == ENOTSUP ? 0 : -1;
    if (!size) return 0;
    char *names = malloc(size); if (!names) return -1;
    ssize_t got = llistxattr(source, names, size); int error = 0;
    if (got < 0) { free(names); return -1; }
    for (ssize_t offset = 0; offset < got;) {
        char *name = names + offset;
        size_t length = strnlen(name, got - offset);
        if (length == (size_t)(got - offset)) { error = -1; break; }
        offset += length + 1;
        if (strncmp(name, "user.", 5)) continue;
        ssize_t n = lgetxattr(source, name, NULL, 0);
        if (n < 0) { error = -1; break; }
        void *value = malloc(n ? n : 1);
        if (!value) { error = -1; break; }
        ssize_t read = lgetxattr(source, name, value, n);
        error = read == n ? lsetxattr(destination, name, value, n, 0) : -1;
        free(value); if (error) break;
    }
    free(names); return error;
}
static int clone_node(const char *source, const char *destination) {
    struct stat st;
    if (lstat(source, &st)) return -1;
    int created = 0, error = 0;
    if (S_ISDIR(st.st_mode)) {
        if (mkdir(destination, 0700)) return -1;
        created = 1;
        DIR *directory = opendir(source);
        if (!directory) { error = -1; goto done; }
        struct dirent *entry;
        while ((entry = readdir(directory))) {
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
            size_t a = strlen(source) + strlen(entry->d_name) + 2, b = strlen(destination) + strlen(entry->d_name) + 2;
            if (a > PATH_MAX || b > PATH_MAX) { errno = ENAMETOOLONG; error = -1; break; }
            char *from = malloc(a), *to = malloc(b);
            if (!from || !to) { free(from); free(to); error = -1; break; }
            snprintf(from, a, "%s/%s", source, entry->d_name); snprintf(to, b, "%s/%s", destination, entry->d_name);
            int child_error = clone_node(from, to); free(from); free(to);
            if (child_error) { error = -1; break; }
        }
        closedir(directory);
    } else if (S_ISREG(st.st_mode)) {
        int in = open(source, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (in < 0) return -1;
        int out = open(destination, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (out < 0) { close(in); return -1; }
        // 游戏工作线程栈很小；递归层和文件缓冲都不能放大数组。
        // Guest worker stacks are small; recursion frames and file buffers must not contain large arrays.
        created = 1; char *buffer = malloc(65536); ssize_t n;
        if (!buffer) { close(in); close(out); error = -1; goto done; }
        for (;;) {
            n = read(in, buffer, 65536);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) { if (n < 0) error = -1; break; }
            for (ssize_t offset = 0; offset < n;) {
                ssize_t wrote = write(out, buffer + offset, n - offset);
                if (wrote < 0 && errno == EINTR) continue;
                if (wrote <= 0) { error = -1; break; }
                offset += wrote;
            }
            if (error) break;
        }
        if (!error && fsync(out)) error = -1;
        free(buffer); close(in); close(out);
    } else if (S_ISLNK(st.st_mode)) {
        char *value = malloc(PATH_MAX); if (!value) return -1;
        ssize_t n = readlink(source, value, PATH_MAX - 1);
        if (n < 0) { free(value); return -1; }
        value[n] = 0;
        int result = symlink(value, destination); free(value); if (result) return -1;
        created = 1;
    } else { errno = ENOTSUP; return -1; }
done:
    if (!error) {
        struct timespec times[] = {st.st_atim, st.st_mtim};
        if (!S_ISLNK(st.st_mode) && chmod(destination, st.st_mode & 0777)) error = -1;
        if (!error && copy_xattrs(source, destination)) error = -1;
        if (!error && utimensat(AT_FDCWD, destination, times, AT_SYMLINK_NOFOLLOW)) error = -1;
    }
    if (error && created) { int saved = errno; remove_created(destination); errno = saved; }
    return error;
}
int dm_android_clonefile(const char *source, const char *destination, int flags) {
    if (flags) { errno = ENOTSUP; return -1; }
    return clone_node(source, destination);
}
int dm_android_rename_exchange(const char *first, const char *second, unsigned flags) {
    if (flags != 2) { errno = EINVAL; return -1; }
    return syscall(SYS_renameat2, AT_FDCWD, first, AT_FDCWD, second, 2);
}
static int attr_name(const char *name, char *out, size_t cap) {
    if (snprintf(out, cap, "%s%s", strncmp(name, "user.", 5) ? "user." : "", name) >= (int)cap) { errno = ENAMETOOLONG; return -1; }
    return 0;
}
ssize_t dm_android_getxattr(const char *path, const char *name, void *value, size_t size, unsigned position, int options) {
    (void)options; char key[256];
    if (position || attr_name(name, key, sizeof key)) { errno = EINVAL; return -1; }
    return lgetxattr(path, key, value, size);
}
int dm_android_setxattr(const char *path, const char *name, const void *value, size_t size, unsigned position, int options) {
    (void)options; char key[256];
    if (position || attr_name(name, key, sizeof key)) { errno = EINVAL; return -1; }
    return lsetxattr(path, key, value, size, 0);
}
int dm_android_removexattr(const char *path, const char *name, int options) {
    (void)options; char key[256]; if (attr_name(name, key, sizeof key)) return -1;
    return lremovexattr(path, key);
}
