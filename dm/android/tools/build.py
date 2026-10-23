#!/usr/bin/env python3
"""构建独立 ARM64 NativeActivity APK 与运行时测试，不依赖 Gradle。
Build a standalone ARM64 NativeActivity APK and runtime tests without Gradle.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import zipfile
import zlib
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parents[1]
PACKAGE_NAME = ET.parse(ROOT / "AndroidManifest.xml").getroot().attrib["package"]
DEFAULT_BUILD = REPO.parent / (REPO.name + "-build") / "android-reference"
TOOLS = Path(os.environ.get("DM_ANDROID_TOOLS", str(Path.home() / "dm-work/tools/android")))
NDK_PREBUILT = os.environ.get("DM_NDK_PREBUILT", "darwin-x86_64" if sys.platform == "darwin" else "linux-x86_64")
BIN = TOOLS / "ndk/toolchains/llvm/prebuilt" / NDK_PREBUILT / "bin"

GAME_ACTIVITY = "org.d2r.android.port.D2RActivity"
LAUNCHER_NAME = "org.d2r.android.port.LauncherActivity"
# 正式构建把仓库清单里的单个 NativeActivity 换成这两条：启动界面当入口，游戏本体降级成它启动的第二个页面。
# 探针构建走不到这里：它没有 dex，清单里不能出现 Java 页面。
# 两个页面都用 sensorLandscape（与仓库清单一致，见 AndroidManifest.xml）：只支援两个横向，但画面随设备保持在正向；
# 用 landscape 会被钉在一个横向朝向上，转到另一个横向就倒过来。configChanges 已含 orientation|screenSize，
# 旋转不重建活动，游戏进程与这一局都保留。
# The release build swaps the repository manifest's single NativeActivity for these two: the launcher is the
# entry point and the game becomes the second page it starts. The probe build never reaches this branch —
# it has no dex, so its manifest must not declare a Java activity.
# Both pages use sensorLandscape (kept in step with the repository manifest, see AndroidManifest.xml): only the two
# landscapes are allowed, but the picture stays upright as the device turns; plain landscape would pin a single
# landscape orientation and go upside down when the device is turned the other way. configChanges already lists
# orientation|screenSize, so a rotation does not recreate the activity and the game process and the run both survive.
LAUNCHER_ACTIVITY_BLOCK = (
    '<activity android:name="' + LAUNCHER_NAME + '" android:exported="true"'
    ' android:screenOrientation="sensorLandscape" android:configChanges="orientation|screenSize|keyboardHidden"'
    ' android:theme="@android:style/Theme.Material.NoActionBar">\n'
    '            <intent-filter>\n'
    '                <action android:name="android.intent.action.MAIN" />\n'
    '                <category android:name="android.intent.category.LAUNCHER" />\n'
    '            </intent-filter>\n'
    '        </activity>\n'
    '        ')
GAME_ACTIVITY_BLOCK = (
    '<activity android:name="' + GAME_ACTIVITY + '" android:exported="false"'
    ' android:screenOrientation="sensorLandscape" android:configChanges="orientation|screenSize|keyboardHidden"'
    ' android:theme="@android:style/Theme.Material.NoActionBar">\n'
    '            <meta-data android:name="android.app.lib_name" android:value="dmandroid" />\n'
    '        </activity>')


def run(args, **kwargs):
    subprocess.run(list(map(str, args)), check=True, **kwargs)


def build_counter_timestamp(build):
    # 生产辅助着色器每次构建从GLSL生成，独立于游戏缓存。
    # Generate the production helper from GLSL independently of the game cache.
    output = build / "counter_timestamp.spv"
    run([os.environ.get("DM_GLSLC", "/opt/homebrew/bin/glslc"), "--target-env=vulkan1.1", "-O",
         ROOT / "native/shaders/counter_timestamp.comp", "-o", output])
    data = output.read_bytes()
    words = struct.unpack("<" + "I" * (len(data) // 4), data)
    text = "// 从生产GLSL自动生成。| Generated from production GLSL.\n#pragma once\n#include <stdint.h>\n"
    text += "static const uint32_t dm_vk_counter_timestamp_spv[] = {\n"
    text += "".join("    " + ",".join(f"0x{word:08x}u" for word in words[i:i+8]) + ",\n" for i in range(0,len(words),8))
    text += "};\n#define DM_VK_COUNTER_TIMESTAMP_SPV_BYTES (sizeof dm_vk_counter_timestamp_spv)\n"
    header = ROOT / "native/counter_timestamp_spv.h"
    if not header.exists() or header.read_text() != text:
        header.write_text(text)


# APK 的版本必须能区分构建。写死的版本名会让系统把两个不同构建当成同一个版本（覆盖安装只提示"重新安装"），
# 玩家也无从看出自己装的是哪一版。版本由构建时给入，CI 用 tag 名（发布）或 main-<短提交>（分支构建）。
# The APK version has to tell builds apart. A hardcoded version name makes the system treat two different builds as
# one version (an overwrite install then reads as "reinstall") and leaves the player unable to see which build they
# have. It is supplied at build time: the tag name for a release, main-<short commit> for a branch build.
def version_name(version):
    """版本名：去掉前导 v 加本移植后缀，例如 v0.3 -> 0.3-android-port。"""
    return version.removeprefix("v") + "-android-port"


def version_code(version):
    """版本号折成整数：v0.3 -> 300，v1.2.3 -> 10203。

    只有 v<数字> 这种形式才折；分支构建（main-<短提交>）固定为 1 —— Android 拒绝版本号**倒退**的安装，
    而按提交折出来的数字可大可小，会把正常安装挡掉。版本号相同是允许覆盖的。
    Only a v<digits> form is folded. Branch builds stay at 1: Android refuses an install whose version code goes
    backwards, and a commit-derived number moves in either direction, which would block a normal install.
    Installing over the same version code is allowed.
    """
    if not re.fullmatch(r"v?\d+(\.\d+)*", version):
        return 1
    numbers = [int(part) for part in version.removeprefix("v").split(".")]
    while len(numbers) < 3:
        numbers.append(0)
    return numbers[0] * 10000 + numbers[1] * 100 + numbers[2]


def validate_touch_art(source):
    """Validate the root-provided generated atlases against the frozen source
    manifest. The art itself is never committed; this only checks bytes and PNG
    dimensions, then returns the files to package."""
    manifest_path = REPO / "dm/ios/Sources/TouchArt/native_art_manifest.json"
    manifest = json.loads(manifest_path.read_text())
    assets = {a["asset"]: a for a in manifest["assets"]}
    files = []
    for i in range(9):
        name = "dm_native_skill_%d.png" % i
        path = source / name
        if not path.is_file():
            raise SystemExit("touch art: missing " + str(path))
        data = path.read_bytes()
        if hashlib.sha256(data).hexdigest() != assets[name]["decoded_png_sha256"]:
            raise SystemExit("touch art: fingerprint mismatch " + str(path))
        if data[:8] != b"\x89PNG\r\n\x1a\n" or data[12:16] != b"IHDR":
            raise SystemExit("touch art: not a PNG " + str(path))
        width, height = struct.unpack(">II", data[16:24])
        if [width, height] != assets[name]["atlas_px"]:
            raise SystemExit("touch art: dimensions differ from manifest " + str(path))
        files.append(path)
    return files


def validate_map_goal_art(source):
    names = ["cross"] + [f"sym_{i}" for i in range(7)]
    names += [f"label_{i}{suffix}" for i in range(31) for suffix in ("", "_en", "_es", "_mx", "_ru")]
    names += [f"label_{i}_old" for i in range(31) if (source / f"label_{i}_old.png").exists()]
    files, total = [], 0
    for name in names:
        path = source / (name + ".png")
        if path.is_symlink() or not path.is_file() or not 33 <= path.stat().st_size <= 4 * 1024 * 1024:
            raise ValueError("map-goal art: missing, linked or oversized " + str(path))
        data = path.read_bytes(); total += len(data)
        if total > 32 * 1024 * 1024 or data[:16] != b"\x89PNG\r\n\x1a\n\0\0\0\rIHDR":
            raise ValueError("map-goal art: invalid PNG or total size " + str(path))
        width, height = struct.unpack(">II", data[16:24])
        if not 0 < width <= 4096 or not 0 < height <= 4096:
            raise ValueError("map-goal art: invalid dimensions " + str(path))
        position, image_data, ended = 8, False, False
        while position + 12 <= len(data):
            length = struct.unpack_from(">I", data, position)[0]
            end = position + 12 + length
            if end > len(data) or zlib.crc32(data[position + 4:end - 4]) != struct.unpack_from(">I", data, end - 4)[0]:
                raise ValueError("map-goal art: truncated PNG or CRC mismatch " + str(path))
            kind = data[position + 4:position + 8]
            image_data |= kind == b"IDAT"
            position = end
            if kind == b"IEND":
                ended = length == 0 and end == len(data)
                break
        if not image_data or not ended:
            raise ValueError("map-goal art: incomplete PNG " + str(path))
        files.append(path)
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install", action="store_true")
    parser.add_argument("--game-source", type=Path, help="Local rewritten Android game modules (includes game code in APK)")
    parser.add_argument("--game-run", action="store_true", help="Run the real game host with NVN/Vulkan instead of the diagnostic frontend")
    parser.add_argument("--shader-cache", type=Path, default=Path.home() / "dm-work/build/android/shaders/spirv")
    parser.add_argument("--version", default="dev",
                        help="写进 APK 的版本：发布用 tag 名（v0.3），分支构建用 main-<短提交>；默认 dev")
    parser.add_argument("--turnip", type=Path, default=ROOT / "prebuilt/turnip/vulkan.purple.so",
                        help="随 APK 分发的 Turnip 驱动；默认用仓库里的 dm/android/prebuilt/turnip/")
    parser.add_argument("--no-turnip", action="store_true",
                        help="不出内置驱动（只出通用包，启动界面里那一项会自己隐藏）")
    parser.add_argument("--touch-art", type=Path,
                        help="root-provided generated TouchArt directory (dm_native_skill_0..8.png); validated against the frozen manifest")
    parser.add_argument("--map-goal-art", type=Path, help="locally generated NativeHUD/mapgoal directory from gen_map_goal_art.py")
    args = parser.parse_args()
    if args.game_source and not args.game_run: parser.error("--game-source only makes sense with --game-run")
    if args.game_run and not args.game_source:
        # 没有游戏源码也照样出 APK：宿主与前端完整，**游戏模块由手机端导入的数据包提供**（启动界面本来就要
        # 先导入才能启动）。CI 用它产出可下载的通用 APK；本机带 --game-source 时照旧把游戏代码打进包里。
        # Without game sources an APK is still produced: host and frontends are complete and the **game modules
        # come from the package imported on the phone** (the launcher already refuses to start without one). CI
        # ships this generic APK; a local build with --game-source still embeds the game code.
        print("Package-only build: game modules come from the imported package", flush=True)
    build = Path(os.environ.get("DM_REFERENCE_BUILD", str(DEFAULT_BUILD)))
    build.mkdir(parents=True, exist_ok=True)
    staging_directories = [build / name for name in ("assets", "res", "java-classes", "dex")]
    for source in (args.touch_art, args.map_goal_art, args.game_source, args.turnip, args.shader_cache):
        if source is not None and any(source.resolve().is_relative_to(path.resolve()) for path in staging_directories):
            sys.exit("Build inputs must be outside the disposable APK staging directories: " + str(source))
    for staging in staging_directories:
        if staging.exists():
            shutil.rmtree(staging)
    if args.game_run: build_counter_timestamp(build)
    for required in [BIN / "aarch64-linux-android29-clang", TOOLS / "jdk/bin/java", TOOLS / "platform/android.jar"]:
        if not required.exists(): sys.exit("Missing tool: " + str(required) + "; run tools/bootstrap.py")
    env = dict(os.environ, JAVA_HOME=str(TOOLS / "jdk"), PATH=str(TOOLS / "jdk/bin") + os.pathsep + os.environ["PATH"],
               DM_REFERENCE_BUILD=str(build))
    host_env = dict(env)
    if args.no_turnip: host_env["DM_SYSTEM_DRIVER_ONLY"] = "1"
    else: host_env.pop("DM_SYSTEM_DRIVER_ONLY", None)
    entry = build / "entry_elf.S"
    run([sys.executable, ROOT / "tools/generate_entry.py", entry])
    run([sys.executable, ROOT / "tools/strings.py", build])
    # 仓库里的资源（应用图标，纯矢量）复制进构建目录，和 strings.py 生成的字符串一起交给 aapt2；
    # 构建目录每次重建，所以这里总是从源码复制，不会留下旧图标的残留。
    # Repository resources (the app icon, pure vectors) are copied into the build directory so aapt2 compiles them
    # with the generated strings. The build directory is rebuilt each time, so this always comes from the source and
    # never leaves a stale icon behind.
    if (ROOT / "res").is_dir():
        shutil.copytree(ROOT / "res", build / "res", dirs_exist_ok=True)
    # 图标覆盖（可选，dm/android/local/ 已 gitignore）：把 ic_launcher.* 放进 local/ 就换成本机自己的图，
    # 与启动背景图同一套约定。位图作为自适应图标的前景层（铺满 108dp 再被系统裁形），建议方形图。
    # Optional icon override (dm/android/local/ is gitignored): drop an ic_launcher.* there and this build uses it,
    # the same convention as the launcher backdrop. The bitmap becomes the adaptive icon's foreground layer (it
    # fills 108dp and the system masks it), so a square image is expected.
    override = next(iter(sorted((ROOT / "local").glob("ic_launcher.*"))), None)
    if override is not None:
        shutil.copyfile(override, build / "res" / "drawable" / ("ic_launcher_custom" + override.suffix.lower()))
        (build / "res" / "mipmap-anydpi-v26" / "ic_launcher.xml").write_text(
            '<?xml version="1.0" encoding="utf-8"?>\n'
            '<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">\n'
            '    <background android:drawable="@drawable/ic_launcher_background" />\n'
            '    <foreground android:drawable="@drawable/ic_launcher_custom" />\n'
            '</adaptive-icon>\n')
        # 位图没有干净的单色轮廓，覆盖时不提供主题图标层（Android 13 会退回彩色图标）。
        # A bitmap has no clean monochrome silhouette, so the override ships no themed layer (Android 13 falls back
        # to the colour icon).
        shutil.rmtree(build / "res" / "mipmap-anydpi-v33", ignore_errors=True)
    run([sys.executable, ROOT / "tools/make_probe.py", build / "probe", BIN])
    if not args.no_turnip:
        run([sys.executable, ROOT / "tools/build_gpu_driver.py", "--output", build / "gpu-driver"])
        # GameActivity uses the real host; legacy diagnostic Activity alone
        # needs the standalone GPU probe executables.
        if not args.game_run:
            run([sys.executable, ROOT / "tools/build_gpu_fixtures.py", "--output", build / "gpu-fixtures"])
    compiler = BIN / "aarch64-linux-android29-clang"
    common = [compiler, "-O1", "-g", "-Wall", "-Wextra", "-I", ROOT / "native", "-I", build, ROOT / "native/runtime.c", ROOT / "native/probe.c", entry]
    if not args.no_turnip:
        common += ["-I", build / "gpu-driver/include"]
    if args.no_turnip:
        common.insert(2, "-DDM_NO_ADRENOTOOLS")
        common.insert(2, "-DDM_SYSTEM_DRIVER_ONLY")
    run(common + ["-ldl", "-o", build / "runtime-smoke"])
    if args.game_run:
        run([sys.executable, ROOT / "tools/host_link.py", "--activity"], env=host_env)
        shutil.copyfile(build / "game-host-activity/libdmandroid.so", build / "libdmandroid.so")
    else:
        run(common + ["-fPIC", "-shared", "-DDM_NATIVE_APP",
                  * (["-DDM_NO_ADRENOTOOLS", "-DDM_SYSTEM_DRIVER_ONLY"] if args.no_turnip else []),
                  "-Wl,-z,max-page-size=16384", "-Wl,-z,defs",
                  ROOT / "native/activity.c", ROOT / "native/app_probe.c", ROOT / "native/vulkan_front.c",
                  ROOT / "native/game_modules.c", ROOT / "native/gpu_driver.c",
                  ROOT / "native/host_posix.c", ROOT / "tests/host_posix_test.c",
                  ROOT.parent / "ios/Sources/dm_touch_controls.c", "-landroid", "-llog", "-ldl", "-lm", "-o", build / "libdmandroid.so"])
    if args.game_run:
        run([compiler, "-O2", "-fPIE", "-pie", "-Wl,-z,max-page-size=16384",
             "-I", ROOT / "native",
             ROOT / "native/driver_check.c", ROOT / "native/gpu_driver.c",
             "-landroid", "-llog", "-ldl", "-o", build / "libdmdrivercheck.so"])
    assets = build / "assets"
    assets.mkdir(exist_ok=True)
    licenses = assets / "licenses"
    licenses.mkdir(exist_ok=True)
    if args.touch_art:
        generated = REPO / "dm/ios/Sources/dm_touch_native_art.c"
        if not generated.is_file():
            raise SystemExit("touch art: missing generated " + str(generated) + "; run generate.py first")
        touch_files = validate_touch_art(args.touch_art)
        (assets / "touch_art").mkdir(exist_ok=True)
        for path in touch_files:
            shutil.copyfile(path, assets / "touch_art" / path.name)
        print("Touch art: packaged %d validated atlases from %s" % (len(touch_files), args.touch_art), flush=True)
    else:
        print("Touch art: not supplied; glyph overlay remains explicitly unknown", flush=True)
    if args.map_goal_art:
        map_files = validate_map_goal_art(args.map_goal_art)
        (assets / "mapgoal").mkdir(exist_ok=True)
        for path in map_files:
            shutil.copyfile(path, assets / "mapgoal" / path.name)
        print("Map-goal art: packaged %d validated images" % len(map_files), flush=True)
    for license_file in (build / "gpu-driver/licenses").glob("*.txt"):
        shutil.copyfile(license_file, licenses / license_file.name)
    # 启动画面背景图随源码发布（dm/android/artwork/），新电脑 clone 后能重建出同样的 APK。
    # dm/android/local/ 是可选覆盖（已 gitignore）：想换成自己的图，把同名文件放那儿即可。
    # The launcher backdrop ships with the source (dm/android/artwork/) so a fresh clone rebuilds the same APK.
    # dm/android/local/ is an optional gitignored override: drop a same-named file there to use your own image.
    for artwork in sorted((ROOT / "artwork").glob("launcher-background.*")):
        shutil.copyfile(artwork, assets / artwork.name)
    for override in sorted((ROOT / "local").glob("launcher-background.*")):
        shutil.copyfile(override, assets / override.name)
    # 内置 Turnip 驱动随 APK 分发（Mesa 的 MIT 许可，构建者见 prebuilt/turnip/meta.json）：启动界面把它复制到
    # 应用内部目录再按 gpu-driver.conf 选择，游戏本身不改一行代码；玩家可在启动界面切回系统驱动。
    # 没有这个文件时照常出通用包，启动界面里那一项自动隐藏 —— 缺驱动不是构建失败。
    # The built-in Turnip driver ships inside the APK (Mesa's MIT licence; builder recorded in
    # prebuilt/turnip/meta.json): the launcher copies it into the app's internal storage and selects it through
    # gpu-driver.conf, so the game code is untouched and the player can switch back to the system driver. Without
    # the file a generic package is produced as before and that launcher row hides itself -- a missing driver is
    # not a build failure.
    if args.no_turnip: print("Turnip driver: excluded (--no-turnip)", flush=True)
    elif args.turnip.is_file():
        (assets / "turnip").mkdir(exist_ok=True)
        shutil.copyfile(args.turnip, assets / "turnip/vulkan.purple.so")
        print("Turnip driver: bundled from " + str(args.turnip) + " (%.1f MB)" % (args.turnip.stat().st_size / 1048576), flush=True)
    else: print("Turnip driver: not present at " + str(args.turnip) + "; generic package only", flush=True)
    # 诊断GLSL缓存独立于游戏的离线Maxwell fullshader缓存，不覆盖游戏资源。
    # Keep diagnostic GLSL shaders separate from the game's offline Maxwell fullshader cache.
    if (build / "gpu-test-shaders").is_dir():
        shutil.copytree(build / "gpu-test-shaders", assets / "gpu-test-shaders", dirs_exist_ok=True)
    for name in ["probe.fixups.tsv", "probe.gates.tsv", "probe.target"]:
        shutil.copyfile(build / "probe" / name, assets / name)
    if args.game_source:
        run([sys.executable, ROOT / "tools/guest_elf.py", args.game_source, args.game_source, "main", "sdk", "subsdk0"])
        for name in ["main", "sdk", "subsdk0"]:
            shutil.copyfile(args.game_source / (name + ".fixups.tsv"), assets / (name + ".fixups.tsv"))
            if args.game_run: shutil.copyfile(args.game_source / (name + ".gates.tsv"), assets / (name + ".gates.tsv"))
            shutil.copyfile(args.game_source / (name + ".target"), assets / (name + ".target"))
    else:
        for name in ["main", "sdk", "subsdk0"]:
            (assets / (name + ".fixups.tsv")).unlink(missing_ok=True)
    unsigned = build / "unsigned.apk"
    activity = "android.app.NativeActivity"
    manifest = ROOT / "AndroidManifest.xml"
    if args.game_run:
        classes, dex = build / "java-classes", build / "dex"
        if classes.exists(): shutil.rmtree(classes)
        classes.mkdir(); dex.mkdir(exist_ok=True)
        run([TOOLS / "jdk/bin/javac", "--release", "8", "-classpath", TOOLS / "platform/android.jar", "-d", classes,
             *sorted((ROOT / "java").rglob("*.java"))])
        run([TOOLS / "build-tools/d8", "--min-api", "29", "--lib", TOOLS / "platform/android.jar", "--output", dex,
             *sorted(classes.rglob("*.class"))], env=env)
        activity = LAUNCHER_NAME
        manifest = build / "game-AndroidManifest.xml"
        # 正式游戏声明游戏类别；探针应用仍保持原类别。
        # Declare the game category only for the playable application, not the probe.
        source = (ROOT / "AndroidManifest.xml").read_text()
        source = source.replace('android:hasCode="false"', 'android:hasCode="true" android:appCategory="game"')
        # 版本写进清单：名字给人看，号码给系统判断升级。两处都要求恰好替换一次，清单改了会立刻报错。
        # The version goes into the manifest: the name is for people, the code is what the system compares for an
        # upgrade. Both must replace exactly once, so a change to the manifest fails loudly instead of silently.
        source, named = re.subn(r'android:versionName="[^"]*"',
                                'android:versionName="%s"' % version_name(args.version), source, count=1)
        source, coded = re.subn(r'android:versionCode="[^"]*"',
                                'android:versionCode="%d"' % version_code(args.version), source, count=1)
        if (named, coded) != (1, 1):
            sys.exit("game manifest: expected exactly one versionName and one versionCode")
        source, replaced = re.subn(r'<activity android:name="android\.app\.NativeActivity".*?</activity>',
                                   LAUNCHER_ACTIVITY_BLOCK + GAME_ACTIVITY_BLOCK, source, count=1, flags=re.S)
        if replaced != 1: sys.exit("game manifest: expected exactly one NativeActivity activity block")
        manifest.write_text(source)
    run([TOOLS / "build-tools/aapt2", "compile", "--dir", build / "res", "-o", build / "resources.zip"])
    run([TOOLS / "build-tools/aapt2", "link", "--manifest", manifest,
         "-I", TOOLS / "platform/android.jar", "-A", assets, "-o", unsigned, build / "resources.zip"])
    with zipfile.ZipFile(unsigned, "a", compression=zipfile.ZIP_STORED) as z:
        if args.game_run:
            z.write(dex / "classes.dex", "classes.dex")
            z.write(build / "libdmdrivercheck.so", "lib/arm64-v8a/libdmdrivercheck.so")
        z.write(build / "libdmandroid.so", "lib/arm64-v8a/libdmandroid.so")
        z.write(build / "probe/libdmguest_probe.so", "lib/arm64-v8a/libdmguest_probe.so")
        if not args.no_turnip:
            for library in sorted((build / "gpu-driver").glob("*.so")):
                z.write(library,"lib/arm64-v8a/" + library.name)
            if not args.game_run:
                for fixture in sorted((build / "gpu-fixtures").glob("*.so")):
                    z.write(fixture,"lib/arm64-v8a/" + fixture.name)
        if args.game_source:
            for name in ["main", "sdk", "subsdk0"]:
                z.write(args.game_source / ("libdmguest_" + name + ".so"), "lib/arm64-v8a/libdmguest_" + name + ".so")
    aligned = build / "aligned.apk"
    run([TOOLS / "build-tools/zipalign", "-f", "-P", "16", "4", unsigned, aligned])
    keystore = TOOLS / "debug.keystore"
    if not keystore.exists():
        run([TOOLS / "jdk/bin/keytool", "-genkeypair", "-keystore", keystore, "-storepass", "android", "-keypass", "android",
             "-alias", "androiddebugkey", "-keyalg", "RSA", "-keysize", "2048", "-validity", "10000",
             "-dname", "CN=Android Debug,O=Android,C=US"], env=env)
    apk = build / "D2R-Android-port.apk"
    run([TOOLS / "build-tools/apksigner", "sign", "--ks", keystore, "--ks-pass", "pass:android", "--key-pass", "pass:android",
         "--out", apk, aligned], env=env)
    run([TOOLS / "build-tools/apksigner", "verify", apk], env=env)
    print("APK:", apk)
    if args.install:
        run([TOOLS / "platform-tools/adb", "install", "-r", apk])
        if args.game_run:
            if not args.shader_cache.is_dir(): sys.exit("Missing shader cache: " + str(args.shader_cache))
            run([TOOLS / "platform-tools/adb", "shell", "mkdir", "-p", f"/sdcard/Android/data/{PACKAGE_NAME}/files/shaders"])
            run([TOOLS / "platform-tools/adb", "push", "--sync", str(args.shader_cache) + "/.", f"/sdcard/Android/data/{PACKAGE_NAME}/files/shaders/"])
        run([TOOLS / "platform-tools/adb", "shell", "am", "start", "-n", PACKAGE_NAME + "/" + activity])


if __name__ == "__main__":
    main()
