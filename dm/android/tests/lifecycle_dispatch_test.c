// 真派发器的CPU并发回归：GPU挂起等待Surface时，UI仍能提交恢复，最终先开图形再音频/焦点。
// CPU concurrency regression of the real dispatcher: UI can post resume while suspend waits for Surface; graphics resumes before audio/focus.
// 用POSIX C编译器运行：cc -pthread lifecycle_dispatch_test.c -o <仓外产物>，再运行该产物。
// Run with a POSIX C compiler: cc -pthread lifecycle_dispatch_test.c -o <external-output>; run the resulting test.
#include "../lifecycle_dispatch.h"
#include <assert.h>
#include <errno.h>
#include <time.h>
#ifdef NDEBUG
#error "Build this CPU regression with assertions enabled"
#endif

static pthread_mutex_t mock_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t mock_changed = PTHREAD_COND_INITIALIZER;
static int suspend_entered, surface_restored, post_finished, graphics_paused, audio_paused, final_focus;
static int order[64], order_count;
static dm_lifecycle_dispatch dispatcher;

static struct timespec deadline(void) { struct timespec t; clock_gettime(CLOCK_REALTIME, &t); t.tv_sec += 3; return t; }
static void record(int value) { assert(order_count < 64); order[order_count++] = value; }
static void focus(int state) { pthread_mutex_lock(&mock_mutex); final_focus = state; record(10 + state); pthread_mutex_unlock(&mock_mutex); }
static void audio(int paused) { pthread_mutex_lock(&mock_mutex); audio_paused = paused; record(20 + paused); pthread_mutex_unlock(&mock_mutex); }
static void graphics(int paused) {
    pthread_mutex_lock(&mock_mutex);
    if (paused) {
        suspend_entered = 1; record(31); pthread_cond_broadcast(&mock_changed);
        while (!surface_restored) pthread_cond_wait(&mock_changed, &mock_mutex);
    } else record(30);
    graphics_paused = paused;
    pthread_mutex_unlock(&mock_mutex);
}
static void *ui_resume(void *unused) {
    (void)unused;
    dm_lifecycle_post(&dispatcher, 2);
    dm_lifecycle_post(&dispatcher, 3);
    pthread_mutex_lock(&mock_mutex); post_finished = 1; pthread_cond_broadcast(&mock_changed); pthread_mutex_unlock(&mock_mutex);
    return NULL;
}
int main(void) {
    dm_lifecycle_ops ops = {focus, audio, graphics};
    assert(!dm_lifecycle_start(&dispatcher, ops));
    dm_lifecycle_post(&dispatcher, 1);
    struct timespec until = deadline();
    pthread_mutex_lock(&mock_mutex);
    while (!suspend_entered) assert(!pthread_cond_timedwait(&mock_changed, &mock_mutex, &until));
    pthread_mutex_unlock(&mock_mutex);
    pthread_t ui; assert(!pthread_create(&ui, NULL, ui_resume, NULL));
    until = deadline();
    pthread_mutex_lock(&mock_mutex);
    int timed_out = 0;
    while (!post_finished) { if (pthread_cond_timedwait(&mock_changed, &mock_mutex, &until) == ETIMEDOUT) { timed_out = 1; break; } }
    // 无论测试成败都先解除模拟GPU等待，避免失败测试自身把线程遗留阻塞。
    // Release the simulated GPU even on failure so a failed test does not leave its worker blocked.
    surface_restored = 1; pthread_cond_broadcast(&mock_changed); pthread_mutex_unlock(&mock_mutex);
    pthread_join(ui, NULL); assert(!timed_out);
    until = deadline();
    pthread_mutex_lock(&dispatcher.mutex);
    while (dispatcher.completed != dispatcher.requested) assert(!pthread_cond_timedwait(&dispatcher.changed, &dispatcher.mutex, &until));
    pthread_mutex_unlock(&dispatcher.mutex);
    dm_lifecycle_stop(&dispatcher);
    assert(!graphics_paused && !audio_paused && final_focus == 1);
    int resumed = -1;
    for (int i = 0; i < order_count; i++) if (order[i] == 30) resumed = i;
    assert(resumed >= 0 && resumed + 2 < order_count && order[resumed + 1] == 20 && order[resumed + 2] == 11);
    return 0;
}
