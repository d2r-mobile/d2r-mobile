# Android reference integration

Source: https://github.com/d2r-mobile/d2r-mobile.git
Pinned source snapshot
License: MIT; original copyright (c) 2026 jhaizhou. Reference Android implementation by the project and contributors. Preserve source notices and third-party licenses.

This branch imports the additive Android native, build/packing tools and Java sources from that snapshot for adaptation. It does not replace dm/host, dm/gfx/nvn, dm/ios/Sources or our existing dm/android runtime/app/tests. In particular our current game-specific touch/custom interaction remains authoritative; reference virtual gamepad controls are not the final touch implementation.

Status: source import only, not linked, not tested, not admitted for execution of real SDK/game. Imported Linux-only tool paths require macOS/NDK adaptation. The imported patcher is a separate implementation whose eligibility must be reviewed; it must not silently replace or weaken our production scanner/quarantine. No game data, keys, saves, game-derived shaders or third-party driver binaries are imported. Build and device evidence will be added as it is produced.
