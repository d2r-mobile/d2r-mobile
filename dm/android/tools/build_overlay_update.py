from pathlib import Path
from typing import Final
import hashlib
import re
import sys
import zipfile

from build_package import FIXED_TIMESTAMP, PackageError, collect_overlay

MANIFEST: Final = "android-update.properties"
MAX_BYTES: Final = 2 << 30


def assemble_overlay_update(overlay: Path, romfs: Path, output: Path, version: str) -> None:
    if re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,79}", version) is None:
        raise PackageError("invalid interface version")
    files: list[Path] = [path for path in collect_overlay(overlay) if path.name != MANIFEST]
    if not 4 <= len(files) <= 64:
        raise PackageError("interface update must contain 4-64 files")
    base = romfs / "data/build_key.txt"
    if not base.is_file() or base.is_symlink() or not 0 < base.stat().st_size <= 4096:
        raise PackageError("missing or oversized base RomFS data/build_key.txt")
    if output.resolve().is_relative_to(overlay.resolve()):
        raise PackageError("update output must be outside the input overlay")
    hashes: list[tuple[str, str]] = []
    total = 0
    for path in files:
        total += path.stat().st_size
        if total > MAX_BYTES:
            raise PackageError("interface update exceeds 2 GiB")
        checksum = hashlib.sha256()
        with path.open("rb") as source:
            while chunk := source.read(65536):
                checksum.update(chunk)
        hashes.append(("overlay/" + path.relative_to(overlay).as_posix(), checksum.hexdigest()))
    content = "".join(f"{name}\t{checksum}\n" for name, checksum in sorted(hashes))
    manifest = (f"format=1\nhost_contract=1\nversion={version}\n"
                f"base_sha256={hashlib.sha256(base.read_bytes()).hexdigest()}\n"
                f"files={len(files)}\nbytes={total}\n"
                f"content_sha256={hashlib.sha256(content.encode('utf-8')).hexdigest()}\n")
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + ".part")
    try:
        with zipfile.ZipFile(temporary, "w", zipfile.ZIP_DEFLATED, allowZip64=True) as archive:
            info = zipfile.ZipInfo(MANIFEST, date_time=FIXED_TIMESTAMP)
            archive.writestr(info, manifest)
            for path in files:
                info = zipfile.ZipInfo("overlay/" + path.relative_to(overlay).as_posix(), date_time=FIXED_TIMESTAMP)
                info.compress_type = zipfile.ZIP_DEFLATED
                with path.open("rb") as source, archive.open(info, "w", force_zip64=True) as destination:
                    while chunk := source.read(65536):
                        destination.write(chunk)
        temporary.replace(output)
    finally:
        temporary.unlink(missing_ok=True)


if __name__ == "__main__":
    if len(sys.argv) != 5:
        sys.exit("Usage: build_overlay_update.py OVERLAY ROMFS OUTPUT.zip VERSION")
    try:
        assemble_overlay_update(Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3]), sys.argv[4])
    except (OSError, PackageError) as error:
        sys.exit(f"interface package error: {error}")
