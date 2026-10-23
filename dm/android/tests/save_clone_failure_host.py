#!/usr/bin/env python3
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "native/host_posix.c").read_text()
cleanup = source[source.index("static int remove_created("):source.index("static int copy_xattrs(")]
clone = source[source.index("static int clone_node("):source.index("int dm_android_clonefile(")]
prefix = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#define st_atim st_atimespec
#define st_mtim st_mtimespec
#endif
static const char *fault_path;
static DIR *fault_directory;
static int fault_mode, saw_file;
static int io_mode, input_fd = -1, output_fd = -1, interrupted;
static int test_open(const char *path, int flags, ...) {
    int fd;
    if (flags & O_CREAT) {
        va_list args; va_start(args, flags); int mode = va_arg(args, int); va_end(args);
        fd = open(path, flags, mode); output_fd = fd;
    } else { fd = open(path, flags); input_fd = fd; }
    return fd;
}
static ssize_t test_read(int fd, void *buffer, size_t size) {
    if (fd == input_fd && io_mode == 6) { errno = EIO; return -1; }
    if (fd == input_fd && io_mode == 5 && !interrupted++) { errno = EINTR; return -1; }
    return read(fd, buffer, size);
}
static ssize_t test_write(int fd, const void *buffer, size_t size) {
    if (fd == output_fd) {
        if (io_mode == 1) {
            if (!interrupted++) return write(fd, buffer, size > 3 ? 3 : size);
            errno = ENOSPC; return -1;
        }
        if (io_mode == 4) { errno = 0; return 0; }
        if (io_mode == 5 && interrupted++ == 1) { errno = EINTR; return -1; }
    }
    return write(fd, buffer, size);
}
static int test_fsync(int fd) {
    if (fd == output_fd && io_mode == 2) { errno = ENOSPC; return -1; }
    return fsync(fd);
}
static int test_close(int fd) {
    int fail = fd == output_fd && (io_mode == 1 || io_mode == 3);
    if (fd == output_fd) output_fd = -1;
    if (fd == input_fd) input_fd = -1;
    int result = close(fd);
    if (fail) { errno = EIO; return -1; }
    return result;
}
static DIR *test_opendir(const char *path) {
    DIR *directory = opendir(path);
    if (fault_path && !strcmp(path, fault_path)) fault_directory = directory;
    return directory;
}
static struct dirent *test_readdir(DIR *directory) {
    if (directory == fault_directory && fault_mode == 1 && saw_file) {
        fault_mode = 0; errno = EIO; return NULL;
    }
    struct dirent *entry = readdir(directory);
    if (directory == fault_directory && entry && entry->d_name[0] != '.') saw_file = 1;
    return entry;
}
static int test_closedir(DIR *directory) {
    int fail = directory == fault_directory && fault_mode == 2;
    int result = closedir(directory);
    if (fail) { fault_mode = 0; errno = EIO; return -1; }
    return result;
}
static int copy_xattrs(const char *source, const char *destination) {
    (void)source; (void)destination; return 0;
}
#define opendir test_opendir
#define readdir test_readdir
#define closedir test_closedir
#define open test_open
#define read test_read
#define write test_write
#define fsync test_fsync
#define close test_close
'''
body = r'''
static void write_save(const char *path) {
    FILE *file = fopen(path, "w"); assert(file);
    assert(fputs("original-save", file) >= 0 && !fclose(file));
}
int main(int argc, char **argv) {
    assert(argc == 2 && !chdir(argv[1]));
    assert(!mkdir("source", 0700));
    write_save("source/hero.d2s");
    for (int mode = 1; mode <= 2; mode++) {
        fault_path = "source"; fault_mode = mode; saw_file = 0;
        assert(clone_node("source", "candidate") == -1 && errno == EIO);
        assert(access("candidate", F_OK) == -1 && errno == ENOENT);
        assert(!access("source/hero.d2s", F_OK));
    }
    assert(!mkdir("source/nested", 0700));
    write_save("source/nested/shared.d2i");
    fault_path = "source/nested"; fault_mode = 1; saw_file = 0;
    assert(clone_node("source", "candidate") == -1 && errno == EIO);
    assert(access("candidate", F_OK) == -1 && errno == ENOENT);
    fault_path = NULL; fault_directory = NULL;
    for (int mode = 1; mode <= 6; mode++) {
        io_mode = mode; interrupted = 0;
        int result = clone_node("source", "candidate");
        if (mode == 5) {
            assert(!result && !remove_created("candidate"));
        } else {
            assert(result == -1 && errno == (mode <= 2 ? ENOSPC : EIO));
            assert(access("candidate", F_OK) == -1 && errno == ENOENT);
        }
        FILE *original = fopen("source/hero.d2s", "r"); assert(original);
        char bytes[32] = {0}; assert(fread(bytes, 1, sizeof bytes - 1, original) == 13);
        assert(!fclose(original) && !strcmp(bytes, "original-save"));
    }
    io_mode = 0;
    assert(!clone_node("source", "candidate"));
    FILE *file = fopen("candidate/nested/shared.d2i", "r"); assert(file);
    char bytes[32] = {0}; assert(fread(bytes, 1, sizeof bytes - 1, file) == 13);
    assert(!fclose(file) && !strcmp(bytes, "original-save"));
    assert(!remove_created("candidate") && !remove_created("source"));
    puts("PASS production save clone: directory/read/write/fsync/close failures, ENOSPC, EINTR, cleanup and retry");
}
'''
with tempfile.TemporaryDirectory(prefix="dm-save-clone-") as temporary:
    fixture = Path(temporary) / "test.c"
    fixture.write_text(prefix + cleanup + clone + body)
    executable = Path(temporary) / "test"
    subprocess.run([sys.argv[1] if len(sys.argv) > 1 else "clang", "-std=c11", "-Wall", "-Wextra",
                    "-Werror", "-fsanitize=address,undefined", str(fixture), "-o", str(executable)], check=True)
    subprocess.run([str(executable), temporary], check=True)
