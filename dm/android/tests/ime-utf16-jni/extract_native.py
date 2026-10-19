#!/usr/bin/env python3
"""Build a narrow real-JVM/JNI harness from the actual native text event source."""
import pathlib
import sys

root = pathlib.Path(__file__).resolve().parents[4]
sys.path.insert(0, str(root / "dm/android/tests"))
from native_callbacks_host import extract_function  # noqa: E402

source = pathlib.Path(sys.argv[1]).read_text()
out = pathlib.Path(sys.argv[2])
names = ["text_jni_stage_failed", "text_payload_valid", "text_payload_clear",
         "native_text_event"]
body = "\n\n".join(extract_function(source, name) for name in names)
prelude = r'''#include <jni.h>
#include <stdint.h>
#include <string.h>
#define DM_TEXT_MAX_UTF16_UNITS 512
#define LOG(...) ((void)0)
typedef struct {
    int text_active, text_show_accepted, arbiter;
    uint64_t text_session;
    uint32_t text_commit_events, text_delete_events, text_submit_events, text_cancel_events;
    uint64_t text_event_total;
    int text_last_units, text_payload_event, text_payload_units;
    uint64_t text_payload_session, text_payload_sequence;
    jchar text_payload[DM_TEXT_MAX_UTF16_UNITS];
} app_state;
static app_state app;
static app_state *g_text_app = &app;
static int64_t monotonic_ns(void) { return 0; }
static void touch_cancel(app_state *s, const char *reason, int64_t now) {
    (void)s; (void)reason; (void)now;
}
'''
suffix = r'''
JNIEXPORT void JNICALL Java_org_d2rmobile_android_diagnostic_DmTextEditorJniBridge_nativeTextEvent
(JNIEnv *env, jclass cls, jlong session, jint event, jstring text, jint units) {
    native_text_event(env, cls, session, event, text, units);
}
JNIEXPORT void JNICALL Java_org_d2rmobile_android_diagnostic_DmUtf16JniTest_testStart
(JNIEnv *env, jclass cls, jlong session) {
    (void)env; (void)cls;
    memset(&app, 0, sizeof(app));
    app.text_active = 1;
    app.text_session = (uint64_t)session;
    app.text_payload_event = -1;
}
JNIEXPORT jint JNICALL Java_org_d2rmobile_android_diagnostic_DmUtf16JniTest_testUnits
(JNIEnv *env, jclass cls) { (void)env; (void)cls; return app.text_payload_units; }
JNIEXPORT jint JNICALL Java_org_d2rmobile_android_diagnostic_DmUtf16JniTest_testEvent
(JNIEnv *env, jclass cls) { (void)env; (void)cls; return app.text_payload_event; }
JNIEXPORT jlong JNICALL Java_org_d2rmobile_android_diagnostic_DmUtf16JniTest_testSequence
(JNIEnv *env, jclass cls) { (void)env; (void)cls; return (jlong)app.text_payload_sequence; }
JNIEXPORT jstring JNICALL Java_org_d2rmobile_android_diagnostic_DmUtf16JniTest_testPayload
(JNIEnv *env, jclass cls) {
    (void)cls;
    return (*env)->NewString(env, app.text_payload, app.text_payload_units);
}
'''
out.write_text(prelude + body + suffix)
