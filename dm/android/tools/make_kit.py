#!/usr/bin/env python3
"""把给玩家电脑用的打包套件打成一个 zip：项目源码（打包工具与技能都在里面）+ 已构建的 APK + 一页说明。

Assemble the kit a player needs on their PC into one zip: the project source (the packing tools and the skill
live in it), the built APK, and a one-page README.

用法 / Usage:
  python3 dm/android/tools/make_kit.py [--apk <apk>] [--output <目录>] [--version <标签>]

只打包 **git 已跟踪** 的文件：构建产物、`.git` 与本机覆盖目录（`dm/android/local/`）都不进去，
所以套件里不会混进游戏数据或本机私有的东西。
Only **git-tracked** files are packed: build output, `.git` and the per-machine override directory
(`dm/android/local/`) stay out, so no game data or machine-private material can leak into the kit.
"""
import argparse
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]      # dm/android
REPO = ROOT.parents[1]                          # repository root
TOP = "d2r-android"                             # one top-level folder so extraction stays tidy
README_DIR = ROOT / "kit"


def tracked_files():
    result = subprocess.run(["git", "-C", REPO, "ls-files", "-z"], stdout=subprocess.PIPE, check=True)
    return [name for name in result.stdout.decode("utf-8").split("\0") if name]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--apk", type=Path, default=ROOT / "build" / "D2R-Android-port.apk")
    parser.add_argument("--output", type=Path, default=REPO / "dist")
    parser.add_argument("--version", default="dev", help="写进文件名，例如 v0.2")
    args = parser.parse_args()

    if not args.apk.is_file():
        sys.exit("APK 不存在：%s（先构建：python3 dm/android/tools/build.py --game-run）" % args.apk)
    for name in ("README.zh-CN.md", "README.md"):
        if not (README_DIR / name).is_file():
            sys.exit("套件说明缺失：%s" % (README_DIR / name))

    args.output.mkdir(parents=True, exist_ok=True)
    kit = args.output / ("d2r-package-kit-%s.zip" % args.version)
    packed = 0
    with zipfile.ZipFile(kit, "w", zipfile.ZIP_DEFLATED) as archive:
        for relative in tracked_files():
            path = REPO / relative
            if not path.is_file():
                continue        # 索引里还留着、但工作区已删除的条目 | indexed but deleted in the worktree
            archive.write(path, TOP + "/" + relative)
            packed += 1
        archive.write(args.apk, TOP + "/" + args.apk.name)
        # 说明页放在顶层，解压后第一眼看到；文件名加 KIT- 前缀，免得和源码里的 README 混淆。
        # The readmes sit at the top so they are the first thing seen; the KIT- prefix keeps them apart from the
        # source tree's own README.
        archive.write(README_DIR / "README.zh-CN.md", TOP + "/KIT-README.zh-CN.md")
        archive.write(README_DIR / "README.md", TOP + "/KIT-README.md")
    print("套件：%s（源码 %d 个文件 + APK + 说明，%.1f MB）"
          % (kit, packed, kit.stat().st_size / 1048576), flush=True)


if __name__ == "__main__":
    main()
