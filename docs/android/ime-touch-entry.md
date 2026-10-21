# Owned diagnostic IME touch entry (#201)

The diagnostic NativeActivity now exposes one owned Android `Button` labelled
`IME test / 输入测试`. It is a diagnostic launcher, not a game button, text
field, provider or full mobile-input implementation.

## Behavior

- On resume/input setup native installs the launcher through the Activity class
  loader. Installation only adds one button to the existing
  `android.R.id.content`; it never replaces the surface, HUD or other views and
  never requests the IME.
- A user click invokes the registered `nativeRequest(long)` with an opaque
  numeric owner token. Native checks the live owner token and the existing
  `text_begin_ready` active/resumed/focused/window/touch guard, then calls the
  actual `text_begin` and its checked Java host show. There is no synthetic
  `KeyEvent`, ADB/input injection, autoshow or raw native pointer in the token.
- The button consumes its own click and is non-focusable. It does not emit a
  gameplay touch source; physical keyboard `T`, BACK-pair gating and
  fresh-DOWN behavior remain unchanged.
- A second owner is rejected. Paused, unfocused, windowless or retired Apps
  reject a click. Teardown invalidates click authorization before removing
  exactly the owned button. If platform removal throws, the Java and native
  cleanup handles retain the old view/token (click disabled) and a retry can
  finish removal without adding another overlay; a new install retries cleanup
  first. A failing terminal `destroy` keeps the native App state/cleanup owner
  reachable instead of freeing it; a later retry that proves removal releases
  exactly once, while permanent failure stays explicit and click-inert. The
  launcher is removed on window destruction and App destroy.
- JNI setup checks each loader/signature/registration/global-ref stage and
  clears pending exceptions before cleanup, so an optional launcher failure
  cannot poison later NativeActivity calls.

## Tests

`dm/android/tests/ime-touch-entry-stubs/run.sh` compiles the exact launcher
against narrow Activity/View/ViewGroup/Button stubs and checks dedicated
install, no autoshow, idempotent install, owned click/token forwarding,
focus-neutral button, stale-token and teardown behavior, second-owner refusal,
attach failure and real `android-35/android.jar` compilation.

`native_text_jni` also extracts and runs the actual `ime_entry_install`,
`ime_entry_uninstall`, `native_ime_entry_request`, `text_begin_ready` and
`text_begin` against its JNI function table. It checks owner/token validation,
actual request-to-text begin/show, busy owner, stale-token request after
teardown, and token invalidation before the Java uninstall call.

## Limits

Guest text trigger/consumer, real device IME visibility and insets/occlusion,
full mobile touch equivalence and gameplay use remain open under parent #38.
Parents #38/#19/#14 remain open; no phone/adb/hardware execution is claimed.
