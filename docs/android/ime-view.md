# Android text editor view / IME session (#179)

`dm/android/app/java/org/d2rmobile/android/diagnostic/DmTextEditorView.java`
is the smallest owned `View`/session boundary around the accepted
`DmInputConnection` (PR #175). It is not attached to the live NativeActivity and
does not handle keyboard insets, occlusion or a guest text consumer yet.

## Caller contract

- Construct `DmTextEditorView(context, InputMethodManager)`. The one-argument
  constructor obtains the real system service.
- `show(callerSink)` is the only way to start a session. It requests focus and
  calls the real `InputMethodManager.showSoftInput(view, SHOW_IMPLICIT)`.
  `true` means the platform accepted the request; it does **not** prove that a
  keyboard became visible. `isShowRequestAccepted()` reports that narrow fact.
- `isSessionActive()` is the only gameplay gate signal in this slice. A caller
  may pause game touch while it is true; no native/guest callback is invented.
- `onCreateInputConnection(EditorInfo)` fills a plain-text, no-suggestion
  contract with `IME_ACTION_DONE`, no personalized learning and the session's one
  accepted `DmInputConnection`. Repeated
  platform queries reuse that connection and preserve committed/composing text;
  `initialSelStart`/`initialSelEnd` report the current logical cursor. Only a new
  `show()` starts a new session and closes the old connection. The accepted
  connection's `MAX_TEXT_UTF16_UNITS` remains the length contract; Android 35
  `EditorInfo` has no `maxLength` field.
- `hide()`, `cancel()`, `release()`, focus/window-focus loss, detach and Back
  retire the session exactly once. Hide/cancel/release deliver `onCancel()` once;
  submit/connection-cancel deliver their own terminal callback and a later hide
  is a no-op. A BACK DOWN owned by the active session cancels once, and its
  matching UP is consumed so the Activity does not see a stray release; an idle
  BACK pair is passed through. One session owns one connection/sink; a new
  `show()` starts a new generation, and stale callbacks are rejected.

All control methods require the Android main looper and throw
`IllegalStateException` on another thread before touching platform state.
Show failure retires the session and returns `false`; it is never reported as
successful display.

## Tests

`dm/android/tests/ime-view-stubs/run.sh` compiles the exact production sources
and uses the narrow local stubs in the same directory for View, Context,
InputMethodManager, Looper and KeyEvent. It exercises explicit show, show false
cleanup, EditorInfo and actual connection, Chinese composing/commit, repeated
query document/cursor preservation with delete/submit, hide,
focus/window/detach/Back cancellation including the complete BACK pair, submit/
cancel retirement, main-thread rejection and stale-session isolation.
`mutation.py` reintroduces the BACK-UP leak and connection recreation on requery
and requires the actual test to fail for both. The exact new production sources
are also compiled against the real android-35/android.jar. No phone, adb, game,
JNI, SDK runtime or native input is executed.

## Remaining integration

The live App call site, window focus/insets behavior, keyboard occlusion, the
native owner and guest text consumption remain parent #38 work. This helper is
not evidence of a keyboard display, Chinese/English device input or playable
App acceptance. Parents #38/#19/#14 remain open.
