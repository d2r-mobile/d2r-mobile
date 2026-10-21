// #253 owned host snapshot guard regression. Compiles the production guard
// include and verifies nesting, errno, fail-closed pairing, and real
// cross-thread writer/snapshot exclusion without sleeps.
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

static int prepare_calls;
static uintptr_t prepare_address;
static size_t prepare_size;
void dm_guest_write_prepare(void *addr, size_t len) {
    prepare_calls++;
    prepare_address = (uintptr_t)addr;
    prepare_size = len;
}
#define die(...) do { _exit(77); } while (0)
#include "guest_snapshot_guard.inc"
#include "guarded_guest_read.inc"

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); } } while (0)

static ssize_t failing_read(int fd, void *buf, size_t size, off_t offset) {
    (void)fd; (void)buf; (void)size; (void)offset;
    errno = EIO;
    return -1;
}

typedef struct { pthread_mutex_t mutex; pthread_cond_t condition; int count, phase; } barrier;
static void barrier_wait(barrier *b) {
    pthread_mutex_lock(&b->mutex);
    int phase = b->phase;
    if (++b->count == 2) { b->count = 0; b->phase++; pthread_cond_broadcast(&b->condition); }
    else while (phase == b->phase) pthread_cond_wait(&b->condition, &b->mutex);
    pthread_mutex_unlock(&b->mutex);
}
static barrier writer_barrier;
static void *writer_thread(void *arg) {
    (void)arg;
    dm_guest_host_write_begin((void *)0x1000, 4);
    barrier_wait(&writer_barrier);
    barrier_wait(&writer_barrier);
    dm_guest_host_write_end();
    return NULL;
}

int main(void) {
    // Nested writer guards call prepare once per begin and preserve errno.
    errno = E2BIG;
    dm_guest_host_write_begin((void *)0x2000, 8);
    CHECK(prepare_calls == 1 && prepare_address == 0x2000 && prepare_size == 8);
    CHECK(dm_guest_snapshot_try_begin() == 0);   // already a host writer in this thread
    dm_guest_host_write_begin((void *)0x3000, 16);
    CHECK(prepare_calls == 2);
    dm_guest_host_write_end();
    dm_guest_host_write_end();
    CHECK(errno == E2BIG);

    // Snapshot guard nests and releases; errno survives.
    errno = ENOENT;
    CHECK(dm_guest_snapshot_try_begin() == 1);
    CHECK(dm_guest_snapshot_try_begin() == 1);
    dm_guest_snapshot_end();
    dm_guest_snapshot_end();
    CHECK(errno == ENOENT);

    // Genuine cross-thread exclusion: another thread holds the writer read
    // lease while this thread's snapshot try must refuse immediately.
    writer_barrier = (barrier){PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0, 0};
    pthread_t writer;
    CHECK(pthread_create(&writer, NULL, writer_thread, NULL) == 0);
    barrier_wait(&writer_barrier);
    errno = EINVAL;
    CHECK(dm_guest_snapshot_try_begin() == 0);
    CHECK(errno == EINVAL);
    barrier_wait(&writer_barrier);
    CHECK(pthread_join(writer, NULL) == 0);

    // Actual host-write caller path: injected pread failure releases the
    // writer lease exactly once; a snapshot can be acquired immediately after.
    uint8_t guest_buffer[8] = {0};
    size_t got = 99;
    errno = EIO;
    CHECK(dm_guarded_guest_read(1, guest_buffer, sizeof guest_buffer, 0, &got, failing_read) == -1);
    CHECK(errno == EIO && got == 99);
    CHECK(dm_guest_snapshot_try_begin() == 1);
    dm_guest_snapshot_end();

    // Unpaired calls fail closed in child processes.
    pid_t child = fork();
    if (child == 0) { dm_guest_host_write_end(); _exit(0); }
    int status = 0;
    CHECK(child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 77);
    child = fork();
    if (child == 0) { dm_guest_snapshot_end(); _exit(0); }
    status = 0;
    CHECK(child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 77);

    if (failures) return 1;
    puts("PASS host snapshot guard: nested/errno/cross-thread/fail-closed");
    return 0;
}
