#!/usr/bin/env python3
"""将改写后的 NSO 映像装进 16K 对齐的 ELF 库，保持模块内部布局。
Pack rewritten NSO images into 16K-aligned ELF libraries, preserving internal layout.
"""
from pathlib import Path
import argparse
import struct

PAGE = 0x4000


def align(n):
    return (n + PAGE - 1) & -PAGE


def build(image, text_end, patch_off, soname):
    size = len(image)
    rw = align(text_end)
    if not (0 < rw <= patch_off <= size and patch_off % PAGE == 0):
        raise ValueError("Invalid module segment boundaries")
    start = PAGE
    tail = align(start + size)
    # 动态元数据位于模块之后；唯一导出是模块基址，客体重定位由宿主处理。
    # Dynamic metadata follows the image; only its base is exported. Host handles guest relocations.
    prefix = b"\0dm_guest_image\0dm_guest_image_end\0"
    strings = prefix + soname.encode() + b"\0"
    str_off = tail
    sym_off = (str_off + len(strings) + 7) & ~7
    symbols = bytes(24) + struct.pack("<IBBHQQ", 1, 0x11, 0, 1, start, size) + struct.pack("<IBBHQQ", 16, 0x11, 0, 1, start + size, 0)
    hash_off = sym_off + len(symbols)
    hashes = struct.pack("<IIIIII", 1, 3, 1, 0, 2, 0)
    dyn_off = (hash_off + len(hashes) + 7) & ~7
    dynamic = b"".join(struct.pack("<qQ", *x) for x in [
        (4, hash_off), (5, str_off), (6, sym_off), (10, len(strings)), (11, 24),
        (14, len(prefix)), (0, 0),
    ])
    end = dyn_off + len(dynamic)
    segments = [
        (1, 4, 0, 0, 0, PAGE, PAGE, PAGE),
        (1, 5, start, start, start, rw, rw, PAGE),
        (1, 6, start + rw, start + rw, start + rw, patch_off - rw, patch_off - rw, PAGE),
        (1, 5, start + patch_off, start + patch_off, start + patch_off,
         align(size) - patch_off, align(size) - patch_off, PAGE),
        (1, 4, tail, tail, tail, end - tail, end - tail, PAGE),
        (2, 4, dyn_off, dyn_off, dyn_off, len(dynamic), len(dynamic), 8),
        (0x6474e551, 6, 0, 0, 0, 0, 0, 16),
    ]
    ident = b"\x7fELF\x02\x01\x01" + bytes(9)
    names = b"\0.image\0.dynstr\0.dynsym\0.hash\0.dynamic\0.shstrtab\0"
    names_off = end
    sections_off = (names_off + len(names) + 7) & ~7
    sections = [
        (0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
        (1, 1, 2, start, start, size, 0, 0, PAGE, 0),
        (8, 3, 2, str_off, str_off, len(strings), 0, 0, 1, 0),
        (16, 11, 2, sym_off, sym_off, len(symbols), 2, 1, 8, 24),
        (24, 5, 2, hash_off, hash_off, len(hashes), 3, 0, 4, 4),
        (30, 6, 2, dyn_off, dyn_off, len(dynamic), 2, 0, 8, 16),
        (39, 3, 0, 0, names_off, len(names), 0, 0, 1, 0),
    ]
    # Android 链接器也核对节表；不能只提供程序头。
    # Android's linker also validates section headers; program headers alone are insufficient.
    header = struct.pack("<16sHHIQQQIHHHHHH", ident, 3, 183, 1, 0, 64, sections_off, 0,
                         64, 56, len(segments), 64, len(sections), 6)
    output = bytearray(sections_off + len(sections) * 64)
    output[:64] = header
    for i, segment in enumerate(segments):
        struct.pack_into("<IIQQQQQQ", output, 64 + i * 56, *segment)
    output[start:start + size] = image
    output[str_off:str_off + len(strings)] = strings
    output[sym_off:sym_off + len(symbols)] = symbols
    output[hash_off:hash_off + len(hashes)] = hashes
    output[dyn_off:dyn_off + len(dynamic)] = dynamic
    output[names_off:names_off + len(names)] = names
    for i, section in enumerate(sections):
        struct.pack_into("<IIQQQQIIQQ", output, sections_off + i * 64, *section)
    return output


def pack(source, output, name):
    layout = {}
    for line in (source / (name + ".fixups.tsv")).read_text().splitlines():
        if line.startswith("# "):
            key, value = line[2:].split()
            layout[key] = int(value, 16)
    if (source / (name + ".target")).read_text().strip() != "android-context-v1":
        raise ValueError("Module was not rewritten for Android")
    image = (source / (name + ".patched.bin")).read_bytes()
    output.mkdir(parents=True, exist_ok=True)
    destination = output / ("libdmguest_" + name + ".so")
    destination.write_bytes(build(image, layout["TEXT_END"], layout["PATCH_OFF"], destination.name))
    print(destination)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("names", nargs="+")
    args = parser.parse_args()
    for name in args.names:
        pack(args.source, args.output, name)
