#ifndef DM_ANDROID_UI_TEXT_H
#define DM_ANDROID_UI_TEXT_H

#ifdef __cplusplus
extern "C" {
#endif

// Android-only UI strings generated from dm/android/i18n/android_ui_text.tsv.
// Returns NULL when the key is not an Android-added string; dm_ui keeps the
// upstream table and locale selection untouched.
const char *dm_android_ui_text(const char *key, int zh);

#ifdef __cplusplus
}
#endif

#endif
