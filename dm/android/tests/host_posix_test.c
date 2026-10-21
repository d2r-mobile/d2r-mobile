// 验证存档快照独立性、扩展属性和原子交换后的旧提交保留。
// Verify save snapshot independence, xattrs, and preservation of the old commit after atomic exchange.
#define _GNU_SOURCE
#include "host_posix.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static void write_file(const char *path, const char *value) {
    FILE *f = fopen(path, "wb"); assert(f); assert(fputs(value, f) >= 0); assert(!fclose(f));
}
static void read_file(const char *path, const char *value) {
    char buffer[100] = {0}; FILE *f = fopen(path, "rb"); assert(f); assert(fread(buffer, 1, sizeof buffer - 1, f) > 0);
    fclose(f); assert(!strcmp(buffer, value));
}
int dm_android_posix_selftest(const char *directory) {
    char root[1024]; snprintf(root, sizeof root, "%s/d2r-save-test-XXXXXX", directory);
    assert(mkdtemp(root));
    char commit[1200], work[1200], original[1400], snapshot[1400];
    snprintf(commit, sizeof commit, "%s/commit", root); snprintf(work, sizeof work, "%s/work", root);
    assert(!mkdir(commit, 0700)); snprintf(original, sizeof original, "%s/player.d2s", commit);
    snprintf(snapshot, sizeof snapshot, "%s/player.d2s", work); write_file(original, "old save");
    assert(!dm_android_setxattr(original, "d2r.guestname", "player", 6, 0, 0));
    assert(!dm_android_clonefile(commit, work, 0));
    read_file(snapshot, "old save"); write_file(snapshot, "new save"); read_file(original, "old save");
    char attribute[20] = {0}; assert(dm_android_getxattr(snapshot, "d2r.guestname", attribute, sizeof attribute, 0, 0) == 6);
    assert(!strcmp(attribute, "player"));
    assert(dm_android_clonefile(commit, work, 0) == -1 && errno == EEXIST); read_file(snapshot, "new save");
    assert(!dm_android_rename_exchange(commit, work, 2)); read_file(original, "new save"); read_file(snapshot, "old save");
    assert(dm_android_clock_ns(CLOCK_MONOTONIC) > 0); assert(dm_android_stack_top(pthread_self()));
    assert(dm_android_stack_size(pthread_self()) > 0); assert(dm_android_fd_limit() > 0);
    char copy[20]; assert(!dm_android_read_memory((uintptr_t)"memory probe", copy, 13)); assert(!strcmp(copy, "memory probe"));
    assert(dm_android_read_memory(1, copy, sizeof copy) == -1);
    unlink(original); unlink(snapshot); rmdir(commit); rmdir(work); rmdir(root);
    puts("Android POSIX host: PASS (snapshot isolation, xattrs, atomic exchange, old commit, time, stack, fd limit)");
    return 0;
}
#ifndef DM_NATIVE_APP
// 游戏工作线程的小栈回归：复制单个存档也不应需要 64 KiB 栈缓冲。
// Small guest-stack regression: even a single save copy must not need a 64 KiB stack buffer.
static void *small_stack_test(void *directory) { return (void *)(intptr_t)dm_android_posix_selftest(directory); }
int main(int argc, char **argv) {
    pthread_attr_t attr; assert(!pthread_attr_init(&attr)); assert(!pthread_attr_setstacksize(&attr, 65536));
    pthread_t thread; assert(!pthread_create(&thread, &attr, small_stack_test, argc > 1 ? argv[1] : "/tmp")); pthread_attr_destroy(&attr);
    void *result; assert(!pthread_join(thread, &result)); return (int)(intptr_t)result;
}
#endif
