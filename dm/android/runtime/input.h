// Android 前端到客体手柄的线程安全边界；返回布局与上游 hle_hid.c 的 NpadState 一致。
// Thread-safe Android frontend -> guest controller boundary; return layout matches upstream hle_hid.c's NpadState.
#pragma once
#include <stdint.h>
typedef struct dm_android_pad dm_android_pad;
typedef struct { uint64_t buttons; int32_t lx, ly, rx, ry; int connected; } dm_pad_sample;
typedef struct {
    int64_t sampling; uint64_t buttons;
    int32_t lx, ly, rx, ry;
    uint32_t attributes, padding;
} dm_npad_state;
_Static_assert(sizeof(dm_npad_state) == 0x28, "Switch NpadState ABI");
dm_android_pad *dm_android_pad_create(void);
// 发布完整快照；短暂按下沿保留到下一次读取，断开时清空；销毁前先停止全部调用者。
// Publish coherent snapshots; retain short press edges until next read, clear on disconnect; stop all callers before destruction.
void dm_android_pad_publish(dm_android_pad *pad, const dm_pad_sample *sample);
dm_npad_state dm_android_pad_read(dm_android_pad *pad);
void dm_android_pad_destroy(dm_android_pad *pad);
