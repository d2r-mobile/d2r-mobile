// Android Vulkan 窗口与诊断绘制，尚非 NVN 回放后端。
// Android Vulkan window and diagnostic drawing; not yet an NVN playback backend.
#pragma once
#include <android/native_window.h>
#include <stdint.h>
typedef struct dm_vk_front dm_vk_front;
dm_vk_front *dm_vk_front_create(ANativeWindow *window);
int dm_vk_front_draw(dm_vk_front *front, const char *status, int runtime_ok, int32_t lx, int32_t ly, uint64_t buttons);
void dm_vk_front_destroy(dm_vk_front *front);
