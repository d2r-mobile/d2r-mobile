// APK 内模块定位，不执行尚未接通系统服务的游戏入口。
// Locate modules inside the APK; do not execute game entry before its services are connected.
#define _GNU_SOURCE
#include "runtime.h"
#include <android/native_activity.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

extern int dm_android_probe(const char *, const char *, char *, size_t);
extern int dm_android_game_modules(const char *, const char *, char *, size_t);
int dm_android_app_probe(ANativeActivity *activity, char *report, size_t cap) {
    Dl_info info;
    if (!dladdr((void *)dm_android_app_probe, &info)) { snprintf(report, cap, "Cannot locate installed library"); return -1; }
    char directory[1024]; snprintf(directory, sizeof directory, "%s", info.dli_fname);
    char *slash = strrchr(directory, '/');
    if (!slash) { snprintf(report, cap, "Invalid library path"); return -1; }
    *slash = 0;
    return dm_android_probe(directory, activity->internalDataPath, report, cap);
}

int dm_android_app_game(ANativeActivity *activity, char *report, size_t cap) {
    Dl_info info;
    if (!dladdr((void *)dm_android_app_game, &info)) return -1;
    char directory[1024]; snprintf(directory, sizeof directory, "%s", info.dli_fname);
    char *slash = strrchr(directory, '/'); if (!slash) return -1;
    *slash = 0;
    return dm_android_game_modules(directory, activity->internalDataPath, report, cap);
}
