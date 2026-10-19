// 将后台调度提示映射到 Android nice；不提高需要特权的优先级。
// Map background scheduling hints to Android nice without raising privileged priorities.
#pragma once
#include <pthread.h>
#include <sys/resource.h>
#include <unistd.h>
typedef unsigned qos_class_t;
enum { QOS_CLASS_UNSPECIFIED = 0, QOS_CLASS_USER_INTERACTIVE = 0x21, QOS_CLASS_USER_INITIATED = 0x19, QOS_CLASS_UTILITY = 0x11, QOS_CLASS_BACKGROUND = 0x9 };
static inline int pthread_set_qos_class_self_np(qos_class_t qos, int relative) {
    (void)relative;
    if (qos == QOS_CLASS_UTILITY || qos == QOS_CLASS_BACKGROUND) return setpriority(PRIO_PROCESS, gettid(), qos == QOS_CLASS_BACKGROUND ? 10 : 5);
    return 0;
}
static inline int pthread_get_qos_class_np(pthread_t thread, qos_class_t *qos, int *relative) {
    int nice = getpriority(PRIO_PROCESS, pthread_gettid_np(thread));
    *qos = nice > 0 ? QOS_CLASS_UTILITY : QOS_CLASS_USER_INTERACTIVE;
    *relative = 0; return 0;
}
