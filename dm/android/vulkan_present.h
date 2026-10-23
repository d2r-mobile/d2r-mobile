#pragma once
#include <stddef.h>
#include <android/native_window.h>
#ifdef __cplusplus
extern "C" {
#endif
/* UI线程：保留窗口；NULL暂停呈现。只更新窗口快照，不调用Vulkan或等待GPU。
 * UI thread: retains window; NULL suspends presentation. Only updates the window
 * snapshot; never calls Vulkan or waits for the GPU. */
void dm_android_vk_set_window(ANativeWindow *window);
/* 宿主线程：在dm_main前初始化一次；Surface暂时撤回时等待恢复。
 * Host thread: before dm_main, after set_window. Waits across surface withdrawal
 * and initializes genuine Vulkan replay once.
 * shader_directory remains external, is copied internally, and must hold .xgs. */
int dm_android_vk_initialize(const char *shader_directory,char *error,size_t bytes);
int dm_android_vk_has_window(void);
#ifdef __cplusplus
}
#endif
