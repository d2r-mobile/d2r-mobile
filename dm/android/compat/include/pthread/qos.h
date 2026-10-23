// 安卓构建：苹果 QoS 类 → Linux nice 值（线程级 setpriority）。数值与苹果 <sys/qos.h> 相同，日志里的 QoS 读数两边可比。
// 应用进程能否降到负 nice 由系统限额决定，失败不致命（保持原优先级）；pthread_get_qos_class_np 回读本线程最后一次设的类。
// Android build: Apple QoS classes → Linux nice values (per-thread setpriority). The values are the same as Apple's <sys/qos.h>, so QoS readings in logs are comparable on both.
// Whether an app process can go to a negative nice is up to the system limits; failure is not fatal (the original priority is kept); pthread_get_qos_class_np reads back the class this thread last set.
#pragma once
#include <pthread.h>
#include <sys/resource.h>
#include <unistd.h>
typedef enum {
    QOS_CLASS_USER_INTERACTIVE = 0x21, QOS_CLASS_USER_INITIATED = 0x19, QOS_CLASS_DEFAULT = 0x15,
    QOS_CLASS_UTILITY = 0x11, QOS_CLASS_BACKGROUND = 0x09, QOS_CLASS_UNSPECIFIED = 0x00,
} qos_class_t;
static __thread qos_class_t dm_qos_self_ = QOS_CLASS_UNSPECIFIED;
static inline int dm_qos_nice_(qos_class_t c) {
    switch (c) {
    case QOS_CLASS_USER_INTERACTIVE: return -10;   // 安卓 THREAD_PRIORITY_URGENT_DISPLAY(-8) 与 AUDIO(-16) 之间 | Between Android THREAD_PRIORITY_URGENT_DISPLAY(-8) and AUDIO(-16)
    case QOS_CLASS_USER_INITIATED: return -4;      // THREAD_PRIORITY_DISPLAY
    case QOS_CLASS_UTILITY: return 5;
    case QOS_CLASS_BACKGROUND: return 10;          // THREAD_PRIORITY_BACKGROUND
    default: return 0;
    }
}
static inline int pthread_set_qos_class_self_np(qos_class_t c, int rel) {
    (void)rel; dm_qos_self_ = c;
    return setpriority(PRIO_PROCESS, (id_t)gettid(), dm_qos_nice_(c)) ? -1 : 0;
}
static inline int pthread_get_qos_class_np(pthread_t t, qos_class_t *c, int *rel) {
    (void)t; if (c) *c = dm_qos_self_; if (rel) *rel = 0; return 0;
}
static inline qos_class_t qos_class_self(void) { return dm_qos_self_; }
