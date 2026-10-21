#!/usr/bin/env python3
"""在仓库外安装 Android 构建工具。 | Install Android build tools outside the repository."""
import concurrent.futures
import hashlib
import json
import os
import subprocess
import stat
from pathlib import Path
import tarfile
import urllib.request
import zipfile

ROOT = Path(os.environ.get("DM_ANDROID_TOOLS", str(Path.home() / "dm-work/tools/android")))
PACKAGES = {
    "ndk": ("android-ndk-r28c-linux.zip", "a7b54a5de87fecd125a17d54f73c446199e72a64"),
    "platform": ("platform-35_r02.zip", "0bb560a90a7a2cbd0dd8348224d518b638fe7949"),
    "build-tools": ("build-tools_r35_linux.zip", "2cfaa0bbb2336e9ec18ed3ecea84fa2e2af607bc"),
    "platform-tools": ("platform-tools_r37.0.1-linux.zip", "477254aa5f903c15cf51001717bdf347fb6b53e0"),
}
SIZES = {"ndk": 722261334, "platform": 64273788, "build-tools": 61958799, "platform-tools": 9054187}


def download(url, path, sha1=None, total=0):
    if path.exists() and (not sha1 or hashlib.file_digest(path.open("rb"), "sha1").hexdigest() == sha1):
        return path
    temp = path.with_suffix(path.suffix + ".part")
    print("Download:", path.name, flush=True)
    # 大工具包分段下载，已下载的连续前缀可续传。
    # Download large tool archives in ranges and resume an existing contiguous prefix.
    if total:
        prefix = temp.stat().st_size if temp.exists() else 0
        fd = os.open(temp, os.O_RDWR | os.O_CREAT, 0o644)
        ranges = [(start, min(start + 8 * 1024 * 1024, total))
                  for start in range(prefix, total, 8 * 1024 * 1024)]
        def part(span):
            start, end = span
            piece = path.with_name(path.name + f".{start}.chunk")
            if not piece.exists() or piece.stat().st_size != end - start:
                subprocess.run(["curl", "--fail", "--silent", "--show-error", "--location",
                                "--retry", "5", "--retry-all-errors", "--connect-timeout", "20",
                                "--max-time", "180", "--range", f"{start}-{end - 1}",
                                "--output", str(piece), url], check=True)
            if piece.stat().st_size != end - start:
                raise RuntimeError("Server returned wrong range: " + piece.name)
            os.pwrite(fd, piece.read_bytes(), start)
        try:
            with concurrent.futures.ThreadPoolExecutor(max_workers=16) as pool:
                list(pool.map(part, ranges))
        except BaseException:
            # 分段文件可能有洞；失败时仅保留此前确定完整的连续前缀。
            # A failed ranged download may have holes; keep only the previous complete prefix.
            os.ftruncate(fd, prefix)
            raise
        finally:
            os.close(fd)
    else:
        subprocess.run(["curl", "--fail", "--silent", "--show-error", "--location",
                        "--retry", "5", "--retry-all-errors", "--output", str(temp), url], check=True)
    if sha1 and hashlib.file_digest(temp.open("rb"), "sha1").hexdigest() != sha1:
        raise RuntimeError("Tool archive checksum mismatch: " + path.name)
    temp.replace(path)
    for piece in path.parent.glob(path.name + ".*.chunk"):
        piece.unlink()
    return path


def install_package(item):
    name, (archive, sha1) = item
    target = ROOT / name
    if target.exists():
        path = ROOT / "downloads" / archive
        if path.exists():
            with zipfile.ZipFile(path) as z:
                for info in z.infolist():
                    if stat.S_ISLNK(info.external_attr >> 16):
                        link = target.joinpath(*Path(info.filename).parts[1:])
                        if not link.is_symlink():
                            if link.exists(): link.unlink()
                            link.symlink_to(z.read(info).decode())
        return
    path = download("https://dl.google.com/android/repository/" + archive, ROOT / "downloads" / archive, sha1, SIZES[name])
    staging = ROOT / (name + ".unpack")
    staging.mkdir(exist_ok=True)
    with zipfile.ZipFile(path) as z:
        z.extractall(staging)
        for info in z.infolist():
            mode = (info.external_attr >> 16) & 0o777
            if stat.S_ISLNK(info.external_attr >> 16):
                link = staging / info.filename
                link.unlink()
                link.symlink_to(z.read(info).decode())
                continue
            if mode and not info.is_dir():
                (staging / info.filename).chmod(mode)
    children = list(staging.iterdir())
    if len(children) != 1 or not children[0].is_dir():
        raise RuntimeError("Unexpected archive layout: " + archive)
    children[0].rename(target)
    staging.rmdir()
    print("Ready:", target, flush=True)


def install_java():
    if (ROOT / "jdk/bin/java").exists():
        return
    url = "https://api.adoptium.net/v3/assets/latest/17/hotspot?architecture=x64&image_type=jdk&os=linux"
    response = subprocess.check_output(["curl", "--fail", "--silent", "--show-error", "--location",
                                        "--retry", "3", "--max-time", "60", url])
    package = json.loads(response)[0]["binary"]["package"]
    path = download(package["link"], ROOT / "downloads" / package["name"], total=package["size"])
    if hashlib.file_digest(path.open("rb"), "sha256").hexdigest() != package["checksum"]:
        raise RuntimeError("JDK archive checksum mismatch")
    staging = ROOT / "jdk.unpack"
    staging.mkdir(exist_ok=True)
    with tarfile.open(path) as t:
        t.extractall(staging, filter="data")
    children = list(staging.iterdir())
    children[0].rename(ROOT / "jdk")
    staging.rmdir()
    print("Ready: JDK", flush=True)


if __name__ == "__main__":
    ROOT.mkdir(parents=True, exist_ok=True)
    (ROOT / "downloads").mkdir(exist_ok=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=5) as pool:
        futures = [pool.submit(install_package, item) for item in PACKAGES.items()]
        futures.append(pool.submit(install_java))
        for future in concurrent.futures.as_completed(futures):
            future.result()
    print("Android toolchain:", ROOT)
