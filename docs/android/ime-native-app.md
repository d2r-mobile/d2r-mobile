# NativeActivity IME/JNI integration (#190)

`dm/android/app/native.c` now reaches the accepted Activity text host through
`DmTextEditorJniBridge` and the packaged Java classes. This is an owned
diagnostic integration, not a game text-field implementation.

## Trigger and gate

- A physical-keyboard `KEYCODE_T` DOWN (repeat count 0, exact keyboard source,
  excluding gamepad source) explicitly starts one text session. A class-button
  bit or a combined keyboard/mouse source is not accepted. Normal App creation
  and lifecycle start do not attach the editor or show a keyboard.
- A request is rejected unless the App is resumed, focused, has a usable
  lifecycle window/surface and touch is currently allowed. A paused,
  unfocused, windowless or retired App never reaches Java `show`.
- Starting a session invalidates the current touch/gamepad contribution to
  neutral. While active, native touch and gamepad routes are gated (no
  movement/skill/gamepad delivery); the Java editor is the only input consumer.
- BACK DOWN while active ends text and is remembered; its matching BACK UP is
  consumed without finishing the Activity. Idle BACK behavior is unchanged.
  Pause, window destruction, input-queue loss, focus loss, submit, cancel and
  destroy end the session and require a fresh physical input after the gate
  reopens.
- This keyboard trigger is an owned diagnostic entry only; no mobile/game
  usability or persistent gameplay control is claimed.

## JNI boundary

The bridge is loaded through the Activity class loader and constructed only on
the main looper. Native callbacks carry the checked session id (never a freed C
pointer) plus the actual event payload as a `String`. Native reads the payload
with `GetStringLength`/`GetStringRegion`, verifies declared UTF-16 units, the
512-unit bound, paired-surrogate shape and cancellation's empty payload, then
copies it into one App-owned recent-event snapshot. Commit is the committed
fragment, delete the deleted fragment and submit the final text; they are not
appended or merged. Cancel/end/new session clear the content. Nothing is logged
and no guest text consumer is invented.

`show`/`hide`/`release` failures are checked and cleared at each JNI stage.
The native session/gate is published before Java `show()`, so a terminal
callback re-entering during `show()` is not overwritten into a claimed active
session; the local bridge is released and the request fails. Release
invalidates the Java session before host cleanup, so late callbacks are
ignored. Native clears its bridge/session before deleting globals and never
retains a raw pointer across a Java callback.

## Tests

`dm/android/tests/ime-native-app-stubs/run.sh` compiles the exact four Java
production classes and the actual bridge/host/View/connection against narrow
Activity/ViewGroup/View/IMM stubs, with a test-only event seam for native
delivery. It verifies construction inactivity, explicit show, bounded
commit/delete/submit events, host detachment, release idempotence and late
callback suppression; exact sources are compiled against real
`android-35/android.jar`.

`native_callbacks` host regression compiles the actual extracted native
functions, including the new text routing/gate, against owned Android/WSI
boundaries; its cases cover partial/combined source rejection, active touch
neutrality, BACK DOWN/UP consumption and fresh DOWN after retirement.
`native_text_jni` extracts the exact `text_begin_ready`, `text_begin`,
`native_text_event` and `text_end` functions and runs them against a narrow JNI
function table. It covers Activity-loader show, callback registration/signature,
bounded/stale session events, submit/release, show false/exception, global-ref
rollback, owner busy, paused/unfocused/windowless/state guards, pause/window-loss
retirement and a terminal callback re-entering synchronously from `show()`.
The actual NDK `dm_android_app` target is linked separately. No
phone/adb/game/SDK runtime/rtld/hardware is used.

## Remaining integration

Actual native caller UI attachment, real keyboard visibility/insets/occlusion,
Chinese/English device text input and the game/guest text consumer remain
parent #38 work. Parents #38/#19/#14 remain open; this is not App/game/device
acceptance.
