# Audio focus and desired-play policy (#153)

This document describes `dm/android/app/audio_focus.{h,c}`: a small, host-testable policy that decides whether the
App should be producing audio, given the platform audio-focus state, the user's playback intent, the App lifecycle,
the output route and the state the injected backend actually accepted.

It is **policy only**. It sends no `AudioManager` focus request, opens no `AAudio` stream, and makes no claim about
device playback, routes, latency, A/V sync or guest audio. Wiring it to `AudioFocusRequest` and to the real audio
backend is separate work.

## Android semantics this policy is based on

From Android's *Manage audio focus* guidance (developer.android.com/media/optimize/audio-focus):

- An app should request audio focus before it starts playing, and verify the request was granted.
- When another app takes focus, the app should stop or pause playback, or duck (lower) its volume.
- When playback stops, the app should abandon focus; it need not abandon focus when the user pauses and might resume.
- From Android 12 (API 31) the system manages focus and fades out an app's playback when another app requests focus.
  Before that the rules are advisory, but a well-behaved app still pauses/ducks.

## Policy choices (Android-specific, chosen here, not platform requirements)

These are explicit decisions of this slice, not claims about what Android mandates:

1. **Intent is separate from permission.** `desired_play` (user play/stop) is never set by focus, lifecycle or backend
   events. Focus and lifecycle only decide whether that intent may currently be honoured.
2. **Ducking is opt-in.** With `duck_enabled = 0` (the default) a duck-capable loss pauses playback. With ducking
   enabled, the policy applies `duck_gain`, which must be finite and in `(0, 1]`; invalid values are refused rather
   than clamped. The gain is restored to `1.0` on the next valid gain event.
3. **Permanent loss requires a fresh user play.** A permanent focus loss stops the stream, keeps the intent recorded
   but sets `requires_user_rearm`. A later focus gain does **not** restart playback; only a new `USER_PLAY` does.
   Transient loss resumes on gain only when intent, foreground state and route all still permit it.
4. **Stopping abandons authority.** `USER_STOP`, permanent loss and route disconnect stop the stream, increment the
   generation and invalidate every late callback of the old stream. Route disconnect is treated like AAudio's
   disconnected error: the stream object is invalid, so it is closed rather than kept paused.
5. **App pause keeps the stream.** Backgrounding pauses (does not stop) when the backend still holds a valid stream, so
   a resume does not have to recreate it. A resume still starts a new generation.
6. **Backend failures are latched, never papered over.** A rejected start/pause/stop/gain marks `DM_AF_OUT_ERROR`,
   records `last_failed_op`, invalidates the generation and stops issuing backend calls until an explicit
   `USER_PLAY`, `USER_STOP`, `APP_RESUME` or `ROUTE_AVAILABLE`. There is no automatic retry loop, and a failure is
   never reported as success.
7. **The real-time path never calls the backend.** `BACKEND_ERROR` is handled by state change only; no backend
   operation is invoked while handling it. Callers queue platform callbacks onto the control thread and call
   `dm_af_handle_generation` there.

## API sketch

```c
dm_af_policy policy;
dm_af_backend backend = {.ctx = &backend_state, .start = ..., .pause = ..., .stop = ..., .set_gain = ...};
dm_af_init(&policy, &backend, /*duck_enabled=*/0, /*duck_gain=*/0.2f);
dm_af_handle(&policy, DM_AF_EV_APP_RESUME);
dm_af_handle(&policy, DM_AF_EV_USER_PLAY);
dm_af_handle(&policy, DM_AF_EV_FOCUS_GAIN);      // may start the stream
dm_af_handle_generation(&policy, generation, DM_AF_EV_BACKEND_ERROR);  // stale generations are rejected
```

Events: `FOCUS_GAIN`, `FOCUS_LOSS`, `FOCUS_LOSS_TRANSIENT`, `FOCUS_CAN_DUCK`, `USER_PLAY`, `USER_STOP`,
`APP_RESUME`, `APP_PAUSE`, `ROUTE_DISCONNECT`, `ROUTE_AVAILABLE`, `BACKEND_ERROR`.

Outputs: `STOPPED`, `PLAYING`, `PAUSED_FOCUS`, `PAUSED_LIFECYCLE`, `PAUSED_ROUTE`, `ERROR`.

Every event is idempotent: when the state already matches the request, no backend operation is issued. Late events
carrying an older generation return `DM_AF_STALE` and change nothing.

## Tests

`dm/android/tests/audio_focus.c` drives the real controller with an injected backend that records calls and can fail
start/pause/stop/gain. It covers held transient loss and gain, pause while focus is absent then gain, user stop
before a late gain, replacement generations, duck/focus combinations, route disconnect, duplicate events and failing
backend operations (225 checks).

`dm/android/tests/audio_focus_mutation.py` is a private regression: it compiles the real controller with the
generation check removed, and with the user intent removed from the play decision, and requires the real tests to
fail in both cases. The CMake test `audio_focus_mutation` runs it.

Build/test: `ctest -R audio_focus` on a host configuration (ASAN/UBSAN used in CI). The module has no
Vulkan/Android dependency, so it also builds in the Vulkan=OFF configuration and cross-compiles for `arm64-v8a`.

## Limits

No `AudioManager`/`AudioFocusRequest` call, no `AAudio`/`Oboe` wiring, no App integration, no device playback, route,
latency, A/V-sync or guest-audio evidence. Parent issue #21 stays open.
