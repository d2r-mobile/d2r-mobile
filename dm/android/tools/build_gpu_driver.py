#!/usr/bin/env python3
"""用固定开源源码构建adrenotools及hooks，无设备操作。
Build adrenotools and hooks from pinned open-source inputs, without device operations.
"""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import argparse
import fcntl
import io
import json
import os
import shlex
import shutil
import subprocess
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
TOOLS = Path(os.environ.get("DM_ANDROID_TOOLS", str(Path.home() / "dm-work/tools/android")))
BIN = TOOLS / "ndk/toolchains/llvm/prebuilt/linux-x86_64/bin"
PINNED = (("eden-emulator/libadrenotools", "8ba23b42d7"),
          ("bylaws/liblinkernsbypass", "aa3975893d"))


def source_directory(index):
    repo, commit = PINNED[index]
    name = repo.split("/")[-1] + "-" + commit
    override = os.environ.get("DM_ADRENOTOOLS_SOURCE" if index == 0 else "DM_LINKERNSBYPASS_SOURCE")
    if override:
        source = Path(override).resolve()
        if not source.is_dir():
            raise ValueError("Missing explicit source directory: " + str(source))
        return source
    research = Path.home() / "dm-work/research/turnip-loader-reference" / name
    if research.is_dir():
        return research
    source = TOOLS / "sources" / name
    if source.is_dir():
        return source
    source.parent.mkdir(parents=True, exist_ok=True)
    archive = urllib.request.urlopen(f"https://codeload.github.com/{repo}/tar.gz/{commit}", timeout=60).read()
    with tarfile.open(fileobj=io.BytesIO(archive), mode="r:gz") as tar:
        members = []
        for item in tar.getmembers():
            parts = Path(item.name).parts[1:]
            if not parts:
                continue
            if ".." in parts or item.issym() or item.islnk():
                raise ValueError("Unsafe source archive entry")
            item.name = str(Path(*parts))
            members.append(item)
        tar.extractall(source, members=members, filter="data")
    return source


def build(output):
    output.mkdir(parents=True, exist_ok=True)
    # APK和fixture可能并行构建；锁住共享依赖的完整编译/链接流程。
    # APK and fixture builds may overlap; lock the entire shared-dependency compile/link operation.
    with (output / ".build.lock").open("a+") as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        return build_locked(output)


def build_locked(output):
    source, linker = source_directory(0), source_directory(1)
    include = output / "include/adrenotools"
    include.mkdir(parents=True, exist_ok=True)
    for header in (source / "include/adrenotools").glob("*.h"):
        target = include / header.name
        if not target.exists() or target.read_bytes() != header.read_bytes():
            shutil.copyfile(header, target)
    sources = [source / "src/driver.cpp", source / "src/bcenabler.cpp",
               linker / "android_linker_ns.cpp", linker / "elf_soname_patcher.cpp",
               source / "src/hook/hook_impl.cpp", source / "src/hook/main_hook.c",
               source / "src/hook/file_redirect_hook.c", source / "src/hook/gsl_alloc_hook.c"]

    def compile_file(path):
        obj = output / (path.stem + ".o")
        deps, stamp = obj.with_suffix(".d"), obj.with_suffix(".cmd")
        cpp = path.suffix == ".cpp"
        compiler = BIN / ("aarch64-linux-android29-clang++" if cpp else "aarch64-linux-android29-clang")
        command = [str(compiler), "-c", "-O2", "-fPIC", "-Wall", "-Wextra",
                   "-MMD", "-MF", str(deps), "-I", str(source), "-I", str(source / "include"),
                   "-I", str(linker), str(path), "-o", str(obj)]
        if cpp:
            command.insert(2, "-std=c++17")
        if path.stem == "hook_impl":
            command.insert(2, "-fvisibility=hidden")
        recorded = json.dumps(command)
        if obj.exists() and deps.exists() and stamp.exists() and stamp.read_text() == recorded:
            paths = shlex.split(deps.read_text().split(":", 1)[1].replace("\\\n", " "))
            if all(Path(p).exists() and Path(p).stat().st_mtime <= obj.stat().st_mtime for p in paths):
                return obj
        result = subprocess.run(command, capture_output=True, text=True)
        (output / (path.stem + ".log")).write_text(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError("GPU driver compile failed: " + str(path) + "\n" + result.stderr)
        stamp.write_text(recorded)
        return obj

    with ThreadPoolExecutor(max_workers=4) as pool:
        objects = list(pool.map(compile_file, sources))
    by_name = {obj.stem: obj for obj in objects}
    archive = output / "liblinkernsbypass.a"
    linker_objects=[by_name["android_linker_ns"],by_name["elf_soname_patcher"]]
    if not archive.exists() or any(p.stat().st_mtime>archive.stat().st_mtime for p in linker_objects):
        subprocess.run([str(BIN / "llvm-ar"), "rcs", str(archive),*map(str,linker_objects)],check=True)
    libraries = []
    targets = {
        "adrenotools": [by_name["driver"], by_name["bcenabler"], archive],
        "hook_impl": [by_name["hook_impl"], archive],
        "main_hook": [by_name["main_hook"]],
        "file_redirect_hook": [by_name["file_redirect_hook"]],
        "gsl_alloc_hook": [by_name["gsl_alloc_hook"]],
    }
    for name, inputs in targets.items():
        library = output / ("lib" + name + ".so")
        command = [str(BIN / "aarch64-linux-android29-clang++"), "-shared", "-static-libstdc++",
                   "-Wl,-z,defs", "-Wl,-z,max-page-size=16384", "-Wl,-soname," + library.name,
                   *map(str, inputs)]
        if name == "adrenotools":
            command.append("-Wl,--exclude-libs,liblinkernsbypass.a")
        if name not in ("adrenotools", "hook_impl"):
            command += ["-Wl,-z,global", "-L", str(output), "-lhook_impl"]
        command += ["-landroid", "-ldl", "-llog", "-lm", "-o", str(library)]
        recorded = json.dumps(command)
        stamp = library.with_suffix(".cmd")
        dependencies = inputs + ([output / "libhook_impl.so"] if name not in ("adrenotools", "hook_impl") else [])
        if not (library.exists() and stamp.exists() and stamp.read_text() == recorded
                and all(p.stat().st_mtime <= library.stat().st_mtime for p in dependencies)):
            result = subprocess.run(command, capture_output=True, text=True)
            (output / (name + "-link.log")).write_text(result.stdout + result.stderr)
            if result.returncode:
                raise RuntimeError("GPU driver link failed: " + name + "\n" + result.stderr)
            stamp.write_text(recorded)
        libraries.append(library)
    licenses = output / "licenses"
    licenses.mkdir(exist_ok=True)
    for name, src in (("libadrenotools", source), ("liblinkernsbypass", linker)):
        shutil.copyfile(src / "LICENSE", licenses / (name + ".txt"))
    manifest = {"sources": [{"repo": repo, "requested_commit": commit, "directory": str(src),
                             "local_input_override":bool(os.environ.get("DM_ADRENOTOOLS_SOURCE" if index==0 else "DM_LINKERNSBYPASS_SOURCE"))}
                            for index,((repo, commit), src) in enumerate(zip(PINNED, (source, linker)))],
                "libraries": [str(p) for p in libraries], "include": str(output / "include"),
                "ndk": str(TOOLS / "ndk"), "abi": "arm64-v8a", "api": 29,
                "cxx_runtime": "static", "max_page_size": 16384}
    (output / "build.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print("GPU driver libraries:", ", ".join(p.name for p in libraries), flush=True)
    return libraries


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "build/gpu-driver")
    build(parser.parse_args().output)
