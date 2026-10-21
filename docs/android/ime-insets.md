# Android IME inset observer (#196)

`DmTextEditorHost` now owns an additive `DmImeInsetsObserver` attached only to
its own 1x1 editor View. It does not replace the Activity/root
`OnApplyWindowInsetsListener`, change accepted show/hide/connection APIs, or
modify window layout/global settings.

On API 30+ the listener delegates to the narrow `DmImeInsetsApi30` helper and
uses the real platform contract
`WindowInsets.Type.ime()`, `WindowInsets.isVisible(type)` and
`WindowInsets.getInsets(type)`. Visible geometry is published as an immutable
`DmImeInsetsObservation` with left/top/right/bottom clamped to non-negative
integers and bounded by the actual root View extent (the editor's 1x1 rectangle
is not used). Compatible horizontal/vertical edge pairs are bounded with
overflow-safe arithmetic; asymmetric insets are preserved when they fit.
Hidden IME publishes a neutral geometry with `imeVisible=false`. If the root
extent is unavailable or unmeasured, the observation is unknown rather than a
claimed valid bounded geometry.

On API 29 and below the observer publishes an explicit unknown observation and
does not call the API 30 Type/isVisible/getInsets methods. No window-height or
other heuristic is used to infer keyboard visibility.

Attach/detach require the main looper. Each attach installs a fresh listener
with a captured attach token; a listener retained from an older show on the
same editor is rejected even after a newer attach. `DmTextEditorHost.hide()` and
`release()` detach and invalidate the observer before the editor session is
retired; a callback retained from an older session is therefore harmless.
Construction/show/hide/release keep the existing peer-content preservation and
idempotence semantics.

`DmImeInsetsTest` compiles the actual host/View/connection/observer classes
against narrow host stubs and separately against the real Android 35 jar. It
covers visible/hidden, asymmetric and negative geometry, root-extent bounding,
oversized/INT_MAX edges, reattach old/new listener separation, unmeasured root
extent, stale callbacks, hide/release, peer content, off-main rejection and
API 29 explicit unknown with no API 30 calls. This is host/real-jar evidence,
not device IME or keyboard-visibility proof; a native viewport consumer remains
the later #190 follow-up and parent #38 stays open.
