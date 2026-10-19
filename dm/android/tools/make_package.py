#!/usr/bin/env python3
"""从玩家自己的游戏文件和密钥生成一个可导入的 Android 数据包（格式见 docs/ANDROID_PACKAGE.zh-CN.md）。

Produce one importable Android data package from the player's own game files and keys.

用法:
  <项目 venv>/bin/python dm/android/tools/make_package.py 游戏包 密钥 [选项]

  游戏包   里面是本体与更新（.nsz / .nsp / .xci）的 .zip，或放着这两个包的目录。**两个都要给**：
           只给一个包认不出谁是本体谁是更新，工具会明确报错。
  密钥     prod.keys，或一个里面装着 prod.keys 的 zip。

选项:
  --workspace 目录   中间产物放哪（默认 ~/dm-work/pack-workspace）
  --output 路径      数据包输出路径（默认 <workspace>/d2r-package.zip）
  --reuse 目录       复用已解出的数据：该目录下应有 ExeFS/、RomFS/、game/、shaders/spirv/，
                     给了就跳过解包与着色器翻译（这两步最慢）。必须先通过现有
                     host-subgroup-size.txt、host-profile.json 和 shaders/work/build-config.json
                     的一致性校验；缺记录、畸形、请求档位不一致或元数据冲突都会拒绝。
  --subgroup N       着色器分组档位，默认 64（已验证的设备配置）；复用时必须与缓存实际档位一致。

必须在项目的 Python 虚拟环境里运行：patch_nso 需要 venv 里的 lz4/capstone，而子进程用的是 sys.executable。
Run it with the project's virtual environment: patch_nso needs lz4/capstone from it and subprocesses inherit
sys.executable, so the wrong interpreter fails there with ModuleNotFoundError.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]          # dm/
# 本目录：extract_packages.py / guest_elf.py / build_shaders.py / build_package.py 这些兄弟脚本都在这儿。
# 刻意**不**用 DM_ANDROID_TOOLS：那个变量在仓库其它地方（bootstrap.py / build.py / ci_checks.py）以及 CI 里
# 指的是安卓 SDK 工具链目录（jdk / build-tools / ndk），里面没有任何 Python 脚本，拿它去找兄弟脚本必然落空。
# This directory: the sibling scripts (extract_packages.py, guest_elf.py, build_shaders.py, build_package.py)
# live here. Deliberately NOT DM_ANDROID_TOOLS: everywhere else (bootstrap.py, build.py, ci_checks.py) and in CI
# that variable means the Android SDK toolchain directory (jdk / build-tools / ndk), which holds no Python at all,
# so using it to find siblings always misses.
HERE = Path(__file__).resolve().parent
HACTOOL = Path(os.environ.get("DM_HACTOOL", str(Path.home() / "dm-work" / "tools" / "hactool" / "hactool")))
DOTNET = Path(os.environ.get("DM_DOTNET", str(Path.home() / "dm-work" / "tools" / "dotnet" / "dotnet")))
NSZ = Path(os.environ.get("DM_NSZ", str(Path(sys.executable).parent / "nsz")))
PACKAGE_NAME = "d2r-package.zip"


class PackageError(Exception):
    pass


def say(message):
    print(f"[{time.strftime('%H:%M:%S')}] {message}", flush=True)


def run(command, label, env=None, timeout=None):
    """跑一条外部命令；失败时把最后几行输出带进错误里，不然只看到"某一步失败"。"""
    say(label)
    result = subprocess.run([str(c) for c in command], stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, env=env, timeout=timeout)
    if result.returncode:
        tail = "\n".join(result.stdout.decode(errors="replace").splitlines()[-15:])
        raise PackageError(f"{label} 失败（退出码 {result.returncode}）\n{tail}")
    return result.stdout.decode(errors="replace")


def capture(command, label, timeout=None):
    """只接 stdout（生成器把表写到 stdout，stderr 上的提示照旧给用户看）。
    Capture stdout only: the generators print the table to stdout, while their progress/warnings on stderr stay
    visible to the user."""
    say(label)
    result = subprocess.run([str(c) for c in command], stdout=subprocess.PIPE, timeout=timeout)
    if result.returncode:
        raise PackageError(f"{label} 失败（退出码 {result.returncode}）")
    return result.stdout.decode(errors="replace")


def derived_tables(romfs, workspace):
    """从 RomFS 生成三张真实表（放进工作目录，**不写源码树**），再拆成运行时的 derived.tsv。
    壳模式的 APK 里只有空表与条数，这三张表的**内容**随数据包走（标签 EXP / VTPART / VTROW / VTSET / BRT）。
    Generate the three real tables from the RomFS into the workspace (never the source tree), then split them into
    the runtime derived.tsv. A shell APK only carries empty tables and their counts; the *content* travels with the
    package (tags EXP / VTPART / VTROW / VTSET / BRT)."""
    tables = workspace / "tables"
    tables.mkdir(parents=True, exist_ok=True)
    # 亮度表要 --rows：默认输出只有"条数"（构建用的替身），数据行由这里进 derived.tsv。
    # The brightness generator needs --rows: by default it only prints the count (the build stub); the data rows
    # are what goes into derived.tsv here.
    generators = (("dm_vault_tab.h", ROOT / "ios/tools/gen_vault_table.py", ()),
                  ("dm_hud_exp.h", ROOT / "ios/tools/gen_hud_exp.py", ()),
                  ("dm_bright_levels.inc", ROOT / "tools/gen_bright_levels.py", ("--rows",)))
    for name, generator, extra in generators:
        table = capture([sys.executable, generator, romfs, *extra], "生成 " + name, timeout=30 * 60)
        if not table.strip():
            raise PackageError(f"{name} 生成结果为空：{generator}")
        (tables / name).write_text(table, encoding="utf-8")
    derived = workspace / "derived.tsv"
    run([sys.executable, ROOT / "tools/shell_stubs.py", "data", ROOT, derived, "--tables", tables],
        "把三张表拆成运行时数据 derived.tsv", timeout=10 * 60)
    return derived


# ---------------------------------------------------------------------------
# 认文件：不依赖固定文件名，靠结构、大小和版本号语义判断。
# Recognising files: not by fixed names but by structure, size and version semantics.
# ---------------------------------------------------------------------------

def version_of(name):
    """文件名里的版本号。只有**非零**版本才算更新包的标志。

    本体自己的名字里通常也带 `[v0]`；把它当更新会把本体认错，所以必须要求非零。
    Only a non-zero version marks an update: the base's own name usually carries `[v0]` too, and treating that
    as an update misreads the base.
    """
    numbers = [int(value) for value in re.findall(r"v(\d+)", name.lower())]
    return max(numbers) if numbers else 0


def find_title_files(directory):
    """认出本体与更新。判据按可靠程度排序，实在分不出就明确报错并列出看到的文件。"""
    candidates = [path for path in sorted(directory.rglob("*"))
                  if path.is_file() and path.suffix.lower() in (".nsp", ".nsz", ".xci")
                  and "dlc" not in path.name.lower()]
    if not candidates:
        raise PackageError("没有找到本体（.nsp/.nsz/.xci）")
    versioned = [path for path in candidates if version_of(path.name) > 0]
    update = max(versioned, key=lambda path: version_of(path.name)) if versioned else None
    remaining = [path for path in candidates if path is not update]
    if not remaining:
        raise PackageError("只看到一个候选文件，认不出本体与更新：" + update.name +
                           "。本体和更新两个包都要给：放进同一个目录，或打成一个 zip")
    if update is None:
        if len(remaining) != 2:
            listing = "、".join(f"{p.name}（{p.stat().st_size / 2**30:.2f} GiB）" for p in remaining)
            raise PackageError(f"认不出哪个是更新包（文件名里没有版本号）。看到：{listing}。"
                               f"本体和更新两个包都要给：放进同一个目录，或打成一个 zip")
        # 只有两个候选时按大小判断：本体是大的那个。
        # With exactly two candidates size decides: the larger one is the base.
        return max(remaining, key=lambda p: p.stat().st_size), min(remaining, key=lambda p: p.stat().st_size)
    return max(remaining, key=lambda path: path.stat().st_size), update


def find_key_file(directory):
    """密钥可能直接给 prod.keys，也可能给一个装着它的 zip。"""
    for path in sorted(directory.rglob("*.keys")):
        return path
    for path in sorted(directory.rglob("*.zip")):
        try:
            with zipfile.ZipFile(path) as archive:
                if any(Path(n).name == "prod.keys" for n in archive.namelist()):
                    return path
        except zipfile.BadZipFile:
            continue
    raise PackageError("没有找到密钥：请提供 prod.keys，或一个里面含 prod.keys 的 zip")


def unpack_keys(source, work):
    if source.is_file() and source.suffix.lower() == ".keys":
        return source
    if source.is_dir():
        return find_key_file(source)
    target = work / "keys"
    target.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(source) as archive:
        for name in archive.namelist():
            if Path(name).name in ("prod.keys", "title.keys"):
                (target / Path(name).name).write_bytes(archive.read(name))
    produced = target / "prod.keys"
    if not produced.is_file():
        raise PackageError("密钥压缩包里没有 prod.keys")
    return produced


def gather_game_files(source, target):
    """把本体与更新的候选文件收集到 target 下，返回该扫哪个目录。

    给的是 zip 就只取可能用得到的几种，不整包解开；给的是目录就**就地扫描、不复制** ——
    那是 9 GB 级的读写，而目录里的东西本来就在原处。
    Gather the base/update candidates under target and return the directory to scan. A zip contributes only the
    entry kinds that can matter, without unpacking the whole thing; a directory is scanned in place and never
    copied, because that would be multi-gigabyte I/O to move files that are already where they are.
    """
    if source.is_dir():
        return source
    target.mkdir(parents=True, exist_ok=True)
    if source.suffix.lower() == ".zip":
        with zipfile.ZipFile(source) as archive:
            for entry in archive.infolist():
                if entry.is_dir() or Path(entry.filename).suffix.lower() not in (".nsp", ".nsz", ".xci"):
                    continue
                destination = target / Path(entry.filename).name
                say(f"取出 {destination.name}（{entry.file_size / 2**30:.2f} GiB）")
                with archive.open(entry) as src, destination.open("wb") as dst:
                    shutil.copyfileobj(src, dst, 1 << 20)
        return target
    shutil.copyfile(source, target / source.name)
    return target


def _read_reuse_json(path, label):
    """Read one generated profile object without ever writing the cache."""
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except OSError as error:
        raise PackageError(f"{label} 读不到：{path}（{error}）")
    except (TypeError, ValueError) as error:
        raise PackageError(f"{label} 不是有效 JSON：{path}（{error}）")
    if not isinstance(value, dict):
        raise PackageError(f"{label} 必须是 JSON 对象：{path}")
    return value


def _require_profile_int(profile, key, label):
    value = profile.get(key)
    if type(value) is not int:
        raise PackageError(f"{label} 缺少整数 {key}：{value!r}")
    return value


def validate_reuse_shader_profile(reuse, requested):
    """Validate the existing shader identity before --reuse packages it.

    The generated cache owns three records: spirv/host-subgroup-size.txt,
    spirv/host-profile.json and work/build-config.json. Reuse may only read
    them; it must never relabel a cache for a different actual subgroup.
    All failures leave every source-cache byte untouched.
    """
    spirv = reuse / "shaders" / "spirv"
    subgroup_path = spirv / "host-subgroup-size.txt"
    profile_path = spirv / "host-profile.json"
    config_path = reuse / "shaders" / "work" / "build-config.json"

    try:
        raw = subgroup_path.read_text(encoding="utf-8")
    except OSError as error:
        raise PackageError(f"复用的着色器缺少 host-subgroup-size.txt：{subgroup_path}（{error}）")
    if not re.fullmatch(r"\d+\s*", raw):
        raise PackageError(f"host-subgroup-size.txt 不是纯整数：{subgroup_path}")
    actual = int(raw.strip())
    if actual not in (32, 64, 128):
        raise PackageError(f"host-subgroup-size.txt 档位非法：{actual}")
    if requested not in (32, 64, 128):
        raise PackageError(f"--subgroup 档位非法：{requested}")
    if requested != actual:
        raise PackageError(
            f"复用缓存实际 subgroup={actual}，但请求 --subgroup={requested}；"
            f"请匹配现有缓存或重新生成着色器，不能改写已有 profile")

    profile = _read_reuse_json(profile_path, "host-profile.json")
    config = _read_reuse_json(config_path, "build-config.json")
    if profile.get("schema") != 1:
        raise PackageError(f"host-profile.json schema 不支持：{profile.get('schema')!r}")
    if _require_profile_int(profile, "host_subgroup_size", "host-profile.json") != actual:
        raise PackageError("host-profile.json 与 host-subgroup-size.txt 的 subgroup 冲突")
    if _require_profile_int(config, "host_subgroup_size", "build-config.json") != actual:
        raise PackageError("build-config.json 与 host-subgroup-size.txt 的 subgroup 冲突")
    for label, value in (("host-profile.json", profile), ("build-config.json", config)):
        if value.get("guest_warp_size") != 32:
            raise PackageError(f"{label} 的 guest_warp_size 不是 32")
        if not isinstance(value.get("shader_host_capabilities"), dict):
            raise PackageError(f"{label} 缺少 shader_host_capabilities 对象")
        if not isinstance(value.get("jobs_sha256"), str) or not value["jobs_sha256"]:
            raise PackageError(f"{label} 缺少 jobs_sha256")
    for key in ("precision", "shader_host_capabilities", "jobs_sha256", "translator_sha256"):
        if key in profile and key in config and profile[key] != config[key]:
            raise PackageError(f"host-profile.json 与 build-config.json 的 {key} 冲突")
    return actual, profile


def to_nsp(source, work):
    """NSZ 是压缩过的 NSP，解包工具只吃 NSP。"""
    if source.suffix.lower() == ".nsp":
        return source
    if not NSZ.is_file():
        raise PackageError("需要 nsz 命令行把 NSZ 转成 NSP，但没找到：" + str(NSZ))
    run([NSZ, "-D", source, "-o", work / "nsp"], f"NSZ 转 NSP：{source.name}（最慢的两步之一）", timeout=6 * 60 * 60)
    for path in sorted((work / "nsp").rglob("*.nsp")):
        return path
    raise PackageError("NSZ 转换后没有产出 NSP")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("game", type=Path,
                        help="本体与更新所在的 zip，或放着这两个包的目录（两个都要有，只给一个认不出）")
    parser.add_argument("keys", type=Path, help="prod.keys，或装着它的 zip")
    parser.add_argument("--workspace", type=Path, default=Path.home() / "dm-work" / "pack-workspace")
    parser.add_argument("--output", type=Path, default=None)
    parser.add_argument("--reuse", type=Path, default=None,
                        help="复用已解出的数据目录（应有 ExeFS/、RomFS/、game/、shaders/spirv/），跳过最慢的两步")
    parser.add_argument("--subgroup", type=int, default=64)
    args = parser.parse_args()

    if args.reuse and args.workspace.resolve().is_relative_to(args.reuse.resolve()):
        sys.exit("打包失败：--workspace 必须在 --reuse 目录外，否则会改写复用缓存")
    work = args.workspace
    work.mkdir(parents=True, exist_ok=True)
    output = args.output or (work / PACKAGE_NAME)

    try:
        if args.reuse:
            # 复用路径只读取真实生成记录；身份不符就拒绝，不补写/改写缓存。
            # Reuse only reads the generated identity; mismatches are rejected rather than relabeled.
            reuse = args.reuse
            for required in ("ExeFS", "RomFS", "game", "shaders/spirv"):
                if not (reuse / required).is_dir():
                    raise PackageError(f"复用目录缺少 {required}：{reuse / required}")
            subgroup, _ = validate_reuse_shader_profile(reuse, args.subgroup)
            if output.resolve().is_relative_to(reuse.resolve()):
                raise PackageError("--output 必须在 --reuse 目录外，否则会写入复用缓存")
            say(f"复用已解出的数据：{reuse}（真实 subgroup={subgroup}）")
            spirv = reuse / "shaders" / "spirv"
            derived_workspace = work / "reuse-derived"
            if derived_workspace.resolve().is_relative_to(reuse.resolve()):
                raise PackageError("--workspace 必须在 --reuse 目录外，否则会改写复用缓存")
            # 复用路径也重做 derived.tsv，但写到工作目录，不能碰原缓存。
            # The reuse path regenerates derived.tsv outside the source cache.
            derived = derived_tables(reuse / "RomFS", derived_workspace)
            run([sys.executable, HERE / "build_package.py", "assemble", output,
                 "--game", reuse / "game", "--romfs", reuse / "RomFS", "--shaders", spirv, "--derived", derived,
                 "--note", "assembled from reused data"], "汇编数据包", timeout=60 * 60)
            say(f"完成：{output}（{output.stat().st_size / 2**30:.2f} GiB）")
            return

        game_input = gather_game_files(args.game, work / "input")
        base, update = find_title_files(game_input)
        say(f"本体 = {base.name}（{base.stat().st_size / 2**30:.2f} GiB）")
        say(f"更新 = {update.name}（{update.stat().st_size / 2**30:.2f} GiB）")
        keys = unpack_keys(args.keys, work)

        base_nsp = to_nsp(base, work)
        update_nsp = to_nsp(update, work)

        produce = work / "produce"
        layout = produce / "layout"
        run([sys.executable, HERE / "extract_packages.py", base_nsp, update_nsp, keys, layout,
             "--hactool", HACTOOL], "用密钥解出 ExeFS 与 RomFS（最慢的两步之一）", timeout=6 * 60 * 60)
        romfs, exefs = layout / "RomFS", layout / "ExeFS"
        for path, label in ((romfs, "RomFS"), (exefs, "ExeFS")):
            if not path.is_dir():
                raise PackageError(f"解包没有产出 {label}：{path}")

        game = produce / "game"
        game.mkdir(exist_ok=True)
        environment = dict(os.environ, DM_TARGET="android", DM_RULESET="touch-run-v4")
        for module in ("main", "sdk", "subsdk0"):
            run([sys.executable, ROOT / "tools" / "patch_nso.py", exefs / module, game, module],
                f"改写模块 {module}", env=environment)
        run([sys.executable, HERE / "guest_elf.py", game, game, "main", "sdk", "subsdk0"],
            "把模块打成 ELF 动态库", env=environment)

        # 三张宿主表从 RomFS 生成到工作目录，再拆成运行时数据 derived.tsv（随包走）。不再写源码树：
        # 仓库里提交的是运行时替身，任何构建都不该被一组由游戏文件生成的头文件污染。
        # The three host tables are generated from the RomFS into the workspace and split into the runtime data
        # derived.tsv (which travels with the package). They no longer touch the source tree: the repository holds
        # runtime stubs, and no build should be polluted by headers generated from the player's game files.
        derived = derived_tables(romfs, produce)

        shaders = produce / "shaders"
        run([sys.executable, HERE / "build_shaders.py", romfs, exefs / "main", shaders,
             "--host-subgroup-size", str(args.subgroup), "--dotnet", DOTNET],
            "翻译着色器（需要 .NET，最慢的一步）", timeout=12 * 60 * 60)
        spirv = shaders / "spirv"
        if not spirv.is_dir():
            raise PackageError("着色器翻译没有产出 spirv 目录：" + str(spirv))
        # 汇编器会校验着色器目录里的分组档位文件，所以必须先写上。
        (spirv / "host-subgroup-size.txt").write_text(str(args.subgroup) + "\n")

        run([sys.executable, HERE / "build_package.py", "assemble", output,
             "--game", game, "--romfs", romfs, "--shaders", spirv, "--derived", derived,
             "--note", "generated by make_package.py"], "汇编数据包", timeout=60 * 60)
        say(f"完成：{output}（{output.stat().st_size / 2**30:.2f} GiB）")
        say("中间产物保留在工作目录，下次可加 --reuse 直接汇编，不必重跑最慢的两步。")
    except PackageError as error:
        sys.exit(f"打包失败：{error}")


if __name__ == "__main__":
    main()
