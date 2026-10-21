#!/usr/bin/env python3
"""重复运行关键 Android 移植测试。| Re-run the key Android port tests."""
from pathlib import Path
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
TOOLS = Path(os.environ.get("DM_ANDROID_TOOLS", str(Path.home() / "dm-work/tools/android")))
BIN = TOOLS / "ndk/toolchains/llvm/prebuilt/linux-x86_64/bin"
BUILD = ROOT / "build"
ADB = TOOLS / "platform-tools/adb"
DEVICE = "/data/local/tmp/d2r-port"


def run(args):
    subprocess.run(list(map(str, args)), check=True)


if __name__ == "__main__":
    BUILD.mkdir(exist_ok=True)
    run([sys.executable, ROOT / "tests/test_rewriter.py"])
    run([sys.executable, ROOT / "tests/test_context_entry.py"])
    run([sys.executable, ROOT / "tests/test_digests.py"])
    run(["python3", ROOT.parent / "i18n/scan_literals.py"])
    run(["python3", ROOT.parent / "ios/tools/dm_i18n.py", "--check"])
    run([sys.executable, ROOT / "tools/generate_entry.py", BUILD / "entry_elf.S"])
    run([sys.executable, ROOT / "tools/make_probe.py", BUILD / "probe", BIN])
    run([sys.executable, ROOT / "tools/prepare_gpu_test.py", BUILD / "gpu-test-shaders", TOOLS / "ndk/shader-tools/linux-x86_64/glslc"])
    run([sys.executable, ROOT / "tools/build_gpu_driver.py"])
    run([ADB, "shell", "mkdir", "-p", DEVICE])
    compiler = BIN / "aarch64-linux-android29-clang"
    common = [compiler, "-O3", "-fno-strict-aliasing", "-fno-omit-frame-pointer", "-g", "-I", ROOT.parent / "host", "-I", ROOT / "native", "-I", ROOT / "native/compat", "-I", BUILD / "gpu-driver/include"]
    native = ROOT / "native"
    context_library = BUILD / "libcontext-entry-test.so"
    run(common + ["-shared", "-fPIC", "-Wl,-z,defs", native / "runtime.c", BUILD / "entry_elf.S",
                  ROOT / "tests/context_entry_probe.S", ROOT / "tests/context_entry_test.c", "-ldl", "-o", context_library])
    run([ADB, "push", context_library, DEVICE + "/"])
    tests = {
        "context-entry-test": [ROOT / "tests/context_entry_driver.c", "-ldl"],
        "runtime-smoke": [native / "runtime.c", native / "probe.c", BUILD / "entry_elf.S", "-ldl"],
        "host-registry-test": [native / "runtime.c", native / "host_tls.c", native / "host_sections.c", ROOT / "tests/host_registry_test.c", BUILD / "entry_elf.S", "-ldl", "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections"],
        "host-posix-test": [native / "host_posix.c", ROOT / "tests/host_posix_test.c"],
        "audio-test": [native / "host_posix.c", ROOT / "tests/audio_test.c", "-laaudio"],
        "ucontext-test": [ROOT / "tests/ucontext_test.c"],
        "lifecycle-test": [native / "host_posix.c", ROOT.parent / "host/hle_lifecycle.c", ROOT / "tests/lifecycle_test.c", "-D_GNU_SOURCE", "-include", native / "compat/host_preinclude.h"],
        "gpu-driver-properties": [ROOT / "tests/gpu_driver_properties.c", native / "gpu_driver.c", "-landroid", "-llog", "-ldl", "-lm"],
        "vulkan-test": [ROOT / "tests/vulkan_test.c", native / "legacy_digest.c", native / "pending_ranges.c", native / "gpu_driver.c", ROOT.parent / "gfx/nvn/nvn_tex.c", ROOT.parent / "gfx/tex/nvn_deswizzle.c", "-I", ROOT.parent / "gfx/nvn/include", "-landroid", "-llog", "-ldl", "-lm"],
    }
    run([ADB, "push", BUILD / "probe/libdmguest_probe.so", BUILD / "probe/probe.fixups.tsv", BUILD / "probe/probe.gates.tsv", DEVICE + "/"])
    run([ADB, "shell", "mkdir", "-p", DEVICE + "/gpu-test-shaders"])
    run([ADB, "push", str(BUILD / "gpu-test-shaders") + "/.", DEVICE + "/gpu-test-shaders/"])
    for name, inputs in tests.items():
        output = BUILD / (name + "-arm64")
        run(common + inputs + ["-o", output])
        run([ADB, "push", output, DEVICE + "/"])
        command = [ADB, "shell"]
        if name == "vulkan-test": command.append("DM_VK_SHADERS=" + DEVICE + "/gpu-test-shaders")
        command.append(DEVICE + "/" + output.name)
        if name in ["runtime-smoke", "host-posix-test"]: command.append(DEVICE)
        if name == "context-entry-test": command.append(DEVICE + "/" + context_library.name)
        run(command)
