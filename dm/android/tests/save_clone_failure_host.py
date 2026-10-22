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
    assert(!clone_node("source", "candidate"));
    FILE *file = fopen("candidate/nested/shared.d2i", "r"); assert(file);
    char bytes[32] = {0}; assert(fread(bytes, 1, sizeof bytes - 1, file) == 13);
    assert(!fclose(file) && !strcmp(bytes, "original-save"));
    assert(!remove_created("candidate") && !remove_created("source"));
    puts("PASS production save clone: enumeration/close errors, nested cleanup, retry and original preservation");
}
'''
with tempfile.TemporaryDirectory(prefix="dm-save-clone-") as temporary:
    fixture = Path(temporary) / "test.c"
    fixture.write_text(prefix + cleanup + clone + body)
    executable = Path(temporary) / "test"
    subprocess.run([sys.argv[1] if len(sys.argv) > 1 else "clang", "-std=c11", "-Wall", "-Wextra",
                    "-Werror", "-fsanitize=address,undefined", str(fixture), "-o", str(executable)], check=True)
    subprocess.run([str(executable), temporary], check=True)
