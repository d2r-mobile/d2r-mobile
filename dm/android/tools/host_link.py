#!/usr/bin/env python3
"""构建原宿主的 Android 无窗口启动探针，缺少的依赖逐项报告。
Build an Android headless boot probe of the original host, reporting remaining dependencies.
"""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import os
import subprocess
import sys
import shlex
import argparse

ROOT = Path(__file__).resolve().parents[2]
TOOLS = Path(os.environ.get("DM_ANDROID_TOOLS", str(Path.home() / "dm-work/tools/android")))
OUT = ROOT / "android/build/host"
CLANG = TOOLS / "ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android29-clang"


def compile(item):
    source, extra = item
    obj = OUT / (source.stem + ".o")
    deps, stamp = obj.with_suffix(".d"), obj.with_suffix(".cmd")
    command = [str(CLANG), "-c", "-fPIC", "-O3", "-fno-strict-aliasing", "-fno-omit-frame-pointer", "-g", "-MMD", "-MF", str(deps), "-D_GNU_SOURCE", "-I", str(ROOT / "host"),
               "-I", str(ROOT / "android/native"), "-I", str(ROOT / "gfx/nvn/include"), "-I", str(ROOT / "android/native/compat"), "-I", str(ROOT / "android/build/gpu-driver/include")]
    if source.parent == ROOT / "host" or source.parent == ROOT / "gfx/nvn": command += ["-include", str(ROOT / "android/native/compat/host_preinclude.h")]
    if source.name == "main.c": command += ["-Dmain=dm_main"]
    command += extra + [str(source), "-o", str(obj)]
    recorded = repr(command)
    if obj.exists() and deps.exists() and stamp.exists() and stamp.read_text() == recorded:
        dependencies = shlex.split(deps.read_text().split(":", 1)[1].replace("\\\n", " "))
        if all(Path(p).exists() and Path(p).stat().st_mtime <= obj.stat().st_mtime for p in dependencies): return obj
    r = subprocess.run(command, capture_output=True, text=True)
    (OUT / (source.stem + ".log")).write_text(r.stdout + r.stderr)
    if r.returncode: print("Compile failed:", source, "see", OUT / (source.stem + ".log")); return None
    stamp.write_text(recorded)
    return obj


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--activity", action="store_true", help="Build the real-game NativeActivity shared library")
    args = parser.parse_args()
    if args.activity: OUT = ROOT / "android/build/game-host"
    OUT.mkdir(parents=True, exist_ok=True)
    subprocess.run([sys.executable,str(ROOT / "android/tools/build_gpu_driver.py")],check=True)
    entry = OUT / "entry_elf.S"
    subprocess.run([sys.executable, str(ROOT / "android/tools/generate_entry.py"), str(entry)], check=True)
    subprocess.run([sys.executable, str(ROOT / "android/tools/generate_nvn.py"), str(OUT)], check=True)
    sources = [(p, []) for p in sorted((ROOT / "host").glob("*.c"))]
    sources += [(ROOT / "android/native" / name, []) for name in ["runtime.c", "host_posix.c", "host_tls.c", "host_sections.c", "legacy_digest.c", "pending_ranges.c", "performance_hint.c", "launch_settings.c", "frontend_activity.c" if args.activity else "frontend_headless.c", "nvn_vulkan.c", "gpu_driver.c"]]
    sources += [(ROOT / "ios/Sources/dm_touch_controls.c", []), (entry, [])]
    sources += [(ROOT / "gfx" / name, []) for name in ["hle_nv.c", "hle_vi.c"]]
    sources += [(ROOT / "gfx/nvn" / name, []) for name in ["hle_nvn.c", "nvn_tex.c", "nvn_dirty.c"]]
    sources += [(ROOT / "gfx/tex/nvn_deswizzle.c", [])]
    sources += [(OUT / (name + "_elf.S"), []) for name in ["nvn_thunks", "nvn_entry"]]
    third = ROOT / "third_party/libdeflate/lib"
    sources += [(third / name, []) for name in ["deflate_decompress.c", "zlib_decompress.c", "adler32.c", "utils.c", "arm/cpu_features.c"]]
    with ThreadPoolExecutor(max_workers=4) as pool: objects = list(pool.map(compile, sources))
    if any(obj is None for obj in objects): sys.exit(1)
    output = OUT / ("libdmandroid.so" if args.activity else "dmhost-android")
    link_flags = ["-shared", "-Wl,-z,defs"] if args.activity else []
    result = subprocess.run([str(CLANG), "-Wl,-z,max-page-size=16384", *map(str, objects), *link_flags, "-landroid", "-llog", "-laaudio", "-ldl", "-lm", "-o", str(output)], capture_output=True, text=True)
    (OUT / "link.log").write_text(result.stdout + result.stderr)
    if result.returncode:
        print("Link failed; see", OUT / "link.log", "\n" + result.stderr[:6000]); sys.exit(1)
    print("Android host:", output)
