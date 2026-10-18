// 手动/自动复位事件，按单调时钟处理超时并循环检查虚假唤醒；依据 POSIX 条件变量。
// Manual / auto-reset events with monotonic deadlines and spurious-wakeup loops; based on POSIX condition variables.
#include "host.h"
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>

struct dm_event { pthread_mutex_t mutex; pthread_cond_t condition; int signaled, auto_clear; size_t waiters; };

dm_event *dm_event_create(int auto_clear) {
    if (auto_clear != 0 && auto_clear != 1) { errno = EINVAL; return NULL; }
    dm_event *event = calloc(1, sizeof *event);
    if (!event) return NULL;
    int rc = pthread_mutex_init(&event->mutex, NULL);
    if (rc) { free(event); errno = rc; return NULL; }
    pthread_condattr_t attr;
    rc = pthread_condattr_init(&attr);
    if (rc) { pthread_mutex_destroy(&event->mutex); free(event); errno = rc; return NULL; }
#if !defined(__APPLE__)
    rc = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
#endif
    if (!rc) rc = pthread_cond_init(&event->condition, &attr);
    pthread_condattr_destroy(&attr);
    if (rc) { pthread_mutex_destroy(&event->mutex); free(event); errno = rc; return NULL; }
    event->auto_clear = auto_clear;
    return event;
}

int dm_event_signal(dm_event *event) {
    pthread_mutex_lock(&event->mutex); event->signaled = 1;
    int rc = event->auto_clear ? pthread_cond_signal(&event->condition) : pthread_cond_broadcast(&event->condition);
    pthread_mutex_unlock(&event->mutex); return rc;
}

int dm_event_clear(dm_event *event) {
    pthread_mutex_lock(&event->mutex); event->signaled = 0;
    pthread_mutex_unlock(&event->mutex); return 0;
}

int dm_event_wait(dm_event *event, int64_t timeout_ns) {
    if (timeout_ns < -1) return EINVAL;
    struct timespec deadline = {0};
    if (timeout_ns >= 0) {
        if (clock_gettime(CLOCK_MONOTONIC, &deadline)) return errno;
        deadline.tv_sec += timeout_ns / 1000000000;
        deadline.tv_nsec += timeout_ns % 1000000000;
        if (deadline.tv_nsec >= 1000000000) { deadline.tv_sec++; deadline.tv_nsec -= 1000000000; }
    }
    pthread_mutex_lock(&event->mutex); event->waiters++;
    int rc = 0;
    // 调用方不得取消正在等待的线程；先发信号、join 再销毁，避免条件变量取消时的锁所有权变化。
    // Caller must not cancel a waiting thread; signal and join before destruction to avoid cancellation's mutex ownership changes.
    while (!event->signaled && !rc) {
        if (timeout_ns == 0) { rc = ETIMEDOUT; break; }
        if (timeout_ns < 0) rc = pthread_cond_wait(&event->condition, &event->mutex);
        else {
#if defined(__APPLE__)
            struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
            struct timespec remaining = {.tv_sec = deadline.tv_sec - now.tv_sec, .tv_nsec = deadline.tv_nsec - now.tv_nsec};
            if (remaining.tv_nsec < 0) { remaining.tv_sec--; remaining.tv_nsec += 1000000000; }
            if (remaining.tv_sec < 0) rc = ETIMEDOUT;
            else rc = pthread_cond_timedwait_relative_np(&event->condition, &event->mutex, &remaining);
#else
            rc = pthread_cond_timedwait(&event->condition, &event->mutex, &deadline);
#endif
        }
    }
    if (event->signaled) { rc = 0; if (event->auto_clear) event->signaled = 0; }
    event->waiters--; pthread_mutex_unlock(&event->mutex);
    return rc;
}

int dm_event_destroy(dm_event *event) {
    pthread_mutex_lock(&event->mutex);
    if (event->waiters) { pthread_mutex_unlock(&event->mutex); return EBUSY; }
    pthread_mutex_unlock(&event->mutex);
    int rc = pthread_cond_destroy(&event->condition);
    if (rc) return rc;
    pthread_mutex_destroy(&event->mutex); free(event); return 0;
}
