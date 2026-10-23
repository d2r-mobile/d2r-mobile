// ANativeWindow → OHNativeWindow（替身，见 ../README.md）。引用计数与尺寸查询语义一一对应。
// ANativeWindow → OHNativeWindow (stand-in, see ../README.md). Reference counting and size queries map one to one.
#pragma once
#include <native_window/external_window.h>
#include <stdint.h>
typedef OHNativeWindow ANativeWindow;
#define ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_DEFAULT 0
static inline void ANativeWindow_acquire(ANativeWindow *w) { OH_NativeWindow_NativeObjectReference(w); }
static inline void ANativeWindow_release(ANativeWindow *w) { OH_NativeWindow_NativeObjectUnreference(w); }
static inline int32_t dm_ohos_geometry(ANativeWindow *w, int want_width) {
    int32_t h = 0, wd = 0; OH_NativeWindow_NativeWindowHandleOpt(w, GET_BUFFER_GEOMETRY, &h, &wd); return want_width ? wd : h;
}
static inline int32_t ANativeWindow_getWidth(ANativeWindow *w) { return dm_ohos_geometry(w, 1); }
static inline int32_t ANativeWindow_getHeight(ANativeWindow *w) { return dm_ohos_geometry(w, 0); }
// 缓冲尺寸：SET_BUFFER_GEOMETRY 参数顺序为宽、高（GET 为高、宽）| buffer size: SET_BUFFER_GEOMETRY takes width, height (GET returns height, width)
static inline int32_t ANativeWindow_setBuffersGeometry(ANativeWindow *w, int32_t width, int32_t height, int32_t format) {
    (void)format; return OH_NativeWindow_NativeWindowHandleOpt(w, SET_BUFFER_GEOMETRY, width, height);
}
