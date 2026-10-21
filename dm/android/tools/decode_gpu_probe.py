#!/usr/bin/env python3
"""Decode the explicit GPU probe's BC blocks with the project's existing BC decoder.
Reads diagnostic files only; writes PNGs, channel statistics and a contact sheet beside them.
"""
from pathlib import Path
import argparse
import csv
import ctypes
import json
import re
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("probe", type=Path, help="Directory containing images.tsv and raw GPU image files")
    parser.add_argument("--output", type=Path, help="Destination (default: probe/decoded-bc)")
    args = parser.parse_args()
    from PIL import Image, ImageDraw
    root = Path(__file__).resolve().parents[2]
    output = args.output or args.probe / "decoded-bc"
    output.mkdir(parents=True, exist_ok=True)
    results = []; tiles = []
    with tempfile.TemporaryDirectory(prefix="d2r-bc-probe-") as work:
        library = Path(work) / "decoder.so"
        subprocess.run(["cc", "-O2", "-shared", "-fPIC", str(root / "gfx/tex/nvn_bcdec.c"), "-lm", "-o", str(library)], check=True)
        decoder = ctypes.CDLL(str(library))
        decoder.nvn_bc_unpacked.argtypes = [ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32), ctypes.POINTER(ctypes.c_int)]
        decoder.nvn_bc_decode.argtypes = [ctypes.c_uint32, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p, ctypes.c_size_t]
        with (args.probe / "images.tsv").open() as source:
            for row in csv.DictReader(source, delimiter="\t"):
                fmt = int(row["format"], 0)
                if not 0x41 <= fmt <= 0x4e:
                    continue
                region = re.fullmatch(r"(\d+),(\d+),(\d+)\+(\d+),(\d+),(\d+)", row["region"])
                if region is None or int(region[6]) != 1:
                    raise ValueError(f"Invalid BC region: {row['region']}")
                width, height = int(region[4]), int(region[5])
                pf, bpp = ctypes.c_uint32(), ctypes.c_int()
                if not decoder.nvn_bc_unpacked(fmt, ctypes.byref(pf), ctypes.byref(bpp)) or bpp.value not in (1, 2, 4):
                    raise ValueError(f"Unsupported BC diagnostic format: {fmt:#x}")
                block_bytes = 8 if fmt in (0x41, 0x42, 0x45, 0x46, 0x49, 0x4a) else 16
                pitch = (width + 3) // 4 * block_bytes
                raw = (args.probe / row["file"]).read_bytes()
                if len(raw) != pitch * ((height + 3) // 4):
                    raise ValueError(f"Incorrect BC block byte count: {row['file']}")
                source_buffer = ctypes.create_string_buffer(raw)
                decoded = ctypes.create_string_buffer(width * height * bpp.value)
                if decoder.nvn_bc_decode(fmt, source_buffer, pitch, width, height, decoded, width * bpp.value):
                    raise ValueError(f"BC decode failed: {row['file']}")
                pixels = decoded.raw; channels = bpp.value
                mode = {1: "L", 2: "LA", 4: "RGBA"}[channels]
                image = Image.frombytes(mode, (width, height), pixels)
                name = Path(row["file"]).stem + ".png"
                image.save(output / name)
                stats = {"serial": int(row["serial"]), "format": fmt, "mip": int(row["mip"]), "layer": int(row["layer"]), "region": row["region"], "png": name,
                         "min": [min(pixels[c::channels]) for c in range(channels)],
                         "max": [max(pixels[c::channels]) for c in range(channels)],
                         "mean": [sum(pixels[c::channels]) / (width * height) for c in range(channels)]}
                results.append(stats)
                # Display RGB even if an albedo/normal-map alpha channel is zero; alpha remains intact in the individual PNG.
                tile = Image.new("RGB", (208, 236), "#202020")
                thumbnail = image.convert("RGB"); thumbnail.thumbnail((200, 200))
                tile.paste(thumbnail, ((208-thumbnail.width)//2, 6+(200-thumbnail.height)//2))
                ImageDraw.Draw(tile).text((5, 209), f"serial{row['serial']} fmt{fmt:#x}\n{width}x{height} mip{row['mip']}", fill="white")
                tiles.append(tile)
    (output / "statistics.json").write_text(json.dumps(results, indent=2) + "\n")
    if tiles:
        columns = min(4, len(tiles)); rows = (len(tiles) + columns - 1) // columns
        sheet = Image.new("RGB", (columns*208, rows*236), "#202020")
        for i, tile in enumerate(tiles): sheet.paste(tile, ((i%columns)*208, (i//columns)*236))
        sheet.save(output / "contact-sheet.png")
    print(f"Decoded {len(results)} actual GPU BC image regions: {output}")


if __name__ == "__main__":
    main()
