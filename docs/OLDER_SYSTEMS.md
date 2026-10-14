# Older systems: iOS 18 – 25 and macOS 15

**English** | [简体中文](OLDER_SYSTEMS.zh-CN.md)

The default build requires iOS 26 / macOS 26: the graphics side is built on a graphics interface that only exists from those systems on (Metal 4). Devices that have not moved to 26 use the "older-API build" — the same source, the same game, the same features, with the graphics side using interfaces that older systems also have. Its minimum is **iOS 18.0 / macOS 15.0**.

## Which one do I need

Look at the system first, then at the chip. The two are independent and can be combined (every device is listed under "[Which devices are supported](../README.md#which-devices-are-supported)" in the README):

| Your device | System is iOS 26 or later | System is iOS 18 – 25 |
|---|---|---|
| **A17 Pro and newer** (iPhone 15 Pro and later, iPhone 16 and later) | Default build, nothing to add | Older-API build |
| **A16 and earlier** (iPhone 14 series, iPhone 15 / 15 Plus and before) | Default build + Legacy Device Pack | Older-API build + Legacy Device Pack |

- The **system** decides which build to use (default / older-API).
- The **chip** decides whether you also need a Legacy Device Pack: the GPU of the A16 and earlier does not accept the format of the game textures. See [LEGACY_DEVICES.md](LEGACY_DEVICES.md).
- On a Mac only the system matters: macOS 26 or later uses the default build, macOS 15 – 25 the older-API build; no Apple silicon Mac needs the texture pack.

## Building the older-API version

### iPhone / iPad, with a Mac and Xcode

Preparation is unchanged (`./setup.sh`); add one switch when building:

```bash
cd dm/ios
DM_IOS_M3=1 ./run.sh build
```

It changes the deployment target to iOS 18.0 and switches the graphics side to the older interfaces. A free Apple ID still works: `DM_FREE_ACCOUNT=1` can be set together with it. For A16 and earlier devices, also bring the texture pack (`DM_ASTC_PACK`) as described in [LEGACY_DEVICES.md](LEGACY_DEVICES.md).

### iPhone / iPad, without Xcode

The Releases page has two shells. Pick the one for your system; every other step is exactly as in [NO_XCODE.md](NO_XCODE.md):

| Shell | For |
|---|---|
| `D2RMobile-shell-build-input.zip` | iOS 26 and later |
| `D2RMobile-shell-ios18-build-input.zip` | iOS 18 – 25 |

The packaging tool accepts both shells with the same command. For A16 and earlier devices add `D2RM_ASTC_PACK` as well (see the "Older devices" section of NO_XCODE) — the texture pack depends only on the chip, and both shells use the same pack.

### Mac

```bash
source ~/dm-work/build/cache/dm/venv/bin/activate
DM_MAC_M3=1 dm/tools/mac_app.sh        # produces ~/dm-work/out/macapp/D2R Mobile M3.app
```

It recompiles the shader libraries for macOS 15 (about 3 minutes, the first time only) into the output directory. The older-API App is called "D2R Mobile M3" and has its own save and log directory, so it can sit next to the default App. Like the default App it runs only on the Mac that built it; **do not delete or move the output directory**, the recompiled shader libraries are in it.

## How it differs from the default build

- Picture, gameplay and save format are the same.
- The graphics side submits about 20 – 30% more commands per draw, so the same picture costs some more CPU time; the frame-time difference we measured is within measurement noise.
- The resource-download extension is not included (it only serves Apple-hosted asset packs, which public builds do not use anyway).

## In practice

macOS 15: a player in our test group has played the older-API build on a Mac mini M4 running macOS 15.7. iOS 18: a community developer has run the game on iOS 18 on their own branch with the same kind of change; the iOS 18 package of this repository's version has just gone out to our test group. If you run into a problem, open an Issue with the device model, the system version and the diagnostic log (pause menu → "Share diagnostic log").
