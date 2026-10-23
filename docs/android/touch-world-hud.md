# Android world HUD

The Android renderer follows the iOS map projection and rift HUD at upstream
`source snapshot`. It reads the existing host state; it does not create objectives or
advance the rift timer locally.

| Surface | State and behavior |
| --- | --- |
| Rift | `dm_rift_hud`: tier, progress, host timer, guardians, waves, overtime and completion |
| Map goals | `dm_map_goals`, map geometry and minimap rectangle; original labels/symbols, with offscreen clamping and skill-area exclusion |
| Drop feedback | Existing haptic queue, unchanged by this renderer |
| Skills | Existing current skill glyphs; upstream has no ammo/cooldown/mana tint to port |

The existing Activity owner publishes an 80-float snapshot and includes it in
the visual change signature. Java receives no guest pointers, and no additional
polling thread is created. Frontend, navigation and controller presentation hide
this touch HUD. Minimap coordinates are normalized against the game frame,
including its letterbox offset, rather than the physical screen.

## Local art

Generate original map art from your own extracted game data:

```sh
DM_GEN_ASSETS=/absolute/output python dm/ios/tools/gen_map_goal_art.py /absolute/RomFS
python dm/android/tools/build.py --game-run --map-goal-art /absolute/output/NativeHUD/mapgoal
```

The packer validates seven symbols, the cross and 31 labels in five languages
(163 PNGs). Traditional Chinese `_old` labels are optional. Files have bounded
dimensions and sizes, checked PNG chunks and CRCs, and cannot be symlinks.
Only allowlisted files are packaged. Generated game art is not committed.
Missing art omits the corresponding marker; it does not invent a target or
placeholder. Rift text uses existing `dm_ui` translations and the system bold
font, not an extracted game font.

## Checks

`world_hud_snapshot_test.c` checks the native snapshot, `WorldHudStateTest.java`
checks rift transitions and map projection (including four aspect ratios and
both axes of letterboxing), and `map_goal_art_test.py` checks pack validation.
The Java boundary gate also compiles the production Android renderer.

A separate APK exercises that same Canvas renderer with explicitly labeled,
owned fixture data. It is not included in the game APK and cannot run the game:

```sh
python dm/android/tests/world_hud_visual.py --output /absolute/fresh-fixture --map-goal-art /absolute/mapgoal
adb install -r /absolute/fresh-fixture/hud-fixture.apk
adb shell am start -S -n org.d2rmobile.android.hudfixture/org.d2r.android.port.WorldHudVisualActivity --es scenario overtime
adb uninstall org.d2rmobile.android.hudfixture
```

Scenarios: `progress`, `overtime`, `done`, `minimap`, `letterbox`, `navigation`,
and `missing`. Fixture rendering does not establish real endgame acceptance.
Actual rifts, map objectives, all classes and other devices remain tracked in
issue #302 until tested with real state.
