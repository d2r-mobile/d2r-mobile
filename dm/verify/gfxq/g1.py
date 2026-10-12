#!/usr/bin/env python3
"""用 NVIDIA FLIP 对同名 PNG 做 G1 逐帧对比。 | Use NVIDIA FLIP to do the G1 frame-by-frame comparison of PNGs with the same name."""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

from PIL import Image


RED = "\033[31m"
RESET = "\033[0m"
MEAN_RE = re.compile(r"^\s*Mean:\s*([0-9]+(?:\.[0-9]+)?)\s*$", re.MULTILINE)


def pngs(directory: Path) -> dict[str, Path]:
    return {
        path.relative_to(directory).as_posix(): path
        for path in directory.rglob("*")
        if path.is_file() and path.suffix.lower() == ".png"
    }


def flip_one(job: tuple[str, str, str, str]) -> tuple[str, float]:
    name, flip, reference, test = job
    result = subprocess.run(
        [flip, "-r", reference, "-t", test, "-ppd", "95", "-v", "1", "-nerm"],
        capture_output=True,
        text=True,
        timeout=120,
    )
    if result.returncode != 0:
        raise RuntimeError(f"FLIP 失败：{name}\n{result.stdout}{result.stderr}")
    match = MEAN_RE.search(result.stdout + result.stderr)
    if not match:
        raise RuntimeError(f"FLIP 输出没有 Mean：{name}\n{result.stdout}{result.stderr}")
    return name, float(match.group(1))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("reference", type=Path)
    parser.add_argument("test", type=Path)
    parser.add_argument(
        "--flip",
        type=Path,
        default=Path(__file__).resolve().parents[3] / "dm/third_party/flip/flip",
    )
    args = parser.parse_args()

    reference = pngs(args.reference)
    test = pngs(args.test)
    missing = sorted(set(reference) - set(test))
    if missing:
        for name in missing:
            print(f"{RED}缺帧：{name}{RESET}", file=sys.stderr)
    common = sorted(set(reference) & set(test))
    jobs: list[tuple[str, str, str, str]] = []
    invalid = bool(missing)
    for name in common:
        with Image.open(reference[name]) as reference_image, Image.open(test[name]) as test_image:
            if reference_image.size != test_image.size:
                print(
                    f"{RED}尺寸不同：{name} 真值={reference_image.size} 被测={test_image.size}{RESET}",
                    file=sys.stderr,
                )
                invalid = True
                continue
        jobs.append((name, str(args.flip), str(reference[name]), str(test[name])))

    if invalid:
        return 1

    if not args.flip.is_file() or not os.access(args.flip, os.X_OK):
        print(f"{RED}FLIP 不可执行：{args.flip}{RESET}", file=sys.stderr)
        return 1

    # PPD 推导：iPhone 17 Pro Max ≈460 ppi、观看 30 cm，
    # 0.30·tan(1°)/0.0254·460 ≈ 95。
    # PPD derivation: iPhone 17 Pro Max ≈460 ppi, viewed at 30 cm,
    # 0.30·tan(1°)/0.0254·460 ≈ 95.
    with ProcessPoolExecutor(max_workers=8) as pool:
        results = list(pool.map(flip_one, jobs))
    values = sorted(results)
    for name, mean in values:
        print(f"{name} {mean:.6f}")
    overall = sum(mean for _, mean in values) / len(values) if values else 0.0
    worst = sorted(values, key=lambda item: item[1], reverse=True)[:3]
    worst_text = ", ".join(f"{name} {mean:.6f}" for name, mean in worst) or "无"
    print(f"G1 均值 {overall:.6f} 最差 3 帧 {worst_text}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
