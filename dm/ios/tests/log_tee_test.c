#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int log_fd;
static int stderr_copy;
static int tee_pipe[2];

static void write_all(int fd, const char *p, size_t n) {
    while (n) {
        ssize_t w = write(fd, p, n);
        if (w > 0) { p += w; n -= (size_t)w; }
        else if (w < 0 && errno == EINTR) continue;
        else abort();
    }
}

static void *tee_main(void *unused) {
    (void)unused;
    char buf[128], line[256];
    size_t used = 0;
    for (;;) {
        ssize_t n = read(tee_pipe[0], buf, sizeof buf);
        if (n == 0) break;
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) abort();
        for (ssize_t i = 0; i < n; i++) {
            line[used++] = buf[i];
            if (buf[i] == '\n') {
                write_all(log_fd, line, used);
                if (fsync(log_fd)) abort();
                write_all(stderr_copy, line, used);
                used = 0;
            }
        }
    }
    if (used) {
        write_all(log_fd, line, used);
        if (fsync(log_fd)) abort();
        write_all(stderr_copy, line, used);
    }
    return NULL;
}

int main(void) {
    char path[] = "dm/ios/tests/.log_tee_test.XXXXXX";
    int capture[2];
    pthread_t thread;
    char expected[] = "LOG one\nUIMSG two\n";
    char actual[sizeof expected] = {0};
    char console[sizeof expected] = {0};
    int fd;

    log_fd = mkstemp(path);
    if (log_fd < 0 || pipe(tee_pipe) || pipe(capture)) return 1;
    stderr_copy = capture[1];
    if (pthread_create(&thread, NULL, tee_main, NULL)) return 1;
    if (dup2(tee_pipe[1], STDERR_FILENO) < 0) return 1;
    write_all(STDERR_FILENO, expected, sizeof expected - 1);
    close(tee_pipe[1]);
    close(STDERR_FILENO);
    if (pthread_join(thread, NULL)) return 1;
    close(capture[1]);
    if (read(capture[0], console, sizeof console - 1) != (ssize_t)(sizeof expected - 1)) return 1;
    if (memcmp(console, expected, sizeof expected - 1)) return 1;
    fd = open(path, O_RDONLY);
    if (fd < 0 || read(fd, actual + 0, sizeof actual - 1) != (ssize_t)(sizeof expected - 1)) return 1;
    lseek(fd, 0, SEEK_SET);
    memset(actual, 0, sizeof actual);
    if (read(fd, actual, sizeof actual - 1) != (ssize_t)(sizeof expected - 1)) return 1;
    if (memcmp(actual, expected, sizeof expected - 1)) return 1;
    unlink(path);
    puts("log tee: file lines and stderr lines match; fsync per line passed");
    return 0;
}
