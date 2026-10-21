#!/usr/bin/env python3
"""#167 private mutation control for the real custom-preferences file store.

The real store tests must fail when the rename commit is skipped.
"""
import argparse
import subprocess
import sys
import tempfile
from pathlib import Path


def compile_and_run(cc: str, app: Path, tests: Path, source: Path, work: Path, label: str):
    exe = work / f"custom-preferences-store-{label}"
    build = subprocess.run(
        [cc, "-std=gnu11", "-g", "-fsanitize=address,undefined", f"-I{app}",
         str(tests), str(source), str(app / "custom_preferences.c"), "-o", str(exe)],
        capture_output=True, text=True)
    if build.returncode != 0:
        return "build_failed", build.returncode, build.stderr
    run = subprocess.run([str(exe)], capture_output=True, text=True,
                         env={"ASAN_OPTIONS": "detect_leaks=0",
                              "UBSAN_OPTIONS": "halt_on_error=1"})
    return "ran", run.returncode, run.stdout + run.stderr


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--app", required=True)
    parser.add_argument("--tests", required=True)
    parser.add_argument("--cc", default="cc")
    args = parser.parse_args()
    app = Path(args.app)
    tests = Path(args.tests)
    source_path = app / "custom_preferences_store.c"
    original = source_path.read_text(encoding="utf-8")
    mutation = (
        "if (renameat(store->dirfd, DM_CP_STORE_TEMP, store->dirfd, DM_CP_STORE_FILE) != 0) {",
        "if (0) {",
    )
    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        status, code, output = compile_and_run(args.cc, app, tests, source_path, work, "control")
        if status != "ran" or code != 0:
            print(f"FAIL: unmutated store controller did not pass (status={status}, exit={code})",
                  file=sys.stderr)
            print(output, file=sys.stderr)
            return 1
        if original.count(mutation[0]) != 1:
            print("fail closed: rename mutation anchor is missing or ambiguous", file=sys.stderr)
            return 1
        mutated = work / "custom_preferences_store_mutant.c"
        mutated.write_text(original.replace(mutation[0], mutation[1], 1), encoding="utf-8")
        status, code, output = compile_and_run(args.cc, app, tests, mutated, work, "skip-rename")
        if status != "ran":
            print("FAIL: mutation did not compile", file=sys.stderr)
            print(output, file=sys.stderr)
            return 1
        if code == 0 or "FAIL " not in output:
            print("FAIL: skip-rename mutation passed or failed outside assertions", file=sys.stderr)
            print(output, file=sys.stderr)
            return 1
        print("mutant skip-rename: rejected as intended")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
