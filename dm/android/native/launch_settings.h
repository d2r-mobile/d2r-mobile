#ifndef DM_ANDROID_LAUNCH_SETTINGS_H
#define DM_ANDROID_LAUNCH_SETTINGS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dm_android_launch_settings {
    int present;
    int operation_mode; /* -1 未指定，0 掌机，1 主机 | -1 unspecified, 0 handheld, 1 console */
    int input_receipts; /* -1 未指定，0 关闭，1 开启 | -1 unspecified, 0 disabled, 1 enabled */
    int debug_overlay; /* -1 未指定，0 关闭，1 开启 | -1 unspecified, 0 hidden, 1 shown */
    int resolution_width, resolution_height; /* -1 未指定；否则为出图分辨率（主机档，掌机档按 0.8 同比例）| -1 unspecified; otherwise the output resolution (docked; handheld scales by 0.8) */
} dm_android_launch_settings;

// 在 boot 设置 DM_TEMP 后、其他线程首次读取运行模式/输入日志前调用；不是运行中切换接口。
// Call in boot after setting DM_TEMP, before other threads first read the operation mode/input log; this is not a live-switch API.
// temporary_directory 必须是本应用自己的绝对 temp 路径，只读取其中 launch-settings.conf。
// temporary_directory must be this application's own absolute temp path; only launch-settings.conf inside it is read.
// 成功返回1（缺文件保留默认），失败返回0并写错误；失败不发布 result，解析错误不改变环境。
// Return 1 on success (a missing file preserves defaults), or 0 with an error; failures leave result untouched and parse errors do not change the environment.
// error/result 可以为 NULL；非 NULL error 必须同时给非零 error_capacity。调用方必须在失败时停止启动。
// error/result may be NULL; a non-NULL error requires a nonzero error_capacity. The caller must stop startup on failure.
int dm_android_launch_settings_apply(const char *temporary_directory,
                                     dm_android_launch_settings *result,
                                     char *error, size_t error_capacity);

#ifdef __cplusplus
}
#endif

#endif
