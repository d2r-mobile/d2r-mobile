// 宿主生命周期：优雅退出（SIGTERM；iOS 由 App 终止回调调 dm_host_shutdown）。
// Host lifecycle: graceful exit (SIGTERM; on iOS the App termination callback calls dm_host_shutdown).
#include "hle.h"
#ifdef __ANDROID__
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#else
#include <dispatch/dispatch.h>
#endif
#include <stdatomic.h>
#include <signal.h>
#include <unistd.h>
void tact_prefetch_stop(void);
void granny_prof_dump(void);
void hlecount_flush(void);
#ifdef __ANDROID__
// Signal handling only writes one byte to a pipe; a normal thread performs shutdown.
static int term_pipe[2] = {-1, -1};
static void *shutdown_timeout(void *arg) {
    (void)arg; usleep(1500000); LOG("Android shutdown timed out"); _exit(0); return NULL;
}
static void term_signal(int signal) { (void)signal; int saved = errno; uint8_t value = 1; (void)write(term_pipe[1], &value, 1); errno = saved; }
static void *term_thread(void *arg) {
    (void)arg;
    struct pollfd fd = {.fd = term_pipe[0], .events = POLLIN};
    for (;;) {
        int result = poll(&fd, 1, -1);
        if (result < 0 && errno == EINTR) continue;
        if (result > 0 && (fd.revents & POLLIN)) { dm_host_shutdown("SIGTERM"); break; }
        if (result < 0 || (fd.revents & (POLLERR | POLLHUP | POLLNVAL))) break;
    }
    return NULL;
}
#endif
void dm_host_shutdown(const char *why) {
    static _Atomic int once; if (atomic_exchange(&once, 1)) return;
    uint64_t t0 = now_ns(); LOG("收到 %s：优雅退出", why);
#ifdef __ANDROID__
    pthread_t timeout; if (!pthread_create(&timeout, NULL, shutdown_timeout, NULL)) pthread_detach(timeout);
#else
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 1500 * NSEC_PER_MSEC), dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), ^{
        LOG("【红】优雅退出 1.5 秒未完成，直接退出"); _exit(0); });
#endif
    tact_prefetch_stop();
    granny_prof_dump();
    hlecount_flush();
    dm_audio_suspend(1);
    dm_gfx_suspend(1);
    LOG("优雅退出完成（%.3f 秒）", (now_ns() - t0) / 1e9);
    _exit(0);
}
void dm_term_install(void) {
#ifdef __ANDROID__
    if (pipe2(term_pipe, O_CLOEXEC | O_NONBLOCK)) die("Android termination pipe failed");
    pthread_t thread;
    if (pthread_create(&thread, NULL, term_thread, NULL)) die("Android termination thread failed");
    pthread_detach(thread);
    struct sigaction action = {.sa_handler = term_signal, .sa_flags = SA_RESTART};
    sigemptyset(&action.sa_mask); sigaction(SIGTERM, &action, NULL);
#else
    signal(SIGTERM, SIG_IGN);
    static dispatch_source_t src;
    src = dispatch_source_create(DISPATCH_SOURCE_TYPE_SIGNAL, SIGTERM, 0, dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0));
    dispatch_source_set_event_handler(src, ^{ dm_host_shutdown("SIGTERM"); });
    dispatch_resume(src);
#endif
}
