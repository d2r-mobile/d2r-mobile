// 安卓上的 clonefile：APFS 写时复制克隆的语义等价物——整树拷贝（目录、普通文件、user.* 扩展属性），目标不得已存在。
// 只给存档事务用（hle_fs.c：挂载时 commit → work、提交时 work → new），存档目录只有几 MB，整拷代价可忽略。
// 失败时删掉拷了一半的目标并返回 -1（errno 为首个失败原因），与 clonefile 一样「要么整份、要么没有」。
// 调用方跑在客体线程的栈上（有的只有十几 KiB，见 hle_thread.c）：缓冲与路径一律放堆上，栈上只留几个指针。
// clonefile on Android: the semantic equivalent of an APFS copy-on-write clone -- a whole-tree copy (directories, regular files, user.* extended attributes); the target must not already exist.
// Only used for save transactions (hle_fs.c: commit → work at mount, work → new at commit); a save directory is only a few MB, so the cost of a full copy is negligible.
// On failure the half-copied target is removed and -1 is returned (errno is the first failure reason), like clonefile: "either the whole thing or nothing".
// The caller runs on a guest thread's stack (some are only a dozen or so KiB, see hle_thread.c): buffers and paths all go on the heap, leaving only a few pointers on the stack.
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

static int copy_xattrs(const char *src, const char *dst) {
    enum { CAP = 4096 };
    char *names = malloc(2 * CAP), *v = names + CAP; if (!names) return -1;
    ssize_t n = llistxattr(src, names, CAP); int rc = 0;
    if (n < 0) rc = errno == ENOTSUP ? 0 : -1;
    for (char *k = names; n > 0 && !rc && k < names + n; k += strlen(k) + 1) {
        if (strncmp(k, "user.", 5)) continue;   // 只搬 user.*（security.* 归系统） | Only move user.* (security.* belongs to the system)
        ssize_t m = lgetxattr(src, k, v, CAP);
        if (m < 0 || lsetxattr(dst, k, v, (size_t)m, 0)) rc = -1;
    }
    { int e = errno; free(names); errno = e; }
    return rc;
}

static int copy_file(const char *src, const char *dst, mode_t mode) {
    int in = open(src, O_RDONLY | O_CLOEXEC), out = -1, rc = -1;
    if (in < 0) return -1;
    char *buf = NULL; ssize_t k;
    if ((out = open(dst, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, mode & 0777)) < 0 || !(buf = malloc(1 << 16))) goto done;
    while ((k = read(in, buf, 1 << 16)) > 0)
        for (ssize_t w = 0; w < k;) { ssize_t z = write(out, buf + w, (size_t)(k - w)); if (z < 0) { if (errno == EINTR) continue; goto done; } w += z; }
    if (k < 0 || fsync(out)) goto done;
    rc = copy_xattrs(src, dst);
done:
    { int e = errno; free(buf); if (out >= 0) close(out); close(in); errno = e; }
    return rc;
}

static int copy_tree(const char *src, const char *dst) {
    struct stat st; if (lstat(src, &st)) return -1;
    if (S_ISREG(st.st_mode)) return copy_file(src, dst, st.st_mode);
    if (!S_ISDIR(st.st_mode)) { errno = ENOTSUP; return -1; }   // 存档里不该有链接 / 设备文件 | Saves should contain no links / device files
    if (mkdir(dst, st.st_mode & 0777) || copy_xattrs(src, dst)) return -1;
    DIR *d = opendir(src); if (!d) return -1;
    struct dirent *e; int rc = 0;
    char *a = malloc(2 * PATH_MAX), *b = a + PATH_MAX; if (!a) { closedir(d); return -1; }
    while (!rc && (e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (snprintf(a, PATH_MAX, "%s/%s", src, e->d_name) >= PATH_MAX || snprintf(b, PATH_MAX, "%s/%s", dst, e->d_name) >= PATH_MAX) { errno = ENAMETOOLONG; rc = -1; break; }
        rc = copy_tree(a, b);
    }
    { int e2 = errno; free(a); closedir(d); errno = e2; }
    return rc;
}

static void remove_tree(const char *p) {
    struct stat st; if (lstat(p, &st)) return;
    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(p); struct dirent *e;
        if (d) {
            char *a = malloc(PATH_MAX);
            while (a && (e = readdir(d))) {
                if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
                if (snprintf(a, PATH_MAX, "%s/%s", p, e->d_name) < PATH_MAX) remove_tree(a);
            }
            free(a);
            closedir(d);
        }
        rmdir(p);
    } else unlink(p);
}

int clonefile(const char *src, const char *dst, int flags) {
    (void)flags;
    struct stat st; if (!lstat(dst, &st)) { errno = EEXIST; return -1; }
    if (!copy_tree(src, dst)) return 0;
    int e = errno; remove_tree(dst); errno = e;
    return -1;
}
