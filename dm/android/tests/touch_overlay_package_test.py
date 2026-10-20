#!/usr/bin/env python3
"""Synthetic package/overlay contract fixture. No Android SDK or game data."""
import sys
import tempfile
import zipfile
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS))
import build_package as package


def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def make_inputs(root):
    game = root / "game"
    for name in package.GAME_MODULES:
        write(game / name, b"module")
    for name in package.GAME_LAYOUTS:
        write(game / name, b"")
    for name in package.GAME_TARGETS:
        write(game / name, b"android-context-v1\n")
    romfs = root / "romfs"
    write(romfs / "data" / "a.data", b"romfs")
    shaders = root / "shaders"
    write(shaders / "host-subgroup-size.txt", b"64\n")
    write(shaders / "a.bin", b"shader")
    return game, romfs, shaders


def make_overlay(root):
    overlay = root / "overlay"
    write(overlay / "ctrl_touch", b"marker\n")
    write(overlay / "touch_strings.tsv", b"A\tABCDEF12\tB\nHiddenHint\t1234abcd\t\n")
    write(overlay / "data" / ".build.config", b"config\n")
    write(overlay / "data" / "00-00000007.data", b"archive\n")
    return overlay


def main():
    with tempfile.TemporaryDirectory(prefix="overlay-package-") as tmp:
        root = Path(tmp)
        game, romfs, shaders = make_inputs(root)
        overlay = make_overlay(root)
        output = root / "package.zip"
        manifest = package.assemble(output, game, romfs, shaders, overlay=overlay, quiet=True)
        assert manifest["counts"]["overlay"] == 4, manifest
        with zipfile.ZipFile(output) as archive:
            names = set(archive.namelist())
        for name in ("overlay/ctrl_touch", "overlay/touch_strings.tsv",
                     "overlay/data/.build.config", "overlay/data/00-00000007.data"):
            assert name in names, name
        package.inspect(output)

        for malformed in (b"A\tABCDEF12\n", b"A\tABCDEF12\tB\textra\n", b"A\tNOTHEX\t\n"):
            write(overlay / "touch_strings.tsv", malformed)
            try:
                package.collect_overlay(overlay)
                raise AssertionError("malformed string record must fail")
            except package.PackageError:
                pass

        no_overlay = root / "no-overlay.zip"
        package.assemble(no_overlay, game, romfs, shaders, quiet=True)
        with zipfile.ZipFile(no_overlay) as archive:
            assert not any(n.startswith("overlay/") for n in archive.namelist())
        package.inspect(no_overlay)

        marker_only = root / "marker-only"
        write(marker_only / "ctrl_touch", b"x")
        try:
            package.collect_overlay(marker_only)
            raise AssertionError("marker-only overlay must fail")
        except package.PackageError as error:
            assert "touch_strings" in str(error) or "data" in str(error), error

        malformed_zip = root / "malformed.zip"
        with zipfile.ZipFile(malformed_zip, "w") as archive:
            archive.writestr("manifest.json", '{"format":1}')
            archive.writestr("overlay/ctrl_touch", "x")
        try:
            package.inspect(malformed_zip)
            raise AssertionError("marker-only package must fail inspect")
        except package.PackageError:
            pass
    print("PASS: synthetic package carries four-file overlay; absent/malformed safe")


if __name__ == "__main__":
    main()
