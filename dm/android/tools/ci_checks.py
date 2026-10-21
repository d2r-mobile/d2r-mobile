#!/usr/bin/env python3
"""可在 Linux / CI 上跑的那部分检查，以及一条"仓库里不许有游戏数据"的卫生检查。

完整的宿主测试套件里有一小部分只能在 macOS 上跑（`t_thread_leak` 用 `<malloc/malloc.h>`、
`t_i18n` 依赖的 `dm_i18n.c` 用 `<TargetConditionals.h>`），这里把它们如实列为"跳过"，
而不是假装全绿。CI 与本地都跑这个脚本，两边看到的是同一组检查。

The checks that work on Linux / CI, plus a hygiene check that no game data is in the repository.
A couple of host tests are macOS-only (`t_thread_leak` includes <malloc/malloc.h>; `dm_i18n.c`, which `t_i18n`
needs, includes <TargetConditionals.h>); they are listed as skipped rather than silently assumed to pass. CI and
a developer run this same script, so both see the same set.
"""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]          # dm/android
REPO = ROOT.parents[1]                              # repository root
TOOLS = Path(os.environ.get("DM_ANDROID_TOOLS", str(Path.home() / "dm-work" / "tools" / "android")))
CC = os.environ.get("CC", "cc")
JAVA = TOOLS / "jdk/bin/java"
JAVAC = TOOLS / "jdk/bin/javac"

# 仓库里绝不允许出现的游戏内容与派生物：本体/更新包、密钥、存档、重写后的游戏模块。
# Game content and derived files that must never appear in the repository: game images, keys, saves, rewritten
# game modules.
FORBIDDEN_SUFFIXES = {".nsp", ".nsz", ".xci", ".nca", ".tik", ".cert", ".d2s", ".d2i", ".ma0", ".ctl"}
FORBIDDEN_NAMES = {"prod.keys", "title.keys", "build_key.txt", ".build.config"}

failures = []
passed = 0
skipped = []


def report(name, ok, detail=""):
    global passed
    if ok:
        passed += 1
        print("✓ " + name, flush=True)
    else:
        failures.append(name)
        print("✗ " + name + ((" — " + detail) if detail else ""), flush=True)


def run(command, **kwargs):
    return subprocess.run([str(c) for c in command], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          text=True, **kwargs)


def python_check(name, script, *args):
    result = run([sys.executable, REPO / script, *args])
    report(name, result.returncode == 0, (result.stdout or "").strip().splitlines()[-1] if result.stdout else "")


def compile_and_run(name, sources, binary, compile_flags=()):
    result = run([CC, *compile_flags, "-o", binary, *sources])
    if result.returncode != 0:
        report(name + "（编译）", False, (result.stdout or "").strip()[-400:])
        return
    result = run([binary])
    report(name, result.returncode == 0, (result.stdout or "").strip().splitlines()[-1] if result.stdout else "")


def hygiene():
    """仓库里不许有游戏内容：用 git 的已跟踪文件清单判断，构建产物与被 gitignore 的东西不在此列。
    No game content in the repository: judged from git's tracked file list, so build output and gitignored
    material are out of scope."""
    result = run(["git", "-C", REPO, "ls-files"])
    if result.returncode != 0:
        report("仓库卫生检查", False, "git ls-files failed")
        return
    bad = []
    for line in result.stdout.splitlines():
        path = Path(line)
        if path.suffix.lower() in FORBIDDEN_SUFFIXES or path.name in FORBIDDEN_NAMES:
            bad.append(line)
    report("仓库卫生检查（无游戏数据/密钥/存档）", not bad, ", ".join(bad[:5]))


