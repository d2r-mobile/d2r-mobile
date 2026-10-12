# Getting your game files

**You must own the game.** This project only lets you run the Nintendo Switch release of Diablo II: Resurrected, which you have already bought, on your own iPhone or iPad. This repository does not contain any game files and never will. Do not ask others for game files or share them, and do not post game files or download links in issues or pull requests.

中文：[GAME_FILES.zh-CN.md](GAME_FILES.zh-CN.md)

## Which files

The Switch release, updated to **3.3.94012**. You need two parts of it:

| Part | Contents | Size |
|---|---|---|
| Program files (ExeFS) | `main`, `sdk`, `subsdk0` | about 26 MB |
| Update data (RomFS) | the `data/` directory, containing `build_manifest.psv` and a set of `.data` files | about 8.4 GB |

The version has to match. The rewrite rules and generation scripts check this version item by item; any other version stops with a message saying what differs, rather than quietly producing a broken build.

## Dumping from your own console

Dump from your own Switch and your own purchased copy. The commonly used open-source tool is **nxdumptool**; its page explains installation and use and is maintained long term:

<https://github.com/DarkMatterCore/nxdumptool>

When dumping, pick the game's **Update** and dump its ExeFS and RomFS sections.

## If you already have a game package: one command

If what you dumped is a whole game package (`.xci` or `.nsp` containing both the base game and the 3.3.94012 update), you do not need to unpack it by hand:

```
dm/tools/extract_game.sh <your game package> <your prod.keys>
```

It unpacks the package, identifies the base game and the update, extracts the two parts listed above, checks the version, and puts them where the next section says. `prod.keys` is the key file dumped from your own console; the package and the keys are only read on your computer and are never copied or uploaded.

You need **hactool** (an open-source tool for unpacking Switch files) first: <https://github.com/SciresM/hactool>. Build it as its README describes, then put it on your `PATH` or point the script at it with `HACTOOL=<path>`. Leave about 20 GB of free disk space.

## Where to put them

Into the workspace (default `~/dm-work`; set `DM_ROOT` to use another location):

```
~/dm-work/data/switch/3.3.94012/Update/ExeFS/main
~/dm-work/data/switch/3.3.94012/Update/ExeFS/sdk
~/dm-work/data/switch/3.3.94012/Update/ExeFS/subsdk0
~/dm-work/data/switch/3.3.94012/Update/RomFS/data/build_manifest.psv
~/dm-work/data/switch/3.3.94012/Update/RomFS/data/*.data
```

Then run `./setup.sh check`: it reports what it found, what is missing and which command to run next, verifies that the three program files are from version 3.3.94012, and changes nothing. The two steps above can also be combined: run `./setup.sh <your game package> <your prod.keys>` in the repository root, and it extracts the game files and then goes straight on to prepare the workspace.

## The "game resource folder" that goes on the phone

After the app is installed, the first launch says "No game resources yet". What it wants is the **18 resource archives** from `RomFS/data` of your game files (about 5.6 GB in total); they are not in the install package:

```
00-00000001.data … 00-00000006.data      (6 files)
01-00000001.data  01-00000002.data
02-00000001.data  02-00000002.data
03-00000001.data  03-00000002.data
04-00000001.data  04-00000002.data
05-00000001.data  05-00000002.data
17-00000001.data  17-00000002.data
```

Put these 18 files into one folder (any name; extra levels of nesting are fine) and copy it into the app's own folder: on the phone, Files app > On My iPhone > the app's name; or connect the phone to a Mac, select it in Finder > Files, and drag the folder onto the app. Then go back to the app and tap Retry. An incomplete copy is not a problem: the screen lists how many are there and which are missing; add them and tap Retry again.

If you build with Xcode and the phone is connected, the "Locating the game assets" section of `dm/ios/README.md` has one command that pushes them for you.

## Why the Switch release and not the PC release

iPhone and Switch use the same kind of processor. Almost all of the Switch release's program can run on an iPhone without changing a single byte, which is what makes smooth play possible; the PC release is built for a different kind of processor and cannot do that. The game data is not interchangeable either: textures, interface art and audio use different formats in the two releases.
