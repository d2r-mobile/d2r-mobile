import argparse
import os
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / "tools"))
from build import TOOLS, validate_map_goal_art


def main():
    parser = argparse.ArgumentParser(description="Build a separate owned HUD fixture APK; never launches the game")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--map-goal-art", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True)
    classes, dex, assets = [args.output / name for name in ("classes", "dex", "assets")]
    for path in (classes, dex, assets):
        path.mkdir()
    if args.map_goal_art:
        (assets / "mapgoal").mkdir()
        for path in validate_map_goal_art(args.map_goal_art):
            shutil.copyfile(path, assets / "mapgoal" / path.name)
    manifest = args.output / "AndroidManifest.xml"
    manifest.write_text('''<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="org.d2rmobile.android.hudfixture">
<uses-sdk android:minSdkVersion="29" android:targetSdkVersion="35"/>
<application android:label="D2R owned HUD fixture" android:theme="@android:style/Theme.Material.NoActionBar.Fullscreen">
<activity android:name="org.d2r.android.port.WorldHudVisualActivity" android:exported="true" android:screenOrientation="sensorLandscape"/>
</application></manifest>''')
    env = dict(os.environ, JAVA_HOME=str(TOOLS / "jdk"))
    def run(command):
        subprocess.run([str(value) for value in command], env=env, check=True)
    run([TOOLS / "jdk/bin/javac", "--release", "11", "-encoding", "UTF-8", "-classpath", TOOLS / "platform/android.jar",
         "-d", classes, root / "java/org/d2r/android/port/WorldHudState.java",
         root / "java/org/d2r/android/port/D2RWorldHud.java", root / "tests/WorldHudVisualActivity.java"])
    run([TOOLS / "build-tools/d8", "--lib", TOOLS / "platform/android.jar", "--min-api", "29", "--output", dex,
         *sorted(classes.rglob("*.class"))])
    unsigned, aligned, apk = [args.output / name for name in ("unsigned.apk", "aligned.apk", "hud-fixture.apk")]
    run([TOOLS / "build-tools/aapt2", "link", "--manifest", manifest, "-I", TOOLS / "platform/android.jar",
         "-A", assets, "-o", unsigned])
    with zipfile.ZipFile(unsigned, "a", compression=zipfile.ZIP_STORED) as archive:
        archive.write(dex / "classes.dex", "classes.dex")
    run([TOOLS / "build-tools/zipalign", "-f", "4", unsigned, aligned])
    run([TOOLS / "build-tools/apksigner", "sign", "--ks", TOOLS / "debug.keystore", "--ks-pass", "pass:android",
         "--out", apk, aligned])
    print(apk)


if __name__ == "__main__":
    main()