def table_stubs():
    """三张派生表必须是**运行时替身**，不能是真实数据：仓库里不许出现游戏内容。
    The three derived tables must be **runtime stubs**, never real data: the repository must hold no game content.
    A real table loses the stub marker and is tens of kilobytes; either way this fails loudly instead of shipping
    game content in a clean checkout."""
    expected = (
        ("dm/host/dm_hud_exp.h", "dm_derived_rt.h"),
        ("dm/host/dm_vault_tab.h", "dm_derived_rt.h"),
        ("dm/host/dm_bright_levels.inc", "DM_BRIGHT_COUNT"),
    )
    bad = []
    for relative, marker in expected:
        path = REPO / relative
        if not path.is_file():
            bad.append(relative + "（缺失）")
        elif marker not in path.read_text(encoding="utf-8", errors="replace"):
            bad.append(relative + "（不是替身）")
        elif path.stat().st_size > 64 * 1024:
            bad.append(relative + "（过大，像真实表）")
    report("派生表是运行时替身（仓库不含游戏内容）", not bad, ", ".join(bad))


def main():
    with tempfile.TemporaryDirectory() as workspace:
        workspace = Path(workspace)
        python_check("中文字面量扫描", "dm/i18n/scan_literals.py")
        python_check("中英表一致性", "dm/ios/tools/dm_i18n.py", "--check")
        # 版本折算：Android 拒绝版本号倒退的安装，这条规则要有回归挡住。
        # The version mapping: Android refuses an install whose version code goes backwards, so it needs a regression.
        python_check("APK 版本折算（test_version）", "dm/android/tests/test_version.py")

        compile_and_run("方向键/扳机映射（t_padmap）", [REPO / "dm/host/tests/t_padmap.c"], workspace / "t_padmap")
        # 启动设置：普通版与"环境写入失败"注入版都要跑（故障恢复那条只有注入版才覆盖）。
        launch = [ROOT / "native/launch_settings.c", ROOT / "tests/launch_settings_test.c"]
        compile_and_run("启动设置（普通）", launch, workspace / "launch_settings",
                        ("-std=c11", "-O3", "-Wall", "-Wextra", "-Werror", "-I", ROOT / "native"))
        compile_and_run("启动设置（环境故障注入）", launch, workspace / "launch_settings_fault",
                        ("-std=c11", "-O3", "-Wall", "-Wextra", "-Werror", "-I", ROOT / "native",
                         "-DDM_LAUNCH_SETTINGS_TEST_WRAP_ENV", "-Wl,--wrap=setenv", "-Wl,--wrap=unsetenv"))

        # 存档的压缩/解压/原子替换、数据包导入的规则与验收都是纯 Java（不碰 Android API），用项目 JDK 在宿主机上跑。
        # 数据包这一支是补上来的：导入曾经只检查"每个类别至少一个文件"，一个残缺的包能通过并让游戏开不了机。
        # Save archive (zip/unzip/atomic swap) and package import (its rules and acceptance check) are both plain
        # Java with no Android APIs, and run here on the host JDK. The package side was added late: import used to
        # accept anything with at least one file per category, so a hollow package could pass and leave the game
        # unable to start.
        for source, test, label in (("SaveArchive.java", "SaveArchiveTest", "存档包"),
                                    ("PackageArchive.java", "PackageArchiveTest", "数据包导入")):
            if not (JAVAC.exists() and JAVA.exists()):
                skipped.append("%s（%s）：没有项目 JDK（%s）" % (label, test, JAVAC))
                continue
            classes = workspace / ("classes-" + test)
            classes.mkdir()
            result = run([JAVAC, "-encoding", "UTF-8", "--release", "8", "-d", classes,
                          ROOT / "java/org/d2r/android/port" / source, ROOT / "tests" / (test + ".java")])
            if result.returncode != 0:
                report("%s（%s，编译）" % (label, test), False, (result.stdout or "").strip()[-400:])
                continue
            result = run([JAVA, "-cp", classes, "org.d2r.android.port." + test])
            report("%s（%s）" % (label, test), result.returncode == 0,
                   (result.stdout or "").strip().splitlines()[-1] if result.stdout else "")

        hygiene()
        table_stubs()

        skipped.append("t_thread_leak / t_i18n：依赖 macOS 专有头文件，Linux/CI 上不跑")
        print()
        for note in skipped:
            print("— 跳过：" + note)
        print("== %d 项通过，%d 项失败 ==" % (passed, len(failures)))
        return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
