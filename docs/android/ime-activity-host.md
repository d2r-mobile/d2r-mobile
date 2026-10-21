# Android Activity text-editor host (#187)

`dm/android/app/java/org/d2rmobile/android/diagnostic/DmTextEditorHost.java`
mounts the accepted `DmTextEditorView` into a real Activity's existing
`android.R.id.content` view. It is an independent boundary: no native/JNI
caller, keyboard insets/occlusion handling or guest text consumer is wired yet.

## Caller contract

- Construct `DmTextEditorHost(activity, InputMethodManager)` on the main
  looper. Construction only creates the owned 1x1 transparent editor and does
  not attach it or show a keyboard.
- `show(callerSink)` is the only attach/show entry. It adds only the owned
  editor, requires the real `View.requestFocus()` to succeed, and only then
  delegates to the accepted `DmTextEditorView.show`. Focus refusal detaches and
  returns false before any IMM show request. `true` means the platform accepted
  the show request, not that a keyboard is visible. False or a platform
  exception rolls back the attachment; missing content also returns false.
- `hide()` removes only the owned editor, clears its focus and retires the
  active session exactly once. `release()` is terminal/idempotent and rejects
  future shows. Both detach the owned view in a `finally` path even if the
  platform IMM hide throws; the exception is propagated honestly, native/peer
  content is untouched, and a later cleanup/retry remains possible. A released
  or inactive host never leaves the editor mounted, so it cannot intercept
  gameplay touch.
- `isSessionActive()` is the caller gate signal. `isShowRequestAccepted()` is
  only the narrow platform request result. `isAttached()` reports the owned
  editor attachment.
- The accepted View/session handles composition, commit, delete, submit,
  cancel, requery cursor and BACK pair semantics. The host does not duplicate
  text state or add a JNI queue. Its wrapper only detaches the owned view when
  a terminal submit/cancel or focus loss retires the session.

## Tests

`dm/android/tests/ime-activity-host-stubs/run.sh` compiles the exact production
sources and actual host/View/connection against an isolated Activity/ViewGroup/
View/IMM boundary. It verifies construction does not attach/show, existing
NativeActivity-like content survives attach/release, focus refusal rolls back
before IMM, explicit show obtains the attached editor, IMM false/platform
exception rollback, hide/release detachment even when IMM hide throws, focus
loss and submit detach, repeated show/hide/release, old-host isolation, and
off-main rejection. Exact production sources are also compiled against real
`android-35/android.jar`. No phone, adb, game, JNI, SDK runtime or native input
execution is used.

## Remaining integration

The actual native caller attachment, window focus forwarding, keyboard
visibility/insets/occlusion, guest text consumption and device tests remain
parent #38. This host is not evidence that the live App displays a keyboard or
accepts text, and parents #38/#19/#14 remain open.
