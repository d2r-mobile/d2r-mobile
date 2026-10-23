#!/usr/bin/env python3
"""读取宿主 Makefile 的源清单，输出 CMake 变量；不处理任何游戏内容。
Read source lists from the host Makefile into CMake variables; no game content.
"""
import argparse
from pathlib import Path
import re


def source_lists(makefile):
    src, libraries, in_src = [], [], False
    for line in makefile.read_text(encoding="utf-8").splitlines():
        if line.startswith("SRC="):
            in_src = True
            line = line[4:]
        if in_src:
            text = line.strip().rstrip("\\").strip()
            if re.fullmatch(r"[A-Za-z0-9_./-]+\.c", text) and text != "main.c" and not Path(text).stem.endswith("_mac"):
                src.append((makefile.parent / text).resolve())
            in_src = line.rstrip().endswith("\\")
        if line.startswith("LIBDEFLATE="):
            libraries = [(makefile.parent / s).resolve() for s in re.findall(r"[A-Za-z0-9_./-]+\.c", line)]
    if len(src) < 40 or not libraries:
        raise ValueError("Host Makefile source lists changed; update the Android parser")
    return src, libraries


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("makefile", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    source, libraries = source_lists(args.makefile)
    text = ""
    for name, paths in (("HOST_SRCS", source), ("LIBDEFLATE_SRCS", libraries)):
        text += "set(" + name + "\n" + "\n".join('  "' + p.as_posix() + '"' for p in paths) + "\n)\n"
    args.output.write_text(text, encoding="utf-8", newline="\n")
