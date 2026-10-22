#!/usr/bin/env python3
"""Exercise APK assembly with synthetic tool outputs; never install or use game data."""
import shutil
import sys
import tempfile
import zipfile
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import build


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="d2r-build-reuse-") as temporary:
        root = Path(temporary)
        tools, output = root / "tools", root / "output"
        for name in ("ndk/aarch64-linux-android29-clang", "jdk/bin/java", "platform/android.jar"):
            path = tools / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.touch()
        output.mkdir()
        (output / "probe").mkdir()
        for name in ("probe.fixups.tsv", "probe.gates.tsv", "probe.target", "libdmguest_probe.so"):
            (output / "probe" / name).write_bytes(b"synthetic")
        (output / "game-host-activity").mkdir()
        (output / "game-host-activity/libdmandroid.so").write_bytes(b"synthetic")
        shader_cache = root / "shaders"
        shader_cache.mkdir()
        commands: list[list[str]] = []

        def run(command: list[str | Path], *, env: dict[str, str] | None = None) -> None:
            arguments = list(map(str, command))
            commands.append(arguments)
            if "strings.py" in arguments[1]:
                (output / "res/values").mkdir(parents=True, exist_ok=True)
            if "-o" in arguments:
                destination = Path(arguments[arguments.index("-o") + 1])
                if "link" in arguments:
                    with zipfile.ZipFile(destination, "w") as archive:
                        for path in sorted((output / "assets").rglob("*")):
                            if path.is_file():
                                archive.write(path, "assets/" + path.relative_to(output / "assets").as_posix())
                else:
                    destination.write_bytes(b"synthetic")
            if "d8" == Path(arguments[0]).name:
                (output / "dex/classes.dex").write_bytes(b"synthetic dex")
            if "zipalign" == Path(arguments[0]).name:
                shutil.copyfile(arguments[-2], arguments[-1])
            if "sign" in arguments:
                shutil.copyfile(arguments[-1], arguments[arguments.index("--out") + 1])

        for name in ("assets/turnip/old.so", "assets/touch_art/old.png", "assets/main.target",
                     "assets/main.gates.tsv", "res/drawable/old.png", "dex/classes2.dex",
                     "gpu-driver/libold-driver.so", "gpu-fixtures/libold-fixture.so"):
            path = output / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"stale")
        with patch.object(build, "TOOLS", tools), patch.object(build, "BIN", tools / "ndk"), \
             patch.object(build, "run", run), patch.object(build, "build_counter_timestamp"), \
             patch.dict("os.environ", {"DM_REFERENCE_BUILD": str(output)}), \
             patch.object(sys, "argv", ["build.py", "--game-run", "--no-turnip", "--install",
                                        "--shader-cache", str(shader_cache)]):
            build.main()
        with zipfile.ZipFile(output / "D2R-Android-port.apk") as archive:
            names = set(archive.namelist())
        forbidden = {"assets/turnip/old.so", "assets/touch_art/old.png", "assets/main.target",
                     "assets/main.gates.tsv", "lib/arm64-v8a/libold-driver.so",
                     "lib/arm64-v8a/libold-fixture.so"}
        assert not names.intersection(forbidden), names.intersection(forbidden)
        assert not (output / "res/drawable/old.png").exists()
        assert not (output / "dex/classes2.dex").exists()
        assert "classes.dex" in names and "lib/arm64-v8a/libdmandroid.so" in names
        assert (output / "gpu-driver/libold-driver.so").exists(), "dependency cache is retained"
        starts = [command for command in commands if "am" in command and "start" in command]
        assert starts[-1][-1] == "org.d2rmobile.android.port/org.d2r.android.port.LauncherActivity", starts
        pushes = [command for command in commands if "push" in command]
        assert pushes[-1][-1] == "/sdcard/Android/data/org.d2rmobile.android.port/files/shaders/", pushes
        protected = output / "assets/touch_art/source.png"
        protected.parent.mkdir(parents=True)
        protected.write_bytes(b"keep-input")
        with patch.dict("os.environ", {"DM_REFERENCE_BUILD": str(output)}), \
             patch.object(sys, "argv", ["build.py", "--touch-art", str(protected.parent)]):
            try:
                build.main()
            except SystemExit as error:
                assert "Build inputs must be outside" in str(error), error
            else:
                raise AssertionError("in-staging source must be rejected before cleanup")
        assert protected.read_bytes() == b"keep-input"
        print("PASS: reused APK staging excludes stale inputs and install uses the manifest package")


if __name__ == "__main__":
    main()
