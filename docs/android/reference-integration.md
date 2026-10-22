# Android reference integration

Source: https://github.com/d2r-mobile/d2r-mobile.git
Pinned source snapshot
License: MIT; original copyright (c) 2026 jhaizhou. Reference Android implementation by the project and contributors. Preserve source notices and third-party licenses.

This branch imports the additive Android native, build/packing tools and Java sources from that snapshot for adaptation. It does not replace dm/host, dm/gfx/nvn, dm/ios/Sources or our existing dm/android runtime/app/tests. In particular our current game-specific touch/custom interaction remains authoritative; reference virtual gamepad controls are not the final touch implementation.

Current status (2026-10-22): the Android host/backend is linked and has run on the Xiaomi 15 Ultra. Startup, camp, normal save/exit and user-observed combat/items are documented in the root README. PR #225 consolidates the accepted Android integration into this fork's `main`; future changes target `main`, while original-project updates are reviewed through `upstream/main`. The initial source-only status below is historical. Preserve scanner/quarantine checks, ownership, W^X and failure boundaries. No game data, keys, saves, game-derived shaders or third-party driver binaries are committed. Full compatibility, touch parity, sustained 60 FPS and energy savings are not established.

## Owned runtime evidence (2026-10-19)

The shared patcher now has an explicit Android context target while retaining its Apple default and original-byte checks. Its actual emitters passed 124 owned ARM64 Unicorn executions across both targets. The imported `tests/probe.S` fixture was rewritten by that patcher, packed as an ELF library and executed with the generated native entry assembly on the Xiaomi 15 Ultra. Thread-context isolation passed four threads with 10,000 iterations each. Rewritten execution passed four threads with 1,000 iterations each, checking x18, guest TLS/TLR, test SVC/timer dispatch, host thread pointer, FP and NZCV preservation. Native executable exit status was zero.

These tests use owned synthetic instructions and synthetic service returns. They do not by themselves establish real SDK/service correctness, graphics integration, game startup or playability. Subsequent device observations have their own bounded acceptance in the root README and issue #1; the historical runtime contracts are not retroactively certified by a successful game launch.
