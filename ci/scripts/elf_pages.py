#!/usr/bin/env python3
"""Static ELF LOAD-alignment inspection for CI artifacts.

This checks file layout only: every PT_LOAD p_align must be a multiple of both
4 KiB and 16 KiB. It does not execute ARM64 binaries, infer the device page
size, or claim runtime compatibility.
"""
import json
import os
import subprocess
import sys
from pathlib import Path


def elf_files(root: Path):
    for path in sorted(root.rglob("*")):
        if not path.is_file() or path.is_symlink():
            continue
        try:
            with path.open("rb") as fh:
                if fh.read(4) != b"\x7fELF":
                    continue
        except OSError:
            continue
        if not os.access(path, os.X_OK):
            continue
        yield path


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: elf_pages.py <readelf> <build-dir>", file=sys.stderr)
        return 2
    readelf, root = sys.argv[1], Path(sys.argv[2])
    rows, failures = [], []
    for path in elf_files(root):
        proc = subprocess.run([readelf, "-lW", str(path)], capture_output=True,
                              text=True)
        if proc.returncode != 0:
            failures.append({"file": str(path.relative_to(root)),
                             "error": f"readelf exit {proc.returncode}"})
            continue
        aligns = []
        for line in proc.stdout.splitlines():
            fields = line.split()
            if fields and fields[0] == "LOAD":
                try:
                    if len(fields) < 8:
                        raise ValueError("short LOAD line")
                    offset = int(fields[1], 16)
                    vaddr = int(fields[2], 16)
                    align = int(fields[-1], 16)
                    if align <= 0 or (align & (align - 1)) != 0:
                        raise ValueError("p_align is not a positive power of two")
                    if (offset - vaddr) % align != 0:
                        raise ValueError("p_offset/p_vaddr not congruent modulo p_align")
                    aligns.append(align)
                except ValueError as exc:
                    failures.append({"file": str(path.relative_to(root)),
                                     "error": str(exc), "line": line})
        if not aligns:
            failures.append({"file": str(path.relative_to(root)),
                             "error": "no valid PT_LOAD segments"})
            continue
        bad = [a for a in aligns if a < 0x1000 or a % 0x1000 != 0 or
               a < 0x4000 or a % 0x4000 != 0]
        rows.append({"file": str(path.relative_to(root)), "load_aligns": aligns,
                     "four_k": all(a >= 0x1000 and a % 0x1000 == 0 for a in aligns),
                     "sixteen_k": all(a >= 0x4000 and a % 0x4000 == 0 for a in aligns)})
        if bad:
            failures.append({"file": str(path.relative_to(root)),
                             "error": "LOAD alignment is not a valid positive 4/16 KiB multiple",
                             "aligns": aligns})
    result = {"checked": len(rows), "files": rows,
              "four_k_static": all(r["four_k"] for r in rows) if rows else False,
              "sixteen_k_static": all(r["sixteen_k"] for r in rows) if rows else False,
              "failures": failures,
              "alignment_check": "positive_power2_p_align_with_offset_vaddr_congruence",
              "runtime_page_size": "not_executed"}
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0 if rows and not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
