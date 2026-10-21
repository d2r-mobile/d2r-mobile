#!/usr/bin/env python3
"""诊断上游宿主在 Android NDK 下的编译依赖，保存每个文件的阻碍。
Diagnose upstream host compilation dependencies under the Android NDK, recording per-file blockers.
"""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import json
import argparse
import os
import subprocess

ROOT = Path(__file__).resolve().parents[2]
TOOLS = Path(os.environ.get("DM_ANDROID_TOOLS", str(Path.home() / "dm-work/tools/android")))
OUT = ROOT / "android/build/host-compile"
COMPAT = False


def compile_source(source):
    output = OUT / (source.stem + ".o")
    command = [str(TOOLS / "ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android29-clang"),
               "-c", "-fPIC", "-O1", "-I", str(ROOT / "host"), "-I", str(ROOT / "gfx/nvn/include"), str(source), "-o", str(output)]
    if COMPAT:
        command += ["-D_GNU_SOURCE", "-I", str(ROOT / "android/native/compat"), "-include", str(ROOT / "android/native/compat/host_preinclude.h")]
    r = subprocess.run(command, capture_output=True, text=True)
    (OUT / (source.stem + ".log")).write_text(r.stdout + r.stderr)
    errors = [line for line in r.stderr.splitlines() if "error:" in line]
    return {"file": str(source.relative_to(ROOT)), "compiled": r.returncode == 0, "errors": errors[:8]}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compat", action="store_true")
    COMPAT = parser.parse_args().compat
    if COMPAT: OUT = ROOT / "android/build/host-compile-compat"
    OUT.mkdir(parents=True, exist_ok=True)
    with ThreadPoolExecutor(max_workers=4) as pool:
        results = list(pool.map(compile_source, sorted((ROOT / "host").glob("*.c"))))
    (OUT / "report.json").write_text(json.dumps(results, indent=2))
    good = sum(r["compiled"] for r in results)
    print(f"Upstream host C files: {good}/{len(results)} compile directly for Android")
    blockers = {}
    for result in results:
        for error in result["errors"][:1]:
            message = error.split("error:", 1)[-1].strip()
            blockers[message] = blockers.get(message, 0) + 1
    for message, count in sorted(blockers.items(), key=lambda x: -x[1]):
        print(count, message)
    print("Details:", OUT / "report.json")
