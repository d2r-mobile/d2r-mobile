// 安卓日志接口 → hilog（替身，见 ../README.md） | Android log API → hilog (stand-in, see ../README.md)
#pragma once
#include <hilog/log.h>
#include <stdarg.h>
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { ANDROID_LOG_UNKNOWN = 0, ANDROID_LOG_DEFAULT, ANDROID_LOG_VERBOSE, ANDROID_LOG_DEBUG, ANDROID_LOG_INFO, ANDROID_LOG_WARN, ANDROID_LOG_ERROR, ANDROID_LOG_FATAL };
static inline LogLevel dm_ohos_level(int p) { return p >= ANDROID_LOG_FATAL ? LOG_FATAL : p == ANDROID_LOG_ERROR ? LOG_ERROR : p == ANDROID_LOG_WARN ? LOG_WARN : p == ANDROID_LOG_INFO ? LOG_INFO : LOG_DEBUG; }
// hilog 默认把参数打成 <private>：先格式化再以 {public} 整串输出 | hilog prints arguments as <private> by default: format first, then print the whole string as {public}
static inline int __android_log_write(int prio, const char *tag, const char *text) {
    return OH_LOG_Print(LOG_APP, dm_ohos_level(prio), 0xD2E0, tag ? tag : "dm", "%{public}s", text ? text : "");
}
static inline int __android_log_print(int prio, const char *tag, const char *fmt, ...) {
    char buf[1024]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    return __android_log_write(prio, tag, buf);
}
#ifdef __cplusplus
}
#endif
