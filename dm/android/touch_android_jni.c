#include <jni.h>
#include "touch_android.h"

/* 使用命名JNI入口，与宿主已有JNI_OnLoad兼容。 | Named JNI entry points coexist with the host's JNI_OnLoad. */
JNIEXPORT void JNICALL Java_com_xiaoboluo_d2r_TouchOverlay_nativeEvent(JNIEnv *env, jclass cls, jint phase, jint slot, jfloat x, jfloat y) {
    (void)env; (void)cls;
    dm_android_touch_event(phase,slot,x,y);
}
JNIEXPORT void JNICALL Java_com_xiaoboluo_d2r_TouchOverlay_nativeResize(JNIEnv *env, jclass cls, jfloat width, jfloat height, jfloat left, jfloat right, jfloat bottom, jboolean tablet) {
    (void)env; (void)cls;
    dm_android_touch_tablet(tablet);
    dm_android_touch_resize(width,height,left,right,bottom);
}
JNIEXPORT void JNICALL Java_com_xiaoboluo_d2r_TouchOverlay_nativeLayout(JNIEnv *env, jclass cls, jfloatArray dst) {
    (void)cls;
    if (!dst || (*env)->GetArrayLength(env,dst)<DM_ANDROID_TOUCH_DRAW_FLOATS) return;
    float data[DM_ANDROID_TOUCH_DRAW_FLOATS];
    dm_android_touch_layout(data);
    (*env)->SetFloatArrayRegion(env,dst,0,DM_ANDROID_TOUCH_DRAW_FLOATS,data);
}
