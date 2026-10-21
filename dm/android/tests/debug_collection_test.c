#define _GNU_SOURCE
#include "host_posix.h"
#include "../../host/dm.h"
#include <assert.h>
#include <errno.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void) {
    int evaluated = 0;
    assert(!dm_diagnostics_enabled());
    LOG("disabled %d", ++evaluated);
    assert(!evaluated);
    assert(!setenv("DM_TEST_GOLD", "999", 1));
    assert(!setenv("DM_VK_CAPTURE", "/invalid", 1));
    assert(!setenv("DM_HID_RECEIPT_LOG", "/invalid", 1));
    assert(!setenv("DM_GAME_RES", "1600x720", 1));
    assert(!setenv("DM_VK_SHADERS", "/shaders", 1));
    assert(!setenv("DM_PRESENT_HZ", "30", 1));
    dm_android_diagnostics_configure(0);
    assert(!getenv("DM_TEST_GOLD") && !getenv("DM_VK_CAPTURE") && !getenv("DM_HID_RECEIPT_LOG"));
    assert(!strcmp(getenv("DM_GAME_RES"), "1600x720"));
    assert(!strcmp(getenv("DM_VK_SHADERS"), "/shaders"));
    assert(!strcmp(getenv("DM_PRESENT_HZ"), "30"));
    int source = 41, destination = 0;
    assert(!dm_android_read_memory((uintptr_t)&source, &destination, sizeof source));
    assert(destination == source);
    assert(dm_android_read_memory(1, &destination, sizeof source) == -1);
    dm_android_read_stats stats;
    dm_android_current_thread_read_stats(&stats);
    assert(!stats.calls && !stats.wall_ns);
    dm_android_diagnostics_configure(1);
    LOG("enabled %d", ++evaluated);
    assert(evaluated == 1);
    assert(!dm_android_read_memory((uintptr_t)&source, &destination, sizeof source));
    dm_android_current_thread_read_stats(&stats);
    assert(stats.calls == 1 && stats.returned_bytes == sizeof source);
    dm_android_diagnostics_configure(0);
    assert(!dm_android_read_memory((uintptr_t)&source, &destination, sizeof source));
    dm_android_current_thread_read_stats(&stats);
    assert(stats.calls == 1);
    int descriptors[2]; assert(!pipe(descriptors));
    pid_t child = fork(); assert(child >= 0);
    if (!child) {
        struct rlimit limit = {0, 0}; setrlimit(RLIMIT_CORE, &limit);
        close(descriptors[0]); assert(dup2(descriptors[1], STDERR_FILENO) >= 0);
        close(descriptors[1]); die("debug-off fatal retained");
    }
    close(descriptors[1]);
    char text[1024] = {0}; size_t used = 0;
    for (;;) {
        char chunk[256]; ssize_t count = read(descriptors[0], chunk, sizeof chunk);
        if (count < 0 && errno == EINTR) continue;
        assert(count >= 0);
        if (!count) break;
        size_t keep = (size_t)count < sizeof text - 1 - used ? (size_t)count : sizeof text - 1 - used;
        memcpy(text + used, chunk, keep); used += keep;
    }
    close(descriptors[0]); int status; assert(waitpid(child, &status, 0) == child);
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
    assert(strstr(text, "FATAL debug-off fatal retained"));
    puts("debug_collection_test PASS");
}
