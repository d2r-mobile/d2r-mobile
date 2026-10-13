# Older devices (A16 and earlier chips): making the texture pack

中文：[LEGACY_DEVICES.zh-CN.md](LEGACY_DEVICES.zh-CN.md)

The iPhone 14 family, iPhone 15 / 15 Plus and earlier models can play; they just need one more step: a **Legacy Device Pack** made from your own game files and placed on the phone. This page covers it from start to finish.

## Why the extra step

Almost all of the game's textures use BC compression. Apple GPUs support that format only from the A17 Pro (iPhone 15 Pro) on; earlier chips cannot read it. The answer is to convert those textures offline, on a computer, into ASTC, which these devices do support — the picture is the same picture. Textures are game content, so we cannot provide this pack; it can only be generated on your own computer from your own game files.

**Which devices need it**: the App decides this itself at launch, so there is no table to consult. A device that needs it shows "Supplementary Pack Required"; if it shows "One More Step for This Device" (older versions say "Device Not Supported") instead, the App was built without the part that goes with the pack — rebuild it as in step 4 below.

## What you need

- The "with a Mac" route (see "Getting started" on the front page), with `./setup.sh` finished.
- The **astcenc** encoder (Arm's open-source texture encoder, Apache 2.0). Build it from source, which takes a minute or two:

  ```bash
  brew install cmake
  git clone https://github.com/ARM-software/astc-encoder
  cmake -S astc-encoder -B astc-encoder/b -DASTCENC_ISA_NEON=ON -DCMAKE_BUILD_TYPE=Release
  cmake --build astc-encoder/b -j
  export ASTCENC=$PWD/astc-encoder/b/Source/astcenc-neon
  ```

  We use v5.2.0.
- Disk: about 3 GB (1.6 GB of intermediate files, 1.0 GB for the pack).

## Steps

Run these from the repository root, inside the Python environment that setup prepared. `D` is an output directory of your choice and `R` is the game resource directory:

```bash
source ~/dm-work/build/cache/dm/venv/bin/activate
R=~/dm-work/data/switch/3.3.94012/Update/RomFS
D=~/dm-work/out/legacy_pack

python3 dm/tools/astc_pack.py "$R" "$D/work" --preset thorough     # 1 transcode, about 8 minutes (all cores of a Mac)
python3 dm/tools/casc_overlay.py pack "$R" "$D/pack" "$D/work/repl.tsv" 0-8   # 2 write the data archive, about half a minute
python3 dm/tools/astc_pack.py finalize "$D/pack"                   # 3 finalize: sizes and checksum
```

Afterwards `$D/pack` contains:

- `Legacy Device Pack/`: **the folder for the phone** (holding `manifest.json` and `data/00-00000008.data`, about 1.0 GB).
- `pack.json`: the size and SHA-256 of this pack; the next step builds it into the App so the phone can verify the pack.

4. **Build the App with it**:

   ```bash
   cd dm/ios && DM_ASTC_PACK="$D/pack" ./run.sh build
   ```

   An App built without this variable only says "One More Step for This Device" (older versions say "Device Not Supported") on older devices.

5. **Install** as usual (`./run.sh install` or Xcode).

6. **Put the pack on the phone**: in the Files app open "On My iPhone → D2R Mobile" and copy the **whole** `Legacy Device Pack` folder into it, at the same level as the game resource folder. Finder works too (connect the phone → Files → D2R Mobile). Then open the game.

## Things to know

- **The pack and the App are a matched pair.** The App checks the pack on the phone against the SHA-256 in `pack.json`; if you regenerate the pack, rebuild the App with the new `$D/pack`. The same game files, the same astcenc version and the same preset (`--preset thorough`) produce a byte-identical pack, so normally there is nothing to redo.
- If the folder was not copied completely, is in the wrong place, or does not match the App, the App says which it is in the dialog.
- The game resource folder is still needed as usual (see [GAME_FILES.md](GAME_FILES.md)); the pack is an additional folder, not a replacement.
- The above describes the "with a Mac" route. On the no-Xcode route (shell plus packaging tool) the three commands that make the pack are the same and only the way it goes into the install package differs; see the "Older devices" section of [NO_XCODE.md](NO_XCODE.md). On Windows the prebuilt astcenc executable from the upstream release page is all you need.

## What it does (if you are curious)

- It converts the BC3 textures, about 27,000 of them and the great majority of all textures; each is converted block by block to ASTC 4×4 with the file size unchanged. The game itself understands ASTC, so only the format changes and nothing about how the game reads textures.
- The few other BC formats, small in volume, are not converted here; the program decodes them at run time on these devices.
- Every texture is compared with the original for fidelity during conversion, with the results in `$D/work/quality.tsv`; the median for colour and material textures is about 60 dB, with no visible difference.

## In practice

The lowest device we measured is the iPhone 14 Pro Max (A16, 6 GB of RAM), running at 60 fps. During internal testing this route was completed by users on A16 devices; the oldest device our test players actually play on is the iPhone 13 Pro Max (A15, 6 GB of RAM), in the 30 fps mode.
